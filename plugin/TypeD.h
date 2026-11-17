#pragma once

#include "TypeDModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace tg {

inline constexpr int matchTaps = 513;
inline constexpr int matchDelay = 128;
inline constexpr double nominalDigitalZeroDbu = 22.0;
inline constexpr double nominalMicZeroDbu = -10.0;

inline float linear(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

struct MatchProfile
{
    int rate = 0;
    float zeroDbu = 0.0f;
    std::array<float, matchTaps> kernel {};

    bool usable(double hostRate) const
    {
        return rate > 0 && std::abs(rate - hostRate) < 0.5;
    }

    bool valid() const
    {
        if (rate < 44100 || rate > 96000 || !std::isfinite(zeroDbu)
            || zeroDbu < -120.0f || zeroDbu > 30.0f)
            return false;
        float energy = 0.0f;
        for (float tap : kernel)
        {
            if (!std::isfinite(tap) || std::abs(tap) > 4.0f)
                return false;
            energy += tap * tap;
        }
        return energy > 0.00001f && energy < 16.0f;
    }
};

class SourceCorrection
{
public:
    void reset()
    {
        samples.fill(0.0f);
        head = 0;
    }

    float process(float input, float mix, const MatchProfile& profile)
    {
        // Mirrored storage keeps the FIR contiguous across a ring rollover.
        if (--head < 0)
            head = matchTaps - 1;
        const auto index = static_cast<size_t>(head);
        samples[index] = samples[index + matchTaps] = input;
        const float* delayed = samples.data() + head;
        float dry = delayed[matchDelay];
        if (mix <= 0.0f)
            return dry;

        float wet = 0.0f;
        for (int tap = 0; tap < matchTaps; ++tap)
            wet += delayed[tap] * profile.kernel[static_cast<size_t>(tap)];
        return dry + mix * (wet - dry);
    }

private:
    std::array<float, 2 * matchTaps> samples {};
    int head = 0;
};

struct Biquad
{
    void prepare(const float* analog, double rate)
    {
        const double k = rate / (std::numbers::pi * 1000.0);
        const auto transform = [k](const float* c)
        {
            const double s2 = c[0] * k * k;
            const double s1 = c[1] * k;
            return std::array {s2 + s1 + c[2], 2.0 * (c[2] - s2), s2 - s1 + c[2]};
        };
        const auto b = transform(analog);
        const auto a = transform(analog + 3);
        b0 = b[0] / a[0];
        b1 = b[1] / a[0];
        b2 = b[2] / a[0];
        a1 = a[1] / a[0];
        a2 = a[2] / a[0];
        if (analog[0] == 0.0f && analog[3] == 0.0f)
        {
            // Cancel the common (1 + z^-1) analytically for first-order sections.
            b1 = (analog[2] - analog[1] * k) / a[0];
            a1 = (analog[5] - analog[4] * k) / a[0];
            b2 = a2 = 0.0;
        }
        z1 = z2 = 0.0;
    }

    float process(float input)
    {
        auto output = input * b0 + z1;
        z1 = input * b1 - output * a1 + z2;
        z2 = input * b2 - output * a2;
        return static_cast<float>(output);
    }

    double b0 {}, b1 {}, b2 {}, a1 {}, a2 {}, z1 {}, z2 {};
};

inline float stageShape(const Model& model, float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    constexpr float scale = static_cast<float>(points - 1) / (2.0f * curveLimit);
    const auto position = std::clamp((value + curveLimit) * scale, 0.0f,
                                     static_cast<float>(points - 1));
    const auto i = std::min(static_cast<int>(position), points - 2);
    const auto t = position - static_cast<float>(i);
    const auto v0 = model.curve[i];
    const auto v1 = model.curve[i + 1];
    const auto m0 = (v1 - model.curve[std::max(0, i - 1)]) * 0.5f;
    const auto m1 = (model.curve[std::min(points - 1, i + 2)] - v0) * 0.5f;
    return v0 + t * (m0 + t * (3.0f * (v1 - v0) - 2.0f * m0 - m1
                      + t * (2.0f * (v0 - v1) + m0 + m1)));
}

template<int Steps, int Banks = 2>
class FittedStage
{
public:
    void prepare(double rate, const Model* table = models)
    {
        profileModels = table;
        for (size_t i = 0; i < filters.size(); ++i)
            for (size_t section = 0; section < filters[i].size(); ++section)
                filters[i][section].prepare(profileModels[i].sos[section], rate);
        dcPole = static_cast<float>(std::exp(-2.0 * std::numbers::pi * 0.35 / rate));
        derivativeScale = static_cast<float>(rate / (2.0 * std::numbers::pi * 1000.0));
        previousLinear.fill(0.0f);
        previousInput = previousOutput = 0.0f;
    }

    float process(float input, float finePosition, float impedance)
    {
        auto base = std::clamp(static_cast<int>(finePosition), 0, std::max(0, Steps - 2));
        auto blend = finePosition - static_cast<float>(base);
        std::array<float, Steps * Banks> outputs {};
        for (size_t i = 0; i < filters.size(); ++i)
        {
            auto value = input;
            for (auto& filter : filters[i])
                value = filter.process(value);
            const auto derivative = (value - previousLinear[i]) * derivativeScale;
            previousLinear[i] = value;
            const auto& shape = profileModels[i];
            const auto quadratic = value * (shape.quadratic[0] * value + shape.quadratic[1] * derivative);
            const auto headroom = std::clamp((6.0f - std::abs(value)) * .25f, 0.0f, 1.0f);
            outputs[i] = stageShape(shape, value) + headroom * quadratic;
        }

        auto channel = [&](int offset)
        {
            auto first = offset + base;
            auto lo = outputs[static_cast<size_t>(first)];
            auto hi = outputs[static_cast<size_t>(first + (Steps > 1))];
            return lo + (hi - lo) * blend;
        };

        const auto unloaded = channel(0);
        auto shaped = unloaded;
        if constexpr (Banks > 1)
            shaped += (channel(Steps) - unloaded) * impedance;
        const auto filtered = shaped - previousInput + dcPole * previousOutput;
        previousInput = shaped;
        previousOutput = filtered;
        return filtered;
    }

    float naturalGain(float finePosition, float impedance) const
    {
        auto base = std::clamp(static_cast<int>(finePosition), 0, std::max(0, Steps - 2));
        auto frac = finePosition - static_cast<float>(base);
        const auto gain = [&](int offset)
        {
            const int i = base + offset;
            return profileModels[i].gain + (profileModels[i + (Steps > 1)].gain - profileModels[i].gain) * frac;
        };
        if constexpr (Banks > 1)
            return gain(0) + (gain(Steps) - gain(0)) * impedance;
        return gain(0);
    }

private:
    const Model* profileModels = models;
    std::array<std::array<Biquad, 4>, Steps * Banks> filters {};
    std::array<float, Steps * Banks> previousLinear {};
    float previousInput {}, previousOutput {}, dcPole {}, derivativeScale {};
};

using TypeD = FittedStage<5>;

}

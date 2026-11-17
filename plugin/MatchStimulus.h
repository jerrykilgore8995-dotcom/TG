#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <numbers>

namespace tg {

inline constexpr float lineOutputDbu = 16.0f;
inline constexpr float defaultTestPadDb = 50.0f;
inline constexpr float minTestPadDb = 20.0f;
inline constexpr float maxTestPadDb = 80.0f;
inline constexpr int testRates[] {44100, 48000, 88200, 96000};

inline bool writeMatchStimulus(const juce::File& destination, int rate, float attenuation)
{
    // Marker positions and tone sequence are shared with calibration/tg_capture.py.
    constexpr int frequencies[] {100, 1000, 5000, 10000};
    constexpr float levels[] {-36.0f, -24.0f, -12.0f, -6.0f};
    constexpr double tau = 2.0 * std::numbers::pi;
    const auto pad = std::pow(10.0f, -attenuation / 20.0f);
    const auto markerLength = rate / 4;
    const auto broadbandAt = 2 * rate;
    const auto tonesAt = 7 * rate + rate / 2;
    const auto lastMarker = tonesAt + 32 * rate;
    const auto taper = [](int i, int size, double alpha)
    {
        const auto u = static_cast<double>(std::min(i, size - 1 - i)) / (size - 1);
        return u >= alpha * 0.5 ? 1.0 : 0.5 - 0.5 * std::cos(tau * u / alpha);
    };

    juce::AudioBuffer<float> audio(1, lastMarker + markerLength + rate);
    audio.clear();
    auto* samples = audio.getWritePointer(0);
    for (int i = 0; i < markerLength; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const auto chirp = std::sin(tau * (350.0 * t + (12000.0 - 350.0) * t * t / 0.5));
        const auto sample = static_cast<float>(chirp * taper(i, markerLength, 0.12) * .12) * pad;
        samples[rate + i] = sample;
        samples[lastMarker + i] = sample;
    }
    const auto highest = std::min(22000.0, std::floor(0.24 * rate) * 2.0);
    for (int i = 0; i < rate; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const auto chirp = std::sin(tau * (12.0 * t + (highest - 12.0) * t * t * 0.5));
        for (int repeat = 0; repeat < 5; ++repeat)
            samples[broadbandAt + repeat * rate + i] = static_cast<float>(chirp * .30) * pad;
    }
    const auto length = 3 * rate / 2;
    int position = tonesAt;
    for (float level : levels)
    {
        const auto amplitude = std::pow(10.0f, level / 20.0f) * pad;
        for (int hz : frequencies)
        {
            for (int i = 0; i < length; ++i)
                samples[position + i] = static_cast<float>(std::sin(tau * hz * i / rate)
                                                           * taper(i, length, .2)) * amplitude;
            position += 2 * rate;
        }
    }

    juce::TemporaryFile temporary(destination);
    auto stream = temporary.getFile().createOutputStream();
    if (stream == nullptr || stream->failedToOpen())
        return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), rate, 1, 32, {}, 0));
    if (writer == nullptr)
        return false;
    (void) stream.release();
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()) || !writer->flush())
        return false;
    writer.reset();
    return temporary.overwriteTargetFileWithTemporary();
}

}

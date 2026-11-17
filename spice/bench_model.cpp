#include "TypeD.h"
#include "ExtraModels.h"
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

template<int Trim, int Banks, size_t Levels>
bool run(const char* mode, const tg::Model* table, double scale, double rate, int cycles,
         const std::array<int, Levels>& levels)
{
    constexpr double tau = 2.0 * std::numbers::pi;
    for (int bank = 0; bank < Banks; ++bank)
        for (int trim = 0; trim < Trim; ++trim)
            for (int hz : {100, 1000, 5000, 10000})
                for (const auto dbu : levels)
                {
                    tg::FittedStage<Trim, Banks> circuit;
                    circuit.prepare(rate, table);
                    const auto amplitude = .775 * std::sqrt(2.0) * tg::linear(static_cast<float>(dbu));
                    const int steady = static_cast<int>(rate * .2);
                    const int samples = static_cast<int>(rate / 100) * cycles;
                    std::array<std::complex<double>, 21> bins {};
                    const auto maximum = std::min(20, static_cast<int>((rate * .5 - 1) / hz));
                    for (int i = 0; i < steady + samples; ++i)
                    {
                        const auto theta = i * hz * tau / rate;
                        const auto value = static_cast<double>(circuit.process(
                            static_cast<float>(amplitude * std::sin(theta)),
                            static_cast<float>(trim), static_cast<float>(bank)));
                        if (!std::isfinite(value))
                            return false;
                        if (i >= steady)
                            for (int k = 1; k <= maximum; ++k)
                                bins[static_cast<size_t>(k)] += value * std::polar(1.0, -k * theta);
                    }
                    const auto fundamental = std::abs(bins[1]);
                    if (fundamental <= 0.0)
                        return false;
                    double thd = 0.0;
                    for (int k = 2; k <= maximum; ++k)
                        thd += std::norm(bins[static_cast<size_t>(k)]) / (fundamental * fundamental);
                    std::cout << mode << ',' << bank << ',' << trim << ',' << hz << ',' << dbu << ','
                              << 2 * fundamental / (samples * scale) << ',' << 100 * std::sqrt(thd)
                              << ',' << std::remainder(std::arg(bins[1]) + tau / 4.0, tau) * 360.0 / tau;
                    for (int k = 2; k <= 20; ++k)
                    {
                        std::cout << ',';
                        if (k <= maximum)
                            std::cout << 20 * std::log10(std::max(1e-15, std::abs(bins[static_cast<size_t>(k)]) / fundamental));
                    }
                    std::cout << '\n';
                }
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 1;
    const auto rate = std::atof(argv[1]);
    const int cycles = std::atoi(argv[2]);
    if (rate < 40000 || cycles <= 0)
        return 1;
    std::cout << std::setprecision(12);
    std::cout << "model,bank,trim,hz,dbu,volts,thd,phase_deg,k2,k3,k4,k5,k6,k7,k8,k9,k10,k11,k12,k13,k14,k15,k16,k17,k18,k19,k20\n";
    constexpr std::array baseline {-65, -50, -40, -30, -20};
    constexpr std::array alternate {-65, -45, -30};
    constexpr std::array tube {-65, -45, -30, -25};
    return run<5, 2>("type-d", tg::models, 1.0, rate, cycles, baseline)
        && run<3, 2>("1290", tg::extraModels, .4, rate, cycles, alternate)
        && run<3, 2>("312", tg::extraModels + 6, .35, rate, cycles, alternate)
        && run<1, 1>("v72", tg::extraModels + 12, .8, rate, cycles, tube) ? 0 : 2;
}

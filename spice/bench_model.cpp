#include "TypeD.h"
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 3)
        return 1;
    const auto rate = std::atof(argv[1]);
    const int cycles = std::atoi(argv[2]);
    if (rate < 40000 || cycles <= 0)
        return 1;
    constexpr double tau = 2.0 * std::numbers::pi;
    std::cout << std::setprecision(12);
    std::cout << "bank,trim,hz,dbu,volts,thd,k2,k3,k4,k5,k6,k7,k8,k9,k10,k11,k12,k13,k14,k15,k16,k17,k18,k19,k20\n";
    for (int bank : {0, 1})
        for (int trim = 0; trim < 5; ++trim)
            for (int hz : {100, 1000, 5000, 10000})
                for (int dbu : {-65, -50, -40, -30, -20})
                {
                    tg::TypeD circuit;
                    circuit.prepare(rate);
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
                            return 2;
                        if (i >= steady)
                            for (int k = 1; k <= maximum; ++k)
                                bins[static_cast<size_t>(k)] += value * std::polar(1.0, -k * theta);
                    }
                    const auto fundamental = std::abs(bins[1]);
                    if (fundamental <= 0.0)
                        return 2;
                    double thd = 0.0;
                    for (int k = 2; k <= maximum; ++k)
                        thd += std::norm(bins[static_cast<size_t>(k)]) / (fundamental * fundamental);
                    std::cout << bank << ',' << trim << ',' << hz << ',' << dbu << ','
                              << 2 * fundamental / samples << ',' << 100 * std::sqrt(thd);
                    for (int k = 2; k <= 20; ++k)
                    {
                        std::cout << ',';
                        if (k <= maximum)
                            std::cout << 20 * std::log10(std::max(1e-15, std::abs(bins[static_cast<size_t>(k)]) / fundamental));
                    }
                    std::cout << '\n';
                }
}

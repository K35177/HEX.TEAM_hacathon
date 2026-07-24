#include "acoustic/fsk.hpp"

#include <cmath>
#include <stdexcept>

namespace acoustic {

std::size_t samples_per_symbol(const FskConfig& config) {
    if (config.symbol_rate == 0 || config.sample_rate % config.symbol_rate != 0) {
        throw std::invalid_argument("sample rate must be divisible by symbol rate");
    }
    return config.sample_rate / config.symbol_rate;
}

std::vector<float> modulate_bits(std::span<const std::uint8_t> bytes,
                                 const FskConfig& config) {
    constexpr double pi = 3.14159265358979323846;
    const auto symbol_samples = samples_per_symbol(config);
    std::vector<float> output;
    output.reserve(bytes.size() * 8 * symbol_samples);
    double phase = 0.0;
    for (const auto byte : bytes) {
        for (int bit = 7; bit >= 0; --bit) {
            const bool one = ((byte >> bit) & 1U) != 0;
            const double frequency = one ? config.frequency_one : config.frequency_zero;
            const double step = 2.0 * pi * frequency / config.sample_rate;
            for (std::size_t i = 0; i < symbol_samples; ++i) {
                output.push_back(static_cast<float>(config.amplitude * std::sin(phase)));
                phase += step;
                if (phase >= 2.0 * pi) phase -= 2.0 * pi;
            }
        }
    }
    return output;
}

std::vector<std::uint8_t> demodulate_bits(std::span<const float> samples,
                                          const FskConfig& config) {
    constexpr double pi = 3.14159265358979323846;
    const auto symbol_samples = samples_per_symbol(config);
    const auto symbol_count = samples.size() / symbol_samples;
    if (symbol_count % 8U != 0) {
        throw std::runtime_error("audio does not contain a whole number of bytes");
    }
    std::vector<std::uint8_t> output(symbol_count / 8U, 0);
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol) {
        double zero_sin = 0.0, zero_cos = 0.0;
        double one_sin = 0.0, one_cos = 0.0;
        for (std::size_t i = 0; i < symbol_samples; ++i) {
            const double sample = samples[symbol * symbol_samples + i];
            const double time = static_cast<double>(i) / config.sample_rate;
            const double zero_phase = 2.0 * pi * config.frequency_zero * time;
            const double one_phase = 2.0 * pi * config.frequency_one * time;
            zero_sin += sample * std::sin(zero_phase);
            zero_cos += sample * std::cos(zero_phase);
            one_sin += sample * std::sin(one_phase);
            one_cos += sample * std::cos(one_phase);
        }
        const double zero_energy = zero_sin * zero_sin + zero_cos * zero_cos;
        const double one_energy = one_sin * one_sin + one_cos * one_cos;
        if (one_energy > zero_energy) {
            output[symbol / 8U] |= static_cast<std::uint8_t>(1U << (7U - symbol % 8U));
        }
    }
    return output;
}

}  // namespace acoustic

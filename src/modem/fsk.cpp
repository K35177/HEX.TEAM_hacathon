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

}  // namespace acoustic

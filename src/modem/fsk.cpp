#include "acoustic/fsk.hpp"

#include <cmath>
#include <stdexcept>

namespace acoustic {
namespace {
constexpr double pi = 3.14159265358979323846;

void validate(const FskConfig& config) {
    if (config.modulation_order != 2 && config.modulation_order != 4) {
        throw std::invalid_argument("FSK modulation order must be 2 or 4");
    }
    if (config.symbol_rate == 0 || config.sample_rate % config.symbol_rate != 0) {
        throw std::invalid_argument("sample rate must be divisible by symbol rate");
    }
}
}  // namespace

std::size_t bits_per_symbol(const FskConfig& config) {
    validate(config);
    return config.modulation_order == 4 ? 2U : 1U;
}

std::size_t samples_per_symbol(const FskConfig& config) {
    validate(config);
    return config.sample_rate / config.symbol_rate;
}

std::vector<float> modulate_bits(std::span<const std::uint8_t> bytes,
                                 const FskConfig& config) {
    const auto symbol_samples = samples_per_symbol(config);
    const auto symbol_bits = bits_per_symbol(config);
    const auto symbols_per_byte = 8U / symbol_bits;
    std::vector<float> output;
    output.reserve(bytes.size() * symbols_per_byte * symbol_samples);
    double phase = 0.0;
    for (const auto byte : bytes) {
        for (std::size_t part = 0; part < symbols_per_byte; ++part) {
            const auto shift = 8U - symbol_bits * (part + 1U);
            const auto symbol = (byte >> shift) & (config.modulation_order - 1U);
            const double frequency = config.base_frequency + symbol * config.frequency_spacing;
            const double step = 2.0 * pi * frequency / config.sample_rate;
            for (std::size_t i = 0; i < symbol_samples; ++i) {
                output.push_back(static_cast<float>(config.amplitude * std::sin(phase)));
                phase = std::fmod(phase + step, 2.0 * pi);
            }
        }
    }
    return output;
}

std::vector<std::uint8_t> demodulate_bits(std::span<const float> samples,
                                          const FskConfig& config) {
    const auto symbol_samples = samples_per_symbol(config);
    const auto symbol_bits = bits_per_symbol(config);
    const auto symbols_per_byte = 8U / symbol_bits;
    const auto symbol_count = samples.size() / symbol_samples;
    if (symbol_count % symbols_per_byte != 0) {
        throw std::runtime_error("audio does not contain a whole number of bytes");
    }
    std::vector<std::uint8_t> output(symbol_count / symbols_per_byte, 0);
    for (std::size_t position = 0; position < symbol_count; ++position) {
        std::uint8_t best_symbol = 0;
        double best_energy = -1.0;
        for (std::uint8_t candidate = 0; candidate < config.modulation_order; ++candidate) {
            const double frequency = config.base_frequency + candidate * config.frequency_spacing;
            double sin_sum = 0.0, cos_sum = 0.0;
            for (std::size_t i = 0; i < symbol_samples; ++i) {
                const double sample = samples[position * symbol_samples + i];
                const double phase = 2.0 * pi * frequency * i / config.sample_rate;
                sin_sum += sample * std::sin(phase);
                cos_sum += sample * std::cos(phase);
            }
            const double energy = sin_sum * sin_sum + cos_sum * cos_sum;
            if (energy > best_energy) {
                best_energy = energy;
                best_symbol = candidate;
            }
        }
        const auto part = position % symbols_per_byte;
        const auto shift = 8U - symbol_bits * (part + 1U);
        output[position / symbols_per_byte] |= static_cast<std::uint8_t>(best_symbol << shift);
    }
    return output;
}

}  // namespace acoustic

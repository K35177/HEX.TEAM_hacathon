#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace acoustic {

struct FskConfig {
    std::uint32_t sample_rate = 48000;
    std::uint32_t symbol_rate = 200;
    std::uint8_t modulation_order = 4;
    double base_frequency = 1200.0;
    double frequency_spacing = 600.0;
    double amplitude = 0.75;
    std::uint8_t symbol_repetitions = 1;
};

struct FskMetrics {
    std::size_t symbol_count{};
    double mean_confidence{};
    double minimum_confidence{};
    double estimated_frequency_offset_hz{};
};

std::vector<float> modulate_bits(std::span<const std::uint8_t> bytes,
                                 const FskConfig& config = {});
std::vector<std::uint8_t> demodulate_bits(std::span<const float> samples,
                                          const FskConfig& config = {},
                                          FskMetrics* metrics = nullptr);
std::size_t bits_per_symbol(const FskConfig& config);
std::size_t samples_per_symbol(const FskConfig& config);

}  // namespace acoustic

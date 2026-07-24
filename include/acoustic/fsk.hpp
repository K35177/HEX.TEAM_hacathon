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
};

std::vector<float> modulate_bits(std::span<const std::uint8_t> bytes,
                                 const FskConfig& config = {});
std::vector<std::uint8_t> demodulate_bits(std::span<const float> samples,
                                          const FskConfig& config = {});
std::size_t bits_per_symbol(const FskConfig& config);
std::size_t samples_per_symbol(const FskConfig& config);

}  // namespace acoustic

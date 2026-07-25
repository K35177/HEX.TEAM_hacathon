#pragma once

#include "acoustic/fsk.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace acoustic {

struct FrameConfig {
    double chirp_duration_seconds = 0.25;
    double guard_duration_seconds = 0.05;
    double chirp_start_frequency = 700.0;
    double chirp_end_frequency = 5000.0;
};

struct ReceiverMetrics {
    double chirp_correlation{};
    double clock_scale{1.0};
    double noise_rms{};
    double signal_rms{};
    double clipping_ratio{};
    double mean_symbol_confidence{};
    double minimum_symbol_confidence{};
    double estimated_frequency_offset_hz{};
    std::size_t decoded_symbols{};
    std::size_t decoded_bytes{};
};

std::vector<float> create_audio_frame(std::span<const std::uint8_t> bytes,
                                      const FskConfig& modem = {},
                                      const FrameConfig& frame = {});
std::vector<std::uint8_t> decode_audio_frame(std::span<const float> samples,
                                             const FskConfig& modem = {},
                                             const FrameConfig& frame = {},
                                             ReceiverMetrics* metrics = nullptr);

}  // namespace acoustic

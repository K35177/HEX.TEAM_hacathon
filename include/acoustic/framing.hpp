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

std::vector<float> create_audio_frame(std::span<const std::uint8_t> bytes,
                                      const FskConfig& modem = {},
                                      const FrameConfig& frame = {});
std::vector<std::uint8_t> decode_audio_frame(std::span<const float> samples,
                                             const FskConfig& modem = {},
                                             const FrameConfig& frame = {});

}  // namespace acoustic

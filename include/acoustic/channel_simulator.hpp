#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace acoustic {

struct ChannelConfig {
    double signal_gain = 1.0;
    double snr_db = std::numeric_limits<double>::infinity();
    double sample_rate_scale = 1.0;
    double clipping_level = 1.0;
    std::size_t leading_silence_samples{};
    std::size_t trailing_silence_samples{};
    std::uint32_t random_seed = 0x48455841U;
};

struct ChannelMetrics {
    double signal_rms{};
    double noise_rms{};
    double clipping_ratio{};
    std::size_t input_samples{};
    std::size_t output_samples{};
};

std::vector<float> simulate_channel(std::span<const float> input,
                                    const ChannelConfig& config = {},
                                    ChannelMetrics* metrics = nullptr);

}  // namespace acoustic

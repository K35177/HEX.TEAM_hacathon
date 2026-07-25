#include "acoustic/channel_simulator.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace acoustic {
namespace {
constexpr double pi = 3.14159265358979323846;
}

std::vector<float> simulate_channel(std::span<const float> input,
                                    const ChannelConfig& config,
                                    ChannelMetrics* metrics) {
    if (input.empty()) throw std::invalid_argument("channel input must not be empty");
    if (config.sample_rate == 0 || !std::isfinite(config.signal_gain) || config.signal_gain < 0.0 ||
        !std::isfinite(config.sample_rate_scale) || config.sample_rate_scale < 0.95 ||
        config.sample_rate_scale > 1.05 || !std::isfinite(config.clipping_level) ||
        config.clipping_level <= 0.0 || config.clipping_level > 1.0 ||
        (std::isfinite(config.lowpass_cutoff_hz) &&
         (config.lowpass_cutoff_hz <= 0.0 ||
          config.lowpass_cutoff_hz >= config.sample_rate * 0.5)) ||
        (!std::isfinite(config.snr_db) && config.snr_db !=
            std::numeric_limits<double>::infinity())) {
        throw std::invalid_argument("invalid channel configuration");
    }

    const auto scaled_size = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::llround(input.size() * config.sample_rate_scale)));
    std::vector<float> scaled;
    scaled.reserve(scaled_size);
    for (std::size_t i = 0; i < scaled_size; ++i) {
        const double source = i / config.sample_rate_scale;
        const auto lower = std::min(static_cast<std::size_t>(source), input.size() - 1U);
        const auto upper = std::min(lower + 1U, input.size() - 1U);
        const double fraction = source - lower;
        scaled.push_back(static_cast<float>(config.signal_gain *
            (input[lower] * (1.0 - fraction) + input[upper] * fraction)));
    }
    if (std::isfinite(config.lowpass_cutoff_hz)) {
        // Four inexpensive one-pole stages model the steep high-frequency
        // roll-off observed in laptop microphones and OS voice processing.
        const double alpha = 1.0 - std::exp(
            -2.0 * pi * config.lowpass_cutoff_hz / config.sample_rate);
        for (int stage = 0; stage < 4; ++stage) {
            double state = 0.0;
            for (float& sample : scaled) {
                state += alpha * (sample - state);
                sample = static_cast<float>(state);
            }
        }
    }

    double signal_energy = 0.0;
    for (const float sample : scaled) signal_energy += sample * sample;
    const double signal_rms = std::sqrt(signal_energy / scaled.size());
    const double noise_rms = std::isinf(config.snr_db) ? 0.0 :
        signal_rms * std::pow(10.0, -config.snr_db / 20.0);
    std::mt19937 generator(config.random_seed);
    std::normal_distribution<double> noise(0.0, noise_rms);

    std::vector<float> output;
    output.reserve(config.leading_silence_samples + scaled.size() +
                   config.trailing_silence_samples);
    const auto append_background = [&](std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            output.push_back(static_cast<float>(std::clamp(noise(generator),
                -config.clipping_level, config.clipping_level)));
        }
    };
    append_background(config.leading_silence_samples);
    std::size_t clipped = 0;
    for (const float sample : scaled) {
        const double impaired = sample + noise(generator);
        if (std::abs(impaired) > config.clipping_level) ++clipped;
        output.push_back(static_cast<float>(std::clamp(impaired,
            -config.clipping_level, config.clipping_level)));
    }
    append_background(config.trailing_silence_samples);

    if (metrics != nullptr) {
        metrics->signal_rms = signal_rms;
        metrics->noise_rms = noise_rms;
        metrics->clipping_ratio = static_cast<double>(clipped) / scaled.size();
        metrics->input_samples = input.size();
        metrics->output_samples = output.size();
    }
    return output;
}

}  // namespace acoustic

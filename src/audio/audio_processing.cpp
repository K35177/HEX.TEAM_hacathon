#include "acoustic/audio_processing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace acoustic {
namespace {

double automatic_gain(std::span<const float> samples, std::size_t analysis_window) {
    if (analysis_window == 0) throw std::invalid_argument("analysis window must not be zero");
    std::vector<double> window_levels;
    window_levels.reserve(1U + (samples.size() - 1U) / analysis_window);
    for (std::size_t offset = 0; offset < samples.size(); offset += analysis_window) {
        const auto count = std::min(analysis_window, samples.size() - offset);
        double energy = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double sample = samples[offset + i];
            energy += sample * sample;
        }
        window_levels.push_back(std::sqrt(energy / count));
    }
    std::sort(window_levels.begin(), window_levels.end());
    // Ignore isolated clicks while still finding a short transmission in a
    // much longer recording.
    const auto index = std::min(window_levels.size() - 1U,
                                window_levels.size() * 98U / 100U);
    const double signal_rms = window_levels[index];
    if (signal_rms < 1e-7) throw std::runtime_error("recording is silent");
    constexpr double target_rms = 0.30;
    return std::clamp(target_rms / signal_rms, 0.25, 20.0);
}

void multiply_samples(std::span<float> samples, double gain) {
    for (float& sample : samples) {
        sample = std::clamp(static_cast<float>(sample * gain), -1.0F, 1.0F);
    }
}

}  // namespace

double apply_receive_gain(std::span<float> samples,
                          double requested_gain,
                          std::size_t analysis_window) {
    if (samples.empty()) throw std::invalid_argument("cannot amplify an empty recording");
    if (!std::isfinite(requested_gain) || requested_gain < 0.0 || requested_gain > 50.0) {
        throw std::invalid_argument("receive gain must be 0 (auto) or between 0.1 and 50");
    }
    double gain = requested_gain;
    if (gain == 0.0) {
        gain = automatic_gain(samples, analysis_window);
    } else if (gain < 0.1) {
        throw std::invalid_argument("manual receive gain must be at least 0.1");
    }
    multiply_samples(samples, gain);
    return gain;
}

}  // namespace acoustic

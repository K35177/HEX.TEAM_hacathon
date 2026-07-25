#include "acoustic/audio_processing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace acoustic {

double apply_receive_gain(std::span<float> samples,
                          double requested_gain,
                          std::size_t analysis_window) {
    if (samples.empty()) throw std::invalid_argument("cannot amplify an empty recording");
    if (requested_gain < 0.0 || requested_gain > 50.0) {
        throw std::invalid_argument("receive gain must be 0 (auto) or between 0.1 and 50");
    }
    double gain = requested_gain;
    if (gain == 0.0) {
        if (analysis_window == 0) throw std::invalid_argument("analysis window must not be zero");
        double maximum_rms = 0.0;
        for (std::size_t offset = 0; offset < samples.size(); offset += analysis_window) {
            const auto count = std::min(analysis_window, samples.size() - offset);
            double energy = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                const double sample = samples[offset + i];
                energy += sample * sample;
            }
            maximum_rms = std::max(maximum_rms, std::sqrt(energy / count));
        }
        if (maximum_rms < 1e-7) throw std::runtime_error("recording is silent");
        constexpr double target_rms = 0.35;
        gain = std::clamp(target_rms / maximum_rms, 1.0, 20.0);
    } else if (gain < 0.1) {
        throw std::invalid_argument("manual receive gain must be at least 0.1");
    }
    for (float& sample : samples) {
        sample = std::clamp(static_cast<float>(sample * gain), -1.0F, 1.0F);
    }
    return gain;
}

}  // namespace acoustic

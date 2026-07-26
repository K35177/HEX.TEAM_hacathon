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

SilenceDetector::SilenceDetector(unsigned sample_rate, double silence_seconds)
    : sample_rate_(sample_rate) {
    if (sample_rate == 0 || !std::isfinite(silence_seconds) || silence_seconds < 1.0 ||
        silence_seconds > 60.0) {
        throw std::invalid_argument("invalid silence detector configuration");
    }
    initialization_samples_ = sample_rate / 5U;
    required_activity_samples_ = sample_rate / 5U;
    required_silence_samples_ = static_cast<std::size_t>(
        std::ceil(silence_seconds * sample_rate));
}

void SilenceDetector::process_pcm16(std::span<const std::int16_t> samples) {
    if (samples.empty() || should_stop_) return;
    double energy = 0.0;
    for (const auto sample : samples) {
        const double normalized = static_cast<double>(sample) / 32768.0;
        energy += normalized * normalized;
    }
    const double rms = std::sqrt(energy / samples.size());
    processed_samples_ += samples.size();

    if (processed_samples_ <= initialization_samples_) {
        const auto previous_samples = processed_samples_ - samples.size();
        noise_rms_ = previous_samples == 0 ? rms :
            (noise_rms_ * previous_samples + rms * samples.size()) / processed_samples_;
        return;
    }

    if (!signal_detected_) {
        const double activation_threshold = std::max(0.001, noise_rms_ * 2.5);
        if (rms > activation_threshold) {
            active_samples_ += samples.size();
            activation_level_sum_ += rms * samples.size();
            if (active_samples_ >= required_activity_samples_) {
                signal_detected_ = true;
                signal_reference_rms_ = activation_level_sum_ / active_samples_;
                silent_samples_ = 0;
            }
        } else {
            active_samples_ = 0;
            activation_level_sum_ = 0.0;
            noise_rms_ = noise_rms_ * 0.98 + rms * 0.02;
        }
        return;
    }

    // A speaker/microphone path often leaves a low-level AGC, echo-canceller
    // or room tail above the noise measured before transmission. Treat a
    // sustained level below -26 dB of the acquired signal as silence. The
    // absolute floor preserves detection of genuinely weak transmissions.
    const double activity_threshold = std::max(
        {0.001, noise_rms_ * 2.5, signal_reference_rms_ * 0.05});
    if (rms < activity_threshold) {
        silent_samples_ += samples.size();
        resumed_activity_samples_ = 0;
        should_stop_ = silent_samples_ >= required_silence_samples_;
    } else {
        resumed_activity_samples_ += samples.size();
        if (resumed_activity_samples_ >= required_activity_samples_) {
            silent_samples_ = 0;
            resumed_activity_samples_ = required_activity_samples_;
        }
    }
}

}  // namespace acoustic

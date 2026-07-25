#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace acoustic {

// Applies either an explicit gain or automatic gain based on short-window RMS.
// Returns the actual multiplier. A requested_gain of 0 enables auto mode.
double apply_receive_gain(std::span<float> samples,
                          double requested_gain = 0.0,
                          std::size_t analysis_window = 240);

// Streaming energy detector used by the audio backends. Initial silence does
// not stop recording: the timer starts only after sustained signal activity.
class SilenceDetector {
public:
    SilenceDetector(unsigned sample_rate, double silence_seconds = 5.0);
    void process_pcm16(std::span<const std::int16_t> samples);
    bool signal_detected() const { return signal_detected_; }
    bool should_stop() const { return should_stop_; }
    double noise_rms() const { return noise_rms_; }

private:
    unsigned sample_rate_{};
    std::size_t initialization_samples_{};
    std::size_t required_activity_samples_{};
    std::size_t required_silence_samples_{};
    std::size_t processed_samples_{};
    std::size_t active_samples_{};
    std::size_t silent_samples_{};
    std::size_t resumed_activity_samples_{};
    double noise_rms_ = 0.003;
    bool signal_detected_{};
    bool should_stop_{};
};

}  // namespace acoustic

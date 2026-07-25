#include "acoustic/framing.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const std::vector<std::uint8_t> data{0x48, 0x45, 0x58, 0x00, 0xFF};
    const auto frame = acoustic::create_audio_frame(data);
    std::vector<float> recording(7200, 0.002F);
    recording.insert(recording.end(), frame.begin(), frame.end());
    // Deliberately leave a tail that ends in the middle of an encoded byte.
    // Real arecord durations are not aligned to the modem's byte boundary.
    recording.insert(recording.end(), 5200, 0.0F);
    const auto decoded = acoustic::decode_audio_frame(recording);
    assert(decoded == data);

    // Simulate a 0.8% playback/capture clock mismatch. The receiver estimates
    // this from the chirp and resamples the payload before FSK decoding.
    constexpr double stretch = 1.008;
    std::vector<float> stretched;
    stretched.reserve(static_cast<std::size_t>(frame.size() * stretch));
    for (std::size_t i = 0; i < static_cast<std::size_t>(frame.size() * stretch); ++i) {
        const double source = i / stretch;
        const auto lower = static_cast<std::size_t>(source);
        const auto upper = std::min(lower + 1U, frame.size() - 1U);
        const double fraction = source - lower;
        stretched.push_back(static_cast<float>(frame[lower] * (1.0 - fraction) +
                                               frame[upper] * fraction));
    }
    std::vector<float> drifted_recording(4800, 0.001F);
    drifted_recording.insert(drifted_recording.end(), stretched.begin(), stretched.end());
    drifted_recording.insert(drifted_recording.end(), 2400, 0.0F);
    assert(acoustic::decode_audio_frame(drifted_recording) == data);

    // A notification/click before the sender starts must not pin chirp search
    // to the first loud event in the recording.
    std::vector<float> distracted(4800, 0.0F);
    for (std::size_t i = 0; i < 2400; ++i) {
        distracted.push_back((i % 2U == 0U) ? 0.45F : -0.45F);
    }
    distracted.insert(distracted.end(), 12000, 0.0F);
    distracted.insert(distracted.end(), frame.begin(), frame.end());
    distracted.insert(distracted.end(), 2400, 0.0F);
    assert(acoustic::decode_audio_frame(distracted) == data);

    // Windows audio processing and inexpensive microphones can heavily
    // distort a wideband preamble even when the following tones remain usable.
    // Exercise soft acquisition below the former 0.18 correlation cutoff.
    auto weak_preamble = frame;
    std::uint32_t state = 0x48455841U;
    constexpr std::size_t chirp_samples = 12000;
    for (std::size_t i = 0; i < chirp_samples; ++i) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        const double noise = (static_cast<double>(state & 0xFFFFU) / 32767.5 - 1.0) * 0.45;
        weak_preamble[i] = static_cast<float>(weak_preamble[i] * 0.08 + noise);
    }
    std::vector<float> weak_recording(2400, 0.001F);
    weak_recording.insert(weak_recording.end(), weak_preamble.begin(), weak_preamble.end());
    weak_recording.insert(weak_recording.end(), 2400, 0.0F);
    acoustic::ReceiverMetrics weak_metrics;
    assert(acoustic::decode_audio_frame(weak_recording, {}, {}, &weak_metrics) == data);
    assert(weak_metrics.chirp_correlation >= 0.08);
    assert(weak_metrics.chirp_correlation < 0.18);

    // A 100 ms dropout used to make the whole transfer unusable. Frame v2
    // interleaving and RS(255,191) must repair this burst transparently.
    std::vector<std::uint8_t> protected_data(900);
    for (std::size_t i = 0; i < protected_data.size(); ++i) {
        protected_data[i] = static_cast<std::uint8_t>((i * 73U + 19U) & 0xFFU);
    }
    auto damaged_frame = acoustic::create_audio_frame(protected_data);
    const auto symbol_samples = acoustic::samples_per_symbol({});
    const auto symbols_per_byte = 8U / acoustic::bits_per_symbol({});
    const auto prefix = static_cast<std::size_t>((0.25 + 0.05) * 48000);
    const auto fec_body = prefix + (4U + 84U) * symbols_per_byte * symbol_samples;
    const auto dropout_samples = 20U * symbols_per_byte * symbol_samples;
    std::fill_n(damaged_frame.begin() + fec_body + 5U * symbols_per_byte * symbol_samples,
                dropout_samples, 0.0F);
    std::vector<float> damaged_recording(2400, 0.001F);
    damaged_recording.insert(damaged_recording.end(), damaged_frame.begin(), damaged_frame.end());
    damaged_recording.insert(damaged_recording.end(), 2400, 0.0F);
    acoustic::ReceiverMetrics repaired_metrics;
    assert(acoustic::decode_audio_frame(damaged_recording, {}, {}, &repaired_metrics) ==
           protected_data);
    assert(repaired_metrics.corrected_bytes > 0);
    assert(repaired_metrics.corrected_codewords > 0);
    std::cout << "framing_tests: OK (chirp found after leading silence)\n";
}

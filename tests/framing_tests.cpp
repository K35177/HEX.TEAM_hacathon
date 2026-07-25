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
    std::cout << "framing_tests: OK (chirp found after leading silence)\n";
}

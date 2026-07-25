#include "acoustic/framing.hpp"

#include <cassert>
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
    std::cout << "framing_tests: OK (chirp found after leading silence)\n";
}

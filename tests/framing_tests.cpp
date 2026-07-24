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
    recording.insert(recording.end(), 4800, 0.0F);
    const auto decoded = acoustic::decode_audio_frame(recording);
    assert(decoded == data);
    std::cout << "framing_tests: OK (chirp found after leading silence)\n";
}

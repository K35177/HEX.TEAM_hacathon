#include "acoustic/fsk.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const std::vector<std::uint8_t> original{0x00, 0x01, 0x55, 0xAA, 0xFE, 0xFF};
    const auto samples = acoustic::modulate_bits(original);
    const auto restored = acoustic::demodulate_bits(samples);
    assert(restored == original);

    auto noisy = samples;
    for (std::size_t i = 0; i < noisy.size(); ++i) {
        noisy[i] += static_cast<float>((static_cast<int>(i % 17U) - 8) * 0.003);
    }
    assert(acoustic::demodulate_bits(noisy) == original);
    std::cout << "fsk_tests: OK\n";
}

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

    // Real microphone captures are not aligned to a byte or symbol boundary.
    auto with_partial_byte = samples;
    const auto one_symbol = acoustic::samples_per_symbol({});
    with_partial_byte.insert(with_partial_byte.end(), one_symbol + one_symbol / 2U, 0.0F);
    assert(acoustic::demodulate_bits(with_partial_byte) == original);

    acoustic::FskConfig robust;
    robust.modulation_order = 2;
    assert(acoustic::demodulate_bits(acoustic::modulate_bits(original, robust), robust) == original);
    std::cout << "fsk_tests: OK\n";
}

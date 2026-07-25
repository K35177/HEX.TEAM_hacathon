#include "acoustic/fsk.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    const std::vector<std::uint8_t> original{0x00, 0x01, 0x55, 0xAA, 0xFE, 0xFF};
    const auto samples = acoustic::modulate_bits(original);
    acoustic::FskMetrics metrics;
    const auto restored = acoustic::demodulate_bits(samples, {}, &metrics);
    assert(restored == original);
    assert(metrics.symbol_count == original.size() * 4U);
    assert(metrics.mean_confidence > 0.9);
    assert(std::abs(metrics.estimated_frequency_offset_hz) < 0.001);

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
    robust.symbol_repetitions = 3;
    assert(acoustic::demodulate_bits(acoustic::modulate_bits(original, robust), robust) == original);

    bool rejected = false;
    try {
        auto invalid = robust;
        invalid.base_frequency = 23900.0;
        (void)acoustic::modulate_bits(original, invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    std::cout << "fsk_tests: OK\n";
}

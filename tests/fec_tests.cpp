#include "acoustic/fec.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main() {
    std::vector<std::uint8_t> data(acoustic::kFecDataBytes);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>((i * 149U + 31U) & 0xFFU);
    }
    const auto encoded = acoustic::encode_fec_block(data);
    assert(acoustic::decode_fec_block(encoded) == data);

    std::mt19937 generator(0x484558U);
    for (std::size_t error_count = 1; error_count <= 32; ++error_count) {
        auto damaged = encoded;
        std::array<std::size_t, acoustic::kFecCodewordBytes> positions{};
        for (std::size_t i = 0; i < positions.size(); ++i) positions[i] = i;
        std::shuffle(positions.begin(), positions.end(), generator);
        for (std::size_t i = 0; i < error_count; ++i) {
            damaged[positions[i]] ^= static_cast<std::uint8_t>(1U + (generator() % 255U));
        }
        assert(acoustic::decode_fec_block(damaged) == data);
    }

    const std::vector<std::uint8_t> short_data{'H', 'E', 'X'};
    const auto padded = acoustic::decode_fec_block(acoustic::encode_fec_block(short_data));
    assert(std::equal(short_data.begin(), short_data.end(), padded.begin()));
    assert(std::all_of(padded.begin() + short_data.size(), padded.end(),
                       [](std::uint8_t value) { return value == 0; }));
    std::cout << "fec_tests: OK\n";
}

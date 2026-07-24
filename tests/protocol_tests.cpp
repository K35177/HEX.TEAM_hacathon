#include "acoustic/crc32.hpp"
#include "acoustic/packet.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    const std::vector<std::uint8_t> check{'1','2','3','4','5','6','7','8','9'};
    assert(acoustic::crc32(check) == 0xCBF43926U);

    acoustic::Packet packet{42, 2, 7, {0x00, 0xFF, 0x10, 0x20}};
    auto bytes = acoustic::serialize_packet(packet);
    const auto decoded = acoustic::deserialize_packet(bytes);
    assert(decoded.file_id == packet.file_id);
    assert(decoded.block_index == packet.block_index);
    assert(decoded.block_count == packet.block_count);
    assert(decoded.payload == packet.payload);

    bytes[15] ^= 1U;
    bool rejected = false;
    try { (void)acoustic::deserialize_packet(bytes); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    std::cout << "protocol_tests: OK\n";
}

#include "acoustic/packet.hpp"

#include "acoustic/crc32.hpp"

#include <limits>
#include <stdexcept>

namespace acoustic {
namespace {

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>((data[offset] << 8U) | data[offset + 1]);
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value = (value << 8U) | data[offset + i];
    return value;
}

}  // namespace

std::vector<std::uint8_t> serialize_packet(const Packet& packet) {
    if (packet.payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("packet payload is too large");
    }
    std::vector<std::uint8_t> result;
    result.reserve(19 + packet.payload.size());
    append_u32(result, kSyncWord);
    result.push_back(kProtocolVersion);
    append_u32(result, packet.file_id);
    append_u16(result, packet.block_index);
    append_u16(result, packet.block_count);
    append_u16(result, static_cast<std::uint16_t>(packet.payload.size()));
    result.insert(result.end(), packet.payload.begin(), packet.payload.end());
    append_u32(result, crc32(result));
    return result;
}

Packet deserialize_packet(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t header_size = 15;
    constexpr std::size_t crc_size = 4;
    if (bytes.size() < header_size + crc_size) throw std::runtime_error("packet is truncated");
    if (read_u32(bytes, 0) != kSyncWord) throw std::runtime_error("invalid sync word");
    if (bytes[4] != kProtocolVersion) throw std::runtime_error("unsupported protocol version");
    const auto payload_size = read_u16(bytes, 13);
    if (bytes.size() != header_size + payload_size + crc_size) {
        throw std::runtime_error("invalid packet length");
    }
    const auto expected_crc = read_u32(bytes, bytes.size() - crc_size);
    if (crc32(bytes.first(bytes.size() - crc_size)) != expected_crc) {
        throw std::runtime_error("CRC32 mismatch");
    }
    Packet result;
    result.file_id = read_u32(bytes, 5);
    result.block_index = read_u16(bytes, 9);
    result.block_count = read_u16(bytes, 11);
    result.payload.assign(bytes.begin() + header_size, bytes.end() - crc_size);
    return result;
}

}  // namespace acoustic

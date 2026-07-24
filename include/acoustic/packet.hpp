#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace acoustic {

inline constexpr std::uint32_t kSyncWord = 0x48455841U;  // "HEXA"
inline constexpr std::uint8_t kProtocolVersion = 1;

struct Packet {
    std::uint32_t file_id{};
    std::uint16_t block_index{};
    std::uint16_t block_count{};
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> serialize_packet(const Packet& packet);
Packet deserialize_packet(std::span<const std::uint8_t> bytes);

}  // namespace acoustic

#pragma once

#include <cstdint>
#include <span>

namespace acoustic {

std::uint32_t crc32(std::span<const std::uint8_t> data);

}  // namespace acoustic

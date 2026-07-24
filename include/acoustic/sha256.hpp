#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace acoustic {

using Sha256Digest = std::array<std::uint8_t, 32>;

Sha256Digest sha256(std::span<const std::uint8_t> data);
std::string sha256_hex(const Sha256Digest& digest);

}  // namespace acoustic

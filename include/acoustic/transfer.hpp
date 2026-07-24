#pragma once

#include "acoustic/fsk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace acoustic {

inline constexpr std::size_t kDefaultBlockSize = 128;

std::vector<std::uint8_t> create_transfer_stream(
    std::span<const std::uint8_t> file_data,
    std::size_t block_size = kDefaultBlockSize);
std::vector<std::uint8_t> restore_transfer_stream(
    std::span<const std::uint8_t> stream);

}  // namespace acoustic

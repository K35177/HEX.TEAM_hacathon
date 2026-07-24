#pragma once

#include "acoustic/sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace acoustic {

inline constexpr std::size_t kDefaultBlockSize = 128;

struct ReceivedFile {
    std::string filename;
    std::vector<std::uint8_t> data;
    Sha256Digest sha256;
};

std::vector<std::uint8_t> create_transfer_stream(
    std::span<const std::uint8_t> file_data,
    std::size_t block_size = kDefaultBlockSize,
    std::string_view filename = {});
ReceivedFile receive_transfer_stream(std::span<const std::uint8_t> stream);
std::vector<std::uint8_t> restore_transfer_stream(std::span<const std::uint8_t> stream);

}  // namespace acoustic

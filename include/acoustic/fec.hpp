#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace acoustic {

inline constexpr std::size_t kFecDataBytes = 191;
inline constexpr std::size_t kFecParityBytes = 64;
inline constexpr std::size_t kFecCodewordBytes = kFecDataBytes + kFecParityBytes;

std::array<std::uint8_t, kFecCodewordBytes> encode_fec_block(
    std::span<const std::uint8_t> data);
std::vector<std::uint8_t> decode_fec_block(
    std::span<const std::uint8_t> codeword,
    std::size_t* corrected_errors = nullptr);

}  // namespace acoustic

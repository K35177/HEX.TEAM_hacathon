#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace acoustic {

struct WavData {
    std::uint32_t sample_rate{};
    std::vector<float> samples;
};

void write_wav(const std::filesystem::path& path,
               std::span<const float> samples,
               std::uint32_t sample_rate);
WavData read_wav(const std::filesystem::path& path);

}  // namespace acoustic

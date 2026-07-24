#pragma once

#include <filesystem>
#include <string>

namespace acoustic {

bool command_available(const std::string& command);
void play_wav_file(const std::filesystem::path& path);
void record_wav_file(const std::filesystem::path& path, unsigned duration_seconds,
                     unsigned sample_rate = 48000);

}  // namespace acoustic

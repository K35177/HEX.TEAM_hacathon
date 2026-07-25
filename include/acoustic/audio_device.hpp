#pragma once

#include <filesystem>
#include <string>

namespace acoustic {

struct AudioDeviceStatus {
    std::string backend;
    bool playback_available{};
    bool recording_available{};
};

bool command_available(const std::string& command);
AudioDeviceStatus audio_device_status();
void play_wav_file(const std::filesystem::path& path);
void record_wav_file(const std::filesystem::path& path, unsigned duration_seconds,
                     unsigned sample_rate = 48000);

}  // namespace acoustic

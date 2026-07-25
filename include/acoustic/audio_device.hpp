#pragma once

#include <filesystem>
#include <string>

namespace acoustic {

struct AudioDeviceStatus {
    std::string backend;
    bool playback_available{};
    bool recording_available{};
};

struct SilenceRecordingResult {
    double duration_seconds{};
    double noise_rms{};
    bool signal_detected{};
    bool stopped_after_silence{};
};

bool command_available(const std::string& command);
AudioDeviceStatus audio_device_status();
void play_wav_file(const std::filesystem::path& path);
void record_wav_file(const std::filesystem::path& path, unsigned duration_seconds,
                     unsigned sample_rate = 48000);
SilenceRecordingResult record_wav_until_silence(
    const std::filesystem::path& path, unsigned sample_rate = 48000,
    unsigned silence_seconds = 5, unsigned maximum_duration_seconds = 3600);

}  // namespace acoustic

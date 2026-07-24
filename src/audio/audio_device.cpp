#include "acoustic/audio_device.hpp"

#include <cstdlib>
#include <stdexcept>

namespace acoustic {
namespace {

std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char ch : value) result += ch == '\'' ? "'\\''" : std::string(1, ch);
    return result + "'";
}

void run(const std::string& command, const std::string& error) {
    if (std::system(command.c_str()) != 0) throw std::runtime_error(error);
}
}  // namespace

bool command_available(const std::string& command) {
    return std::system(("command -v " + shell_quote(command) + " >/dev/null 2>&1").c_str()) == 0;
}

void play_wav_file(const std::filesystem::path& path) {
    if (!command_available("aplay")) throw std::runtime_error("aplay is not installed");
    run("aplay -q " + shell_quote(path.string()), "audio playback failed");
}

void record_wav_file(const std::filesystem::path& path, unsigned duration_seconds,
                     unsigned sample_rate) {
    if (!command_available("arecord")) throw std::runtime_error("arecord is not installed");
    const auto command = "arecord -q -t wav -f S16_LE -c 1 -r " +
        std::to_string(sample_rate) + " -d " + std::to_string(duration_seconds) + " " +
        shell_quote(path.string());
    run(command, "audio recording failed");
}

}  // namespace acoustic

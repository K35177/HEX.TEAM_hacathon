#include "acoustic/audio_device.hpp"

#include "acoustic/wav.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace acoustic {
namespace {

std::string shell_quote(const std::string& value) {
#if defined(_WIN32)
    std::string result = "\"";
    for (const char ch : value) result += ch == '"' ? "\\\"" : std::string(1, ch);
    return result + "\"";
#else
    std::string result = "'";
    for (const char ch : value) result += ch == '\'' ? "'\\''" : std::string(1, ch);
    return result + "'";
#endif
}

#if !defined(_WIN32)
void run(const std::string& command, const std::string& error) {
    if (std::system(command.c_str()) != 0) throw std::runtime_error(error);
}
#endif

#if defined(_WIN32) || defined(__APPLE__)
std::size_t capture_sample_count(unsigned duration_seconds, unsigned sample_rate) {
    if (duration_seconds == 0 || sample_rate == 0 ||
        duration_seconds > std::numeric_limits<std::size_t>::max() / sample_rate) {
        throw std::invalid_argument("invalid audio capture duration or sample rate");
    }
    return static_cast<std::size_t>(duration_seconds) * sample_rate;
}

void write_pcm(const std::filesystem::path& path, const std::vector<std::int16_t>& pcm,
               unsigned sample_rate) {
    if (pcm.empty()) throw std::runtime_error("audio recording returned no samples");
    std::vector<float> samples;
    samples.reserve(pcm.size());
    for (const auto sample : pcm) {
        samples.push_back(std::clamp(static_cast<float>(sample) / 32768.0F, -1.0F, 1.0F));
    }
    write_wav(path, samples, sample_rate);
}
#endif

#if defined(_WIN32)

std::string windows_audio_error(MMRESULT result) {
    wchar_t text[MAXERRORLENGTH]{};
    if (waveInGetErrorTextW(result, text, MAXERRORLENGTH) == MMSYSERR_NOERROR) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        std::string converted(size > 0 ? static_cast<std::size_t>(size) : 0, '\0');
        if (size > 1) {
            WideCharToMultiByte(CP_UTF8, 0, text, -1, converted.data(), size, nullptr, nullptr);
            converted.pop_back();
        }
        return converted;
    }
    return "Windows multimedia error " + std::to_string(result);
}

void check_windows_audio(MMRESULT result, const char* action) {
    if (result != MMSYSERR_NOERROR) {
        throw std::runtime_error(std::string(action) + ": " + windows_audio_error(result));
    }
}

#elif defined(__APPLE__)

void check_core_audio(OSStatus status, const char* action) {
    if (status != noErr) {
        throw std::runtime_error(std::string(action) + " failed (CoreAudio status " +
                                 std::to_string(status) + ")");
    }
}

struct MacCapture {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::int16_t> samples;
    std::size_t target_samples{};
    bool done{};
};

void mac_input_callback(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer,
                        const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*) {
    auto& capture = *static_cast<MacCapture*>(context);
    const auto* incoming = static_cast<const std::int16_t*>(buffer->mAudioData);
    const auto available = static_cast<std::size_t>(buffer->mAudioDataByteSize / sizeof(std::int16_t));
    bool enqueue_again = true;
    {
        std::lock_guard lock(capture.mutex);
        const auto remaining = capture.target_samples - capture.samples.size();
        const auto count = std::min(available, remaining);
        capture.samples.insert(capture.samples.end(), incoming, incoming + count);
        if (capture.samples.size() >= capture.target_samples) {
            capture.done = true;
            enqueue_again = false;
            capture.changed.notify_one();
        }
    }
    if (enqueue_again) {
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
        (void)AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    }
}

#endif

}  // namespace

bool command_available(const std::string& command) {
#if defined(_WIN32)
    return std::system(("where " + shell_quote(command) + " >NUL 2>NUL").c_str()) == 0;
#else
    return std::system(("command -v " + shell_quote(command) + " >/dev/null 2>&1").c_str()) == 0;
#endif
}

AudioDeviceStatus audio_device_status() {
#if defined(_WIN32)
    return {"Windows Multimedia", waveOutGetNumDevs() > 0, waveInGetNumDevs() > 0};
#elif defined(__APPLE__)
    return {"CoreAudio", command_available("afplay"), true};
#else
    return {"ALSA utilities", command_available("aplay"), command_available("arecord")};
#endif
}

void play_wav_file(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("WAV file does not exist: " + path.string());
    }
#if defined(_WIN32)
    if (!PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_SYNC | SND_NODEFAULT)) {
        throw std::runtime_error("Windows audio playback failed");
    }
#elif defined(__APPLE__)
    if (!command_available("afplay")) throw std::runtime_error("afplay is not available");
    run("afplay " + shell_quote(path.string()), "CoreAudio playback failed");
#else
    if (!command_available("aplay")) throw std::runtime_error("aplay is not installed");
    run("aplay -q " + shell_quote(path.string()), "ALSA audio playback failed");
#endif
}

void record_wav_file(const std::filesystem::path& path, unsigned duration_seconds,
                     unsigned sample_rate) {
#if defined(_WIN32) || defined(__APPLE__)
    const auto target_samples = capture_sample_count(duration_seconds, sample_rate);
#endif
#if defined(_WIN32)
    if (target_samples > std::numeric_limits<DWORD>::max() / sizeof(std::int16_t)) {
        throw std::invalid_argument("Windows audio capture is too long");
    }
    std::vector<std::int16_t> pcm(target_samples);
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8U;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (completed == nullptr) throw std::runtime_error("cannot create Windows audio event");
    HWAVEIN input{};
    MMRESULT result = waveInOpen(&input, WAVE_MAPPER, &format,
                                 reinterpret_cast<DWORD_PTR>(completed), 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        CloseHandle(completed);
        check_windows_audio(result, "cannot open microphone");
    }
    WAVEHDR header{};
    header.lpData = reinterpret_cast<LPSTR>(pcm.data());
    header.dwBufferLength = static_cast<DWORD>(pcm.size() * sizeof(std::int16_t));
    try {
        check_windows_audio(waveInPrepareHeader(input, &header, sizeof(header)),
                            "cannot prepare microphone buffer");
        check_windows_audio(waveInAddBuffer(input, &header, sizeof(header)),
                            "cannot queue microphone buffer");
        // waveInOpen may signal a CALLBACK_EVENT for the open notification.
        // Clear it after the buffer is queued so only buffer completion wakes
        // the recording wait.
        if (!ResetEvent(completed)) throw std::runtime_error("cannot reset microphone event");
        check_windows_audio(waveInStart(input), "cannot start microphone");
        const auto timeout = static_cast<DWORD>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(duration_seconds) * 1000U + 5000U,
            std::numeric_limits<DWORD>::max()));
        const auto wait_result = WaitForSingleObject(completed, timeout);
        if (wait_result == WAIT_FAILED) {
            throw std::runtime_error("waiting for microphone failed");
        }
        if (wait_result == WAIT_TIMEOUT) throw std::runtime_error("microphone recording timed out");
        (void)waveInStop(input);
        (void)waveInReset(input);
        const auto recorded = std::min<std::size_t>(pcm.size(),
            header.dwBytesRecorded / sizeof(std::int16_t));
        pcm.resize(recorded);
        (void)waveInUnprepareHeader(input, &header, sizeof(header));
        (void)waveInClose(input);
        CloseHandle(completed);
    } catch (...) {
        (void)waveInStop(input);
        (void)waveInReset(input);
        (void)waveInUnprepareHeader(input, &header, sizeof(header));
        (void)waveInClose(input);
        CloseHandle(completed);
        throw;
    }
    write_pcm(path, pcm, sample_rate);
#elif defined(__APPLE__)
    MacCapture capture;
    capture.target_samples = target_samples;
    capture.samples.reserve(target_samples);
    AudioStreamBasicDescription format{};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = sizeof(std::int16_t);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(std::int16_t);
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 16;
    AudioQueueRef queue{};
    check_core_audio(AudioQueueNewInput(&format, mac_input_callback, &capture, nullptr,
                                         nullptr, 0, &queue), "opening microphone");
    try {
        constexpr UInt32 buffer_bytes = 4096;
        for (int i = 0; i < 3; ++i) {
            AudioQueueBufferRef buffer{};
            check_core_audio(AudioQueueAllocateBuffer(queue, buffer_bytes, &buffer),
                             "allocating microphone buffer");
            buffer->mAudioDataByteSize = buffer_bytes;
            check_core_audio(AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr),
                             "queuing microphone buffer");
        }
        check_core_audio(AudioQueueStart(queue, nullptr), "starting microphone");
        std::unique_lock lock(capture.mutex);
        if (!capture.changed.wait_for(lock, std::chrono::seconds(duration_seconds + 5U),
                                      [&] { return capture.done; })) {
            throw std::runtime_error("CoreAudio recording timed out");
        }
        lock.unlock();
        check_core_audio(AudioQueueStop(queue, true), "stopping microphone");
        check_core_audio(AudioQueueDispose(queue, true), "closing microphone");
    } catch (...) {
        (void)AudioQueueStop(queue, true);
        (void)AudioQueueDispose(queue, true);
        throw;
    }
    write_pcm(path, capture.samples, sample_rate);
#else
    if (duration_seconds == 0 || sample_rate == 0) {
        throw std::invalid_argument("invalid audio capture duration or sample rate");
    }
    if (!command_available("arecord")) throw std::runtime_error("arecord is not installed");
    const auto command = "arecord -q -t wav -f S16_LE -c 1 -r " +
        std::to_string(sample_rate) + " -d " + std::to_string(duration_seconds) + " " +
        shell_quote(path.string());
    run(command, "ALSA audio recording failed");
#endif
}

}  // namespace acoustic

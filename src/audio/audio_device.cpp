#include "acoustic/audio_device.hpp"

#include "acoustic/audio_processing.hpp"
#include "acoustic/wav.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
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

struct WinCaptureState {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<WAVEHDR*> completed;
};

void CALLBACK wave_input_callback(HWAVEIN, UINT message, DWORD_PTR instance,
                                  DWORD_PTR parameter, DWORD_PTR) {
    if (message != WIM_DATA || instance == 0 || parameter == 0) return;
    auto& state = *reinterpret_cast<WinCaptureState*>(instance);
    {
        std::lock_guard lock(state.mutex);
        state.completed.push_back(reinterpret_cast<WAVEHDR*>(parameter));
    }
    state.changed.notify_one();
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
    SilenceDetector* silence_detector{};
    bool stopped_after_silence{};
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
        if (capture.done) {
            enqueue_again = false;
        } else {
            const auto remaining = capture.target_samples - capture.samples.size();
            const auto count = std::min(available, remaining);
            capture.samples.insert(capture.samples.end(), incoming, incoming + count);
            if (capture.silence_detector != nullptr) {
                capture.silence_detector->process_pcm16(
                    std::span<const std::int16_t>(incoming, count));
                capture.stopped_after_silence = capture.silence_detector->should_stop();
            }
        }
        if (capture.stopped_after_silence || capture.samples.size() >= capture.target_samples) {
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

SilenceRecordingResult record_wav_until_silence(
    const std::filesystem::path& path, unsigned sample_rate,
    unsigned silence_seconds, unsigned maximum_duration_seconds) {
    if (silence_seconds == 0 || maximum_duration_seconds <= silence_seconds) {
        throw std::invalid_argument("invalid silence recording duration");
    }
    const auto maximum_samples = capture_sample_count(maximum_duration_seconds, sample_rate);
    const auto chunk_samples = std::max<std::size_t>(1, sample_rate / 10U);
    SilenceDetector detector(sample_rate, silence_seconds);
    std::vector<std::int16_t> pcm;
    pcm.reserve(std::min(maximum_samples, static_cast<std::size_t>(sample_rate) * 60U));

#if defined(_WIN32)
    if (chunk_samples > std::numeric_limits<DWORD>::max() / sizeof(std::int16_t)) {
        throw std::invalid_argument("Windows audio chunk is too large");
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8U;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    constexpr std::size_t buffer_count = 3;
    std::array<std::vector<std::int16_t>, buffer_count> buffers;
    std::array<WAVEHDR, buffer_count> headers{};
    WinCaptureState state;
    HWAVEIN input{};
    check_windows_audio(waveInOpen(&input, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(wave_input_callback),
        reinterpret_cast<DWORD_PTR>(&state), CALLBACK_FUNCTION), "cannot open microphone");
    std::size_t prepared = 0;
    try {
        for (std::size_t i = 0; i < buffer_count; ++i) {
            buffers[i].resize(chunk_samples);
            headers[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
            headers[i].dwBufferLength = static_cast<DWORD>(chunk_samples * sizeof(std::int16_t));
            check_windows_audio(waveInPrepareHeader(input, &headers[i], sizeof(WAVEHDR)),
                                "cannot prepare microphone buffer");
            ++prepared;
            check_windows_audio(waveInAddBuffer(input, &headers[i], sizeof(WAVEHDR)),
                                "cannot queue microphone buffer");
        }
        check_windows_audio(waveInStart(input), "cannot start microphone");
        bool done = false;
        while (!done) {
            WAVEHDR* header = nullptr;
            {
                std::unique_lock lock(state.mutex);
                if (!state.changed.wait_for(lock, std::chrono::seconds(5),
                                            [&] { return !state.completed.empty(); })) {
                    throw std::runtime_error("microphone recording timed out");
                }
                header = state.completed.front();
                state.completed.pop_front();
            }
            const auto available = std::min<std::size_t>(
                header->dwBytesRecorded / sizeof(std::int16_t),
                maximum_samples - pcm.size());
            const auto* incoming = reinterpret_cast<const std::int16_t*>(header->lpData);
            pcm.insert(pcm.end(), incoming, incoming + available);
            detector.process_pcm16(std::span<const std::int16_t>(incoming, available));
            done = detector.should_stop() || pcm.size() >= maximum_samples;
            if (!done) {
                header->dwBytesRecorded = 0;
                header->dwFlags &= ~WHDR_DONE;
                check_windows_audio(waveInAddBuffer(input, header, sizeof(WAVEHDR)),
                                    "cannot requeue microphone buffer");
            }
        }
        (void)waveInStop(input);
        (void)waveInReset(input);
        for (std::size_t i = 0; i < prepared; ++i) {
            (void)waveInUnprepareHeader(input, &headers[i], sizeof(WAVEHDR));
        }
        (void)waveInClose(input);
    } catch (...) {
        (void)waveInStop(input);
        (void)waveInReset(input);
        for (std::size_t i = 0; i < prepared; ++i) {
            (void)waveInUnprepareHeader(input, &headers[i], sizeof(WAVEHDR));
        }
        (void)waveInClose(input);
        throw;
    }
#elif defined(__APPLE__)
    MacCapture capture;
    capture.target_samples = maximum_samples;
    capture.samples.reserve(std::min(maximum_samples,
        static_cast<std::size_t>(sample_rate) * 60U));
    capture.silence_detector = &detector;
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
        const UInt32 buffer_bytes = static_cast<UInt32>(chunk_samples * sizeof(std::int16_t));
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
        if (!capture.changed.wait_for(lock, std::chrono::seconds(maximum_duration_seconds + 5U),
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
    pcm = std::move(capture.samples);
#else
    if (!command_available("arecord")) throw std::runtime_error("arecord is not installed");
    const auto command = "arecord -q -t raw -f S16_LE -c 1 -r " +
        std::to_string(sample_rate) + " -d " + std::to_string(maximum_duration_seconds) + " -";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) throw std::runtime_error("cannot start ALSA recording");
    std::vector<std::int16_t> chunk(chunk_samples);
    while (pcm.size() < maximum_samples && !detector.should_stop()) {
        const auto count = std::fread(chunk.data(), sizeof(std::int16_t), chunk.size(), pipe);
        if (count == 0) break;
        const auto accepted = std::min(count, maximum_samples - pcm.size());
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + accepted);
        detector.process_pcm16(std::span<const std::int16_t>(chunk.data(), accepted));
    }
    const int status = pclose(pipe);
    if (status != 0 && !detector.should_stop()) {
        throw std::runtime_error("ALSA audio recording failed");
    }
#endif

    write_pcm(path, pcm, sample_rate);
    return {
        pcm.size() / static_cast<double>(sample_rate),
        detector.noise_rms(),
        detector.signal_detected(),
        detector.should_stop(),
    };
}

}  // namespace acoustic

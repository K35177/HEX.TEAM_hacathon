#include "acoustic/framing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace acoustic {
namespace {
constexpr double pi = 3.14159265358979323846;
}

std::vector<float> create_audio_frame(std::span<const std::uint8_t> bytes,
                                      const FskConfig& modem,
                                      const FrameConfig& frame) {
    const auto chirp_samples = static_cast<std::size_t>(frame.chirp_duration_seconds * modem.sample_rate);
    const auto guard_samples = static_cast<std::size_t>(frame.guard_duration_seconds * modem.sample_rate);
    std::vector<float> output;
    output.reserve(chirp_samples + guard_samples + bytes.size() * 4U * samples_per_symbol(modem));
    double phase = 0.0;
    for (std::size_t i = 0; i < chirp_samples; ++i) {
        const double ratio = static_cast<double>(i) / std::max<std::size_t>(1, chirp_samples - 1);
        const double frequency = frame.chirp_start_frequency +
            ratio * (frame.chirp_end_frequency - frame.chirp_start_frequency);
        phase += 2.0 * pi * frequency / modem.sample_rate;
        output.push_back(static_cast<float>(modem.amplitude * std::sin(phase)));
    }
    output.insert(output.end(), guard_samples, 0.0F);
    if (bytes.size() > 0xFFFFFFFFULL) throw std::invalid_argument("audio frame payload is too large");
    std::vector<std::uint8_t> framed_bytes{
        static_cast<std::uint8_t>(bytes.size() >> 24U),
        static_cast<std::uint8_t>(bytes.size() >> 16U),
        static_cast<std::uint8_t>(bytes.size() >> 8U),
        static_cast<std::uint8_t>(bytes.size())};
    framed_bytes.insert(framed_bytes.end(), bytes.begin(), bytes.end());
    const auto payload = modulate_bits(framed_bytes, modem);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::vector<std::uint8_t> decode_audio_frame(std::span<const float> samples,
                                             const FskConfig& modem,
                                             const FrameConfig& frame) {
    const auto chirp_samples = static_cast<std::size_t>(frame.chirp_duration_seconds * modem.sample_rate);
    const auto guard_samples = static_cast<std::size_t>(frame.guard_duration_seconds * modem.sample_rate);
    const auto window = std::max<std::size_t>(64, modem.sample_rate / 200U);
    if (samples.size() < chirp_samples + guard_samples + window) {
        throw std::runtime_error("recording is too short for an audio frame");
    }
    const auto noise_search = std::min<std::size_t>(modem.sample_rate / 2U, samples.size());
    double noise_rms = 1.0;
    for (std::size_t offset = 0; offset + window <= noise_search; offset += window) {
        double energy = 0.0;
        for (std::size_t i = 0; i < window; ++i) energy += samples[offset + i] * samples[offset + i];
        noise_rms = std::min(noise_rms, std::sqrt(energy / window));
    }
    const double threshold = std::max(0.05, noise_rms * 4.0);
    std::size_t approximate_start = samples.size();
    for (std::size_t offset = 0; offset + window <= samples.size(); offset += window / 2U) {
        double energy = 0.0;
        for (std::size_t i = 0; i < window; ++i) energy += samples[offset + i] * samples[offset + i];
        if (std::sqrt(energy / window) > threshold) { approximate_start = offset; break; }
    }
    if (approximate_start == samples.size()) throw std::runtime_error("chirp preamble not detected");
    std::vector<float> reference;
    reference.reserve(chirp_samples);
    double phase = 0.0;
    for (std::size_t i = 0; i < chirp_samples; ++i) {
        const double ratio = static_cast<double>(i) / std::max<std::size_t>(1, chirp_samples - 1);
        const double frequency = frame.chirp_start_frequency +
            ratio * (frame.chirp_end_frequency - frame.chirp_start_frequency);
        phase += 2.0 * pi * frequency / modem.sample_rate;
        reference.push_back(static_cast<float>(modem.amplitude * std::sin(phase)));
    }
    const auto search_begin = approximate_start > window ? approximate_start - window : 0;
    const auto search_end = std::min(samples.size() - chirp_samples,
                                     approximate_start + 2U * window);
    std::size_t chirp_start = search_begin;
    double best_correlation = -1.0;
    for (std::size_t candidate = search_begin; candidate <= search_end; ++candidate) {
        double correlation = 0.0;
        for (std::size_t i = 0; i < chirp_samples; ++i) {
            correlation += samples[candidate + i] * reference[i];
        }
        correlation = std::abs(correlation);
        if (correlation > best_correlation) {
            best_correlation = correlation;
            chirp_start = candidate;
        }
    }
    const auto payload_start = chirp_start + chirp_samples + guard_samples;
    if (payload_start >= samples.size()) throw std::runtime_error("audio frame has no payload");
    const auto symbol_samples = samples_per_symbol(modem);
    const auto byte_symbols = 8U / bits_per_symbol(modem);
    const auto available_symbols = (samples.size() - payload_start) / symbol_samples;
    const auto complete_symbols = (available_symbols / byte_symbols) * byte_symbols;
    const auto usable = complete_symbols * symbol_samples;
    auto decoded = demodulate_bits(samples.subspan(payload_start, usable), modem);
    if (decoded.size() < 4) throw std::runtime_error("audio frame length is missing");
    const std::size_t payload_size = (static_cast<std::size_t>(decoded[0]) << 24U) |
        (static_cast<std::size_t>(decoded[1]) << 16U) |
        (static_cast<std::size_t>(decoded[2]) << 8U) | decoded[3];
    if (payload_size > decoded.size() - 4U) throw std::runtime_error("audio frame payload is truncated");
    return {decoded.begin() + 4, decoded.begin() + 4 + payload_size};
}

}  // namespace acoustic

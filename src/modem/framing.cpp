#include "acoustic/framing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace acoustic {
namespace {
constexpr double pi = 3.14159265358979323846;

void validate_frame(const FskConfig& modem, const FrameConfig& frame) {
    (void)samples_per_symbol(modem);
    if (!std::isfinite(frame.chirp_duration_seconds) ||
        !std::isfinite(frame.guard_duration_seconds) ||
        !std::isfinite(frame.chirp_start_frequency) ||
        !std::isfinite(frame.chirp_end_frequency) ||
        frame.chirp_duration_seconds <= 0.0 || frame.guard_duration_seconds < 0.0 ||
        frame.chirp_start_frequency <= 0.0 ||
        frame.chirp_end_frequency <= frame.chirp_start_frequency ||
        frame.chirp_end_frequency >= modem.sample_rate * 0.45) {
        throw std::invalid_argument("invalid audio frame configuration");
    }
}

std::vector<float> create_chirp(std::size_t chirp_samples, const FskConfig& modem,
                                const FrameConfig& frame) {
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
    return reference;
}

double normalized_correlation(std::span<const float> samples, std::size_t start,
                              std::span<const float> reference, double scale,
                              std::size_t stride = 1) {
    if (scale <= 0.0 || reference.empty()) return -1.0;
    const auto final_index = start + static_cast<std::size_t>(
        std::llround((reference.size() - 1U) * scale));
    if (final_index >= samples.size()) return -1.0;
    double product = 0.0;
    double sample_energy = 0.0;
    double reference_energy = 0.0;
    for (std::size_t i = 0; i < reference.size(); i += stride) {
        const auto source = start + static_cast<std::size_t>(std::llround(i * scale));
        const double observed = samples[source];
        const double expected = reference[i];
        product += observed * expected;
        sample_energy += observed * observed;
        reference_energy += expected * expected;
    }
    const double denominator = std::sqrt(sample_energy * reference_energy);
    return denominator <= std::numeric_limits<double>::epsilon() ? -1.0 :
        std::abs(product) / denominator;
}
}

std::vector<float> create_audio_frame(std::span<const std::uint8_t> bytes,
                                      const FskConfig& modem,
                                      const FrameConfig& frame) {
    validate_frame(modem, frame);
    const auto chirp_samples = static_cast<std::size_t>(frame.chirp_duration_seconds * modem.sample_rate);
    const auto guard_samples = static_cast<std::size_t>(frame.guard_duration_seconds * modem.sample_rate);
    std::vector<float> output;
    const auto symbols_per_byte = 8U / bits_per_symbol(modem);
    output.reserve(chirp_samples + guard_samples +
                   (bytes.size() + 4U) * symbols_per_byte * samples_per_symbol(modem));
    const auto chirp = create_chirp(chirp_samples, modem, frame);
    output.insert(output.end(), chirp.begin(), chirp.end());
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
                                             const FrameConfig& frame,
                                             ReceiverMetrics* metrics) {
    validate_frame(modem, frame);
    if (metrics != nullptr) {
        *metrics = {};
        metrics->clock_scale = 1.0;
        std::size_t clipped = 0;
        for (const float sample : samples) {
            if (std::abs(sample) >= 0.999F) ++clipped;
        }
        metrics->clipping_ratio = samples.empty() ? 0.0 :
            static_cast<double>(clipped) / samples.size();
    }
    const auto chirp_samples = static_cast<std::size_t>(frame.chirp_duration_seconds * modem.sample_rate);
    const auto guard_samples = static_cast<std::size_t>(frame.guard_duration_seconds * modem.sample_rate);
    const auto window = std::max<std::size_t>(64, modem.sample_rate / 200U);
    if (samples.size() < chirp_samples + guard_samples + window) {
        throw std::runtime_error("recording is too short for an audio frame");
    }
    const auto noise_search = std::min<std::size_t>(modem.sample_rate / 2U, samples.size());
    std::vector<double> noise_windows;
    for (std::size_t offset = 0; offset + window <= noise_search; offset += window) {
        double energy = 0.0;
        for (std::size_t i = 0; i < window; ++i) energy += samples[offset + i] * samples[offset + i];
        noise_windows.push_back(std::sqrt(energy / window));
    }
    std::sort(noise_windows.begin(), noise_windows.end());
    const double noise_rms = noise_windows.empty() ? 0.0 :
        noise_windows[noise_windows.size() / 20U];
    if (metrics != nullptr) metrics->noise_rms = noise_rms;
    const double threshold = std::max(0.015, noise_rms * 4.0);
    std::size_t approximate_start = samples.size();
    for (std::size_t offset = 0; offset + window <= samples.size(); offset += window / 2U) {
        double energy = 0.0;
        for (std::size_t i = 0; i < window; ++i) energy += samples[offset + i] * samples[offset + i];
        if (std::sqrt(energy / window) > threshold) { approximate_start = offset; break; }
    }
    if (approximate_start == samples.size()) throw std::runtime_error("chirp preamble not detected");
    const auto reference = create_chirp(chirp_samples, modem, frame);
    const auto search_begin = approximate_start > 2U * window ? approximate_start - 2U * window : 0;
    const auto search_end = std::min(samples.size() - chirp_samples,
                                     approximate_start + 3U * window);
    std::size_t chirp_start = search_begin;
    double best_correlation = -1.0;
    for (std::size_t candidate = search_begin; candidate <= search_end; ++candidate) {
        const double correlation = normalized_correlation(samples, candidate, reference, 1.0, 2);
        if (correlation > best_correlation) {
            best_correlation = correlation;
            chirp_start = candidate;
        }
    }
    if (metrics != nullptr) metrics->chirp_correlation = best_correlation;
    if (best_correlation < 0.18) throw std::runtime_error("chirp preamble correlation is too weak");

    // Estimate the playback/capture clock ratio from the chirp and resample
    // the payload to nominal timing. This prevents symbol drift on long files.
    double clock_scale = 1.0;
    double best_scale_correlation = -1.0;
    std::size_t scaled_chirp_start = chirp_start;
    const auto scale_search_begin = chirp_start > window ? chirp_start - window : 0U;
    const auto scale_search_end = std::min(samples.size() - chirp_samples,
                                           chirp_start + window);
    for (int step = -20; step <= 20; ++step) {
        const double candidate_scale = 1.0 + step * 0.001;
        for (std::size_t candidate_start = scale_search_begin;
             candidate_start <= scale_search_end; candidate_start += 4U) {
            const double correlation = normalized_correlation(samples, candidate_start, reference,
                                                              candidate_scale, 4);
            if (correlation > best_scale_correlation) {
                best_scale_correlation = correlation;
                clock_scale = candidate_scale;
                scaled_chirp_start = candidate_start;
            }
        }
    }
    const auto refine_begin = scaled_chirp_start > 4U ? scaled_chirp_start - 4U : 0U;
    const auto refine_end = std::min(samples.size() - chirp_samples, scaled_chirp_start + 4U);
    for (std::size_t candidate_start = refine_begin; candidate_start <= refine_end;
         ++candidate_start) {
        const double correlation = normalized_correlation(samples, candidate_start, reference,
                                                          clock_scale, 2);
        if (correlation > best_scale_correlation) {
            best_scale_correlation = correlation;
            scaled_chirp_start = candidate_start;
        }
    }
    chirp_start = scaled_chirp_start;
    best_correlation = best_scale_correlation;
    if (metrics != nullptr) {
        metrics->chirp_correlation = best_correlation;
        metrics->clock_scale = clock_scale;
    }
    const auto payload_start = chirp_start + static_cast<std::size_t>(std::llround(
        (chirp_samples + guard_samples) * clock_scale));
    if (payload_start >= samples.size()) throw std::runtime_error("audio frame has no payload");
    std::vector<float> timing_corrected;
    std::span<const float> payload_samples = samples.subspan(payload_start);
    if (std::abs(clock_scale - 1.0) >= 0.0005) {
        const auto corrected_size = static_cast<std::size_t>(payload_samples.size() / clock_scale);
        timing_corrected.reserve(corrected_size);
        for (std::size_t i = 0; i < corrected_size; ++i) {
            const double source = i * clock_scale;
            const auto lower = static_cast<std::size_t>(source);
            const auto upper = std::min(lower + 1U, payload_samples.size() - 1U);
            const double fraction = source - lower;
            timing_corrected.push_back(static_cast<float>(
                payload_samples[lower] * (1.0 - fraction) + payload_samples[upper] * fraction));
        }
        payload_samples = timing_corrected;
    }
    const auto symbol_samples = samples_per_symbol(modem);
    const auto byte_symbols = 8U / bits_per_symbol(modem);
    const auto available_symbols = payload_samples.size() / symbol_samples;
    const auto complete_symbols = (available_symbols / byte_symbols) * byte_symbols;
    const auto usable = complete_symbols * symbol_samples;
    FskMetrics fsk_metrics;
    auto decoded = demodulate_bits(payload_samples.first(usable), modem, &fsk_metrics);
    if (decoded.size() < 4) throw std::runtime_error("audio frame length is missing");
    const std::size_t payload_size = (static_cast<std::size_t>(decoded[0]) << 24U) |
        (static_cast<std::size_t>(decoded[1]) << 16U) |
        (static_cast<std::size_t>(decoded[2]) << 8U) | decoded[3];
    if (payload_size > decoded.size() - 4U) throw std::runtime_error("audio frame payload is truncated");
    if (metrics != nullptr) {
        double signal_energy = 0.0;
        const auto observed_chirp_samples = std::min(
            samples.size() - chirp_start,
            static_cast<std::size_t>(std::llround(chirp_samples * clock_scale)));
        for (std::size_t i = 0; i < observed_chirp_samples; ++i) {
            const double sample = samples[chirp_start + i];
            signal_energy += sample * sample;
        }
        metrics->chirp_correlation = best_correlation;
        metrics->clock_scale = clock_scale;
        metrics->noise_rms = noise_rms;
        metrics->signal_rms = observed_chirp_samples == 0 ? 0.0 :
            std::sqrt(signal_energy / observed_chirp_samples);
        metrics->mean_symbol_confidence = fsk_metrics.mean_confidence;
        metrics->minimum_symbol_confidence = fsk_metrics.minimum_confidence;
        metrics->estimated_frequency_offset_hz = fsk_metrics.estimated_frequency_offset_hz;
        metrics->decoded_symbols = fsk_metrics.symbol_count;
        metrics->decoded_bytes = payload_size;
    }
    return {decoded.begin() + 4, decoded.begin() + 4 + payload_size};
}

}  // namespace acoustic

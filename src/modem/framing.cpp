#include "acoustic/framing.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

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
    const double final_position = start + (reference.size() - 1U) * scale;
    if (std::ceil(final_position) >= samples.size()) return -1.0;
    double product = 0.0;
    double sample_energy = 0.0;
    double reference_energy = 0.0;
    for (std::size_t i = 0; i < reference.size(); i += stride) {
        const double source = start + i * scale;
        const auto lower = static_cast<std::size_t>(source);
        const auto upper = lower + 1U;
        const double fraction = source - lower;
        const double observed = samples[lower] * (1.0 - fraction) + samples[upper] * fraction;
        const double expected = reference[i];
        product += observed * expected;
        sample_energy += observed * observed;
        reference_energy += expected * expected;
    }
    const double denominator = std::sqrt(sample_energy * reference_energy);
    return denominator <= std::numeric_limits<double>::epsilon() ? -1.0 :
        std::abs(product) / denominator;
}

std::vector<float> resample_region(std::span<const float> samples, std::size_t start,
                                   double scale, std::size_t output_samples) {
    if (output_samples == 0) return {};
    const double final_position = start + (output_samples - 1U) * scale;
    if (scale <= 0.0 || final_position > samples.size() - 1U) {
        throw std::runtime_error("audio frame payload is truncated");
    }
    std::vector<float> corrected;
    corrected.reserve(output_samples);
    for (std::size_t i = 0; i < output_samples; ++i) {
        const double source = start + i * scale;
        const auto lower = static_cast<std::size_t>(source);
        const auto upper = std::min(lower + 1U, samples.size() - 1U);
        const double fraction = source - lower;
        corrected.push_back(static_cast<float>(
            samples[lower] * (1.0 - fraction) + samples[upper] * fraction));
    }
    return corrected;
}

std::size_t sustained_activity_end(std::span<const float> samples,
                                   std::size_t expected_end,
                                   std::size_t nominal_frame_samples,
                                   unsigned sample_rate,
                                   double noise_rms) {
    const auto window = std::max<std::size_t>(16U, sample_rate / 1000U);
    const auto search_radius = std::max<std::size_t>(
        static_cast<std::size_t>(sample_rate) * 2U, nominal_frame_samples / 20U);
    const auto begin = expected_end > search_radius ? expected_end - search_radius : 0U;
    const auto end = std::min(samples.size(), expected_end + search_radius);
    if (end <= begin + window) return 0;
    const double threshold = std::max(0.01, noise_rms * 4.0);
    const auto minimum_run = std::max<std::size_t>(window, sample_rate / 100U);
    std::size_t run_begin = 0;
    bool in_run = false;
    std::size_t best_end = 0;
    std::size_t best_distance = std::numeric_limits<std::size_t>::max();
    const auto consider_run = [&](std::size_t run_end) {
        if (!in_run || run_end - run_begin < minimum_run) return;
        const auto distance = run_end > expected_end ? run_end - expected_end :
                                                        expected_end - run_end;
        if (distance < best_distance) {
            best_distance = distance;
            best_end = run_end;
        }
    };
    for (std::size_t offset = begin; offset + window <= end; offset += window) {
        double energy = 0.0;
        for (std::size_t i = 0; i < window; ++i) {
            const double sample = samples[offset + i];
            energy += sample * sample;
        }
        const bool active = std::sqrt(energy / window) > threshold;
        if (active && !in_run) {
            run_begin = offset;
            in_run = true;
        } else if (!active && in_run) {
            consider_run(offset);
            in_run = false;
        }
    }
    if (in_run) consider_run(end);
    return best_end;
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
    std::vector<std::pair<double, std::size_t>> energy_onsets;
    bool energy_active = false;
    for (std::size_t offset = 0; offset + window <= samples.size(); offset += window / 2U) {
        double energy = 0.0;
        for (std::size_t i = 0; i < window; ++i) energy += samples[offset + i] * samples[offset + i];
        const double level = std::sqrt(energy / window);
        const bool above_threshold = level > threshold;
        if (above_threshold && !energy_active) energy_onsets.emplace_back(level, offset);
        energy_active = above_threshold;
    }
    if (energy_onsets.empty()) throw std::runtime_error("chirp preamble not detected");
    // A click, speech or notification may happen between starting the receiver
    // and starting the sender. Search the strongest independent onsets instead
    // of assuming the first loud window is the modem preamble.
    constexpr std::size_t maximum_onsets = 32;
    if (energy_onsets.size() > maximum_onsets) {
        std::partial_sort(energy_onsets.begin(), energy_onsets.begin() + maximum_onsets,
                          energy_onsets.end(), std::greater<>());
        energy_onsets.resize(maximum_onsets);
    }
    const auto reference = create_chirp(chirp_samples, modem, frame);
    std::size_t approximate_start = energy_onsets.front().second;
    std::size_t chirp_start = 0;
    double best_correlation = -1.0;
    for (const auto& [level, onset] : energy_onsets) {
        (void)level;
        const auto search_begin = onset > 2U * window ? onset - 2U * window : 0U;
        const auto search_end = std::min(samples.size() - chirp_samples,
                                         onset + 3U * window);
        for (std::size_t candidate = search_begin; candidate <= search_end; candidate += 4U) {
            const double correlation = normalized_correlation(samples, candidate, reference, 1.0, 8);
            if (correlation > best_correlation) {
                best_correlation = correlation;
                chirp_start = candidate;
                approximate_start = onset;
            }
        }
    }
    const auto initial_refine_begin = chirp_start > 4U ? chirp_start - 4U : 0U;
    const auto initial_refine_end = std::min(samples.size() - chirp_samples, chirp_start + 4U);
    for (std::size_t candidate = initial_refine_begin; candidate <= initial_refine_end; ++candidate) {
        const double correlation = normalized_correlation(samples, candidate, reference, 1.0, 2);
        if (correlation > best_correlation) {
            best_correlation = correlation;
            chirp_start = candidate;
        }
    }
    if (metrics != nullptr) metrics->chirp_correlation = best_correlation;

    // Estimate the playback/capture clock ratio from the chirp and resample
    // the payload to nominal timing. This prevents symbol drift on long files.
    double clock_scale = 1.0;
    double best_scale_correlation = -1.0;
    std::size_t scaled_chirp_start = chirp_start;
    // The best unscaled chirp match can move hundreds of samples when a
    // wideband chirp is captured with clock drift.  Anchor the joint
    // start/scale search to the energy onset instead of that biased match.
    const auto scale_search_begin = approximate_start > window ? approximate_start - window : 0U;
    const auto scale_search_end = std::min(samples.size() - chirp_samples,
                                           approximate_start + 2U * window);
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
    // Do not reject a marginal coarse match before the joint position/clock
    // search has had a chance to recover it.  Payload CRC32 and the transfer
    // SHA-256 remain the hard acceptance gates, so a softer acquisition limit
    // improves real microphone tolerance without accepting a damaged file.
    constexpr double minimum_chirp_correlation = 0.08;
    if (best_correlation < minimum_chirp_correlation) {
        throw std::runtime_error("chirp preamble correlation is too weak (best=" +
                                 std::to_string(best_correlation) + ", minimum=" +
                                 std::to_string(minimum_chirp_correlation) + ')');
    }
    const auto symbol_samples = samples_per_symbol(modem);
    const auto byte_symbols = 8U / bits_per_symbol(modem);
    const auto header_samples = 4U * byte_symbols * symbol_samples;
    const auto nominal_payload_start = chirp_start + static_cast<std::size_t>(std::llround(
        (chirp_samples + guard_samples) * clock_scale));
    if (nominal_payload_start >= samples.size()) {
        throw std::runtime_error("audio frame has no payload");
    }
    struct HeaderCandidate {
        std::size_t payload_start{};
        std::size_t payload_size{};
        std::size_t nominal_payload_samples{};
        double clock_scale{};
        double confidence{};
        int offset{};
    };
    HeaderCandidate best_header;
    bool header_found = false;
    const auto consider_header = [&](int offset) {
        if (offset < 0 && nominal_payload_start < static_cast<std::size_t>(-offset)) return;
        const auto candidate_start = offset < 0 ?
            nominal_payload_start - static_cast<std::size_t>(-offset) :
            nominal_payload_start + static_cast<std::size_t>(offset);
        if (candidate_start >= samples.size()) return;
        try {
            const auto header_audio = resample_region(samples, candidate_start, clock_scale,
                                                      header_samples);
            FskMetrics header_metrics;
            const auto header = demodulate_bits(header_audio, modem, &header_metrics);
            if (header.size() < 4) return;
            const std::size_t candidate_size =
                (static_cast<std::size_t>(header[0]) << 24U) |
                (static_cast<std::size_t>(header[1]) << 16U) |
                (static_cast<std::size_t>(header[2]) << 8U) | header[3];
            const auto samples_per_byte = byte_symbols * symbol_samples;
            if (candidate_size > (samples.size() - candidate_start) / samples_per_byte ||
                candidate_size > (std::numeric_limits<std::size_t>::max() /
                    samples_per_byte) - 4U) return;
            const auto candidate_samples = (candidate_size + 4U) * samples_per_byte;
            const auto expected_end = candidate_start + static_cast<std::size_t>(std::llround(
                candidate_samples * clock_scale));
            const auto activity_end = sustained_activity_end(
                samples, expected_end, candidate_samples, modem.sample_rate, noise_rms);
            double endpoint_scale = clock_scale;
            bool endpoint_valid = false;
            if (activity_end > candidate_start) {
                const double measured_scale = (activity_end - candidate_start) /
                    static_cast<double>(candidate_samples);
                if (measured_scale >= 0.98 && measured_scale <= 1.02) {
                    endpoint_scale = measured_scale;
                    endpoint_valid = true;
                }
            }
            const double candidate_score = header_metrics.mean_confidence +
                (endpoint_valid ? 1.0 : 0.0) -
                0.25 * std::abs(offset) / static_cast<double>(symbol_samples);
            if (!header_found || candidate_score > best_header.confidence) {
                best_header = {candidate_start, candidate_size, candidate_samples,
                               endpoint_scale, candidate_score, offset};
                header_found = true;
            }
        } catch (const std::exception&) {
            // This timing hypothesis cannot contain a complete header.
        }
    };
    const auto coarse_step = std::max<std::size_t>(1U, symbol_samples / 20U);
    const auto timing_search = static_cast<int>(symbol_samples * 2U);
    for (int offset = -timing_search; offset <= timing_search;
         offset += static_cast<int>(coarse_step)) {
        consider_header(offset);
    }
    if (header_found && coarse_step > 1U) {
        const auto coarse_offset = best_header.offset;
        for (int offset = coarse_offset - static_cast<int>(coarse_step) + 1;
             offset < coarse_offset + static_cast<int>(coarse_step); ++offset) {
            consider_header(offset);
        }
    }
    if (!header_found) {
        throw std::runtime_error("audio frame length header is not plausible");
    }
    const auto payload_start = best_header.payload_start;
    const auto initial_payload_size = best_header.payload_size;
    const auto nominal_payload_samples = best_header.nominal_payload_samples;
    clock_scale = best_header.clock_scale;
    if (metrics != nullptr) metrics->clock_scale = clock_scale;
    const auto payload_samples = resample_region(samples, payload_start, clock_scale,
                                                 nominal_payload_samples);
    FskMetrics fsk_metrics;
    auto decoded = demodulate_bits(payload_samples, modem, &fsk_metrics);
    if (decoded.size() < 4) throw std::runtime_error("audio frame length is missing");
    const std::size_t payload_size = (static_cast<std::size_t>(decoded[0]) << 24U) |
        (static_cast<std::size_t>(decoded[1]) << 16U) |
        (static_cast<std::size_t>(decoded[2]) << 8U) | decoded[3];
    if (payload_size != initial_payload_size || payload_size > decoded.size() - 4U) {
        throw std::runtime_error("audio frame length changed after clock refinement (initial=" +
                                 std::to_string(initial_payload_size) + ", refined=" +
                                 std::to_string(payload_size) + ')');
    }
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

#include "acoustic/fsk.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace acoustic {
namespace {
constexpr double pi = 3.14159265358979323846;

void validate(const FskConfig& config) {
    if (config.modulation_order != 2 && config.modulation_order != 4) {
        throw std::invalid_argument("FSK modulation order must be 2 or 4");
    }
    if (config.sample_rate == 0 || config.symbol_rate == 0 ||
        config.sample_rate % config.symbol_rate != 0) {
        throw std::invalid_argument("sample rate must be divisible by symbol rate");
    }
    if (config.symbol_repetitions == 0 || config.symbol_repetitions > 16) {
        throw std::invalid_argument("FSK symbol repetitions must be between 1 and 16");
    }
    if (!std::isfinite(config.base_frequency) || !std::isfinite(config.frequency_spacing) ||
        !std::isfinite(config.amplitude) || config.base_frequency <= 0.0 ||
        config.frequency_spacing <= 0.0 || config.amplitude <= 0.0 || config.amplitude > 1.0) {
        throw std::invalid_argument("invalid FSK frequencies or amplitude");
    }
    const double highest_frequency = config.base_frequency +
        (config.modulation_order - 1U) * config.frequency_spacing;
    if (highest_frequency >= static_cast<double>(config.sample_rate) * 0.45) {
        throw std::invalid_argument("FSK frequencies are too close to the Nyquist limit");
    }
}

struct ToneDetector {
    double coefficient{};
    double offset_hz{};
};

double tone_energy(std::span<const float> samples, std::size_t start,
                   std::span<const double> window, const ToneDetector& detector) {
    // Goertzel evaluates one DFT frequency with one multiply per sample.  The
    // previous implementation evaluated sin/cos for every sample, candidate
    // and symbol, which dominated decode time for long transfers.
    double previous = 0.0;
    double previous_two = 0.0;
    for (std::size_t i = 0; i < window.size(); ++i) {
        const double current = samples[start + i] * window[i] +
            detector.coefficient * previous - previous_two;
        previous_two = previous;
        previous = current;
    }
    return std::max(0.0, previous * previous + previous_two * previous_two -
                           detector.coefficient * previous * previous_two);
}
}  // namespace

std::size_t bits_per_symbol(const FskConfig& config) {
    validate(config);
    return config.modulation_order == 4 ? 2U : 1U;
}

std::size_t samples_per_symbol(const FskConfig& config) {
    validate(config);
    const auto base_samples = config.sample_rate / config.symbol_rate;
    if (base_samples > std::numeric_limits<std::size_t>::max() / config.symbol_repetitions) {
        throw std::invalid_argument("FSK symbol is too long");
    }
    return base_samples * config.symbol_repetitions;
}

std::vector<float> modulate_bits(std::span<const std::uint8_t> bytes,
                                 const FskConfig& config) {
    const auto symbol_samples = samples_per_symbol(config);
    const auto symbol_bits = bits_per_symbol(config);
    const auto symbols_per_byte = 8U / symbol_bits;
    std::vector<float> output;
    output.reserve(bytes.size() * symbols_per_byte * symbol_samples);
    std::array<double, 4> step_sines{};
    std::array<double, 4> step_cosines{};
    for (std::uint8_t symbol = 0; symbol < config.modulation_order; ++symbol) {
        const double frequency = config.base_frequency + symbol * config.frequency_spacing;
        const double step = 2.0 * pi * frequency / config.sample_rate;
        step_sines[symbol] = std::sin(step);
        step_cosines[symbol] = std::cos(step);
    }
    double phase_sine = 0.0;
    double phase_cosine = 1.0;
    std::size_t generated = 0;
    for (const auto byte : bytes) {
        for (std::size_t part = 0; part < symbols_per_byte; ++part) {
            const auto shift = 8U - symbol_bits * (part + 1U);
            const auto symbol = (byte >> shift) & (config.modulation_order - 1U);
            for (std::size_t i = 0; i < symbol_samples; ++i) {
                output.push_back(static_cast<float>(config.amplitude * phase_sine));
                const double next_sine = phase_sine * step_cosines[symbol] +
                                         phase_cosine * step_sines[symbol];
                const double next_cosine = phase_cosine * step_cosines[symbol] -
                                           phase_sine * step_sines[symbol];
                phase_sine = next_sine;
                phase_cosine = next_cosine;
                ++generated;
                if ((generated & 4095U) == 0U) {
                    const double magnitude = std::hypot(phase_sine, phase_cosine);
                    phase_sine /= magnitude;
                    phase_cosine /= magnitude;
                }
            }
        }
    }
    return output;
}

std::vector<std::uint8_t> demodulate_bits(std::span<const float> samples,
                                          const FskConfig& config,
                                          FskMetrics* metrics) {
    const auto symbol_samples = samples_per_symbol(config);
    const auto symbol_bits = bits_per_symbol(config);
    const auto symbols_per_byte = 8U / symbol_bits;
    // A microphone recording normally ends between modem symbols/bytes.  Only
    // complete bytes can be decoded; the audio-frame length field decides how
    // many of them belong to the payload.
    const auto available_symbols = samples.size() / symbol_samples;
    const auto symbol_count = available_symbols - available_symbols % symbols_per_byte;
    std::vector<double> window(symbol_samples, 1.0);
    if (symbol_samples > 1) {
        for (std::size_t i = 0; i < symbol_samples; ++i) {
            window[i] = 0.5 - 0.5 *
                std::cos(2.0 * pi * i / static_cast<double>(symbol_samples - 1U));
        }
    }
    const double tolerance = std::min(config.frequency_spacing * 0.12,
                                      config.symbol_rate * 0.20);
    std::array<std::array<ToneDetector, 3>, 4> detectors{};
    constexpr std::array<double, 3> offset_multipliers{-1.0, 0.0, 1.0};
    for (std::uint8_t candidate = 0; candidate < config.modulation_order; ++candidate) {
        const double frequency = config.base_frequency + candidate * config.frequency_spacing;
        for (std::size_t offset_index = 0; offset_index < offset_multipliers.size(); ++offset_index) {
            const double offset = offset_multipliers[offset_index] * tolerance;
            detectors[candidate][offset_index] = {
                2.0 * std::cos(2.0 * pi * (frequency + offset) / config.sample_rate), offset};
        }
    }
    std::vector<std::uint8_t> output(symbol_count / symbols_per_byte, 0);
    double confidence_sum = 0.0;
    double minimum_confidence = symbol_count == 0 ? 0.0 : 1.0;
    double frequency_offset_sum = 0.0;
    for (std::size_t position = 0; position < symbol_count; ++position) {
        std::uint8_t best_symbol = 0;
        double best_energy = -1.0;
        double second_energy = -1.0;
        double best_offset = 0.0;
        for (std::uint8_t candidate = 0; candidate < config.modulation_order; ++candidate) {
            // Search a small neighbourhood around every configured carrier.
            // This tolerates normal speaker/microphone clock and pitch error
            // without allowing adjacent FSK bins to overlap.
            double energy = 0.0;
            double candidate_offset = 0.0;
            for (const auto& detector : detectors[candidate]) {
                const double detected = tone_energy(samples, position * symbol_samples,
                                                    window, detector);
                if (detected > energy) {
                    energy = detected;
                    candidate_offset = detector.offset_hz;
                }
            }
            if (energy > best_energy) {
                second_energy = best_energy;
                best_energy = energy;
                best_symbol = candidate;
                best_offset = candidate_offset;
            } else if (energy > second_energy) {
                second_energy = energy;
            }
        }
        const double confidence = best_energy <= std::numeric_limits<double>::epsilon() ? 0.0 :
            std::clamp((best_energy - std::max(0.0, second_energy)) / best_energy, 0.0, 1.0);
        confidence_sum += confidence;
        minimum_confidence = std::min(minimum_confidence, confidence);
        frequency_offset_sum += best_offset;
        const auto part = position % symbols_per_byte;
        const auto shift = 8U - symbol_bits * (part + 1U);
        output[position / symbols_per_byte] |= static_cast<std::uint8_t>(best_symbol << shift);
    }
    if (metrics != nullptr) {
        metrics->symbol_count = symbol_count;
        metrics->mean_confidence = symbol_count == 0 ? 0.0 : confidence_sum / symbol_count;
        metrics->minimum_confidence = minimum_confidence;
        metrics->estimated_frequency_offset_hz = symbol_count == 0 ? 0.0 :
            frequency_offset_sum / symbol_count;
    }
    return output;
}

}  // namespace acoustic

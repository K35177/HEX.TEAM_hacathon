#include "acoustic/channel_simulator.hpp"
#include "acoustic/framing.hpp"
#include "acoustic/profile.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const std::vector<std::uint8_t> payload{'H', 'E', 'X', 0x00, 0xFF};
    const auto clean = acoustic::create_audio_frame(payload);

    acoustic::ChannelConfig channel;
    channel.snr_db = 24.0;
    channel.sample_rate_scale = 1.004;
    channel.leading_silence_samples = 2400;
    channel.trailing_silence_samples = 2400;
    channel.random_seed = 42;
    acoustic::ChannelMetrics channel_metrics;
    const auto impaired = acoustic::simulate_channel(clean, channel, &channel_metrics);
    assert(impaired == acoustic::simulate_channel(clean, channel));
    assert(channel_metrics.output_samples > channel_metrics.input_samples);
    assert(channel_metrics.noise_rms > 0.0);

    acoustic::ReceiverMetrics receiver_metrics;
    assert(acoustic::decode_audio_frame(impaired, {}, {}, &receiver_metrics) == payload);
    assert(receiver_metrics.chirp_correlation > 0.18);
    assert(std::abs(receiver_metrics.clock_scale - channel.sample_rate_scale) < 0.003);
    assert(receiver_metrics.mean_symbol_confidence > 0.5);

    channel.signal_gain = 2.0;
    channel.clipping_level = 0.4;
    const auto clipped = acoustic::simulate_channel(clean, channel, &channel_metrics);
    assert(!clipped.empty());
    assert(channel_metrics.clipping_ratio > 0.0);

    const auto turbo = acoustic::load_profile("turbo");
    std::vector<std::uint8_t> turbo_payload(512);
    for (std::size_t i = 0; i < turbo_payload.size(); ++i) {
        turbo_payload[i] = static_cast<std::uint8_t>((i * 131U + 17U) & 0xFFU);
    }
    const auto turbo_clean = acoustic::create_audio_frame(
        turbo_payload, turbo.modem, turbo.frame);
    channel.snr_db = 12.0;
    channel.sample_rate_scale = 1.002;
    channel.signal_gain = 1.0;
    channel.clipping_level = 1.0;
    const auto turbo_impaired = acoustic::simulate_channel(turbo_clean, channel);
    acoustic::ReceiverMetrics turbo_metrics;
    assert(acoustic::decode_audio_frame(turbo_impaired, turbo.modem, turbo.frame,
                                        &turbo_metrics) == turbo_payload);
    assert(std::abs(turbo_metrics.clock_scale - channel.sample_rate_scale) < 0.001);
    assert(turbo_metrics.mean_symbol_confidence > 0.45);

    std::cout << "channel_simulator_tests: OK\n";
}

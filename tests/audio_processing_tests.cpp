#include "acoustic/audio_processing.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    std::vector<float> quiet(480, 0.01F);
    const auto automatic = acoustic::apply_receive_gain(quiet);
    assert(automatic > 1.0);
    assert(std::abs(quiet.front()) > 0.01F);

    std::vector<float> manual{0.1F, -0.2F, 0.8F};
    const auto applied = acoustic::apply_receive_gain(manual, 4.0);
    assert(applied == 4.0);
    assert(std::abs(manual[0] - 0.4F) < 0.0001F);
    assert(manual[2] == 1.0F);

    acoustic::SilenceDetector detector(1000, 5.0);
    const std::vector<std::int16_t> silence(100, 60);
    const std::vector<std::int16_t> signal(100, 8000);
    for (int i = 0; i < 10; ++i) detector.process_pcm16(silence);
    assert(!detector.signal_detected());
    assert(!detector.should_stop());
    detector.process_pcm16(signal);
    assert(!detector.signal_detected());
    detector.process_pcm16(signal);
    assert(detector.signal_detected());
    for (int i = 0; i < 49; ++i) detector.process_pcm16(silence);
    assert(!detector.should_stop());
    detector.process_pcm16(silence);
    assert(detector.should_stop());

    // A realistic quiet-room floor (~-41 dBFS) must not be mistaken for a
    // transmission, while a signal around the user's observed x7 auto-gain
    // level must start the silence timer reliably.
    acoustic::SilenceDetector room_detector(1000, 5.0);
    const std::vector<std::int16_t> room_noise(100, 300);
    const std::vector<std::int16_t> moderate_signal(100, 1500);
    for (int i = 0; i < 10; ++i) room_detector.process_pcm16(room_noise);
    assert(!room_detector.signal_detected());
    room_detector.process_pcm16(moderate_signal);
    room_detector.process_pcm16(moderate_signal);
    assert(room_detector.signal_detected());
    for (int i = 0; i < 50; ++i) room_detector.process_pcm16(room_noise);
    assert(room_detector.should_stop());

    // Real audio processing can leave an elevated residual tail after a loud
    // transfer. It is well above the pre-transfer noise, but more than 26 dB
    // below the transmission and must still count as silence. A quieter data
    // section above that relative threshold must remain active.
    acoustic::SilenceDetector residual_detector(1000, 5.0);
    const std::vector<std::int16_t> low_noise(100, 7);
    const std::vector<std::int16_t> loud_signal(100, 2300);
    const std::vector<std::int16_t> faded_signal(100, 200);
    const std::vector<std::int16_t> residual_tail(100, 53);
    for (int i = 0; i < 10; ++i) residual_detector.process_pcm16(low_noise);
    residual_detector.process_pcm16(loud_signal);
    residual_detector.process_pcm16(loud_signal);
    assert(residual_detector.signal_detected());
    for (int i = 0; i < 60; ++i) residual_detector.process_pcm16(faded_signal);
    assert(!residual_detector.should_stop());
    for (int i = 0; i < 49; ++i) residual_detector.process_pcm16(residual_tail);
    assert(!residual_detector.should_stop());
    residual_detector.process_pcm16(residual_tail);
    assert(residual_detector.should_stop());

    acoustic::SilenceDetector weak_detector(1000, 5.0);
    const std::vector<std::int16_t> very_quiet_room(100, 2);
    const std::vector<std::int16_t> weak_signal(100, 48);
    for (int i = 0; i < 10; ++i) weak_detector.process_pcm16(very_quiet_room);
    weak_detector.process_pcm16(weak_signal);
    weak_detector.process_pcm16(weak_signal);
    assert(weak_detector.signal_detected());
    for (int i = 0; i < 25; ++i) weak_detector.process_pcm16(very_quiet_room);
    // One short notification must not restart the five-second timer.
    weak_detector.process_pcm16(weak_signal);
    for (int i = 0; i < 25; ++i) weak_detector.process_pcm16(very_quiet_room);
    assert(weak_detector.should_stop());

    acoustic::SilenceDetector click_detector(1000, 5.0);
    click_detector.process_pcm16(silence);
    click_detector.process_pcm16(silence);
    click_detector.process_pcm16(signal);
    click_detector.process_pcm16(silence);
    for (int i = 0; i < 50; ++i) click_detector.process_pcm16(silence);
    assert(!click_detector.signal_detected());
    assert(!click_detector.should_stop());
    std::cout << "audio_processing_tests: OK\n";
}

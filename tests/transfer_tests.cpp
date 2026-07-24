#include "acoustic/fsk.hpp"
#include "acoustic/transfer.hpp"
#include "acoustic/wav.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    std::vector<std::uint8_t> original(300);
    for (std::size_t i = 0; i < original.size(); ++i) {
        original[i] = static_cast<std::uint8_t>((i * 37U) & 0xFFU);
    }
    const auto stream = acoustic::create_transfer_stream(original, 128, "binary.dat");
    const auto samples = acoustic::modulate_bits(stream);
    const auto wav_path = std::filesystem::temp_directory_path() / "acoustic_transfer_test.wav";
    acoustic::write_wav(wav_path, samples, 48000);
    const auto wav = acoustic::read_wav(wav_path);
    const auto received_stream = acoustic::demodulate_bits(wav.samples);
    const auto received = acoustic::receive_transfer_stream(received_stream);
    std::filesystem::remove(wav_path);
    assert(received.data == original);
    assert(received.filename == "binary.dat");

    auto damaged_metadata = stream;
    damaged_metadata[15] ^= 1U;  // first byte of the declared SHA-256
    bool sha_rejected = false;
    try {
        (void)acoustic::receive_transfer_stream(damaged_metadata);
    } catch (const std::runtime_error&) {
        sha_rejected = true;
    }
    assert(sha_rejected);
    std::cout << "transfer_tests: OK (300-byte binary round-trip)\n";
}

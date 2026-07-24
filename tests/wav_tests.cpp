#include "acoustic/wav.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

int main() {
    const auto path = std::filesystem::temp_directory_path() / "acoustic_wav_test.wav";
    const std::vector<float> samples{-0.75F, -0.25F, 0.0F, 0.25F, 0.75F};
    acoustic::write_wav(path, samples, 48000);
    const auto restored = acoustic::read_wav(path);
    assert(restored.sample_rate == 48000);
    assert(restored.samples.size() == samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        assert(std::abs(restored.samples[i] - samples[i]) < 0.0001F);
    }
    std::filesystem::remove(path);
    std::cout << "wav_tests: OK\n";
}

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
    std::cout << "audio_processing_tests: OK\n";
}

#include "acoustic/profile.hpp"

#include <cassert>
#include <iostream>

int main() {
    const auto turbo = acoustic::load_profile("turbo");
    const auto wideband = acoustic::load_profile("wideband");
    const auto fast = acoustic::load_profile("fast");
    const auto balanced = acoustic::load_profile("balanced");
    const auto robust = acoustic::load_profile("robust");
    assert(turbo.modem.modulation_order == 16);
    assert(turbo.modem.symbol_rate == 480);
    assert(turbo.modem.base_frequency == 960.0);
    assert(turbo.modem.base_frequency + 15.0 * turbo.modem.frequency_spacing == 8160.0);
    assert(turbo.block_size == 4096);
    assert(acoustic::bits_per_symbol(turbo.modem) == 4);
    assert(wideband.modem.symbol_rate == 800);
    assert(wideband.modem.base_frequency + 15.0 * wideband.modem.frequency_spacing == 15000.0);
    assert(fast.modem.modulation_order == 4);
    assert(fast.modem.symbol_rate == 300);
    assert(fast.block_size == 1024);
    assert(balanced.modem.base_frequency == 1200.0);
    assert(balanced.block_size == 512);
    assert(robust.modem.modulation_order == 2);
    assert(robust.modem.symbol_repetitions == 3);
    assert(robust.block_size == 256);
    assert(robust.modem.base_frequency + robust.modem.frequency_spacing == 2200.0);
    std::cout << "profile_tests: OK\n";
}

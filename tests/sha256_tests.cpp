#include "acoustic/sha256.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const std::vector<std::uint8_t> abc{'a','b','c'};
    assert(acoustic::sha256_hex(acoustic::sha256(abc)) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::cout << "sha256_tests: OK\n";
}

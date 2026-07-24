#include "acoustic/crc32.hpp"
#include "acoustic/fsk.hpp"
#include "acoustic/packet.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void print_help() {
    std::cout
        << "Acoustic File Transfer 0.1.0\n\n"
        << "Usage:\n"
        << "  acoustic-transfer self-test\n"
        << "  acoustic-transfer encode <input> <output.wav>   (next stage)\n"
        << "  acoustic-transfer decode <input.wav> <output>   (next stage)\n"
        << "  acoustic-transfer send <input>                  (planned)\n"
        << "  acoustic-transfer receive <output-dir>          (planned)\n";
}

int self_test() {
    const std::vector<std::uint8_t> payload{'H', 'E', 'X'};
    const acoustic::Packet original{0x12345678U, 0, 1, payload};
    const auto encoded = acoustic::serialize_packet(original);
    const auto decoded = acoustic::deserialize_packet(encoded);
    const auto samples = acoustic::modulate_bits(encoded);
    if (decoded.payload != payload || samples.empty()) return 1;
    std::cout << "Self-test OK: packet=" << encoded.size()
              << " bytes, audio=" << samples.size() << " samples, CRC32 verified\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        print_help();
        return argc < 2 ? 1 : 0;
    }
    if (std::string_view(argv[1]) == "self-test") return self_test();
    std::cerr << "Command is not implemented yet. See --help and docs/RUN.md.\n";
    return 2;
}

#include "acoustic/crc32.hpp"
#include "acoustic/fsk.hpp"
#include "acoustic/framing.hpp"
#include "acoustic/packet.hpp"
#include "acoustic/transfer.hpp"
#include "acoustic/wav.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void print_help() {
    std::cout
        << "Acoustic File Transfer 0.2.0\n\n"
        << "Usage:\n"
        << "  acoustic-transfer encode <input> <output.wav>\n"
        << "  acoustic-transfer decode <input.wav> <output>\n"
        << "  acoustic-transfer self-test\n"
        << "  acoustic-transfer send <input>          (planned)\n"
        << "  acoustic-transfer receive <output-dir>  (planned)\n";
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open input file: " + path.string());
    const auto size = in.tellg();
    if (size < 0) throw std::runtime_error("cannot determine input file size");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in && !data.empty()) throw std::runtime_error("cannot read input file");
    return data;
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create output file: " + path.string());
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("cannot write output file");
}

void print_crc(std::span<const std::uint8_t> data) {
    std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
              << acoustic::crc32(data) << std::dec;
}

int encode(const std::filesystem::path& input, const std::filesystem::path& output) {
    const auto data = read_file(input);
    const auto stream = acoustic::create_transfer_stream(data);
    const acoustic::FskConfig config;
    const auto samples = acoustic::create_audio_frame(stream, config);
    acoustic::write_wav(output, samples, config.sample_rate);
    std::cout << "Encoded " << data.size() << " bytes into " << output
              << " (" << samples.size() << " samples, CRC32=";
    print_crc(data);
    std::cout << ")\n";
    return 0;
}

int decode(const std::filesystem::path& input, const std::filesystem::path& output) {
    const auto wav = acoustic::read_wav(input);
    acoustic::FskConfig config;
    config.sample_rate = wav.sample_rate;
    const auto stream = acoustic::decode_audio_frame(wav.samples, config);
    const auto data = acoustic::restore_transfer_stream(stream);
    write_file(output, data);
    std::cout << "Decoded " << data.size() << " bytes into " << output
              << " (integrity OK, CRC32=";
    print_crc(data);
    std::cout << ")\n";
    return 0;
}

int self_test() {
    const std::vector<std::uint8_t> payload{'H', 'E', 'X'};
    const auto stream = acoustic::create_transfer_stream(payload);
    const auto samples = acoustic::modulate_bits(stream);
    const auto decoded = acoustic::restore_transfer_stream(acoustic::demodulate_bits(samples));
    if (decoded != payload) return 1;
    std::cout << "Self-test OK: file -> packets -> FSK -> packets -> file, CRC32 verified\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help" ||
            std::string_view(argv[1]) == "-h") {
            print_help();
            return argc < 2 ? 1 : 0;
        }
        const std::string_view command = argv[1];
        if (command == "self-test") return self_test();
        if (command == "encode" && argc == 4) return encode(argv[2], argv[3]);
        if (command == "decode" && argc == 4) return decode(argv[2], argv[3]);
        if (command == "send" || command == "receive") {
            std::cerr << "Live audio is planned for the next stage.\n";
            return 2;
        }
        print_help();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

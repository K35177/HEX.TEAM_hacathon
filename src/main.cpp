#include "acoustic/audio_device.hpp"
#include "acoustic/audio_processing.hpp"
#include "acoustic/crc32.hpp"
#include "acoustic/fsk.hpp"
#include "acoustic/framing.hpp"
#include "acoustic/packet.hpp"
#include "acoustic/sha256.hpp"
#include "acoustic/transfer.hpp"
#include "acoustic/wav.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

int self_test();

void print_help() {
    std::cout
        << "Acoustic File Transfer 0.3.0\n\n"
        << "Usage:\n"
        << "  acoustic-transfer                 # interactive menu\n"
        << "  acoustic-transfer menu\n"
        << "  acoustic-transfer encode <input> <output.wav>\n"
        << "  acoustic-transfer decode <input.wav> <output>\n"
        << "  acoustic-transfer check-audio\n"
        << "  acoustic-transfer self-test\n"
        << "  acoustic-transfer send <input>\n"
        << "  acoustic-transfer receive <output-dir> [seconds] [gain]\n"
        << "    gain: 0=auto (default), 1=no boost, 2..50=manual boost\n";
}

std::string human_size(std::size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << units[unit];
    return out.str();
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1U);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2U);
    }
    return value;
}

std::filesystem::path normalize_path(std::string value) {
    value = trim(std::move(value));
    if ((value == "~" || value.starts_with("~/"))) {
        if (const char* home = std::getenv("HOME")) value.replace(0, 1, home);
    }
    return value;
}

std::filesystem::path prompt_input_file() {
    for (;;) {
        std::cout << "Путь к файлу (Enter — отмена): " << std::flush;
        std::string value;
        if (!std::getline(std::cin, value) || trim(value).empty()) return {};
        const auto path = normalize_path(std::move(value));
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return path;
        std::cout << "Файл не найден: " << path << "\n"
                  << "Проверьте имя и регистр букв и попробуйте снова.\n";
    }
}

void draw_progress(std::string_view title, unsigned percent) {
    constexpr unsigned width = 30;
    const unsigned filled = percent * width / 100;
    std::cout << '\r' << title << " [";
    for (unsigned i = 0; i < width; ++i) std::cout << (i < filled ? "█" : "░");
    std::cout << "] " << std::setw(3) << percent << '%' << std::flush;
    if (percent == 100) std::cout << '\n';
}

template <typename Operation>
void run_with_progress(std::string_view title, double expected_seconds, Operation operation) {
    std::atomic<bool> done{false};
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            operation();
        } catch (...) {
            failure = std::current_exception();
        }
        done.store(true, std::memory_order_release);
    });
    const auto started = std::chrono::steady_clock::now();
    while (!done.load(std::memory_order_acquire)) {
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        const auto percent = static_cast<unsigned>(std::min(99.0, 100.0 * elapsed / expected_seconds));
        draw_progress(title, percent);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    worker.join();
    if (failure) std::rethrow_exception(failure);
    draw_progress(title, 100);
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
    const auto stream = acoustic::create_transfer_stream(
        data, acoustic::kDefaultBlockSize, input.filename().string());
    const acoustic::FskConfig config;
    const auto samples = acoustic::create_audio_frame(stream, config);
    acoustic::write_wav(output, samples, config.sample_rate);
    std::cout << "Encoded " << data.size() << " bytes into " << output
              << " (" << samples.size() << " samples, CRC32=";
    print_crc(data);
    std::cout << ", SHA-256=" << acoustic::sha256_hex(acoustic::sha256(data)) << ")\n";
    return 0;
}

int decode(const std::filesystem::path& input, const std::filesystem::path& output,
           double requested_gain = 0.0) {
    auto wav = acoustic::read_wav(input);
    const auto applied_gain = acoustic::apply_receive_gain(wav.samples, requested_gain);
    std::cout << "Усиление записи: x" << std::fixed << std::setprecision(2)
              << applied_gain << (requested_gain == 0.0 ? " (auto)\n" : " (manual)\n");
    acoustic::FskConfig config;
    config.sample_rate = wav.sample_rate;
    const auto stream = acoustic::decode_audio_frame(wav.samples, config);
    const auto received = acoustic::receive_transfer_stream(stream);
    auto destination = output;
    if (std::filesystem::is_directory(output)) {
        auto safe_name = std::filesystem::path(received.filename).filename();
        if (safe_name.empty() || safe_name == "." || safe_name == "..") safe_name = "received.bin";
        destination /= safe_name;
    }
    write_file(destination, received.data);
    std::cout << "Decoded " << received.data.size() << " bytes into " << destination
              << " (integrity OK, CRC32=";
    print_crc(received.data);
    std::cout << ", SHA-256=" << acoustic::sha256_hex(received.sha256) << ")\n";
    return 0;
}

int send(const std::filesystem::path& input) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(input, error)) {
        throw std::runtime_error("файл не найден: " + input.string());
    }
    const auto file_size = std::filesystem::file_size(input, error);
    if (error) throw std::runtime_error("не удалось определить размер файла: " + input.string());
    const auto blocks = std::max<std::uintmax_t>(1, (file_size + acoustic::kDefaultBlockSize - 1) /
                                                       acoustic::kDefaultBlockSize);
    std::cout << "\nПередача: " << input.filename() << "\n"
              << "Размер: " << human_size(file_size) << "\n"
              << "Блоков: " << blocks << "\n\n";
    const auto temporary = std::filesystem::temp_directory_path() / "acoustic-transfer-send.wav";
    encode(input, temporary);
    const auto wav = acoustic::read_wav(temporary);
    const auto duration = wav.samples.size() / static_cast<double>(wav.sample_rate);
    std::cout << "Длительность сигнала: " << std::fixed << std::setprecision(1)
              << duration << " с. На приёмнике задайте запись не короче "
              << static_cast<unsigned>(std::ceil(duration + 10.0)) << " с.\n";
    std::cout << "Держите устройства на расстоянии 0,5–1 м.\n";
    run_with_progress("Передача", duration, [&] { acoustic::play_wav_file(temporary); });
    std::filesystem::remove(temporary);
    std::cout << "Передача завершена.\n";
    return 0;
}

int receive(const std::filesystem::path& output_directory, unsigned seconds,
            double gain = 0.0) {
    if (seconds == 0 || seconds > 3600) {
        throw std::invalid_argument("длительность записи должна быть от 1 до 3600 секунд");
    }
    std::filesystem::create_directories(output_directory);
    const auto temporary = std::filesystem::temp_directory_path() / "acoustic-transfer-receive.wav";
    std::cout << "\nПриём: запись " << seconds << " с. Запустите передатчик сейчас.\n";
    run_with_progress("Запись   ", seconds, [&] { acoustic::record_wav_file(temporary, seconds); });
    std::cout << "Поиск начала передачи и проверка файла...\n";
    const auto result = decode(temporary, output_directory, gain);
    std::filesystem::remove(temporary);
    return result;
}

int check_audio_devices() {
    const bool speaker = acoustic::command_available("aplay");
    const bool microphone = acoustic::command_available("arecord");
    std::cout << "\nДинамик (aplay):    " << (speaker ? "OK" : "НЕ НАЙДЕН")
              << "\nМикрофон (arecord): " << (microphone ? "OK" : "НЕ НАЙДЕН") << "\n";
    if (!speaker || !microphone) std::cout << "Установите пакет alsa-utils.\n";
    return speaker && microphone ? 0 : 1;
}

int interactive_menu() {
    for (;;) {
        std::cout << "\n╔══════════════════════════════════════╗\n"
                  << "║       Acoustic File Transfer         ║\n"
                  << "╠══════════════════════════════════════╣\n"
                  << "║  1. Передать файл                    ║\n"
                  << "║  2. Принять файл                     ║\n"
                  << "║  3. Проверить аудиоустройства        ║\n"
                  << "║  4. Запустить самопроверку           ║\n"
                  << "║  0. Выход                            ║\n"
                  << "╚══════════════════════════════════════╝\n"
                  << "Команда: " << std::flush;
        std::string command;
        if (!std::getline(std::cin, command) || command == "0" || command == "q") return 0;
        try {
            if (command == "1") {
                const auto path = prompt_input_file();
                if (!path.empty()) send(path);
            } else if (command == "2") {
                std::cout << "Папка для сохранения [artifacts/received]: " << std::flush;
                std::string directory;
                std::getline(std::cin, directory);
                if (directory.empty()) directory = "artifacts/received";
                std::cout << "Длительность записи, сек [30]: " << std::flush;
                std::string duration;
                std::getline(std::cin, duration);
                std::cout << "Усиление [auto; число 1–50 для ручного]: " << std::flush;
                std::string gain;
                std::getline(std::cin, gain);
                receive(directory,
                        duration.empty() ? 30U : static_cast<unsigned>(std::stoul(duration)),
                        gain.empty() ? 0.0 : std::stod(gain));
            } else if (command == "3") {
                check_audio_devices();
            } else if (command == "4") {
                self_test();
            } else {
                std::cout << "Неизвестная команда. Выберите 0–4.\n";
            }
        } catch (const std::exception& error) {
            std::cerr << "Ошибка: " << error.what() << '\n';
        }
    }
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
        if (argc < 2) return interactive_menu();
        if (std::string_view(argv[1]) == "--help" ||
            std::string_view(argv[1]) == "-h") {
            print_help();
            return 0;
        }
        const std::string_view command = argv[1];
        if (command == "self-test") return self_test();
        if (command == "menu") return interactive_menu();
        if (command == "check-audio") return check_audio_devices();
        if (command == "encode" && argc == 4) return encode(argv[2], argv[3]);
        if (command == "decode" && argc == 4) return decode(argv[2], argv[3]);
        if (command == "send" && argc == 3) return send(argv[2]);
        if (command == "receive" && (argc >= 3 && argc <= 5)) {
            const auto parsed_seconds = argc >= 4 ? std::stoul(argv[3]) : 30UL;
            const auto gain = argc == 5 ? std::stod(argv[4]) : 0.0;
            return receive(argv[2], static_cast<unsigned>(parsed_seconds), gain);
        }
        print_help();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

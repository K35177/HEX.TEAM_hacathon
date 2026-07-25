#include "acoustic/audio_device.hpp"
#include "acoustic/audio_processing.hpp"
#include "acoustic/channel_simulator.hpp"
#include "acoustic/crc32.hpp"
#include "acoustic/framing.hpp"
#include "acoustic/profile.hpp"
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
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct CliArguments {
    std::vector<std::string> positional;
    std::string profile = "balanced";
    double gain = 0.0;
    unsigned seconds = 3600;
    bool gain_set = false;
    bool seconds_set = false;
    bool force = false;
    bool json = false;
};

struct FileEstimate {
    acoustic::TransferEstimate transfer;
    std::uintmax_t file_bytes{};
    std::size_t audio_samples{};
    double duration_seconds{};
    std::uint64_t wav_bytes{};
    std::uint64_t working_memory_bytes{};
};

int self_test(const acoustic::TransferProfile& profile);

void print_help() {
    std::cout
        << "Acoustic File Transfer 0.7.0\n\n"
        << "Использование:\n"
        << "  acoustic-transfer                         # интерактивное меню\n"
        << "  acoustic-transfer menu\n"
        << "  acoustic-transfer ui                       # лёгкая графическая панель\n"
        << "  acoustic-transfer profiles\n"
        << "  acoustic-transfer estimate <файл> [--profile <имя>] [--json]\n"
        << "  acoustic-transfer benchmark [байты] [--profile <имя>] [--json]\n"
        << "  acoustic-transfer channel-test [байты] [--profile <имя>] [--json]\n"
        << "  acoustic-transfer encode <файл> <output.wav> [--profile <имя>] [--force]\n"
        << "  acoustic-transfer decode <input.wav> <файл|папка> [--profile <имя>] [--gain <x>] [--force]\n"
        << "  acoustic-transfer send <файл> [--profile <имя>]\n"
        << "  acoustic-transfer receive <папка> [--gain <x>] [--max-seconds <n>] [--profile <имя>] [--force]\n"
        << "  acoustic-transfer calibrate [--profile <имя>]\n"
        << "  acoustic-transfer check-audio\n"
        << "  acoustic-transfer self-test [--profile <имя>]\n\n"
        << "Профили: turbo, wideband, fast, balanced (по умолчанию), robust. Усиление 0 включает auto.\n";
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
    if (value == "~" || value.starts_with("~/") || value.starts_with("~\\")) {
        const char* home = std::getenv("HOME");
#if defined(_WIN32)
        if (home == nullptr) home = std::getenv("USERPROFILE");
#endif
        if (home != nullptr) value.replace(0, 1, home);
    }
    return std::filesystem::path(value);
}

unsigned parse_unsigned(std::string_view value, std::string_view label) {
    if (value.empty() || value.front() == '-') {
        throw std::invalid_argument(std::string(label) + " должно быть положительным целым числом");
    }
    std::size_t used = 0;
    const auto parsed = std::stoull(std::string(value), &used);
    if (used != value.size() || parsed > std::numeric_limits<unsigned>::max()) {
        throw std::invalid_argument("некорректное значение: " + std::string(label));
    }
    return static_cast<unsigned>(parsed);
}

double parse_double(std::string_view value, std::string_view label) {
    std::size_t used = 0;
    const double parsed = std::stod(std::string(value), &used);
    if (used != value.size() || !std::isfinite(parsed)) {
        throw std::invalid_argument("некорректное значение: " + std::string(label));
    }
    return parsed;
}

CliArguments parse_arguments(int argc, char** argv, int first) {
    CliArguments result;
    for (int i = first; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto require_value = [&](std::string_view option) -> std::string_view {
            if (i + 1 >= argc) throw std::invalid_argument("для " + std::string(option) + " требуется значение");
            return argv[++i];
        };
        if (argument == "--profile") result.profile = std::string(require_value(argument));
        else if (argument == "--gain") {
            result.gain = parse_double(require_value(argument), "усиление");
            result.gain_set = true;
        } else if (argument == "--seconds" || argument == "--max-seconds") {
            result.seconds = parse_unsigned(require_value(argument), "длительность");
            result.seconds_set = true;
        } else if (argument == "--force") result.force = true;
        else if (argument == "--json") result.json = true;
        else if (argument.starts_with("--")) throw std::invalid_argument("неизвестный параметр: " + std::string(argument));
        else result.positional.emplace_back(argument);
    }
    return result;
}

std::string human_size(std::uint64_t bytes) {
    constexpr const char* units[] = {"B", "KiB", "MiB", "GiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << units[unit];
    return output.str();
}

std::string human_duration(double seconds) {
    const auto rounded = static_cast<std::uint64_t>(std::ceil(seconds));
    const auto hours = rounded / 3600U;
    const auto minutes = (rounded % 3600U) / 60U;
    const auto remainder = rounded % 60U;
    std::ostringstream output;
    if (hours != 0) output << hours << " ч ";
    if (minutes != 0) output << minutes << " мин ";
    output << remainder << " с";
    return output.str();
}

std::string json_quote(std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned>(character) << std::dec << std::setfill(' ');
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    output << '"';
    return output.str();
}

std::string unique_token() {
    std::random_device source;
    const auto clock = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream token;
    token << std::hex << clock << '-' << source() << '-' << source();
    return token.str();
}

class TemporaryPath {
public:
    explicit TemporaryPath(std::string_view purpose, std::string_view extension = ".wav")
        : path_(std::filesystem::temp_directory_path() /
                ("acoustic-transfer-" + std::string(purpose) + '-' + unique_token() +
                 std::string(extension))) {}
    TemporaryPath(const TemporaryPath&) = delete;
    TemporaryPath& operator=(const TemporaryPath&) = delete;
    ~TemporaryPath() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("не удалось создать файл: " + path.string());
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!output) throw std::runtime_error("не удалось записать файл: " + path.string());
}

void commit_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                 bool force) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (!force) throw std::runtime_error("файл уже существует (используйте --force): " + destination.string());
        if (!std::filesystem::is_regular_file(destination, error)) {
            throw std::runtime_error("путь назначения не является обычным файлом: " + destination.string());
        }
        std::filesystem::remove(destination);
    }
    if (force) {
        std::filesystem::rename(temporary, destination);
        return;
    }
    // A hard-link commit is an atomic no-clobber operation on all supported
    // desktop filesystems. Unlike POSIX rename(), it cannot replace a file
    // that appears between the existence check and the commit.
    std::filesystem::create_hard_link(temporary, destination, error);
    if (error) {
        throw std::runtime_error("не удалось безопасно зафиксировать файл: " + error.message());
    }
    std::filesystem::remove(temporary);
}

std::filesystem::path sibling_temporary(const std::filesystem::path& destination) {
    auto parent = destination.parent_path();
    if (parent.empty()) parent = ".";
    return parent / ('.' + destination.filename().string() + ".part-" + unique_token());
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("не удалось открыть файл: " + path.string());
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("не удалось определить размер файла");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input && !data.empty()) throw std::runtime_error("не удалось прочитать файл");
    return data;
}

void print_crc(std::span<const std::uint8_t> data) {
    std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
              << acoustic::crc32(data) << std::dec << std::setfill(' ');
}

void draw_progress(std::string_view title, unsigned percent) {
    constexpr unsigned width = 28;
    const unsigned filled = percent * width / 100U;
    std::cout << '\r' << title << " [";
    for (unsigned i = 0; i < width; ++i) std::cout << (i < filled ? '#' : '-');
    std::cout << "] " << std::setw(3) << percent << '%' << std::flush;
    if (percent == 100) std::cout << '\n';
}

template <typename Operation>
void run_with_progress(std::string_view title, double expected_seconds, Operation operation) {
    std::atomic<bool> done{false};
    std::exception_ptr failure;
    std::thread worker([&] {
        try { operation(); }
        catch (...) { failure = std::current_exception(); }
        done.store(true, std::memory_order_release);
    });
    const auto started = std::chrono::steady_clock::now();
    while (!done.load(std::memory_order_acquire)) {
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        const auto percent = expected_seconds <= 0.0 ? 0U :
            static_cast<unsigned>(std::min(99.0, 100.0 * elapsed / expected_seconds));
        draw_progress(title, percent);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    worker.join();
    if (failure) std::rethrow_exception(failure);
    draw_progress(title, 100);
}

FileEstimate estimate_file(const std::filesystem::path& input,
                           const acoustic::TransferProfile& profile) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(input, error)) {
        throw std::runtime_error("файл не найден: " + input.string());
    }
    const auto file_bytes = std::filesystem::file_size(input, error);
    if (error || file_bytes > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("не удалось определить поддерживаемый размер файла");
    }
    const auto transfer = acoustic::estimate_transfer(static_cast<std::size_t>(file_bytes),
                                                       profile.block_size,
                                                       input.filename().string().size());
    const auto symbols_per_byte = 8U / acoustic::bits_per_symbol(profile.modem);
    const auto protected_bytes = acoustic::protected_frame_payload_bytes(transfer.stream_bytes);
    const long double modem_samples = static_cast<long double>(protected_bytes + 4U) *
        symbols_per_byte * acoustic::samples_per_symbol(profile.modem);
    const long double prefix_samples =
        (profile.frame.chirp_duration_seconds + profile.frame.guard_duration_seconds) *
        profile.modem.sample_rate;
    const long double total_samples = modem_samples + prefix_samples;
    const auto max_wav_samples = (std::numeric_limits<std::uint32_t>::max() - 36ULL) / 2ULL;
    if (total_samples > max_wav_samples || total_samples > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("файл создаст WAV больше допустимого размера");
    }
    FileEstimate result;
    result.transfer = transfer;
    result.file_bytes = file_bytes;
    result.audio_samples = static_cast<std::size_t>(std::ceil(total_samples));
    result.duration_seconds = result.audio_samples / static_cast<double>(profile.modem.sample_rate);
    result.wav_bytes = 44ULL + result.audio_samples * 2ULL;
    result.working_memory_bytes = result.audio_samples * (sizeof(float) + sizeof(std::int16_t)) +
        protected_bytes + transfer.stream_bytes + file_bytes;
    return result;
}

void print_estimate(const std::filesystem::path& input, const acoustic::TransferProfile& profile,
                    const FileEstimate& estimate, bool json = false) {
    const double raw_bitrate = profile.modem.symbol_rate *
        acoustic::bits_per_symbol(profile.modem) / profile.modem.symbol_repetitions;
    const double goodput = estimate.duration_seconds <= 0.0 ? 0.0 :
        estimate.file_bytes * 8.0 / estimate.duration_seconds;
    const double efficiency = raw_bitrate <= 0.0 ? 0.0 : goodput / raw_bitrate * 100.0;
    if (json) {
        std::cout << std::fixed << std::setprecision(6)
                  << "{\"command\":\"estimate\",\"profile\":" << json_quote(profile.name)
                  << ",\"file_bytes\":" << estimate.file_bytes
                  << ",\"stream_bytes\":" << estimate.transfer.stream_bytes
                  << ",\"block_count\":" << estimate.transfer.block_count
                  << ",\"audio_samples\":" << estimate.audio_samples
                  << ",\"airtime_seconds\":" << estimate.duration_seconds
                  << ",\"channel_bps\":" << raw_bitrate
                  << ",\"goodput_bps\":" << goodput
                  << ",\"efficiency_percent\":" << efficiency
                  << ",\"wav_bytes\":" << estimate.wav_bytes
                  << ",\"peak_memory_bytes\":" << estimate.working_memory_bytes << "}\n";
        return;
    }
    std::cout << "\nОценка передачи\n"
              << "  Файл:              " << input.filename() << '\n'
              << "  Размер:            " << human_size(estimate.file_bytes) << '\n'
              << "  Профиль:           " << profile.name << '\n'
              << "  Частоты:           " << profile.modem.base_frequency << "–"
              << profile.modem.base_frequency +
                    (profile.modem.modulation_order - 1U) * profile.modem.frequency_spacing
              << " Гц\n"
              << "  Канальный bitrate: " << raw_bitrate << " бит/с\n"
              << "  Полезный goodput:  " << std::fixed << std::setprecision(1) << goodput
              << " бит/с\n"
              << "  Эффективность:     " << efficiency << "% полезных бит/airtime\n"
              << "  Блоков:            " << estimate.transfer.block_count << '\n'
              << "  Аудио:             " << human_duration(estimate.duration_seconds) << '\n'
              << "  Приёмник:          остановится после 5 с тишины\n"
              << "  WAV:               " << human_size(estimate.wav_bytes) << '\n'
              << "  Пиковая память:    около " << human_size(estimate.working_memory_bytes) << "\n\n";
}

int estimate_command(const std::filesystem::path& input, const acoustic::TransferProfile& profile,
                     bool json = false) {
    print_estimate(input, profile, estimate_file(input, profile), json);
    return 0;
}

int encode(const std::filesystem::path& input, const std::filesystem::path& output,
           const acoustic::TransferProfile& profile, bool force, bool verbose = true) {
    const auto estimate = estimate_file(input, profile);
    const auto data = read_file(input);
    const auto stream = acoustic::create_transfer_stream(data, profile.block_size,
                                                         input.filename().string());
    const auto samples = acoustic::create_audio_frame(stream, profile.modem, profile.frame);
    auto parent = output.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent)) {
        throw std::runtime_error("папка назначения не существует: " + parent.string());
    }
    const auto temporary = sibling_temporary(output);
    try {
        acoustic::write_wav(temporary, samples, profile.modem.sample_rate);
        commit_file(temporary, output, force);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
    if (verbose) {
        std::cout << "Закодировано " << data.size() << " байт в " << output
                  << " (профиль=" << profile.name << ", "
                  << human_duration(estimate.duration_seconds) << ", CRC32=";
        print_crc(data);
        std::cout << ", SHA-256=" << acoustic::sha256_hex(acoustic::sha256(data)) << ")\n";
    }
    return 0;
}

std::filesystem::path received_destination(const std::filesystem::path& output,
                                           std::string_view received_name, bool force) {
    if (!std::filesystem::is_directory(output)) return output;
    auto safe_name = std::filesystem::path(received_name).filename();
    if (safe_name.empty() || safe_name == "." || safe_name == "..") safe_name = "received.bin";
    auto destination = output / safe_name;
    if (force || !std::filesystem::exists(destination)) return destination;
    const auto stem = destination.stem().string();
    const auto extension = destination.extension().string();
    for (unsigned copy = 1; copy < 10000; ++copy) {
        const auto candidate = output / (stem + " (" + std::to_string(copy) + ')' + extension);
        if (!std::filesystem::exists(candidate)) return candidate;
    }
    throw std::runtime_error("не удалось подобрать свободное имя принятого файла");
}

int decode(const std::filesystem::path& input, const std::filesystem::path& output,
           const acoustic::TransferProfile& profile, double requested_gain, bool force,
           bool detect_profile = false) {
    auto wav = acoustic::read_wav(input);
    const auto applied_gain = acoustic::apply_receive_gain(wav.samples, requested_gain);
    std::cout << "Усиление записи: x" << std::fixed << std::setprecision(2) << applied_gain
              << (requested_gain == 0.0 ? " (auto)\n" : " (manual)\n");
    if (requested_gain == 0.0 && applied_gain >= 19.99) {
        std::cout << "Предупреждение: запись очень тихая. Проверьте вход Windows, "
                     "громкость передатчика и расстояние.\n";
    }

    std::vector<acoustic::TransferProfile> candidates{profile};
    if (detect_profile) {
        for (const auto& builtin : acoustic::builtin_profiles()) {
            if (builtin.name != profile.name) candidates.push_back(acoustic::load_profile(builtin.name));
        }
    }
    acoustic::ReceiverMetrics metrics;
    acoustic::ReceivedFile received;
    acoustic::TransferProfile detected_profile = profile;
    std::vector<std::string> failures;
    bool decoded = false;
    bool damaged_legacy_turbo = false;
    for (auto candidate : candidates) {
        candidate.modem.sample_rate = wav.sample_rate;
        acoustic::ReceiverMetrics candidate_metrics;
        try {
            const auto stream = acoustic::decode_audio_frame(
                wav.samples, candidate.modem, candidate.frame, &candidate_metrics);
            auto candidate_file = acoustic::receive_transfer_stream(stream);
            metrics = candidate_metrics;
            received = std::move(candidate_file);
            detected_profile = std::move(candidate);
            decoded = true;
            break;
        } catch (const std::exception& error) {
            if (candidate.name == "turbo-v1" && candidate_metrics.chirp_correlation >= 0.20) {
                damaged_legacy_turbo = true;
            }
            std::ostringstream detail;
            detail << candidate.name << ": " << error.what();
            if (candidate_metrics.chirp_correlation > 0.0) {
                detail << std::fixed << std::setprecision(3)
                       << " [chirp=" << candidate_metrics.chirp_correlation
                       << ", clock=" << (candidate_metrics.clock_scale - 1.0) * 1'000'000.0
                       << " ppm";
                if (candidate_metrics.mean_symbol_confidence > 0.0) {
                    detail << ", confidence="
                           << candidate_metrics.mean_symbol_confidence * 100.0 << '%';
                }
                detail << ']';
            }
            failures.push_back(detail.str());
        } catch (...) {
            failures.push_back(candidate.name + ": неизвестная ошибка декодирования");
        }
    }
    if (!decoded) {
        std::ostringstream message;
        message << "не удалось распознать передачу ни в одном профиле";
        for (const auto& failure : failures) message << "\n  " << failure;
        if (damaged_legacy_turbo) {
            message << "\n  Запись уверенно найдена как turbo-v1, но старый формат не содержит FEC: "
                       "повреждённые символы восстановить нельзя. Повторите передачу новой версией "
                       "с профилем turbo; вручную перебирать профили не требуется.";
        } else {
            message << "\n  Совет: используйте новый turbo (1,2–6,0 кГц с FEC); "
                       "приёмник определяет профиль автоматически.";
        }
        throw std::runtime_error(message.str());
    }
    if (detected_profile.name != profile.name) {
        std::cout << "Профиль определён автоматически: " << detected_profile.name << '\n';
    }
    const auto destination = received_destination(output, received.filename, force);
    auto parent = destination.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent)) {
        throw std::runtime_error("папка назначения не существует: " + parent.string());
    }
    const auto temporary = sibling_temporary(destination);
    try {
        write_bytes(temporary, received.data);
        commit_file(temporary, destination, force);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
    std::cout << "Восстановлено " << received.data.size() << " байт в " << destination
              << " (integrity OK, CRC32=";
    print_crc(received.data);
    std::cout << ", SHA-256=" << acoustic::sha256_hex(received.sha256) << ")\n"
              << std::fixed << std::setprecision(3)
              << "Профиль: " << detected_profile.name << '\n'
              << "Метрики: chirp=" << metrics.chirp_correlation
              << ", clock=" << (metrics.clock_scale - 1.0) * 1'000'000.0 << " ppm"
              << ", confidence=" << metrics.mean_symbol_confidence * 100.0 << "%"
              << ", freq-offset=" << metrics.estimated_frequency_offset_hz << " Hz"
              << ", clipping=" << metrics.clipping_ratio * 100.0 << "%"
              << ", FEC=" << metrics.corrected_bytes << " bytes/"
              << metrics.corrected_codewords << " blocks\n";
    return 0;
}

int benchmark(std::size_t payload_size, const acoustic::TransferProfile& profile, bool json) {
    constexpr std::size_t maximum_benchmark_bytes = 1024U * 1024U;
    if (payload_size > maximum_benchmark_bytes) {
        throw std::invalid_argument("benchmark ограничен 1 MiB, чтобы не исчерпать память");
    }
    std::vector<std::uint8_t> payload(payload_size);
    std::uint32_t state = 0x48455841U;
    for (auto& byte : payload) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        byte = static_cast<std::uint8_t>(state);
    }

    const auto encode_started = std::chrono::steady_clock::now();
    const auto stream = acoustic::create_transfer_stream(payload, profile.block_size,
                                                         "benchmark.bin");
    const auto samples = acoustic::create_audio_frame(stream, profile.modem, profile.frame);
    const double encode_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - encode_started).count();

    acoustic::ReceiverMetrics metrics;
    const auto decode_started = std::chrono::steady_clock::now();
    const auto decoded_stream = acoustic::decode_audio_frame(samples, profile.modem,
                                                              profile.frame, &metrics);
    const auto restored = acoustic::receive_transfer_stream(decoded_stream);
    const double decode_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - decode_started).count();
    if (restored.data != payload) throw std::runtime_error("benchmark round-trip failed");

    const double airtime = samples.size() / static_cast<double>(profile.modem.sample_rate);
    const double raw_bitrate = profile.modem.symbol_rate *
        acoustic::bits_per_symbol(profile.modem) / profile.modem.symbol_repetitions;
    const double goodput = airtime <= 0.0 ? 0.0 : payload.size() * 8.0 / airtime;
    const double efficiency = raw_bitrate <= 0.0 ? 0.0 : goodput / raw_bitrate * 100.0;
    const double encode_realtime = encode_seconds <= 0.0 ? 0.0 : airtime / encode_seconds;
    const double decode_realtime = decode_seconds <= 0.0 ? 0.0 : airtime / decode_seconds;

    if (json) {
        std::cout << std::fixed << std::setprecision(6)
                  << "{\"command\":\"benchmark\",\"profile\":" << json_quote(profile.name)
                  << ",\"payload_bytes\":" << payload.size()
                  << ",\"stream_bytes\":" << stream.size()
                  << ",\"audio_samples\":" << samples.size()
                  << ",\"airtime_seconds\":" << airtime
                  << ",\"channel_bps\":" << raw_bitrate
                  << ",\"goodput_bps\":" << goodput
                  << ",\"efficiency_percent\":" << efficiency
                  << ",\"encode_ms\":" << encode_seconds * 1000.0
                  << ",\"decode_ms\":" << decode_seconds * 1000.0
                  << ",\"encode_realtime_factor\":" << encode_realtime
                  << ",\"decode_realtime_factor\":" << decode_realtime
                  << ",\"mean_confidence\":" << metrics.mean_symbol_confidence
                  << ",\"waveform_bytes\":" << samples.size() * sizeof(float) << "}\n";
        return 0;
    }

    std::cout << "\nBenchmark эффективности [" << profile.name << "]\n"
              << "  Payload:             " << human_size(payload.size()) << '\n'
              << "  Transfer stream:     " << human_size(stream.size()) << '\n'
              << std::fixed << std::setprecision(2)
              << "  Airtime:             " << airtime << " s\n"
              << "  Канальный bitrate:   " << raw_bitrate << " bit/s\n"
              << "  Полезный goodput:    " << goodput << " bit/s\n"
              << "  Эффективность:       " << efficiency << "%\n"
              << "  Encode CPU:          " << encode_seconds * 1000.0 << " ms ("
              << encode_realtime << "x realtime)\n"
              << "  Decode CPU:          " << decode_seconds * 1000.0 << " ms ("
              << decode_realtime << "x realtime)\n"
              << "  Mean confidence:     " << metrics.mean_symbol_confidence * 100.0 << "%\n"
              << "  Waveform memory:     " << human_size(samples.size() * sizeof(float)) << "\n";
    return 0;
}

int channel_test(std::size_t payload_size, const acoustic::TransferProfile& profile, bool json) {
    if (payload_size > 64U * 1024U) {
        throw std::invalid_argument("channel-test ограничен 64 KiB");
    }
    std::vector<std::uint8_t> payload(payload_size);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>((i * 131U + 17U) & 0xFFU);
    }
    const auto stream = acoustic::create_transfer_stream(payload, profile.block_size,
                                                         "channel-test.bin");
    const auto clean = acoustic::create_audio_frame(stream, profile.modem, profile.frame);
    const auto silence = profile.modem.sample_rate / 20U;

    struct Scenario {
        const char* name;
        acoustic::ChannelConfig channel;
    };
    std::vector<Scenario> scenarios;
    const auto add_scenario = [&](const char* name, double snr_db, double clock_scale,
                                  double gain = 1.0, double clipping = 1.0) {
        acoustic::ChannelConfig channel;
        channel.snr_db = snr_db;
        channel.sample_rate_scale = clock_scale;
        channel.signal_gain = gain;
        channel.clipping_level = clipping;
        channel.leading_silence_samples = silence;
        channel.trailing_silence_samples = silence;
        channel.random_seed = static_cast<std::uint32_t>(0x48455841U + scenarios.size());
        scenarios.push_back({name, channel});
    };
    add_scenario("clean", std::numeric_limits<double>::infinity(), 1.0);
    add_scenario("snr24", 24.0, 1.0);
    add_scenario("drift4000ppm", 24.0, 1.004);
    add_scenario("snr18-drift2000ppm", 18.0, 1.002);
    add_scenario("snr12-drift2000ppm", 12.0, 1.002);
    add_scenario("clipping", 24.0, 1.0, 1.4, 0.70);

    struct Result {
        std::string name;
        bool passed{};
        double airtime{};
        double decode_ms{};
        acoustic::ReceiverMetrics receiver;
        acoustic::ChannelMetrics channel;
        std::string error;
    };
    std::vector<Result> results;
    std::size_t passed_count = 0;
    double total_airtime = 0.0;
    for (const auto& scenario : scenarios) {
        Result result;
        result.name = scenario.name;
        const auto impaired = acoustic::simulate_channel(clean, scenario.channel, &result.channel);
        result.airtime = impaired.size() / static_cast<double>(profile.modem.sample_rate);
        total_airtime += result.airtime;
        const auto started = std::chrono::steady_clock::now();
        try {
            const auto received_stream = acoustic::decode_audio_frame(
                impaired, profile.modem, profile.frame, &result.receiver);
            const auto restored = acoustic::receive_transfer_stream(received_stream);
            result.passed = restored.data == payload;
            if (!result.passed) result.error = "payload mismatch";
        } catch (const std::exception& error) {
            result.error = error.what();
        }
        result.decode_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        if (result.passed) ++passed_count;
        results.push_back(std::move(result));
    }

    const double completion = static_cast<double>(passed_count) / results.size();
    const double raw_bitrate = profile.modem.symbol_rate *
        acoustic::bits_per_symbol(profile.modem) / profile.modem.symbol_repetitions;
    const double delivered_goodput = total_airtime <= 0.0 ? 0.0 :
        passed_count * payload.size() * 8.0 / total_airtime;
    const double delivery_efficiency = raw_bitrate <= 0.0 ? 0.0 :
        delivered_goodput / raw_bitrate * 100.0;

    if (json) {
        std::cout << std::fixed << std::setprecision(6)
                  << "{\"command\":\"channel-test\",\"profile\":" << json_quote(profile.name)
                  << ",\"payload_bytes\":" << payload.size()
                  << ",\"passed\":" << passed_count
                  << ",\"scenario_count\":" << results.size()
                  << ",\"completion_rate\":" << completion
                  << ",\"delivered_goodput_bps\":" << delivered_goodput
                  << ",\"delivery_efficiency_percent\":" << delivery_efficiency
                  << ",\"scenarios\":[";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            if (i != 0) std::cout << ',';
            std::cout << "{\"name\":" << json_quote(result.name)
                      << ",\"passed\":" << (result.passed ? "true" : "false")
                      << ",\"airtime_seconds\":" << result.airtime
                      << ",\"decode_ms\":" << result.decode_ms
                      << ",\"chirp_correlation\":" << result.receiver.chirp_correlation
                      << ",\"clock_scale\":" << result.receiver.clock_scale
                      << ",\"confidence\":" << result.receiver.mean_symbol_confidence
                      << ",\"clipping_ratio\":" << result.channel.clipping_ratio
                      << ",\"error\":" << json_quote(result.error) << '}';
        }
        std::cout << "]}\n";
    } else {
        std::cout << "\nChannel test [" << profile.name << ", " << payload.size() << " B]\n";
        for (const auto& result : results) {
            std::cout << "  " << std::left << std::setw(22) << result.name << std::right
                      << (result.passed ? "PASS" : "FAIL")
                      << std::fixed << std::setprecision(1)
                      << "  decode=" << result.decode_ms << " ms"
                      << "  chirp=" << std::setprecision(3) << result.receiver.chirp_correlation
                      << "  confidence=" << result.receiver.mean_symbol_confidence * 100.0 << '%';
            if (!result.error.empty()) std::cout << "  error=" << result.error;
            std::cout << '\n';
        }
        std::cout << std::fixed << std::setprecision(1)
                  << "  Completion:          " << completion * 100.0 << "%\n"
                  << "  Delivered goodput:   " << delivered_goodput << " bit/s\n"
                  << "  Delivery efficiency: " << delivery_efficiency << "%\n";
    }
    return passed_count == results.size() ? 0 : 1;
}

int send(const std::filesystem::path& input, const acoustic::TransferProfile& profile) {
    const auto estimate = estimate_file(input, profile);
    print_estimate(input, profile, estimate);
    TemporaryPath temporary("send");
    encode(input, temporary.path(), profile, true, false);
    std::cout << "Держите устройства на расстоянии 0,5–1 м.\n";
    run_with_progress("Передача", estimate.duration_seconds,
                      [&] { acoustic::play_wav_file(temporary.path()); });
    std::cout << "Передача завершена.\n";
    return 0;
}

int receive(const std::filesystem::path& output_directory, unsigned maximum_seconds, double gain,
            const acoustic::TransferProfile& profile, bool force) {
    if (maximum_seconds <= 5 || maximum_seconds > 3600) {
        throw std::invalid_argument("максимальная длительность записи должна быть от 6 до 3600 секунд");
    }
    if (std::filesystem::exists(output_directory) &&
        !std::filesystem::is_directory(output_directory)) {
        throw std::runtime_error("путь приёма не является папкой: " + output_directory.string());
    }
    std::filesystem::create_directories(output_directory);
    TemporaryPath temporary("receive");
    std::cout << "\nПриём: профиль " << profile.name
              << ". Запустите передатчик сейчас. Запись остановится после 5 с тишины.\n";
    const auto recording = acoustic::record_wav_until_silence(
        temporary.path(), profile.modem.sample_rate, 5, maximum_seconds);
    std::cout << std::fixed << std::setprecision(1)
              << "Запись завершена: " << recording.duration_seconds << " с"
              << (recording.stopped_after_silence ? " (обнаружено 5 с тишины)\n" :
                  " (достигнут защитный лимит)\n");
    std::cout << "Поиск сигнала и проверка файла...\n";
    try {
        return decode(temporary.path(), output_directory, profile, gain, force, true);
    } catch (...) {
        const auto diagnostic = received_destination(output_directory, "failed-receive.wav", false);
        std::error_code copy_error;
        std::filesystem::copy_file(temporary.path(), diagnostic,
                                   std::filesystem::copy_options::none, copy_error);
        if (!copy_error) {
            std::cout << "Диагностическая запись сохранена: " << diagnostic << '\n';
        }
        throw;
    }
}

int check_audio_devices() {
    const auto status = acoustic::audio_device_status();
    std::cout << "\nАудиобэкенд: " << status.backend
              << "\nДинамик:      " << (status.playback_available ? "OK" : "НЕ НАЙДЕН")
              << "\nМикрофон:    " << (status.recording_available ? "OK" : "НЕ НАЙДЕН") << "\n";
#if !defined(_WIN32) && !defined(__APPLE__)
    if (!status.playback_available || !status.recording_available) {
        std::cout << "Для Linux установите alsa-utils.\n";
    }
#endif
    return status.playback_available && status.recording_available ? 0 : 1;
}

int calibrate(const acoustic::TransferProfile& profile) {
    const auto devices = acoustic::audio_device_status();
    if (!devices.playback_available || !devices.recording_available) {
        throw std::runtime_error("для калибровки нужны доступные динамик и микрофон");
    }
    const std::vector<std::uint8_t> payload{'H','E','X','-','C','A','L','-','1'};
    const auto stream = acoustic::create_transfer_stream(payload, profile.block_size, "calibration.bin");
    const auto signal = acoustic::create_audio_frame(stream, profile.modem, profile.frame);
    const double signal_seconds = signal.size() / static_cast<double>(profile.modem.sample_rate);
    const unsigned capture_seconds = static_cast<unsigned>(std::ceil(signal_seconds + 1.5));
    TemporaryPath playback("calibration-play");
    TemporaryPath recording("calibration-record");
    acoustic::write_wav(playback.path(), signal, profile.modem.sample_rate);

    std::cout << "\nКалибровка профиля " << profile.name << " через " << devices.backend
              << ". Не меняйте громкость в течение " << capture_seconds << " с.\n";
    std::exception_ptr record_failure;
    std::thread recorder([&] {
        try { acoustic::record_wav_file(recording.path(), capture_seconds, profile.modem.sample_rate); }
        catch (...) { record_failure = std::current_exception(); }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::exception_ptr playback_failure;
    try { acoustic::play_wav_file(playback.path()); }
    catch (...) { playback_failure = std::current_exception(); }
    recorder.join();
    if (record_failure) std::rethrow_exception(record_failure);
    if (playback_failure) std::rethrow_exception(playback_failure);

    auto captured = acoustic::read_wav(recording.path());
    const auto gain = acoustic::apply_receive_gain(captured.samples, 0.0);
    acoustic::ReceiverMetrics metrics;
    std::vector<std::uint8_t> decoded_stream;
    try {
        decoded_stream = acoustic::decode_audio_frame(captured.samples, profile.modem,
                                                      profile.frame, &metrics);
    } catch (const std::exception&) {
        std::cout << std::fixed << std::setprecision(3)
                  << "Калибровка FAILED\n"
                  << "  Chirp:       " << metrics.chirp_correlation << '\n'
                  << "  Noise RMS:   " << metrics.noise_rms << '\n'
                  << "  Clipping:    " << metrics.clipping_ratio * 100.0 << "%\n"
                  << "  Рекомендация: проверьте выбранные устройства, разрешение микрофона, "
                     "громкость и расстояние; затем повторите с "
                  << (profile.name == "wideband" ? "turbo.\n" : "robust.\n");
        throw;
    }
    const auto restored = acoustic::receive_transfer_stream(decoded_stream);
    if (restored.data != payload) throw std::runtime_error("калибровочный кадр не прошёл проверку");
    const double signal_only_rms = std::sqrt(std::max(0.0,
        metrics.signal_rms * metrics.signal_rms - metrics.noise_rms * metrics.noise_rms));
    const double snr = 20.0 * std::log10(std::max(signal_only_rms, 1e-9) /
                                        std::max(metrics.noise_rms, 1e-9));

    const char* quality = snr >= 18.0 ? "отличный" : snr >= 10.0 ? "рабочий" : "слабый";
    std::cout << std::fixed << std::setprecision(1)
              << "Калибровка OK\n"
              << "  Канал:       " << quality << '\n'
              << "  SNR estimate:" << snr << " dB\n"
              << "  Auto gain:   x" << std::setprecision(2) << gain << '\n'
              << "  Chirp:       " << metrics.chirp_correlation << '\n'
              << "  Confidence:  " << metrics.mean_symbol_confidence * 100.0 << "%\n"
              << "  Clock error: " << (metrics.clock_scale - 1.0) * 1'000'000.0 << " ppm\n"
              << "  Freq offset: " << metrics.estimated_frequency_offset_hz << " Hz\n"
              << "  Clipping:    " << metrics.clipping_ratio * 100.0 << "%\n"
              << "  Рекомендация: "
              << (snr < 10.0 && profile.name != "robust" ? "используйте --profile robust\n" :
                  "выбранный профиль подходит\n");
    return 0;
}

void print_profiles() {
    std::cout << "\nДоступные профили:\n";
    for (const auto& builtin : acoustic::builtin_profiles()) {
        const auto profile = acoustic::load_profile(builtin.name);
        const auto bitrate = profile.modem.symbol_rate * acoustic::bits_per_symbol(profile.modem) /
                             profile.modem.symbol_repetitions;
        std::cout << "  " << std::left << std::setw(10) << profile.name << std::right
                  << bitrate << " бит/с, " << static_cast<unsigned>(profile.modem.modulation_order)
                  << "-FSK, " << profile.description << '\n';
    }
}

std::filesystem::path prompt_input_file() {
    for (;;) {
        std::cout << "Путь к файлу (Enter — отмена): " << std::flush;
        std::string value;
        if (!std::getline(std::cin, value) || trim(value).empty()) return {};
        const auto path = normalize_path(std::move(value));
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return path;
        std::cout << "Файл не найден: " << path << '\n';
    }
}

std::string prompt_profile(std::string current) {
    std::cout << "Профиль turbo/wideband/fast/balanced/robust [" << current << "]: " << std::flush;
    std::string value;
    std::getline(std::cin, value);
    value = trim(std::move(value));
    if (!value.empty()) {
        (void)acoustic::load_profile(value);
        return value;
    }
    return current;
}

int interactive_menu() {
    std::string profile_name = "balanced";
    for (;;) {
        std::cout << "\n=== Acoustic File Transfer ===\n"
                  << "Профиль: " << profile_name << "\n\n"
                  << "  1. Передать файл\n"
                  << "  2. Принять файл\n"
                  << "  3. Оценить передачу\n"
                  << "  4. Калибровать канал\n"
                  << "  5. Проверить аудио\n"
                  << "  6. Самопроверка\n"
                  << "  7. Выбрать профиль\n"
                  << "  0. Выход\n\nКоманда: " << std::flush;
        std::string command;
        if (!std::getline(std::cin, command) || command == "0" || command == "q") return 0;
        try {
            const auto profile = acoustic::load_profile(profile_name);
            if (command == "1") {
                const auto path = prompt_input_file();
                if (!path.empty()) send(path, profile);
            } else if (command == "2") {
                std::cout << "Папка [artifacts/received]: " << std::flush;
                std::string directory;
                std::getline(std::cin, directory);
                if (trim(directory).empty()) directory = "artifacts/received";
                std::cout << "Усиление [auto]: " << std::flush;
                std::string gain;
                std::getline(std::cin, gain);
                receive(normalize_path(directory), 3600U,
                        trim(gain).empty() ? 0.0 : parse_double(trim(gain), "усиление"), profile, false);
            } else if (command == "3") {
                const auto path = prompt_input_file();
                if (!path.empty()) estimate_command(path, profile);
            } else if (command == "4") calibrate(profile);
            else if (command == "5") check_audio_devices();
            else if (command == "6") self_test(profile);
            else if (command == "7") profile_name = prompt_profile(profile_name);
            else std::cout << "Неизвестная команда. Выберите 0–7.\n";
        } catch (const std::exception& error) {
            std::cerr << "Ошибка: " << error.what() << '\n';
        }
    }
}

int self_test(const acoustic::TransferProfile& profile) {
    const std::vector<std::uint8_t> payload{'H', 'E', 'X'};
    const auto stream = acoustic::create_transfer_stream(payload, profile.block_size, "self-test.bin");
    const auto samples = acoustic::create_audio_frame(stream, profile.modem, profile.frame);
    std::vector<float> recording(profile.modem.sample_rate / 20U, 0.001F);
    recording.insert(recording.end(), samples.begin(), samples.end());
    recording.insert(recording.end(), profile.modem.sample_rate / 20U, 0.0F);
    acoustic::ReceiverMetrics metrics;
    const auto decoded = acoustic::decode_audio_frame(recording, profile.modem, profile.frame,
                                                       &metrics);
    const auto restored = acoustic::receive_transfer_stream(decoded);
    if (restored.data != payload || restored.filename != "self-test.bin") return 1;
    std::cout << "Self-test OK [" << profile.name
              << "]: framing -> FSK -> packets -> CRC32/SHA-256"
              << " (confidence=" << std::fixed << std::setprecision(1)
              << metrics.mean_symbol_confidence * 100.0 << "%)\n";
    return 0;
}

std::string process_quote(const std::filesystem::path& value) {
    std::string text = value.string();
#if defined(_WIN32)
    std::string result = "\"";
    for (const char character : text) result += character == '"' ? "\\\"" : std::string(1, character);
    return result + '"';
#else
    std::string result = "'";
    for (const char character : text) result += character == '\'' ? "'\\''" : std::string(1, character);
    return result + '\'';
#endif
}

int launch_ui(const std::filesystem::path& executable) {
    std::error_code error;
    const auto absolute_executable = std::filesystem::absolute(executable, error);
    const std::vector<std::filesystem::path> candidates{
        std::filesystem::current_path() / "ui" / "acoustic_ui.py",
        absolute_executable.parent_path() / "ui" / "acoustic_ui.py",
        absolute_executable.parent_path().parent_path() / "ui" / "acoustic_ui.py"};
    std::filesystem::path script;
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) { script = candidate; break; }
    }
    if (script.empty()) throw std::runtime_error("не найден ui/acoustic_ui.py");
#if defined(_WIN32)
    // Prefer an actual python.exe.  The Windows `py` launcher can exist even
    // when it has no registered interpreter, which previously made `ui` fail
    // despite a working Python being available on PATH.
    const std::string interpreter = acoustic::command_available("python") ? "python" : "py -3";
#else
    const std::string interpreter = "python3";
#endif
    const auto command = interpreter + " " + process_quote(script) + " --binary " +
                         process_quote(absolute_executable);
    const int result = std::system(command.c_str());
    return result == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) return interactive_menu();
        const std::string_view command = argv[1];
        if (command == "--help" || command == "-h") {
            print_help();
            return 0;
        }
        if (command == "menu") return interactive_menu();
        if (command == "ui") {
            if (argc != 2) throw std::invalid_argument("ui не принимает параметры");
            return launch_ui(argv[0]);
        }
        if (command == "profiles") {
            if (argc != 2) throw std::invalid_argument("profiles не принимает параметры");
            print_profiles();
            return 0;
        }
        if (command == "check-audio") {
            if (argc != 2) throw std::invalid_argument("check-audio не принимает параметры");
            return check_audio_devices();
        }

        auto arguments = parse_arguments(argc, argv, 2);
        const auto profile = acoustic::load_profile(arguments.profile);
        if (command == "self-test" && arguments.positional.empty()) {
            if (arguments.gain_set || arguments.seconds_set || arguments.force || arguments.json) {
                throw std::invalid_argument("self-test принимает только --profile");
            }
            return self_test(profile);
        }
        if (command == "calibrate" && arguments.positional.empty()) {
            if (arguments.gain_set || arguments.seconds_set || arguments.force || arguments.json) {
                throw std::invalid_argument("calibrate принимает только --profile");
            }
            return calibrate(profile);
        }
        if (command == "estimate" && arguments.positional.size() == 1) {
            if (arguments.gain_set || arguments.seconds_set || arguments.force) {
                throw std::invalid_argument("estimate принимает только --profile и --json");
            }
            return estimate_command(normalize_path(arguments.positional[0]), profile, arguments.json);
        }
        if (command == "benchmark" && arguments.positional.size() <= 1) {
            if (arguments.gain_set || arguments.seconds_set || arguments.force) {
                throw std::invalid_argument("benchmark принимает только размер, --profile и --json");
            }
            const auto bytes = arguments.positional.empty() ? 256U :
                parse_unsigned(arguments.positional[0], "размер benchmark");
            return benchmark(bytes, profile, arguments.json);
        }
        if (command == "channel-test" && arguments.positional.size() <= 1) {
            if (arguments.gain_set || arguments.seconds_set || arguments.force) {
                throw std::invalid_argument("channel-test принимает только размер, --profile и --json");
            }
            const auto bytes = arguments.positional.empty() ? 128U :
                parse_unsigned(arguments.positional[0], "размер channel-test");
            return channel_test(bytes, profile, arguments.json);
        }
        if (command == "encode" && arguments.positional.size() == 2) {
            if (arguments.gain_set || arguments.seconds_set || arguments.json) {
                throw std::invalid_argument("encode не принимает --gain или --seconds");
            }
            return encode(normalize_path(arguments.positional[0]), normalize_path(arguments.positional[1]),
                          profile, arguments.force);
        }
        if (command == "decode" && arguments.positional.size() == 2) {
            if (arguments.seconds_set || arguments.json) {
                throw std::invalid_argument("decode не принимает --seconds или --json");
            }
            return decode(normalize_path(arguments.positional[0]), normalize_path(arguments.positional[1]),
                          profile, arguments.gain, arguments.force, true);
        }
        if (command == "send" && arguments.positional.size() == 1) {
            if (arguments.gain_set || arguments.seconds_set || arguments.force || arguments.json) {
                throw std::invalid_argument("send принимает только --profile");
            }
            return send(normalize_path(arguments.positional[0]), profile);
        }
        if (command == "receive" && arguments.positional.size() == 1) {
            if (arguments.json) throw std::invalid_argument("receive не принимает --json");
            return receive(normalize_path(arguments.positional[0]), arguments.seconds, arguments.gain,
                           profile, arguments.force);
        }
        print_help();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }
}

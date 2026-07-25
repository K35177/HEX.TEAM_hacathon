#include "acoustic/profile.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace acoustic {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

double parse_double(const std::string& value, const std::string& key) {
    std::size_t used = 0;
    const double parsed = std::stod(value, &used);
    if (used != value.size() || !std::isfinite(parsed)) {
        throw std::runtime_error("invalid profile value for " + key);
    }
    return parsed;
}

std::uint64_t parse_unsigned(const std::string& value, const std::string& key) {
    if (value.empty() || value.front() == '-') {
        throw std::runtime_error("invalid profile value for " + key);
    }
    std::size_t used = 0;
    const auto parsed = std::stoull(value, &used);
    if (used != value.size()) throw std::runtime_error("invalid profile value for " + key);
    return parsed;
}

template <typename Integer>
Integer narrow_unsigned(const std::string& value, const std::string& key) {
    const auto parsed = parse_unsigned(value, key);
    if (parsed > std::numeric_limits<Integer>::max()) {
        throw std::runtime_error("profile value is too large for " + key);
    }
    return static_cast<Integer>(parsed);
}

bool apply_modem_setting(TransferProfile& profile, const std::string& key,
                         const std::string& value) {
    if (key == "sample_rate") profile.modem.sample_rate = narrow_unsigned<std::uint32_t>(value, key);
    else if (key == "symbol_rate") profile.modem.symbol_rate = narrow_unsigned<std::uint32_t>(value, key);
    else if (key == "modulation_order") profile.modem.modulation_order = narrow_unsigned<std::uint8_t>(value, key);
    else if (key == "base_frequency" || key == "frequency_zero") profile.modem.base_frequency = parse_double(value, key);
    else if (key == "frequency_spacing") profile.modem.frequency_spacing = parse_double(value, key);
    else if (key == "frequency_one") profile.modem.frequency_spacing = parse_double(value, key) - profile.modem.base_frequency;
    else if (key == "amplitude") profile.modem.amplitude = parse_double(value, key);
    else if (key == "repetitions") profile.modem.symbol_repetitions = narrow_unsigned<std::uint8_t>(value, key);
    else return false;
    return true;
}

bool apply_frame_setting(TransferProfile& profile, const std::string& key,
                         const std::string& value) {
    if (key == "chirp_duration") profile.frame.chirp_duration_seconds = parse_double(value, key);
    else if (key == "guard_duration") profile.frame.guard_duration_seconds = parse_double(value, key);
    else if (key == "chirp_start_frequency") profile.frame.chirp_start_frequency = parse_double(value, key);
    else if (key == "chirp_end_frequency") profile.frame.chirp_end_frequency = parse_double(value, key);
    else return false;
    return true;
}

TransferProfile default_profile(std::string_view name) {
    for (auto profile : builtin_profiles()) {
        if (profile.name == name) return profile;
    }
    throw std::runtime_error("unknown profile: " + std::string(name));
}

TransferProfile load_file(const std::filesystem::path& path, TransferProfile profile) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open profile: " + path.string());
    profile.name = path.stem().string();
    std::string line;
    while (std::getline(input, line)) {
        if (const auto comment = line.find('#'); comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(std::move(line));
        if (line.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("invalid profile line: " + line);
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1U));
        if (key == "description") profile.description = value;
        else if (key == "block_size") profile.block_size = narrow_unsigned<std::size_t>(value, key);
        else if (!apply_modem_setting(profile, key, value) &&
                 !apply_frame_setting(profile, key, value)) {
            throw std::runtime_error("unknown profile key: " + key);
        }
    }
    validate_profile(profile);
    return profile;
}

}  // namespace

std::vector<TransferProfile> builtin_profiles() {
    TransferProfile turbo;
    turbo.name = "turbo";
    turbo.description = "1920 bit/s below 8.2 kHz for common speakers and microphones";
    turbo.modem.symbol_rate = 480;
    turbo.modem.modulation_order = 16;
    turbo.modem.base_frequency = 960.0;
    turbo.modem.frequency_spacing = 480.0;
    turbo.modem.amplitude = 0.70;
    turbo.block_size = 4096;
    turbo.frame.chirp_duration_seconds = 0.22;
    turbo.frame.guard_duration_seconds = 0.03;
    turbo.frame.chirp_start_frequency = 700.0;
    turbo.frame.chirp_end_frequency = 8500.0;

    TransferProfile wideband;
    wideband.name = "wideband";
    wideband.description = "3200 bit/s for verified full-band hardware";
    wideband.modem.symbol_rate = 800;
    wideband.modem.modulation_order = 16;
    wideband.modem.base_frequency = 1500.0;
    wideband.modem.frequency_spacing = 900.0;
    wideband.modem.amplitude = 0.70;
    wideband.block_size = 4096;
    wideband.frame.chirp_duration_seconds = 0.20;
    wideband.frame.guard_duration_seconds = 0.03;
    wideband.frame.chirp_start_frequency = 900.0;
    wideband.frame.chirp_end_frequency = 19000.0;

    TransferProfile fast;
    fast.name = "fast";
    fast.description = "600 bit/s for a quiet short-range channel";
    fast.modem.symbol_rate = 300;
    fast.modem.base_frequency = 1500.0;
    fast.modem.frequency_spacing = 900.0;
    fast.block_size = 1024;
    fast.frame.chirp_start_frequency = 900.0;
    fast.frame.chirp_end_frequency = 4800.0;

    TransferProfile balanced;
    balanced.name = "balanced";
    balanced.description = "400 bit/s default profile";
    balanced.block_size = 512;

    TransferProfile robust;
    robust.name = "robust";
    robust.description = "repeated 2-FSK for noisy or reverberant rooms";
    robust.modem.symbol_rate = 200;
    robust.modem.modulation_order = 2;
    robust.modem.base_frequency = 1200.0;
    robust.modem.frequency_spacing = 1000.0;
    robust.modem.amplitude = 0.70;
    robust.modem.symbol_repetitions = 3;
    robust.block_size = 256;
    robust.frame.chirp_duration_seconds = 0.35;
    robust.frame.guard_duration_seconds = 0.08;
    robust.frame.chirp_start_frequency = 700.0;
    robust.frame.chirp_end_frequency = 4200.0;

    return {turbo, wideband, fast, balanced, robust};
}

void validate_profile(const TransferProfile& profile) {
    (void)samples_per_symbol(profile.modem);
    if (profile.name.empty()) throw std::invalid_argument("profile name is empty");
    if (profile.block_size == 0 || profile.block_size > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("profile block size is invalid");
    }
    if (!std::isfinite(profile.frame.chirp_duration_seconds) ||
        !std::isfinite(profile.frame.guard_duration_seconds) ||
        !std::isfinite(profile.frame.chirp_start_frequency) ||
        !std::isfinite(profile.frame.chirp_end_frequency) ||
        profile.frame.chirp_duration_seconds <= 0.0 ||
        profile.frame.guard_duration_seconds < 0.0 ||
        profile.frame.chirp_start_frequency <= 0.0 ||
        profile.frame.chirp_end_frequency <= profile.frame.chirp_start_frequency ||
        profile.frame.chirp_end_frequency >= profile.modem.sample_rate * 0.45) {
        throw std::invalid_argument("profile chirp settings are invalid");
    }
}

TransferProfile load_profile(std::string_view name_or_path) {
    if (name_or_path.empty()) name_or_path = "balanced";
    const std::filesystem::path requested{name_or_path};
    std::error_code error;
    if (std::filesystem::is_regular_file(requested, error)) {
        TransferProfile base;
        try { base = default_profile(requested.stem().string()); }
        catch (const std::exception&) { base = default_profile("balanced"); }
        return load_file(requested, std::move(base));
    }
    const auto configured = std::filesystem::path("config") /
        (std::string(name_or_path) + ".conf");
    if (std::filesystem::is_regular_file(configured, error)) {
        return load_file(configured, default_profile(name_or_path));
    }
    auto profile = default_profile(name_or_path);
    validate_profile(profile);
    return profile;
}

}  // namespace acoustic

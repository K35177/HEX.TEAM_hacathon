#include "acoustic/wav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace acoustic {
namespace {

void write_u16(std::ostream& out, std::uint16_t value) {
    const std::array<char, 2> bytes{static_cast<char>(value),
                                    static_cast<char>(value >> 8U)};
    out.write(bytes.data(), bytes.size());
}

void write_u32(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> bytes{static_cast<char>(value),
                                    static_cast<char>(value >> 8U),
                                    static_cast<char>(value >> 16U),
                                    static_cast<char>(value >> 24U)};
    out.write(bytes.data(), bytes.size());
}

std::uint16_t read_u16(std::istream& in) {
    std::array<unsigned char, 2> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!in) throw std::runtime_error("unexpected end of WAV file");
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8U));
}

std::uint32_t read_u32(std::istream& in) {
    std::array<unsigned char, 4> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!in) throw std::runtime_error("unexpected end of WAV file");
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::string read_tag(std::istream& in) {
    std::array<char, 4> tag{};
    in.read(tag.data(), tag.size());
    if (!in) throw std::runtime_error("unexpected end of WAV file");
    return {tag.data(), tag.size()};
}

}  // namespace

void write_wav(const std::filesystem::path& path,
               std::span<const float> samples,
               std::uint32_t sample_rate) {
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits_per_sample = 16;
    if (samples.size() > (std::numeric_limits<std::uint32_t>::max() - 36U) / 2U) {
        throw std::runtime_error("audio is too large for WAV format");
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create WAV file: " + path.string());
    const auto data_size = static_cast<std::uint32_t>(samples.size() * 2U);
    out.write("RIFF", 4); write_u32(out, 36U + data_size); out.write("WAVE", 4);
    out.write("fmt ", 4); write_u32(out, 16); write_u16(out, 1);
    write_u16(out, channels); write_u32(out, sample_rate);
    write_u32(out, sample_rate * channels * bits_per_sample / 8U);
    write_u16(out, channels * bits_per_sample / 8U); write_u16(out, bits_per_sample);
    out.write("data", 4); write_u32(out, data_size);
    for (const float sample : samples) {
        const float limited = std::clamp(sample, -1.0F, 1.0F);
        const auto pcm = static_cast<std::int16_t>(std::lround(limited * 32767.0F));
        write_u16(out, static_cast<std::uint16_t>(pcm));
    }
    if (!out) throw std::runtime_error("failed while writing WAV file");
}

WavData read_wav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open WAV file: " + path.string());
    if (read_tag(in) != "RIFF") throw std::runtime_error("not a RIFF file");
    (void)read_u32(in);
    if (read_tag(in) != "WAVE") throw std::runtime_error("not a WAVE file");

    WavData result;
    bool format_found = false;
    while (in && !result.samples.size()) {
        const auto tag = read_tag(in);
        const auto size = read_u32(in);
        if (tag == "fmt ") {
            if (size < 16) throw std::runtime_error("invalid WAV fmt chunk");
            const auto format = read_u16(in);
            const auto channels = read_u16(in);
            result.sample_rate = read_u32(in);
            (void)read_u32(in); (void)read_u16(in);
            const auto bits = read_u16(in);
            if (format != 1 || channels != 1 || bits != 16) {
                throw std::runtime_error("only mono 16-bit PCM WAV is supported");
            }
            if (size > 16) in.seekg(size - 16, std::ios::cur);
            format_found = true;
        } else if (tag == "data") {
            if (!format_found) throw std::runtime_error("WAV data precedes format");
            if (size % 2U != 0) throw std::runtime_error("invalid PCM data size");
            result.samples.reserve(size / 2U);
            for (std::uint32_t i = 0; i < size / 2U; ++i) {
                const auto pcm = static_cast<std::int16_t>(read_u16(in));
                result.samples.push_back(static_cast<float>(pcm) / 32767.0F);
            }
        } else {
            in.seekg(size + (size & 1U), std::ios::cur);
        }
    }
    if (!format_found || result.samples.empty()) throw std::runtime_error("WAV has no audio data");
    return result;
}

}  // namespace acoustic

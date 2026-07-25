#include "acoustic/transfer.hpp"

#include "acoustic/crc32.hpp"
#include "acoustic/packet.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace acoustic {
namespace {
constexpr std::size_t metadata_size = 47;

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) value = (value << 8U) | data[offset + i];
    return value;
}
}  // namespace

TransferEstimate estimate_transfer(std::size_t file_size, std::size_t block_size,
                                   std::size_t filename_size) {
    if (block_size == 0 || block_size > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("invalid transfer block size");
    }
    if (filename_size > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("filename is too long");
    }
    const std::size_t count = file_size == 0 ? 1U : 1U + (file_size - 1U) / block_size;
    if (count > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("file requires too many blocks");
    }
    constexpr std::size_t packet_overhead = 19;
    if (file_size > std::numeric_limits<std::size_t>::max() - metadata_size - filename_size ||
        count > (std::numeric_limits<std::size_t>::max() - metadata_size - filename_size - file_size) /
                    packet_overhead) {
        throw std::invalid_argument("estimated transfer size overflows");
    }
    return {count, metadata_size + filename_size + file_size + count * packet_overhead};
}

std::vector<std::uint8_t> create_transfer_stream(
    std::span<const std::uint8_t> file_data, std::size_t block_size, std::string_view filename) {
    const auto estimate = estimate_transfer(file_data.size(), block_size, filename.size());
    const auto count = estimate.block_count;
    std::vector<std::uint8_t> stream{'H','X','M','D',1,
        static_cast<std::uint8_t>(filename.size() >> 8U), static_cast<std::uint8_t>(filename.size())};
    append_u64(stream, file_data.size());
    const auto digest = sha256(file_data);
    stream.insert(stream.end(), digest.begin(), digest.end());
    stream.insert(stream.end(), filename.begin(), filename.end());
    const auto file_id = crc32(file_data);
    for (std::size_t index = 0; index < count; ++index) {
        const auto begin = std::min(index * block_size, file_data.size());
        const auto end = std::min(begin + block_size, file_data.size());
        Packet packet{file_id, static_cast<std::uint16_t>(index),
                      static_cast<std::uint16_t>(count), {}};
        packet.payload.assign(file_data.begin() + begin, file_data.begin() + end);
        const auto bytes = serialize_packet(packet);
        stream.insert(stream.end(), bytes.begin(), bytes.end());
    }
    return stream;
}

ReceivedFile receive_transfer_stream(std::span<const std::uint8_t> stream) {
    constexpr std::size_t header_size = 15;
    constexpr std::size_t crc_size = 4;
    if (stream.size() < metadata_size || stream[0] != 'H' || stream[1] != 'X' ||
        stream[2] != 'M' || stream[3] != 'D' || stream[4] != 1) {
        throw std::runtime_error("invalid transfer metadata");
    }
    const std::size_t filename_size = (static_cast<std::size_t>(stream[5]) << 8U) | stream[6];
    if (metadata_size + filename_size > stream.size()) throw std::runtime_error("truncated metadata");
    const auto expected_size = read_u64(stream, 7);
    Sha256Digest expected_digest{};
    std::copy_n(stream.begin() + 15, expected_digest.size(), expected_digest.begin());
    ReceivedFile result;
    result.filename.assign(stream.begin() + metadata_size, stream.begin() + metadata_size + filename_size);
    std::size_t offset = metadata_size + filename_size;
    std::uint32_t file_id = 0;
    std::uint16_t block_count = 0;
    std::uint16_t expected_index = 0;
    while (offset < stream.size()) {
        if (stream.size() - offset < header_size + crc_size) throw std::runtime_error("truncated packet");
        const std::size_t payload_size =
            (static_cast<std::size_t>(stream[offset + 13]) << 8U) | stream[offset + 14];
        const std::size_t packet_size = header_size + payload_size + crc_size;
        if (packet_size > stream.size() - offset) throw std::runtime_error("truncated payload");
        const auto packet = deserialize_packet(stream.subspan(offset, packet_size));
        if (expected_index == 0) {
            file_id = packet.file_id;
            block_count = packet.block_count;
            if (block_count == 0) throw std::runtime_error("invalid zero block count");
        }
        if (packet.file_id != file_id || packet.block_count != block_count ||
            packet.block_index != expected_index) throw std::runtime_error("unexpected or missing block");
        result.data.insert(result.data.end(), packet.payload.begin(), packet.payload.end());
        ++expected_index;
        offset += packet_size;
    }
    if (expected_index != block_count) throw std::runtime_error("transfer is incomplete");
    if (result.data.size() != expected_size) throw std::runtime_error("whole-file size mismatch");
    if (crc32(result.data) != file_id) throw std::runtime_error("whole-file CRC32 mismatch");
    result.sha256 = sha256(result.data);
    if (result.sha256 != expected_digest) throw std::runtime_error("whole-file SHA-256 mismatch");
    return result;
}

std::vector<std::uint8_t> restore_transfer_stream(std::span<const std::uint8_t> stream) {
    return receive_transfer_stream(stream).data;
}

}  // namespace acoustic

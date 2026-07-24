#include "acoustic/transfer.hpp"

#include "acoustic/crc32.hpp"
#include "acoustic/packet.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace acoustic {

std::vector<std::uint8_t> create_transfer_stream(
    std::span<const std::uint8_t> file_data, std::size_t block_size) {
    if (block_size == 0 || block_size > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("invalid transfer block size");
    }
    const std::size_t count = std::max<std::size_t>(1, (file_data.size() + block_size - 1) / block_size);
    if (count > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("file requires too many blocks");
    }
    std::vector<std::uint8_t> stream;
    const auto file_id = crc32(file_data);
    for (std::size_t index = 0; index < count; ++index) {
        const auto begin = std::min(index * block_size, file_data.size());
        const auto end = std::min(begin + block_size, file_data.size());
        Packet packet;
        packet.file_id = file_id;
        packet.block_index = static_cast<std::uint16_t>(index);
        packet.block_count = static_cast<std::uint16_t>(count);
        packet.payload.assign(file_data.begin() + begin, file_data.begin() + end);
        const auto bytes = serialize_packet(packet);
        stream.insert(stream.end(), bytes.begin(), bytes.end());
    }
    return stream;
}

std::vector<std::uint8_t> restore_transfer_stream(std::span<const std::uint8_t> stream) {
    constexpr std::size_t header_size = 15;
    constexpr std::size_t crc_size = 4;
    std::vector<std::uint8_t> file_data;
    std::size_t offset = 0;
    std::uint32_t file_id = 0;
    std::uint16_t block_count = 0;
    std::uint16_t expected_index = 0;
    while (offset < stream.size()) {
        if (stream.size() - offset < header_size + crc_size) {
            throw std::runtime_error("truncated packet in transfer stream");
        }
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
            packet.block_index != expected_index) {
            throw std::runtime_error("unexpected or missing transfer block");
        }
        file_data.insert(file_data.end(), packet.payload.begin(), packet.payload.end());
        ++expected_index;
        offset += packet_size;
    }
    if (expected_index != block_count) throw std::runtime_error("transfer is incomplete");
    if (crc32(file_data) != file_id) throw std::runtime_error("whole-file CRC32 mismatch");
    return file_data;
}

}  // namespace acoustic

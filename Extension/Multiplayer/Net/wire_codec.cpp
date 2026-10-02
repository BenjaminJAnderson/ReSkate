#include "wire_codec.h"
#include "block_codec.h"
#include <algorithm>
#include <cstring>
namespace dingosdk::multiplayer {
namespace {
constexpr std::array<std::uint8_t, 4> compressed_magic{'R', 'M', 'C', '1'};
constexpr std::size_t compression_header = 12;
bool compressed(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= compression_header &&
           (std::equal(compressed_magic.begin(), compressed_magic.end(), bytes.begin()) ||
            std::memcmp(bytes.data(), "RMZ1", 4) == 0);
}
std::uint32_t integer(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t out{};
    for (unsigned i = 0; i < 4; ++i)
        out |= std::uint32_t{bytes[at + i]} << (8 * i);
    return out;
}
void put(std::vector<std::uint8_t> &bytes, std::size_t at, std::size_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
} // namespace
std::vector<std::uint8_t> encode_wire(const Packet &packet) {
    return encode_wire_bytes(encode(packet, true));
}
std::vector<std::uint8_t> encode_wire_bytes(std::span<const std::uint8_t> raw) {
    if (raw.size() < 256)
        return {raw.begin(), raw.end()};
    auto block = compress_block(raw);
    if (block.bytes.size() + compression_header >= raw.size()) return {raw.begin(), raw.end()};
    std::vector<std::uint8_t> out(compression_header);
    std::copy(compressed_magic.begin(), compressed_magic.end(), out.begin());
    if (block.codec == BlockCodec::zstd) out[2] = 'Z';
    put(out, 4, raw.size());
    put(out, 8, block.bytes.size());
    out.insert(out.end(), block.bytes.begin(), block.bytes.end());
    return out;
}
std::size_t wire_original_size(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "RMB1", 4) == 0)
        return wire_original_size(bytes.subspan(4));
    if (bytes.size() >= 30 && (std::memcmp(bytes.data(), "RMD1", 4) == 0 ||
                             std::memcmp(bytes.data(), "RMS1", 4) == 0 ||
                             std::memcmp(bytes.data(), "RMD2", 4) == 0 || std::memcmp(bytes.data(), "RMS2", 4) == 0))
        return integer(bytes, 26);
    return compressed(bytes) ? integer(bytes, 4) : bytes.size();
}
std::optional<std::vector<std::uint8_t>> decode_wire_bytes(std::span<const std::uint8_t> bytes) noexcept {
    try {
        if (bytes.size() > max_packet)
            return {};
        if (!compressed(bytes))
            return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        const auto raw_size = integer(bytes, 4), payload = integer(bytes, 8);
        if (raw_size < packet_header_size || raw_size > max_packet || payload != bytes.size() - compression_header ||
            raw_size <= bytes.size())
            return {};
        std::vector<std::uint8_t> raw(raw_size);
        if (!decompress_block(bytes.subspan(compression_header), raw,
            bytes[2] == 'Z' ? BlockCodec::zstd : BlockCodec::lz4)) return {};
        return raw;
    } catch (...) {
        return {};
    }
}
std::optional<Packet> decode_wire(std::span<const std::uint8_t> bytes) noexcept {
    const auto raw = decode_wire_bytes(bytes);
    return raw ? decode(*raw) : std::nullopt;
}
} // namespace dingosdk::multiplayer

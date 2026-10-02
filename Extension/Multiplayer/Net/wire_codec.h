#pragma once
#include "protocol.h"
namespace dingosdk::multiplayer {
// Independent lossless blocks: a dropped pose never invalidates later poses.
std::vector<std::uint8_t> encode_wire(const Packet &);
std::vector<std::uint8_t> encode_wire_bytes(std::span<const std::uint8_t>);
std::optional<std::vector<std::uint8_t>> decode_wire_bytes(std::span<const std::uint8_t>) noexcept;
std::optional<Packet> decode_wire(std::span<const std::uint8_t>) noexcept;
std::size_t wire_original_size(std::span<const std::uint8_t>) noexcept;
} // namespace dingosdk::multiplayer

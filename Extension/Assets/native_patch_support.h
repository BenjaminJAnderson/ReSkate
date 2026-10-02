#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace dingosdk {
// Version 1: Studio's unsigned TOCs. Version 2 also accepts edited signed
// InitFS/package envelopes through the native verifier's signature check.
inline constexpr std::uint32_t native_patch_support_version = 2;
inline constexpr std::size_t native_patch_envelope_size = 0x22c;

// Recognizes the supported native container layout, not its authenticity.
// Obfuscated, incomplete and unknown envelopes retain the native failure path.
constexpr bool is_native_data_envelope(std::span<const std::uint8_t> header,
    std::uint64_t length, std::uintptr_t address) noexcept {
    if (!address || header.size() < native_patch_envelope_size ||
        length <= native_patch_envelope_size ||
        length > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    return header[0] == 0x00 && header[1] == 0xd1 && header[2] == 0xce && header[3] == 0x01;
}

// The exact zero-signature envelope emitted by Studio's native TOC writer.
constexpr bool is_unsigned_native_patch_envelope(std::span<const std::uint8_t> header,
    std::uint64_t length, std::uintptr_t address) noexcept {
    if (!is_native_data_envelope(header, length, address)) return false;
    for (std::size_t i = 4; i < native_patch_envelope_size; ++i)
        if (header[i] != 0) return false;
    return true;
}

// Call once after Detours hook service initialization, while the launcher still holds entry.
// Signed envelopes use the original verifier, allowing STATUS_INVALID_SIGNATURE
// only for its exact signature call and the current recognized data envelope.
bool start_native_patch_support(std::uintptr_t base, std::string& error);
} // namespace dingosdk

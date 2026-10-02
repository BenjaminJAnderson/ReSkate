#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dingosdk::game::build::v20260929::native_patch_support {
template<std::size_t N> consteval auto hex_bytes(const char (&hex)[N]) {
    static_assert(N % 2 == 1);
    std::array<std::uint8_t, (N - 1) / 2> result{};
    const auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw "Invalid native contract hex";
    };
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    return result;
}

// Data-envelope verifier hooked to accept unsigned patch envelopes.
inline constexpr std::uintptr_t verifier = 0x041fb4c0;
// IAT slot of BCryptVerifySignature.
inline constexpr std::uintptr_t signature_import = 0x05febbc8;
// Return address of the verifier's BCryptVerifySignature call.
inline constexpr std::uintptr_t signature_return = 0x041fb5f1;

// September 29 2026, supported_build::game_sha256. Complete 392-byte verifier,
// SHA256 34fc18454f75487e459a36fb175ee3ffabe8e83e50f78cc5afe6aae0d1f11ea9.
// RCX=raw envelope, RDX=uint64_t* length; success returns raw+0x22c and
// subtracts 0x22c from length. Crypto/provider errors retain native behavior;
// only the inspected BCryptVerifySignature call can accept an edited payload.
inline constexpr auto verifier_bytes = hex_bytes(
    "405355564881ec80000000488b05ee8efc024833c44889442470488bd9488b0a"
    "4881f92c0200000f824f0100008b032d00d1ce00a9fffffffe0f853d01000048"
    "8db1d4fdffff488932813b00d1ce010f85270100004533c948c7442448000000"
    "004533c048c744244000000000488d15c4833902488d4c2448e894262701c1e8"
    "1f84c00f85f3000000488b4c2448488d057b7d3902c7442430000000004c8d4c"
    "2440c74424281b0100004c8d058f8339024889bc24b000000033d24889442420"
    "4032ffe892262701c1e81f84c075754c8d0d8a833902c7442420330000004c8b"
    "c6488d932c020000488d4c2458e83ea03afd488b4c2440488d059a8339024889"
    "4424504c8d442458488d4308c744243002000000c744242800010000488d5424"
    "5041b9140000004889442420e835262701488b4c24408bf8c1ef1f4080f701e8"
    "1c262701488b4c244833d2e8c82527014084ff488bbc24b0000000741f488d83"
    "2c020000488b4c24704833cce8ff0327014881c4800000005e5d5bc3b92a0000"
    "00ff15e901df01cc");
static_assert(verifier_bytes.size() == 392);
inline constexpr auto crypto_thunk = hex_bytes("ff259cdfb700");
inline constexpr auto reader_call = hex_bytes("488d542458488bcee8c9de0000");
inline constexpr auto layout_call = hex_bytes("488b4c2468488d5588e832df0300");
inline constexpr auto unwind_root = hex_bytes("191a04000bf204600350023010b94605");
inline constexpr auto unwind_middle = hex_bytes("2108020008741600c0b41f0471b51f04e480e406");
inline constexpr auto unwind_return = hex_bytes("21000000c0b41f0471b51f04e480e406");
struct Contract { std::uintptr_t rva; std::span<const std::uint8_t> bytes; };
inline constexpr Contract contracts[]{
    {verifier, verifier_bytes}, {0x0546dc26, crypto_thunk},
    {0x041ed5ea, reader_call}, {0x041bd580, layout_call},
    {0x06e480e4, unwind_root}, {0x06e480f8, unwind_middle}, {0x06e4810c, unwind_return}
};
}

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dingosdk::game::build::v20260929::protossl {
template<std::size_t N> consteval auto hex_bytes(const char (&hex)[N]) {
    static_assert(N % 2 == 1);
    std::array<unsigned char, (N - 1) / 2> result{};
    const auto nibble = [](char ch) -> unsigned {
        if (ch >= '0' && ch <= '9') return static_cast<unsigned>(ch - '0');
        if (ch >= 'a' && ch <= 'f') return static_cast<unsigned>(ch - 'a' + 10);
        throw "Invalid native contract hex";
    };
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<unsigned char>((nibble(hex[index * 2]) << 4) | nibble(hex[index * 2 + 1]));
    return result;
}

// DirtySDK _ProtoSSLSetCACert(const uint8_t* pem, int32_t size, uint8_t verify):
// adds PEM CA certificates to the game's TLS trust list and returns how many
// were added (negative on error). The game itself adds GlobalSign, DigiCert G2
// and Amazon roots through it. With verify set, each certificate must chain to
// an already trusted root, which a new root cannot.
inline constexpr std::uintptr_t set_ca_cert = 0x1fdd9c0;
inline constexpr auto set_ca_cert_prefix = hex_bytes("405553565741564157488dac2448f5ffff4881ecb80b0000");
// DirtySDK's allocator. Until the game sets it, set_ca_cert allocates through
// a fallback that is not ready yet and crashes; wait for it to be non-null.
inline constexpr std::uintptr_t dirty_allocator = 0x73c5330;
}

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dingosdk::game::build::v20260929::loose_files {
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

// VFS open. The Lua vfs.open binding and the engine's read-file wrapper both
// reach this open ABI.
inline constexpr std::uintptr_t vfs_open = 0x4b51130;
inline constexpr auto vfs_open_prefix = hex_bytes("4c8bdc5557498dabc8f9ffff4881ec28070000488b05763267024833c4488985");
// VFS exists.
inline constexpr std::uintptr_t vfs_exists = 0x4b52660;
inline constexpr auto vfs_exists_prefix = hex_bytes("40574881ec80050000488b05501d67024833c448898424700500004c8bc2c744");
// Directory file-system factory.
inline constexpr std::uintptr_t create_directory = 0x4b4c220;
inline constexpr auto create_directory_prefix = hex_bytes("48895c24184889742420574883ec40488b154aac7a02488bd9b9a0000000e8ad");
// VFS mount.
inline constexpr std::uintptr_t vfs_mount = 0x4b53f90;
inline constexpr auto vfs_mount_prefix = hex_bytes("40535556574883ec28488b5910488be9488bcb498bf8488bf2e8d2bda3fc488b");
// Returns the global VFS.
inline constexpr std::uintptr_t global_vfs = 0x4b4d410;
inline constexpr auto global_vfs_prefix = hex_bytes("488b058919c902c3");
// Reference taken inside mount: the factory initializes its intrusive
// reference count at +8 and Mount takes a ref.
inline constexpr std::uintptr_t mount_reference = 0x4b53e62;
inline constexpr auto mount_reference_prefix = hex_bytes("4d85ff7405f041ff4708");
}

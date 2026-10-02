#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>

// Checks that the running game is exactly the supported Skate.exe build.
namespace dingosdk::supported_build {
// Every Skate.exe build loads at this preferred base (ASLR relocates it).
inline constexpr std::uintptr_t preferred_image_base = 0x140000000ULL;

// True when the loaded image at `base` carries the supported PE headers.
bool image_headers_match(std::uintptr_t base) noexcept;

// True when `base` is this process's Skate.exe, its headers match, and the
// file on disk hashes to game_sha256. The file is hashed once per process.
bool running_image_matches(std::uintptr_t base) noexcept;

// SHA-256 of a whole file, read from the start.
bool file_sha256(HANDLE file, std::array<unsigned char, 32>& digest) noexcept;
bool file_sha256_matches(HANDLE file, const std::array<unsigned char, 32>& expected) noexcept;
// SHA-256 of `size` bytes (at most 1 MiB) at `offset`.
bool file_region_sha256_matches(HANDLE file, std::uint64_t offset, std::size_t size,
    const std::array<unsigned char, 32>& expected) noexcept;
}

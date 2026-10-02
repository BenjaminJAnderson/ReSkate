#include "image_identity.h"
#include "supported_build.h"
#include "Engine/Core/Platform/memory.h"
#include <bcrypt.h>
#include <algorithm>

namespace dingosdk::supported_build {
namespace {
struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    bool ready{};
    Sha256() noexcept {
        ready = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    bool add(const unsigned char* data, DWORD size) noexcept {
        return BCryptHashData(hash, const_cast<PUCHAR>(data), size, 0) >= 0;
    }
    bool finish(std::array<unsigned char, 32>& digest) noexcept {
        return BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    }
};

bool seek(HANDLE file, std::uint64_t offset) noexcept {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
}

bool hash_running_image(std::uintptr_t base) noexcept {
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(reinterpret_cast<HMODULE>(base), path.data(),
        static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return false;
    const auto file = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool matched = file_sha256_matches(file, game_sha256_bytes);
    CloseHandle(file);
    return matched;
}
}

bool image_headers_match(std::uintptr_t base) noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return base && memory::read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew >= static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) && dos.e_lfanew <= 0x100000 &&
        memory::read(base + static_cast<std::uintptr_t>(dos.e_lfanew), nt) &&
        nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.OptionalHeader.ImageBase == preferred_image_base &&
        nt.OptionalHeader.SizeOfImage == game_image_size;
}

bool running_image_matches(std::uintptr_t base) noexcept {
    if (!base || reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != base ||
        !image_headers_match(base))
        return false;
    static const bool matched = hash_running_image(base);
    return matched;
}

bool file_sha256(HANDLE file, std::array<unsigned char, 32>& digest) noexcept {
    if (!file || file == INVALID_HANDLE_VALUE || !seek(file, 0)) return false;
    Sha256 sha;
    if (!sha.ready) return false;
    std::array<unsigned char, 65536> block{};
    for (;;) {
        DWORD count{};
        if (!ReadFile(file, block.data(), static_cast<DWORD>(block.size()), &count, nullptr)) return false;
        if (!count) break;
        if (!sha.add(block.data(), count)) return false;
    }
    return sha.finish(digest);
}

bool file_sha256_matches(HANDLE file, const std::array<unsigned char, 32>& expected) noexcept {
    std::array<unsigned char, 32> digest{};
    return file_sha256(file, digest) && digest == expected;
}

bool file_region_sha256_matches(HANDLE file, std::uint64_t offset, std::size_t size,
    const std::array<unsigned char, 32>& expected) noexcept {
    if (!file || file == INVALID_HANDLE_VALUE || !size || size > 1024 * 1024 || !seek(file, offset))
        return false;
    Sha256 sha;
    if (!sha.ready) return false;
    std::array<unsigned char, 4096> block{};
    for (auto remaining = size; remaining;) {
        const auto wanted = static_cast<DWORD>(std::min(remaining, block.size()));
        DWORD count{};
        if (!ReadFile(file, block.data(), wanted, &count, nullptr) || count != wanted ||
            !sha.add(block.data(), count))
            return false;
        remaining -= count;
    }
    std::array<unsigned char, 32> digest{};
    return sha.finish(digest) && digest == expected;
}
}

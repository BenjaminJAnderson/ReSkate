#include "native_entity_pages.h"

#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/20260929/entity_pages.h"
#include "Engine/Game/Build/20260929/spatial_buckets.h"
#include "Engine/Game/Build/20260929/pending_updates.h"

#include <Windows.h>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>

namespace dingosdk {
namespace {
namespace contract = game::build::v20260929::entity_pages;
namespace spatial = game::build::v20260929::spatial_buckets;
namespace updates = game::build::v20260929::pending_updates;
constexpr auto patches = [] {
    std::array<contract::Patch, std::size(contract::patches) + std::size(spatial::patches) +
        std::size(updates::patches)> result{};
    std::size_t index{};
    for (const auto& patch : contract::patches) result[index++] = patch;
    for (const auto& patch : spatial::patches) result[index++] = patch;
    for (const auto& patch : updates::patches) result[index++] = patch;
    return result;
}();
std::mutex install_mutex;
std::uintptr_t installed_base{};

bool read(std::uintptr_t address, void* output, std::size_t size) noexcept {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        output, size, &count) && count == size;
}

bool matches(std::uintptr_t address, std::span<const std::uint8_t> expected) noexcept {
    std::array<std::uint8_t, 15> actual{};
    return expected.size() <= actual.size() && read(address, actual.data(), expected.size()) &&
        std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
}

bool write(std::uintptr_t address, std::span<const std::uint8_t> bytes) noexcept {
    auto* target = reinterpret_cast<void*>(address);
    DWORD previous{};
    if (!VirtualProtect(target, bytes.size(), PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(target, bytes.data(), bytes.size());
    DWORD ignored{};
    const bool restored = VirtualProtect(target, bytes.size(), previous, &ignored) != FALSE;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, bytes.size()) != FALSE;
    return restored && flushed && matches(address, bytes);
}

bool valid_image(std::uintptr_t base) noexcept {
    if (!base || base > std::numeric_limits<std::uintptr_t>::max() - supported_build::game_image_size)
        return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return read(base, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew >= static_cast<LONG>(sizeof(dos)) && dos.e_lfanew <= 0x1000 &&
        read(base + dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.OptionalHeader.SizeOfImage == supported_build::game_image_size;
}
}

bool start_native_entity_pages(std::uintptr_t base, std::string& error) {
    std::lock_guard lock(install_mutex);
    error.clear();
    if (installed_base) {
        if (installed_base == base) return true;
        error = "Entity pages already installed for a different image";
        return false;
    }
    if (!valid_image(base)) {
        error = "Unsupported native entity-page image";
        return false;
    }
    // Validate all contracts before writing. Page and bucket layout changes
    // must never leave mixed readers/writers or a stale vector stride literal.
    for (const auto& patch : patches) {
        if (patch.rva > supported_build::game_image_size - patch.size ||
            !matches(base + patch.rva, {patch.expected.data(), patch.size})) {
            error = "Native entity-page instruction mismatch at RVA " + std::to_string(patch.rva);
            return false;
        }
    }
    std::size_t applied{};
    for (const auto& patch : patches) {
        ++applied;
        if (write(base + patch.rva, {patch.replacement.data(), patch.size})) continue;
        while (applied) {
            const auto& restore = patches[--applied];
            if (!write(base + restore.rva, {restore.expected.data(), restore.size})) {
                RaiseFailFastException(nullptr, nullptr, 0);
                std::terminate();
            }
        }
        error = "Could not install native entity pages; original instructions restored";
        return false;
    }
    installed_base = base;
    return true;
}
}

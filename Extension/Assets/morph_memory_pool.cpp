#include "morph_memory_pool.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/morph_memory_pool.h"

#include <Windows.h>
#include "Engine/Core/Hooks/hooks.h"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>

namespace dingosdk {
namespace {
// September 8 2026 executable, supported_build::game_sha256. The constructor
// copies all 32 descriptor bytes into this+0x10 before calling anything else.
// Input+0x10 becomes allocator+0x20: both backing-heap creation and free-span
// registration read that same capacity. Only input+0x08 is an alignment mask.
struct MorphHeapDescriptor {
    std::uintptr_t backing_allocator;
    std::uint64_t alignment;
    std::uint64_t capacity;
    std::array<std::uint8_t, 8> flags_and_padding;
};
static_assert(sizeof(MorphHeapDescriptor) == 32);
static_assert(offsetof(MorphHeapDescriptor, capacity) == 0x10);
static_assert(morph_render_heap_bytes == 0x10000000);
static_assert(morph_render_heap_bytes % 0x10000 == 0);
using MorphHeapConstructor = void* (*)(void*, const MorphHeapDescriptor*);
namespace morph = addr::morph_memory_pool;
std::atomic<MorphHeapConstructor> original_constructor{};
std::atomic<std::uintptr_t> temporary_return_address{};
std::atomic<std::uintptr_t> permanent_return_address{};
std::uintptr_t installed_base{};
std::mutex install_mutex;

bool read_memory(const void* source, void* destination, std::size_t size) noexcept {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(), source, destination, size, &count) && count == size;
}

bool validate_contract(std::uintptr_t base) noexcept {
    if (!base || base > std::numeric_limits<std::uintptr_t>::max() - supported_build::game_image_size)
        return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read_memory(reinterpret_cast<const void*>(base), &dos, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        dos.e_lfanew > 0x1000 ||
        !read_memory(reinterpret_cast<const void*>(base + dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage != supported_build::game_image_size) return false;
    for (const auto& contract : morph::contracts) {
        std::array<std::uint8_t, morph::max_contract_size> actual{};
        if (!read_memory(reinterpret_cast<const void*>(base + contract.rva), actual.data(), contract.bytes.size()) ||
            std::memcmp(actual.data(), contract.bytes.data(), contract.bytes.size()) != 0) return false;
    }
    return true;
}

bool pools_not_constructed(std::uintptr_t base) noexcept {
    std::uintptr_t temporary{}, permanent{};
    return read_memory(reinterpret_cast<const void*>(base + morph::temporary_pool), &temporary, sizeof(temporary)) &&
        read_memory(reinterpret_cast<const void*>(base + morph::permanent_pool), &permanent, sizeof(permanent)) &&
        !temporary && !permanent;
}

void* construct_for_caller(void* self, const MorphHeapDescriptor* descriptor, std::uintptr_t caller) {
    const auto incoming_error = GetLastError();
    const auto original = original_constructor.load(std::memory_order_acquire);
    alignas(32) MorphHeapDescriptor local{};
    const auto temporary_caller = temporary_return_address.load(std::memory_order_acquire);
    const auto permanent_caller = permanent_return_address.load(std::memory_order_acquire);
    if (((temporary_caller && caller == temporary_caller) || (permanent_caller && caller == permanent_caller)) &&
        read_memory(descriptor, &local, sizeof(local)) && local.backing_allocator &&
        local.alignment == 0x10000 && local.flags_and_padding[0] == 1 &&
        local.capacity > 0 && local.capacity < morph_render_heap_bytes) {
        local.capacity = morph_render_heap_bytes;
        SetLastError(incoming_error);
        return original(self, &local);
    }
    // Unknown callers/descriptors and already-larger configured
    // heaps retain their exact native pointer, arguments and behavior.
    SetLastError(incoming_error);
    return original(self, descriptor);
}

__declspec(noinline) void* construct_morph_heap(void* self, const MorphHeapDescriptor* descriptor) {
    return construct_for_caller(self, descriptor, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
} // namespace

bool start_morph_memory_pool(std::uintptr_t base, std::string& error) {
    std::lock_guard lock(install_mutex);
    error.clear();
    if (installed_base) {
        if (installed_base == base) return true;
        error = "Morph memory support is already installed on another image";
        return false;
    }
    if (!validate_contract(base)) {
        error = "Morph allocator contract does not match the supported game";
        return false;
    }
    if (!pools_not_constructed(base)) {
        error = "Morph allocators already exist; restart through ReSkate Launcher";
        return false;
    }
    auto* target = reinterpret_cast<void*>(base + morph::constructor_rva);
    MorphHeapConstructor original{};
    const auto created = hook_prepare(target, reinterpret_cast<void*>(&construct_morph_heap),
        reinterpret_cast<void**>(&original));
    if (created != HookOk) {
        error = "Cannot create morph allocator hook (Detours hook service status " + std::to_string(created) + ")";
        return false;
    }
    if (!original) {
        hook_remove(target);
        error = "Morph allocator trampoline is unavailable";
        return false;
    }
    // The entry gate is required, not a late runtime migration. Recheck before
    // enabling; existing heaps cannot safely have their capacity reinterpreted.
    if (!pools_not_constructed(base)) {
        hook_remove(target);
        error = "Morph allocators were constructed before hook installation";
        return false;
    }
    temporary_return_address.store(base + morph::temporary_return, std::memory_order_release);
    permanent_return_address.store(base + morph::permanent_return, std::memory_order_release);
    original_constructor.store(original, std::memory_order_release);
    const auto enabled = hook_enable(target);
    if (enabled != HookOk) {
        hook_remove(target);
        original_constructor.store(nullptr, std::memory_order_release);
        temporary_return_address.store(0, std::memory_order_release);
        permanent_return_address.store(0, std::memory_order_release);
        error = "Cannot enable morph allocator hook (Detours hook service status " + std::to_string(enabled) + ")";
        return false;
    }
    installed_base = base;
    return true;
}
} // namespace dingosdk

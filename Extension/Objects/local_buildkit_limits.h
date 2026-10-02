#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct BuildKitLimits {
    bool enabled{};
    std::uint32_t max_objects{};
    float radius{};
};

BuildKitLimits buildkit_limits_from(const profile::Snapshot& snapshot);

struct BuildKitLimitsRuntime {
    BuildKitLimits config;
    std::uintptr_t (*lookup)(std::uintptr_t, const void*){};
    void (*grabber_settings)(std::uintptr_t, const void*){};
    std::uintptr_t logged_settings{};
    std::uintptr_t rejected_settings{};
    std::atomic<bool> logged_cursor{};
};

BuildKitLimitsRuntime& buildkit_limits_runtime();

bool buildkit_limits_active();

bool buildkit_settings_identity(std::uintptr_t object);

template<class T> bool write_buildkit_setting(std::uintptr_t address, T value) {
    T current{};
    if (!read(address, current)) return false;
    if (current == value) return true;
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        !(memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) ||
        address + sizeof(T) > reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize) return false;
    SIZE_T written{};
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value), &written) &&
        written == sizeof(value) && read(address, current) && current == value;
}

bool apply_buildkit_limits(std::uintptr_t object);

void update_buildkit_limits();

void buildkit_grabber_settings_hook(std::uintptr_t system, const void* settings);
}

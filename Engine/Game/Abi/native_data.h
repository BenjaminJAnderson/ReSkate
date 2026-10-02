#pragma once
#include "Engine/Core/Platform/memory.h"
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace dingosdk::game {
// All pointers refer to the validated build. Model values are borrowed; callers
// hold ModelWriteLock across reads and synchronous publication.
struct NativeModelFunctions {
    using Lock = void (*)(std::uintptr_t);
    std::uintptr_t (*get_model)(std::uint32_t){};
    std::uint64_t (*create)(std::uintptr_t, std::uintptr_t, std::uint64_t, std::uint32_t, bool, std::uint8_t){};
    std::uintptr_t (*value)(std::uintptr_t, std::uint64_t, std::uint64_t, std::uint64_t){};
    std::uint64_t (*find)(std::uintptr_t, std::uintptr_t, std::uint64_t, std::uint32_t, std::uint8_t){};
    std::uint64_t (*field)(std::uintptr_t, std::uint64_t, unsigned, std::uint32_t, bool){};
    bool (*publish)(std::uintptr_t, std::uint64_t, std::uintptr_t, const void*){};
    Lock lock{}, unlock{};
    void* (*destroy)(std::uintptr_t, void*, std::uint64_t, std::uint8_t, bool, bool){};
};
struct NativeValueFunctions {
    void* (*assign)(void*, const char*, std::uint32_t){};
    void (*copy_delegate)(void*, const void*){};
    void (*destroy_delegate)(void*){};
    void (*invoke)(const void*, const void*){};
    void* (*null_reference)(void*, std::uintptr_t){};
};
struct NativeData {
    NativeModelFunctions models;
    NativeValueFunctions values;
    // Loaded named assets only; native lookup locks the asset registries.
    std::uintptr_t (*find_asset)(std::uint16_t, const char*){};
};
inline NativeData& native_data() { static NativeData value; return value; }
// Runs before publishing active providers or enabling their hooks.
bool initialize_native_data(std::uintptr_t base);

inline std::uint32_t native_name_hash(std::string_view name) noexcept {
    std::uint32_t hash = 5381;
    for (const unsigned char c : name) hash = hash * 33 ^ c;
    return hash;
}

class ModelWriteLock {
    NativeModelFunctions::Lock unlock_;
    std::uintptr_t address_;
public:
    explicit ModelWriteLock(std::uintptr_t manager, const NativeModelFunctions& functions = native_data().models)
        : unlock_(functions.unlock), address_(manager + 0xf20) {
        if (!manager || manager > UINTPTR_MAX - 0xf20 || !functions.lock || !unlock_)
            throw std::runtime_error("Native model lock is unavailable");
        functions.lock(address_);
    }
    ~ModelWriteLock() { unlock_(address_); }
    ModelWriteLock(const ModelWriteLock&) = delete;
    ModelWriteLock& operator=(const ModelWriteLock&) = delete;
};

// Holds one owned native callback. Raw value transfers are explicit at the
// asynchronous queue boundary; this guard must never be copied.
class NativeDelegateGuard {
    void (*destroy_)(void*) = native_data().values.destroy_delegate;
public:
    std::uintptr_t value{};
    NativeDelegateGuard() = default;
    ~NativeDelegateGuard() { if (value && destroy_) destroy_(&value); }
    NativeDelegateGuard(const NativeDelegateGuard&) = delete;
    NativeDelegateGuard& operator=(const NativeDelegateGuard&) = delete;
    std::uintptr_t release() noexcept { const auto result = value; value = 0; return result; }
};

// Frostbite's counted-array ABI, distinct from EASTL vectors and strings.
// This storage is borrowed only for calls that synchronously copy its values.
template<class T, std::size_t N> struct NativeArrayStorage {
    static_assert(N <= UINT32_MAX && alignof(T) <= 8);
    std::uint32_t capacity{static_cast<std::uint32_t>(N)}, count{};
    std::array<T, N> values{};
};
template<class T> bool native_array(const void* wrapper, std::uintptr_t& data,
                                    std::uint32_t& count, std::uint32_t maximum) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uintptr_t pointer{}; std::uint32_t size{};
    data = 0; count = 0;
    if (!memory::read(reinterpret_cast<std::uintptr_t>(wrapper), pointer) || pointer < 4 ||
        !memory::read(pointer - 4, size)) return false;
    size &= 0x7fffffff;
    if (size > maximum || pointer > UINTPTR_MAX - static_cast<std::uintptr_t>(size) * sizeof(T))
        return false;
    data = pointer; count = size;
    return true;
}
} // namespace dingosdk::game

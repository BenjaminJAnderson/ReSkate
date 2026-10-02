#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace dingosdk::game {
// Native eastl string: 16 bytes of inline data/heap metadata and allocator.
// These are borrowed only during Cache(), which copies both strings itself.
struct NativeStringView {
    std::array<std::byte, 16> storage{};
    std::uintptr_t allocator{};
    explicit NativeStringView(const std::string& value) {
        if (value.size() < 16) {
            std::memcpy(storage.data(), value.data(), value.size());
            storage[15] = static_cast<std::byte>(15 - value.size());
        } else {
            const auto pointer = value.c_str();
            const auto size = static_cast<std::uint32_t>(value.size());
            const auto capacity = size | 0x80000000;
            std::memcpy(storage.data(), &pointer, 8);
            std::memcpy(storage.data() + 8, &size, 4);
            std::memcpy(storage.data() + 12, &capacity, 4);
        }
    }
};
static_assert(sizeof(NativeStringView) == 0x18);
static_assert(offsetof(NativeStringView, allocator) == 0x10);
}

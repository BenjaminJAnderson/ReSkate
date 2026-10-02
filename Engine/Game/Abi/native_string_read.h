#pragma once
#include "Engine/Core/Platform/memory.h"
#include <array>
#include <cstring>
#include <string>

namespace dingosdk::game {
// Copy a bounded ASCII identifier from the supported build's EASTL string.
// Empty names are valid (the park notification uses them to clear a lot).
// Call only while the native callback owns the borrowed string's lifetime.
inline bool read_native_identifier(std::uintptr_t address, std::string& result) {
    result.clear();
    std::array<unsigned char, 24> data{}, checked{};
    if (!memory::read(address, data)) return false;
    std::array<char, 256> text{};
    std::uint32_t length{};
    if (data[15] & 0x80) {
        std::uintptr_t pointer{};
        std::uint32_t capacity{};
        std::memcpy(&pointer, data.data(), sizeof(pointer));
        std::memcpy(&length, data.data() + 8, sizeof(length));
        std::memcpy(&capacity, data.data() + 12, sizeof(capacity));
        if (length >= text.size() || length > (capacity & 0x7fffffff) ||
            !memory::read_bytes(pointer, text.data(), static_cast<std::size_t>(length) + 1)) return false;
    } else {
        if (data[15] > 15) return false;
        length = 15 - data[15];
        std::memcpy(text.data(), data.data(), length);
    }
    if (text[length] || !memory::read(address, checked) || checked != data) return false;
    for (std::size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(text[i]) < 32 || static_cast<unsigned char>(text[i]) > 126) return false;
    result.assign(text.data(), length);
    return true;
}
}

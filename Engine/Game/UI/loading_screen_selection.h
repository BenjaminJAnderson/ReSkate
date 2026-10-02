#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace dingosdk::loading_screen {
inline bool same_map(std::string_view a, std::string_view b) {
    const auto normalize = [](unsigned char c) {
        if (c == '\\') return static_cast<unsigned char>('/');
        return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
    };
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [&](unsigned char x, unsigned char y) { return normalize(x) == normalize(y); });
}
// Only use overrides authored for every platform/session type with no custom
// inclusion criteria. More specialized rules remain under native control.
inline bool unconditional(const std::array<std::uint8_t, 15>& conditions, unsigned custom_count) {
    return custom_count == 0 && std::all_of(conditions.begin(), conditions.end(), [](auto value) { return value == 1; });
}
struct Pending {
    std::uintptr_t screen{};
    std::uint64_t expires{};
    std::string destination;

    void clear() { screen = 0; expires = 0; destination.clear(); }
    std::string take(std::uintptr_t owner, bool boot, std::uint64_t now) {
        if (now >= expires) { clear(); return {}; }
        if (boot || !screen || owner != screen) return {};
        auto result = std::move(destination);
        clear();
        return result;
    }
};
}

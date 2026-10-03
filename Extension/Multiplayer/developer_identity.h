#pragma once
#include <array>
#include <cstdint>

namespace dingosdk::multiplayer {
// Shared by developer tags and cosmetics. Steam authenticates these identities.
inline constexpr std::array<std::uint64_t, 2> reskate_developers{
    76561198084159190ULL, // zee_x64
    76561198255588397ULL, // reglitched
};
inline bool reskate_developer(std::uint64_t id) noexcept {
    for (const auto developer : reskate_developers)
        if (developer == id) return true;
    return false;
}
} // namespace dingosdk::multiplayer

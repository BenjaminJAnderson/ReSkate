#pragma once
#include "Engine/Game/Multiplayer/session_tools.h"
#include <array>
#include <string_view>

namespace dingosdk {
// Engine settings that change how the game plays: the simulation clock (slow motion), physics,
// skating, activities and throwdowns. While a multiplayer session runs, a player cannot change
// them (the console, the debug settings editor) and their own changes are put back, so nobody
// skates a throwdown in slow motion or with debug physics. ReSkate's own internal settings
// changes are not affected.
namespace settings_lock_detail {
// Groups as the console names them ("SimulationTime.TimeScale"); the debug settings catalog
// names the same groups with "Settings" on the end ("SimulationTimeSettings").
inline constexpr std::array<std::string_view, 11> groups{
    "SimulationTime", "Physics", "PhysicsQuality", "FBCorePhysics", "DingoGameplay", "DingoActivity",
    "DingoThrowdowns", "DingoThrowdown", "DelMarGame", "AutoPlayers", "SkaterRecord",
};
inline bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i];
        const auto y = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
        if (x != y) return false;
    }
    return true;
}
} // namespace settings_lock_detail

// `name` is "Group.Field" (or "Group.Struct.Field").
inline bool locked_in_multiplayer(std::string_view name) noexcept {
    using namespace settings_lock_detail;
    auto group = name.substr(0, name.find('.'));
    if (group.size() == name.size()) return false;
    constexpr std::string_view suffix = "Settings";
    if (group.size() > suffix.size() && same(group.substr(group.size() - suffix.size()), suffix))
        group.remove_suffix(suffix.size());
    // The catalog's AutoPlayerSettings is the console's AutoPlayers.
    if (same(group, "AutoPlayer")) return true;
    for (const auto group_name : groups)
        if (same(group, group_name)) return true;
    return false;
}
inline bool multiplayer_settings_locked() noexcept { return multiplayer_session_active(); }
} // namespace dingosdk

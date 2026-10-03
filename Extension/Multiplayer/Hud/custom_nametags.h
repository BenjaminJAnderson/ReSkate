#pragma once
#include "Extension/UI/Overlay/overlay.h"
#include "Extension/Multiplayer/developer_identity.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ReSkate's own nametags (drawn by the overlay, Extension/UI/Overlay/nametag_overlay.cpp)
// in place of the game's nametag and compass arrows; the pause map's player icons stay.
namespace dingosdk::multiplayer {
struct NametagPlayer {
    std::array<float, 3> head{}; // world position above the skater
    std::string name;
    std::uint32_t color{0xffffffffU}; // IM_COL32 layout
    std::string tag;                  // role badge before the name, as in chat ("" for none)
    bool talking{};
};
// Role colours; friends are the viewer's own Steam friends.
inline constexpr std::uint32_t nametag_white = 0xffffffffU, nametag_developer = 0xffff78b4U, // purple
                               nametag_admin = 0xff3dc8ffU,                              // gold
                               nametag_host = 0xffffc85eU,                               // blue
                               nametag_friend = 0xff8ae07bU;                             // green
inline constexpr std::uint32_t nametag_developer_start = 0xffff206eU; // #6E20FF, IM_COL32 layout

// Client thread, once per rendered frame: everyone to label and the local skater's position
// (for distances). Also reads whether the game is hiding its own nametags right now.
void publish_custom_nametags(std::uintptr_t base, std::vector<NametagPlayer> players,
                             std::optional<std::array<float, 3>> local) noexcept;
void set_custom_nametags_enabled(bool enabled) noexcept;
bool custom_nametags_enabled() noexcept;
// The overlay's feed (any thread): nothing while off, stale, or while the game hides nametags.
overlay::Nametags custom_nametags();
} // namespace dingosdk::multiplayer

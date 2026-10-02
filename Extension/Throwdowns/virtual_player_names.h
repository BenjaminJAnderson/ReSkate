#pragma once
// Backend-style names (EIDs) for virtual participants of a coop challenge
// (analysis/coop-challenges-mp.md). Offline every native player's EID is empty; a virtual id
// has no native player, and GetPlayerName answered "" for it too. Challenge begin/end requests
// list one name per participant, so two virtual players (or one and the local player) collided.
// Virtual ids taking part in a coop challenge answer GetPlayerName with one of these instead;
// nothing is ever sent to them (native_throwdowns.cpp drops those sends).
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace dingosdk::multiplayer {
inline constexpr std::string_view virtual_eid_prefix = "rsk:v";
inline std::string virtual_eid(std::uint32_t player_id) { return std::format("rsk:v{:x}", player_id); }
inline bool is_virtual_eid(std::string_view name) noexcept { return name.starts_with(virtual_eid_prefix); }
} // namespace dingosdk::multiplayer

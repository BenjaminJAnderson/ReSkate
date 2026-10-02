#pragma once
#include "Extension/Multiplayer/Net/protocol.h"
#include <string>

namespace dingosdk::multiplayer {
// Publishes owned values. Native UI hooks consume them on the game's UI update path.
void update_player_ui(std::uintptr_t base, const Pose *pose, std::string name) noexcept;
// Installs the player map hooks now (client thread) instead of on the first remote pose.
void prepare_player_ui(std::uintptr_t base) noexcept;
std::string player_ui_status();
std::uint64_t player_map_updates() noexcept;
} // namespace dingosdk::multiplayer

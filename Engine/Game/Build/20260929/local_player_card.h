#pragma once
#include <cstdint>

namespace dingosdk::game::build::v20260929::local_player_card {
// Appearance (RIP Card recipe) manager, read through engine::appearance_manager.
inline constexpr std::uintptr_t appearance_manager_vtable = 0x6084c28;
// TypeInfo of the UIPlayerInfo presence connection-state value (field 7).
inline constexpr std::uintptr_t connection_state_type = 0x7258ef8;
// TypeInfo of the UIPlayerInfo player network (field 10, record +0xf4): the platform
// logo beside the name follows it. Values as EadpPlayerNetwork: 0 unknown (shown as EA),
// 1 EA, 2 PSN, 3 Xbox, 4 Steam, 5 Nintendo, 6 Apple, 7 Google.
inline constexpr std::uintptr_t player_network_type = 0x723b8f8;
inline constexpr std::uint32_t steam_network = 4;
}

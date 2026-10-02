#pragma once
#include <cstdint>

namespace dingosdk::game::build::v20260929::world_model {
// Pointer to the level registry (level metadata array at +0x20..+0x28).
inline constexpr std::uintptr_t level_registry = 0x777c9a8;
// Pointer to the server sublevel manager.
inline constexpr std::uintptr_t sublevel_manager = 0x75c1a10;
}

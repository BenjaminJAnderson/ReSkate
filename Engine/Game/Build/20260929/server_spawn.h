#pragma once
#include <cstdint>

// Server-side player spawners.
namespace dingosdk::game::build::v20260929::server_spawn {
inline constexpr std::uintptr_t spawner_vtable = 0x63ba798;
// Spawner list heads, 16 bytes each, indexed by partition + 13 (or + 8 for flagged spawners).
inline constexpr std::uintptr_t spawner_registry = 0x75803b0;
}

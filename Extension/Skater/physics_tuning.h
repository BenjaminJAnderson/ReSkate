#pragma once
// Gameplay/SkatePhysicsTuning is one asset that every skater's physics in the process reads,
// so a player who edits theirs (truck positions, jump heights, ...) skates differently, and
// everyone sees it. In a session a host's differences from the game's own tuning go to its
// guests, who skate with the game's tuning plus those; a dedicated server's guests skate
// with the game's. Client thread only.
#include "physics_tuning_model.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace dingosdk::physics_tuning {
// Reads the game's own tuning from the game data, once, in the background.
void prepare() noexcept;
// Host: how the running game's tuning differs from the game's own, in at most `limit` bytes;
// nothing while either is unknown (still reading, or this build's tuning does not match).
std::optional<Encoded> local_differences(std::uintptr_t base, std::size_t limit);
// Guest, each client tick in a world: keeps the game's tuning plus `differences` in the
// running game (checked about once a second, at once when they change), and refreshes the
// local skater's cached copy of the values after a change. Masses and collision sizes a
// skater's rig was built with change on its next respawn.
void enforce(std::uintptr_t base, std::uintptr_t entity, std::span<const std::uint8_t> differences) noexcept;
// Puts the player's own values back (the ones from before enforce). Cheap when nothing is enforced.
void release(std::uintptr_t base) noexcept;
// What enforcement is doing, for the session UI and the log; empty when off.
std::string status();
} // namespace dingosdk::physics_tuning

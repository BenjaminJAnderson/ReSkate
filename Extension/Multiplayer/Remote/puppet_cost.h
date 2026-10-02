#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Internal side of the far-player savings declared in native_skater.h, used by
// native_skater.cpp and native_skater_spawn.cpp.
namespace dingosdk::multiplayer::puppet_cost {
// Component update hooks prepared (not yet enabled) by prepare_hooks.
struct PreparedHooks {
    std::array<void *, 4> targets{};
    std::size_t count{};
};
// Client thread, from native_skater_detail::install before its hook transaction. Prepares the
// navigator, stimulus, pedestrian collider and ECS attach hooks whose code matches the
// supported build; a mismatch only leaves that saving unavailable. The caller queues and
// applies the returned targets in its own transaction, then reports the result.
PreparedHooks prepare_hooks(std::uintptr_t base) noexcept;
void hooks_applied(const PreparedHooks &hooks, bool enabled) noexcept;
// The current slot's skater (and with it the skateboard) or only its skateboard was removed
// or destroyed: forget what was applied to it. Any thread; touches only this slot's state.
void forget_skater() noexcept;
void forget_board() noexcept;
} // namespace dingosdk::multiplayer::puppet_cost

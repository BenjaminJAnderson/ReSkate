#pragma once
#include <cstdint>

namespace dingosdk::profile_runtime {
// Retail binds the authored graphs' error logger to a shared no-op. Rebind that
// one native so the game's own script errors reach the runtime log.
void install_script_error_log(std::uintptr_t base) noexcept;
// Rebinds the SetFields native's table slot so board wear can hold the authored
// per-tick reset of the wear block. Every other SetFields call is forwarded.
void install_board_wear_hold(std::uintptr_t base) noexcept;
// Clears accumulated board wear by letting the authored graph's own per-tick
// reset through once, instead of writing the component from here. Takes effect
// on the next tick the graph runs, so it is a no-op while board wear is off.
void reset_board_wear() noexcept;
}

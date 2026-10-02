#pragma once
#include <cstdint>
#include <string>

// Skips the UI's per-frame pointer hit-test while the game has no cursor (Engine/Game/Build/
// <build>/ui_pointer.h). Every client frame the game hit-tests the mouse position down the widget
// tree of every input-taking UI view, moved or not: 0.2-0.6 ms of a ~4 ms client frame in free
// roam (profiled 2026-10-01). With the cursor mode off nothing can be hovered or clicked, and the
// hit list the test fills is left empty (the game clears it first) -- what "no cursor" means.
namespace dingosdk::ui_pointer {
// Installs the hooks (once); false if this build's functions differ.
bool install(std::uintptr_t base) noexcept;
void set_skip(bool enabled) noexcept;
bool skip() noexcept;
// For `perf uipointer`: whether the cursor object is known and on, and what was skipped.
std::string status();
}

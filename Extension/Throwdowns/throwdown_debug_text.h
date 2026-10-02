#pragma once
#include "Extension/UI/Overlay/overlay.h"
#include <cstdint>

// ClientSkateThrowdown.OnDebugPrint runs every few frames of a S.K.A.T.E. throwdown (its re-post is
// slowed from every frame to every 3; skate_debug_text_ran) and is its
// only HUD — "Set a trick!", the trick to copy, everyone's S.K.A.T.E letters — all drawn
// through debug text natives that retail reduced to stubs. That one graph's calls are
// pointed at working ones and its text is drawn by the overlay.
namespace dingosdk::multiplayer {
// Called by the expression pump after that graph has run (client realm): patches the
// graph once per loaded copy and publishes the text it drew this frame.
void skate_debug_text_ran(std::uintptr_t base, std::uintptr_t vm) noexcept;
// Overlay feed: the latest frame's text, empty once the graph stops running (and while
// ReSkate's own HUD shows it instead).
overlay::GameText skate_debug_text();
// Overlay feed: the same frame read as messages and players for ReSkate's S.K.A.T.E. HUD,
// empty while the game's debug text is shown instead.
overlay::SkateHud skate_hud();
// ReSkate's HUD (the default) or the game's own debug text.
void set_skate_hud_reskate(bool reskate) noexcept;
bool skate_hud_reskate() noexcept;
// Everyone's S.K.A.T.E. letters added up, as the running throwdown's HUD last listed them
// (its {Uid, Score} list), or -1 while none is showing. Any thread.
int skate_letters_total() noexcept;
} // namespace dingosdk::multiplayer

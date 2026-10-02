#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk::multiplayer {
// Research aid: writes the live native UI model tree to `ui-dump-<what>-<tick>.txt`
// beside the runtime log. Read-only with respect to the game; the walk runs on
// the client thread under the native model write lock.
//
// what: "all" (every root), "roots" (one line per root), "0x<schema>" (roots of
// one schema), or free text (roots whose tree mentions it). A delay lets the
// caller arm the dump, open the native screen they want, and have it fire there.
std::string request_ui_dump(std::string_view what, unsigned delay_seconds = 0);
void run_pending_ui_dump(std::uintptr_t base) noexcept;
} // namespace dingosdk::multiplayer

#pragma once
#include <cstdint>

namespace dingosdk::preset_lookup_guard {
// A preset whose bundle cannot be found (a cosmetic mod's registration missing
// from the character bundle-reference table) is treated as not found instead
// of crashing the game in the named lookup.
bool start(std::uintptr_t base) noexcept;
}

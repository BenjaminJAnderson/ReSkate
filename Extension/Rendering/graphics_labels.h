#pragma once

#include <cstdint>

namespace dingosdk {
// Fills empty Tier Manager display strings after the native list is rebuilt.
// Requires the exact-build authored-offline route; never changes option values.
bool initialize_graphics_labels(std::uintptr_t base, bool authored_offline);
}

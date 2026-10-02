#pragma once
#include "Extension/Multiplayer/Net/protocol.h"
#include <string>

namespace dingosdk::multiplayer {
void update_native_indicators(std::uintptr_t base, const Pose *pose, const std::string &name) noexcept;
// Local display preference. Off unregisters the peer name labels and keeps them
// unregistered; the off-screen compass tags are unaffected.
void set_native_nametags_enabled(bool enabled) noexcept;
// The compass tag (the arrow toward an off-screen player); the pause map's icon is separate.
void set_native_compass_enabled(bool enabled) noexcept;
// Installs the indicator hooks now (client thread) instead of on the first remote pose.
void prepare_native_indicators(std::uintptr_t base) noexcept;
std::string native_indicators_status();
} // namespace dingosdk::multiplayer

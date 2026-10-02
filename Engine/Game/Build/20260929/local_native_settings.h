#pragma once
#include <cstdint>

namespace dingosdk::game::build::v20260929::local_native_settings {
// Pointer to DingoProfileSettingsManager; its first member is a vector of the
// three option groups (LocalPerDevice, then the two cloud groups).
inline constexpr std::uintptr_t profile_settings_manager = 0x71eff00;
}

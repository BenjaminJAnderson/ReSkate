#pragma once
#include <cstdint>

namespace dingosdk::game::build::v20260929::population {
// Traffic/pedestrian population manager vtable.
inline constexpr std::uintptr_t manager_vtable = 0x64391a0;
// Population manager pointers, indexed by realm (0 or 1).
inline constexpr std::uintptr_t managers = 0x76103f0;
// TrafficSystemConfig TypeInfo.
inline constexpr std::uintptr_t traffic_system_config_type = 0x7618978;
}

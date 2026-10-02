#pragma once
#include <cstdint>

// The select hook itself is level_loading::loading_screen_select_contract.
namespace dingosdk::game::build::v20260929::loading_screen {
// RimeLoadScreen vtable.
inline constexpr std::uintptr_t screen_vtable = 0x65a75f0;
// RimeLoadScreenConfig TypeInfo.
inline constexpr std::uintptr_t config_type = 0x7767300;
// RimeLoadScreenInfo TypeInfo.
inline constexpr std::uintptr_t info_type = 0x77671e8;
}

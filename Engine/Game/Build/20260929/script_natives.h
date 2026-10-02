#pragma once
#include <cstdint>

// Authored-graph natives ReSkate rebinds: the script error logger and SetFields.
namespace dingosdk::game::build::v20260929::script_natives {
// Script error logger native record (hash 0xa5910db3) and the shared no-op
// stub retail binds into its slots.
inline constexpr std::uintptr_t script_error_record = 0x8136230;
inline constexpr std::uintptr_t script_error_stub = 0x58ada50;
// SetFields native table row (hash 0xbdc6524d) and its function.
inline constexpr std::uintptr_t set_fields_entry = 0x71a1640;
inline constexpr std::uintptr_t set_fields = 0x4681170;
// The retail ownable-context resolve (native 0xdbfa3b23) is the stub
// FUN_1407bcbf0 that always reports failure; see the board-wear notes.
}

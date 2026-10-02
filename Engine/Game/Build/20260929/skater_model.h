#pragma once
#include <array>
#include <cstdint>

// Skater component sampling (game/skater/skater_model.cpp).
namespace dingosdk::game::build::v20260929::skater_model {
// Globals whose presence the skater component sample reports.
inline constexpr std::array<std::uintptr_t, 3> component_globals{0x727fc48, 0x727fc50, 0x7785dc0};
}

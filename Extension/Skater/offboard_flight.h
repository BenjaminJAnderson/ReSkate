#pragma once
#include <array>
#include <cstdint>

namespace dingosdk {
bool offboard_flight_compatible(std::uintptr_t base) noexcept;
// Only call during a freshly validated local noclip motion update, with live
// input. Synchronize the active falling/ground state, preserving its fourth lane.
bool sync_offboard_flight_velocity(std::uintptr_t base, std::uintptr_t core,
    std::uintptr_t context, std::uintptr_t rig, const std::array<float, 3>& velocity) noexcept;
}

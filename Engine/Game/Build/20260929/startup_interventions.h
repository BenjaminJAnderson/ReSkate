#pragma once
#include <array>
#include <cstdint>

namespace dingosdk::game::build::v20260929::startup_interventions {
// Interpreter frame clear (REP STOSB) that overlaps the protected frame state.
inline constexpr std::uintptr_t frame = 0x0880d7d5;
inline constexpr std::array<std::uint8_t, 2> frame_instruction{0xf3, 0xaa};
// Handler and data expected in R9/R14 at the frame clear.
inline constexpr std::uintptr_t frame_handler = 0x0880d710;
inline constexpr std::uintptr_t frame_data = 0x086772fc;
// Exit thunk (JMP [rip+...]) and the return address it is expected to be called from.
inline constexpr std::uintptr_t exit_thunk = 0x08a06ff3;
inline constexpr std::uintptr_t exit_return = 0x087755f2;
inline constexpr std::array<std::uint8_t, 6> exit_instruction{
    0xff, 0x25, 0x7f, 0x43, 0x5e, 0xfd};
}

#pragma once
#include "Engine/Game/Build/image_identity.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/startup_interventions.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace dingosdk {

inline constexpr std::uint32_t startup_expected_image_size = supported_build::game_image_size;
inline constexpr std::uintptr_t startup_frame_rva = addr::startup_interventions::frame;
inline constexpr std::uintptr_t startup_frame_data_rva = addr::startup_interventions::frame_data;
inline constexpr std::uintptr_t startup_frame_cursor_rva = addr::startup_interventions::frame_cursor;
inline constexpr std::uintptr_t startup_frame_size = addr::startup_interventions::frame_size;
inline constexpr std::uintptr_t startup_frame_cursor_offset = addr::startup_interventions::frame_cursor_offset;
inline constexpr std::uintptr_t startup_exit_rva = addr::startup_interventions::exit_thunk;
inline constexpr std::uintptr_t startup_exit_return_rva = addr::startup_interventions::exit_return;
inline constexpr auto startup_frame_instruction = addr::startup_interventions::frame_instruction;
inline constexpr auto startup_frame_fingerprint = addr::startup_interventions::frame_fingerprint;
inline constexpr auto startup_exit_instruction = addr::startup_interventions::exit_instruction;

enum class StartupInterventionPhase : std::uint32_t {
    uninitialized, armed, handling_frame, frame_captured, handling_exit, complete, failed,
};

struct StartupInterventionRegisters {
    std::uint64_t rip{}, rsp{}, rax{}, rbx{}, rcx{}, rdx{}, rbp{}, rsi{}, rdi{};
    std::uint64_t r8{}, r9{}, r10{}, r11{}, r14{}, r15{};
    std::uint32_t eflags{};
};

struct StartupFrameMetadata {
    std::uint64_t cursor{};
};

bool startup_frame_clear_matches(std::uintptr_t image_base,
    const StartupInterventionRegisters& registers,
    const std::array<std::uint8_t, 3>& instruction,
    const StartupFrameMetadata& metadata) noexcept;

// One-shot capture/exit sequence. The runtime serializes transitions and owns
// the frame bytes; ordinary VM byte stores do not advance this state machine.
class StartupInterventionMachine {
public:
    bool arm() noexcept;
    bool capture_frame(std::uintptr_t image_base, const StartupInterventionRegisters& registers,
                       const std::array<std::uint8_t, 3>& instruction,
                       const StartupFrameMetadata& metadata, std::uint32_t thread_id) noexcept;
    bool return_from_exit(std::uintptr_t image_base, StartupInterventionRegisters& registers,
                          const std::array<std::uint8_t, 6>& instruction,
                          std::uint64_t return_address, std::uint32_t thread_id) noexcept;
    void fail() noexcept;
    StartupInterventionPhase phase() const noexcept;

private:
    std::atomic<StartupInterventionPhase> phase_{StartupInterventionPhase::uninitialized};
    std::uint64_t frame_base_{};
    std::uint32_t frame_thread_{};
};

bool startup_intervention_fingerprints_match(std::uintptr_t image_base, std::uint32_t image_size,
    const std::array<std::uint8_t, 8>& frame, const std::array<std::uint8_t, 6>& exit) noexcept;

// Installs both verified sites while the caller holds existing game threads
// suspended. Capture retires the byte-store breakpoint; exit restores the
// captured native frame and retires the exit breakpoint before returning FALSE.
bool start_startup_interventions(std::uintptr_t image_base, std::string& error) noexcept;

} // namespace dingosdk

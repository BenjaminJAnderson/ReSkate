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
inline constexpr std::uintptr_t startup_frame_handler_rva = addr::startup_interventions::frame_handler;
inline constexpr std::uintptr_t startup_frame_data_rva = addr::startup_interventions::frame_data;
inline constexpr std::uintptr_t startup_frame_state_offset = 0x70;
inline constexpr std::uintptr_t startup_exit_rva = addr::startup_interventions::exit_thunk;
inline constexpr std::uintptr_t startup_exit_return_rva = addr::startup_interventions::exit_return;
inline constexpr auto startup_frame_instruction = addr::startup_interventions::frame_instruction;
inline constexpr auto startup_exit_instruction = addr::startup_interventions::exit_instruction;

enum class StartupInterventionPhase : std::uint32_t {
    uninitialized,
    armed,
    handling_frame,
    frame_preserved,
    handling_exit,
    complete,
    failed,
};

struct StartupInterventionRegisters {
    std::uint64_t rip{};
    std::uint64_t rsp{};
    std::uint64_t rax{};
    std::uint64_t rbx{};
    std::uint64_t rcx{};
    std::uint64_t rdx{};
    std::uint64_t rbp{};
    std::uint64_t rsi{};
    std::uint64_t rdi{};
    std::uint64_t r8{};
    std::uint64_t r9{};
    std::uint64_t r14{};
    std::uint32_t eflags{};
};

// Pure guarded state machine shared by the runtime exception handler and its
// focused tests. A matching transition is accepted once; a guard mismatch at
// an armed site permanently fails the sequence.
class StartupInterventionMachine {
public:
    bool arm() noexcept;
    bool preserve_frame(std::uintptr_t image_base, StartupInterventionRegisters& registers,
                        const std::array<std::uint8_t, 2>& instruction,
                        std::uint64_t protected_state) noexcept;
    bool return_from_exit(std::uintptr_t image_base, StartupInterventionRegisters& registers,
                          const std::array<std::uint8_t, 6>& instruction,
                          std::uint64_t return_address) noexcept;
    void fail() noexcept;
    StartupInterventionPhase phase() const noexcept;

private:
    std::atomic<StartupInterventionPhase> phase_{StartupInterventionPhase::uninitialized};
};

// The frame site is checked separately: it is optional (see start_startup_interventions).
bool startup_intervention_fingerprints_match(
    std::uintptr_t image_base, std::uint32_t image_size,
    const std::array<std::uint8_t, 6>& exit_instruction) noexcept;

// Installs two process-wide, one-shot breakpoint interventions for the exact
// inspected Skate image. The caller keeps all existing game threads suspended
// during installation; future worker threads are covered by the code sites.
bool start_startup_interventions(std::uintptr_t image_base, std::string& error) noexcept;

} // namespace dingosdk

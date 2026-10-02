#include "startup_interventions.h"
#include "Engine/Core/Log/logging.h"

#include <Windows.h>

#include <array>
#include <limits>
#include <stdexcept>

namespace dingosdk {
namespace {

// Re-observed on the supported image: the interpreter frame is based on RBP,
// and its 0x630-byte clear overlaps that frame. These are not Season 4 offsets.
constexpr std::uint64_t protected_state = 0x343cfc26;
constexpr std::uint32_t direction_flag = 0x400;
constexpr std::uint8_t breakpoint_opcode = 0xcc;

StartupInterventionMachine machine;
std::atomic<std::uintptr_t> installed_base{};
// The frame site sits in protector code that is regenerated for every game
// build; when the table has no verified site for this build only the exit
// intervention is armed.
std::atomic_bool frame_armed{};
PVOID handler_handle{};

template<class T>
bool read_local(std::uintptr_t address, T& output) noexcept {
    SIZE_T count{};
    return address && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(address), &output, sizeof(output), &count) &&
        count == sizeof(output);
}

bool executable_image_range(std::uintptr_t image_base, std::uintptr_t address,
                            std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    if (!image_base || !address || !size ||
        !VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory))) return false;
    const auto executable = [](DWORD protection) {
        if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
        switch (protection & 0xff) {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    };
    const auto end = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return memory.State == MEM_COMMIT && memory.Type == MEM_IMAGE &&
        reinterpret_cast<std::uintptr_t>(memory.AllocationBase) == image_base &&
        executable(memory.Protect) && address <= end && size <= end - address;
}

bool write_opcode(std::uintptr_t address, std::uint8_t opcode) noexcept {
    DWORD old_protection{};
    if (!VirtualProtect(reinterpret_cast<void*>(address), 1, PAGE_EXECUTE_READWRITE,
            &old_protection)) return false;
    SIZE_T count{};
    const auto wrote = WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
        &opcode, sizeof(opcode), &count) && count == sizeof(opcode);
    const auto flushed = wrote && FlushInstructionCache(
        GetCurrentProcess(), reinterpret_cast<void*>(address), 1) != FALSE;
    DWORD ignored{};
    const auto protected_again = VirtualProtect(
        reinterpret_cast<void*>(address), 1, old_protection, &ignored) != FALSE;
    return wrote && flushed && protected_again;
}

StartupInterventionRegisters capture_registers(const CONTEXT& context,
                                               std::uintptr_t exception_address) noexcept {
    return {exception_address, context.Rsp, context.Rax, context.Rbx, context.Rcx,
            context.Rdx, context.Rbp, context.Rsi, context.Rdi, context.R8,
            context.R9, context.R14, context.EFlags};
}

void apply_registers(CONTEXT& context, const StartupInterventionRegisters& registers) noexcept {
    context.Rip = registers.rip;
    context.Rsp = registers.rsp;
    context.Rax = registers.rax;
    context.Rcx = registers.rcx;
    context.Rdi = registers.rdi;
}

LONG CALLBACK intervention_handler(EXCEPTION_POINTERS* pointers) {
    if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord ||
        pointers->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto image_base = installed_base.load(std::memory_order_acquire);
    if (!image_base) return EXCEPTION_CONTINUE_SEARCH;
    const auto address = reinterpret_cast<std::uintptr_t>(
        pointers->ExceptionRecord->ExceptionAddress);
    auto registers = capture_registers(*pointers->ContextRecord, address);

    if (address == image_base + startup_frame_rva) {
        std::array<std::uint8_t, 2> patched{};
        std::uint64_t state{};
        if (!read_local(address, patched) || patched[0] != breakpoint_opcode ||
            registers.rbp > std::numeric_limits<std::uintptr_t>::max() - startup_frame_state_offset ||
            !read_local(registers.rbp + startup_frame_state_offset, state)) {
            machine.fail();
            return EXCEPTION_CONTINUE_SEARCH;
        }
        const std::array<std::uint8_t, 2> original{startup_frame_instruction[0], patched[1]};
        if (!machine.preserve_frame(image_base, registers, original, state) ||
            !write_opcode(address, startup_frame_instruction[0])) {
            machine.fail();
            return EXCEPTION_CONTINUE_SEARCH;
        }
        apply_registers(*pointers->ContextRecord, registers);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (address == image_base + startup_exit_rva) {
        std::array<std::uint8_t, 6> patched{};
        std::uint64_t return_address{};
        if (!read_local(address, patched) || patched[0] != breakpoint_opcode ||
            registers.rsp > std::numeric_limits<std::uintptr_t>::max() - sizeof(return_address) ||
            !read_local(registers.rsp, return_address)) {
            machine.fail();
            return EXCEPTION_CONTINUE_SEARCH;
        }
        patched[0] = startup_exit_instruction[0];
        logging::log(logging::Level::info, logging::Channel::runtime,
            "Startup exit intervention hit: return {:#x}, rcx {:#x}, rdx {:#x}, frame site {}",
            return_address >= image_base ? return_address - image_base : return_address, registers.rcx,
            registers.rdx, frame_armed.load() ? "armed" : "not armed");
        if (!machine.return_from_exit(image_base, registers, patched, return_address) ||
            !write_opcode(address, startup_exit_instruction[0])) {
            machine.fail();
            return EXCEPTION_CONTINUE_SEARCH;
        }
        apply_registers(*pointers->ContextRecord, registers);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

bool StartupInterventionMachine::arm() noexcept {
    auto expected = StartupInterventionPhase::uninitialized;
    return phase_.compare_exchange_strong(expected, StartupInterventionPhase::armed,
        std::memory_order_acq_rel);
}

bool StartupInterventionMachine::preserve_frame(
    std::uintptr_t image_base, StartupInterventionRegisters& registers,
    const std::array<std::uint8_t, 2>& instruction,
    std::uint64_t state) noexcept {
    auto expected = StartupInterventionPhase::armed;
    if (!phase_.compare_exchange_strong(expected, StartupInterventionPhase::handling_frame,
            std::memory_order_acq_rel)) return false;
    const auto matches = image_base == supported_build::preferred_image_base &&
        registers.rip == image_base + startup_frame_rva && registers.rsp == registers.rbp &&
        registers.rcx == 0x630 &&
        registers.rdi <= std::numeric_limits<std::uint64_t>::max() - 0x210 &&
        registers.rdi + 0x210 == registers.rsp && registers.r8 == registers.rdi &&
        registers.r9 == image_base + startup_frame_handler_rva &&
        registers.r14 == image_base + startup_frame_data_rva &&
        (registers.rax & 0xff) == 0 && !(registers.eflags & direction_flag) &&
        instruction == startup_frame_instruction && state == protected_state &&
        registers.rdi <= std::numeric_limits<std::uint64_t>::max() - registers.rcx;
    if (!matches) {
        phase_.store(StartupInterventionPhase::failed, std::memory_order_release);
        return false;
    }
    registers.rdi += registers.rcx;
    registers.rcx = 0;
    registers.rip += instruction.size();
    phase_.store(StartupInterventionPhase::frame_preserved, std::memory_order_release);
    return true;
}

bool StartupInterventionMachine::return_from_exit(
    std::uintptr_t image_base, StartupInterventionRegisters& registers,
    const std::array<std::uint8_t, 6>& instruction,
    std::uint64_t return_address) noexcept {
    const bool frame = frame_armed.load(std::memory_order_acquire);
    auto expected = frame ? StartupInterventionPhase::frame_preserved : StartupInterventionPhase::armed;
    if (!phase_.compare_exchange_strong(expected, StartupInterventionPhase::handling_exit,
            std::memory_order_acq_rel)) return false;
    // With the frame site verified the exact protector return address is pinned
    // too; without it, TerminateProcess(current process, 0x32) from this build's
    // protector thunk back into the image is the guard.
    const bool return_matches = return_address == image_base + startup_exit_return_rva ||
        (!frame && return_address >= image_base && return_address - image_base < startup_expected_image_size);
    const auto matches = image_base == supported_build::preferred_image_base &&
        registers.rip == image_base + startup_exit_rva &&
        registers.rcx == std::numeric_limits<std::uint64_t>::max() &&
        registers.rdx == 0x32 && return_matches &&
        instruction == startup_exit_instruction &&
        registers.rsp <= std::numeric_limits<std::uint64_t>::max() - sizeof(return_address);
    if (!matches) {
        phase_.store(StartupInterventionPhase::failed, std::memory_order_release);
        return false;
    }
    registers.rax = FALSE;
    registers.rip = return_address;
    registers.rsp += sizeof(return_address);
    phase_.store(StartupInterventionPhase::complete, std::memory_order_release);
    return true;
}

void StartupInterventionMachine::fail() noexcept {
    phase_.store(StartupInterventionPhase::failed, std::memory_order_release);
}

StartupInterventionPhase StartupInterventionMachine::phase() const noexcept {
    return phase_.load(std::memory_order_acquire);
}

bool startup_intervention_fingerprints_match(
    std::uintptr_t image_base, std::uint32_t image_size,
    const std::array<std::uint8_t, 6>& exit_instruction) noexcept {
    return image_base == supported_build::preferred_image_base &&
        image_size == startup_expected_image_size &&
        exit_instruction == startup_exit_instruction;
}

bool start_startup_interventions(std::uintptr_t image_base, std::string& error) noexcept {
    error.clear();
    try {
        if (handler_handle || installed_base.load())
            throw std::runtime_error("Startup interventions are already installed");
        IMAGE_DOS_HEADER dos{};
        if (!read_local(image_base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000)
            throw std::runtime_error("Startup intervention image has an invalid DOS header");
        IMAGE_NT_HEADERS64 nt{};
        if (!read_local(image_base + static_cast<std::uintptr_t>(dos.e_lfanew), nt) ||
            nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            throw std::runtime_error("Startup intervention image has an invalid PE header");
        const auto frame_address = image_base + startup_frame_rva;
        const auto exit_address = image_base + startup_exit_rva;
        if (exit_address < image_base ||
            !executable_image_range(image_base, exit_address, startup_exit_instruction.size()))
            throw std::runtime_error("Startup intervention sites are outside executable image pages");
        std::array<std::uint8_t, 2> frame{};
        std::array<std::uint8_t, 6> exit{};
        if (!read_local(exit_address, exit) ||
            !startup_intervention_fingerprints_match(image_base,
                nt.OptionalHeader.SizeOfImage, exit))
            throw std::runtime_error("Startup intervention image fingerprint does not match");
        const bool arm_frame = frame_address >= image_base &&
            executable_image_range(image_base, frame_address, startup_frame_instruction.size()) &&
            read_local(frame_address, frame) && frame == startup_frame_instruction;
        frame_armed.store(arm_frame, std::memory_order_release);

        installed_base.store(image_base, std::memory_order_release);
        handler_handle = AddVectoredExceptionHandler(1, intervention_handler);
        if (!handler_handle) {
            installed_base.store(0, std::memory_order_release);
            throw std::runtime_error("Cannot install startup intervention exception handler");
        }
        bool frame_patched = false;
        bool exit_patched = false;
        if (write_opcode(exit_address, breakpoint_opcode)) exit_patched = true;
        else error = "Cannot arm the guarded startup-exit intervention";
        if (error.empty() && arm_frame) {
            if (write_opcode(frame_address, breakpoint_opcode)) frame_patched = true;
            else error = "Cannot arm the guarded frame-preservation intervention";
        }
        if (error.empty() && !machine.arm())
            error = "Cannot arm the startup intervention state machine";
        if (!error.empty()) {
            if (frame_patched) write_opcode(frame_address, startup_frame_instruction[0]);
            if (exit_patched) write_opcode(exit_address, startup_exit_instruction[0]);
            RemoveVectoredExceptionHandler(handler_handle);
            handler_handle = nullptr;
            installed_base.store(0, std::memory_order_release);
            machine.fail();
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        machine.fail();
        return false;
    }
}

} // namespace dingosdk

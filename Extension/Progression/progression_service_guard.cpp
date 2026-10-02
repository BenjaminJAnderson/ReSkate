#include "progression_service_guard.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/image_identity.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/progression_service_guard.h"

#include <Windows.h>
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Platform/memory.h"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <sstream>

namespace dingosdk {
namespace {
namespace guard = addr::progression_service_guard;

using ProgressionDispatch = void (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t);
using ImageVerifier = bool (*)(std::uintptr_t) noexcept;
using CallerClassifier = bool (*)(std::uintptr_t, std::uintptr_t) noexcept;

constexpr std::uintptr_t image_size = supported_build::game_image_size;

static_assert(guard::dispatch + guard::dispatch_prefix.size() <= image_size &&
    guard::progression_call + guard::progression_call_prefix.size() <= image_size,
    "Guard fingerprints must remain within the inspected image");


enum class GateStatus : unsigned char {
    not_attempted,
    image_mismatch,
    dispatch_fingerprint_mismatch,
    callsite_fingerprint_mismatch,
    create_failed,
    enable_failed,
    enable_failed_disable_failed,
    preparing,
    prepared,
    active
};

bool exact_progression_caller(std::uintptr_t return_address, std::uintptr_t base) noexcept;

struct ProgressionServiceGuardState {
    std::uintptr_t base{};
    std::atomic<ProgressionDispatch> original{};
    std::atomic<GateStatus> gate{GateStatus::not_attempted};
    std::atomic<std::uint64_t> invocations{};
    std::atomic<std::uint64_t> null_service_skips{};
    std::atomic<std::uint64_t> forwards{};
    std::atomic<std::uint64_t> matching_nonnull_forwards{};
    std::atomic<std::uint64_t> unmatched_null_forwards{};
    std::atomic<std::uint64_t> native_exceptions{};
    std::atomic<std::uint64_t> unarmed_forwards{};
    std::atomic<std::uint64_t> preparing_invocations{};
    std::mutex initialization_mutex;
    ImageVerifier verify_image{&supported_build::running_image_matches};
    CallerClassifier classify_caller{&exact_progression_caller};
    bool attempted{};
};

struct PreserveError {
    DWORD value{GetLastError()};
    ~PreserveError() { SetLastError(value); }
};

ProgressionServiceGuardState& progression_service_guard_state() {
    static auto* value = new ProgressionServiceGuardState;
    return *value;
}

template<std::size_t N>
bool fingerprint_matches(std::uintptr_t address, const std::array<unsigned char, N>& expected) noexcept {
    std::array<unsigned char, N> actual{};
    return memory::read(address, actual) && actual == expected;
}

bool exact_progression_caller(
    std::uintptr_t return_address, std::uintptr_t base) noexcept {
    return base && return_address == base + guard::progression_caller_return;
}

const char* gate_name(GateStatus gate) noexcept {
    switch (gate) {
    case GateStatus::not_attempted: return "not_attempted";
    case GateStatus::image_mismatch: return "image_mismatch";
    case GateStatus::dispatch_fingerprint_mismatch: return "dispatch_fingerprint_mismatch";
    case GateStatus::callsite_fingerprint_mismatch: return "callsite_fingerprint_mismatch";
    case GateStatus::create_failed: return "create_failed";
    case GateStatus::enable_failed: return "enable_failed";
    case GateStatus::enable_failed_disable_failed: return "enable_failed_disable_failed";
    case GateStatus::preparing: return "preparing";
    case GateStatus::prepared: return "prepared";
    case GateStatus::active: return "active";
    }
    return "unknown";
}

void progression_dispatch_hook(
    std::uintptr_t service, std::uintptr_t events, std::uintptr_t context) {
    const auto incoming_error = GetLastError();
    auto& state = progression_service_guard_state();
    const auto gate = state.gate.load(std::memory_order_acquire);
    const bool armed = gate == GateStatus::active;
    const bool preparing = gate == GateStatus::preparing;
    // The gate publishes the trampoline and image base.
    const auto original = state.original.load(std::memory_order_acquire);
    bool matching_caller{};
    if (armed) {
        ++state.invocations;
        matching_caller = state.classify_caller(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()), state.base);
        if (!service && matching_caller) {
            ++state.null_service_skips;
            SetLastError(incoming_error);
            return;
        }
        ++state.forwards;
        if (matching_caller) ++state.matching_nonnull_forwards;
        else if (!service) ++state.unmatched_null_forwards;
    }
    else {
        ++state.unarmed_forwards;
        if (preparing) ++state.preparing_invocations;
    }

    SetLastError(incoming_error);
    try {
        original(service, events, context);
    } catch (...) {
        const auto native_error = GetLastError();
        if (armed) ++state.native_exceptions;
        SetLastError(native_error);
        throw;
    }
    const auto native_error = GetLastError();
    SetLastError(native_error);
}

} // namespace

bool prepare_progression_service_guard(std::uintptr_t base) noexcept {
    PreserveError preserve;
    auto& state = progression_service_guard_state();
    try {
        std::lock_guard lock(state.initialization_mutex);
        if (state.attempted)
            return state.base == base &&
                (state.gate.load(std::memory_order_acquire) == GateStatus::prepared ||
                 state.gate.load(std::memory_order_acquire) == GateStatus::active);
        state.attempted = true;
        state.base = base;
        if (!state.verify_image(base)) {
            state.gate.store(GateStatus::image_mismatch, std::memory_order_release);
            return false;
        }
        if (!fingerprint_matches(base + guard::dispatch, guard::dispatch_prefix)) {
            state.gate.store(GateStatus::dispatch_fingerprint_mismatch, std::memory_order_release);
            return false;
        }
        if (!fingerprint_matches(base + guard::progression_call,
                guard::progression_call_prefix)) {
            state.gate.store(GateStatus::callsite_fingerprint_mismatch, std::memory_order_release);
            return false;
        }

        auto* target = reinterpret_cast<void*>(base + guard::dispatch);
        void* trampoline{};
        const auto created = hook_prepare(target,
            reinterpret_cast<void*>(&progression_dispatch_hook), &trampoline);
        if (created != HookOk || !trampoline) {
            if (created == HookOk) hook_remove(target);
            state.gate.store(GateStatus::create_failed, std::memory_order_release);
            return false;
        }
        state.original.store(reinterpret_cast<ProgressionDispatch>(trampoline),
            std::memory_order_release);
        // If redirection becomes visible inside hook_enable, every call still
        // forwards. A failed enable disables the target and retains the
        // trampoline for threads that may still be returning.
        state.gate.store(GateStatus::preparing, std::memory_order_release);
        if (hook_enable(target) != HookOk) {
            state.gate.store(GateStatus::enable_failed, std::memory_order_release);
            const auto disabled = hook_disable(target);
            state.gate.store(disabled == HookOk || disabled == HookDisabled ?
                GateStatus::enable_failed : GateStatus::enable_failed_disable_failed,
                std::memory_order_release);
            return false;
        }
        state.gate.store(GateStatus::prepared, std::memory_order_release);
        return true;
    } catch (...) {
        return false;
    }
}

bool arm_progression_service_guard(std::uintptr_t base) noexcept {
    PreserveError preserve;
    auto& state = progression_service_guard_state();
    try {
        std::lock_guard lock(state.initialization_mutex);
        const auto gate = state.gate.load(std::memory_order_acquire);
        if (state.base != base || (gate != GateStatus::prepared && gate != GateStatus::active))
            return false;
        if (gate == GateStatus::active) return true;
        state.gate.store(GateStatus::active, std::memory_order_release);
        return true;
    } catch (...) {
        return false;
    }
}

ProgressionServiceGuardObservation progression_service_guard_observation() {
    PreserveError preserve;
    auto& state = progression_service_guard_state();
    const auto gate = state.gate.load(std::memory_order_acquire);
    const bool armed = gate == GateStatus::active;
    const auto invocations = state.invocations.load(std::memory_order_relaxed);
    const auto skips = state.null_service_skips.load(std::memory_order_relaxed);
    const auto forwards = state.forwards.load(std::memory_order_relaxed);
    const auto matching_nonnull =
        state.matching_nonnull_forwards.load(std::memory_order_relaxed);
    const auto unmatched_null = state.unmatched_null_forwards.load(std::memory_order_relaxed);
    const auto exceptions = state.native_exceptions.load(std::memory_order_relaxed);
    const auto unarmed = state.unarmed_forwards.load(std::memory_order_relaxed);
    const auto preparing = state.preparing_invocations.load(std::memory_order_relaxed);

    std::ostringstream json;
    json << "{\"event\":\"progression_service_guard_observation\",\"gate\":\""
         << gate_name(gate) << "\",\"global_offline_prerequisite\":"
         << (armed ? "true" : "false")
         << ",\"armed\":"
         << (armed ? "true" : "false")
         << ",\"dispatch_rva\":\"0x" << std::hex << guard::dispatch << "\""
         << ",\"caller_return_rva\":\"0x" << guard::progression_caller_return << std::dec << "\""
         << ",\"invocations\":" << invocations
         << ",\"null_service_skips\":" << skips
         << ",\"forwards\":" << forwards
         << ",\"matching_nonnull_forwards\":" << matching_nonnull
         << ",\"unmatched_null_forwards\":" << unmatched_null
         << ",\"native_exceptions\":" << exceptions
         << ",\"unarmed_forwards\":" << unarmed
         << ",\"preparing_invocations\":" << preparing << '}';

    std::ostringstream detail;
    detail << "Progression service guard: " << gate_name(gate);
    if (gate == GateStatus::active)
        detail << " | null skips " << skips << " | native forwards " << forwards;
    return {json.str(), detail.str()};
}

} // namespace dingosdk

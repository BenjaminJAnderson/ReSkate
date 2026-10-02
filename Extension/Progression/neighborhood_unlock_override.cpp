#include "neighborhood_unlock_override.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/image_identity.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/neighborhood_unlock.h"
#include "Engine/Core/Profiling/profiler.h"

#include <Windows.h>
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Platform/memory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <sstream>

namespace dingosdk {
namespace {
namespace unlock = addr::neighborhood_unlock;

using ExpressionArgumentBinder = void (*)(
    std::uintptr_t, std::uintptr_t, void**, void**, std::uintptr_t);
using ImageVerifier = bool (*)(std::uintptr_t) noexcept;
using DataVerifier = bool (*)(std::uintptr_t) noexcept;

constexpr std::uintptr_t image_size = supported_build::game_image_size;
constexpr std::uintptr_t serialized_header_offset = 0x10;
constexpr std::uintptr_t resource_template_offset = 0x38;
constexpr std::uintptr_t serialized_input_mapping_offset = 0x58;

static_assert(unlock::one_shot_bind_call + unlock::one_shot_bind_call_prefix.size() <= image_size);


// These are the immutable scalar bytes at resource template +0x10. The binder
// receives a prepared wrapper, whose +0x38 points to that template. The exact
// sequence occurs only for NeighborhoodBoundaryComponent_UpdateIsUnlocked in
// all 18,702 installed expression payloads for the inspected executable. The
// 664-byte stored payload is byte-identical to the previous build; only its CAS
// location changed. Do not weaken the independent Data identity checks.
constexpr std::array<unsigned char, 40> updater_serialized_header{
    0x16,0x39,0xcc,0x22,0xff,0xff,0xff,0xff,0x10,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
    0x40,0x00,0x00,0x00,0x10,0x02,0x00,0x00,0x3e,0x00,0x00,0x00,0x40,0x01,0x00,0x00,
    0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
struct ExpressionInputMapping {
    std::uint32_t register_offset{};
    std::uint32_t native_metadata{};
};
static_assert(sizeof(ExpressionInputMapping) == 8);
constexpr std::uint32_t updater_input_mapping_count = 2;
constexpr std::array<std::uint32_t, updater_input_mapping_count>
    updater_input_register_offsets{0x20, 0x28};
constexpr bool forced_unlocked = true;

enum class GateStatus : unsigned char {
    not_attempted,
    image_mismatch,
    data_mismatch,
    binder_fingerprint_mismatch,
    callsite_fingerprint_mismatch,
    create_failed,
    rollback_failed,
    enable_failed,
    enable_failed_disable_failed,
    prepared,
};

bool exact_data_matches(std::uintptr_t base) noexcept;

struct NeighborhoodUnlockState {
    std::uintptr_t base{};
    std::atomic<ExpressionArgumentBinder> original{};
    std::atomic<ExpressionBinderObserver> observer{};
    std::atomic<GateStatus> gate{GateStatus::not_attempted};
    std::atomic_bool enabled{};
    std::atomic<std::uint64_t> invocations{};
    std::atomic<std::uint64_t> target_matches{};
    std::atomic<std::uint64_t> overrides{};
    std::atomic<std::uint64_t> native_forwards{};
    std::atomic<std::uint64_t> unreadable_headers{};
    std::atomic<std::uint64_t> rejected_input_mappings{};
    std::atomic<std::uint64_t> unreadable_inputs{};
    std::atomic<std::uint64_t> preactive_forwards{};
    std::mutex initialization_mutex;
    ImageVerifier verify_image{&supported_build::running_image_matches};
    DataVerifier verify_data{&exact_data_matches};
    bool attempted{};
};

struct PreserveError {
    DWORD value{GetLastError()};
    ~PreserveError() { SetLastError(value); }
};

NeighborhoodUnlockState& neighborhood_unlock_state() {
    static auto* value = new NeighborhoodUnlockState;
    return *value;
}

bool valid_range(std::uintptr_t address, std::size_t size) noexcept {
    return address >= 0x10000 && size && size <= memory::highest_user_address &&
        address <= memory::highest_user_address - size;
}

bool exact_data_matches(std::uintptr_t base) noexcept {
    try {
        std::array<wchar_t, 32768> path{};
        const auto length = GetModuleFileNameW(reinterpret_cast<HMODULE>(base), path.data(),
            static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) return false;
        const auto game = std::filesystem::path(std::wstring(path.data(), length)).parent_path();

        const auto layout_path = game / L"Data" / L"layout.toc";
        const auto layout = CreateFileW(layout_path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (layout == INVALID_HANDLE_VALUE) return false;
        const bool layout_matches = supported_build::file_sha256_matches(layout, unlock::layout_toc_sha256);
        CloseHandle(layout);
        if (!layout_matches) return false;

        const auto cas_path = game / L"Data" / L"Win32" / L"configurations" / L"layout" /
            L"defaultinstallpackage" / unlock::updater_cas_file;
        const auto cas = CreateFileW(cas_path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (cas == INVALID_HANDLE_VALUE) return false;
        const bool cas_matches = supported_build::file_region_sha256_matches(cas, unlock::updater_cas_offset,
            unlock::updater_cas_stored_size, unlock::updater_stored_sha256);
        CloseHandle(cas);
        return cas_matches;
    } catch (...) {
        return false;
    }
}

template<std::size_t N>
bool fingerprint_matches(std::uintptr_t address,
    const std::array<unsigned char, N>& expected) noexcept {
    std::array<unsigned char, N> actual{};
    return memory::read(address, actual) && actual == expected;
}

bool exact_updater_input_mapping(std::uintptr_t serialized_graph) noexcept {
    std::uintptr_t mappings{};
    if (!memory::read(serialized_graph + serialized_input_mapping_offset, mappings) ||
        mappings < sizeof(std::uint32_t))
        return false;
    std::uint32_t encoded_count{};
    if (!memory::read(mappings - sizeof(encoded_count), encoded_count) ||
        (encoded_count & 0x7fffffffU) != updater_input_mapping_count)
        return false;
    std::array<ExpressionInputMapping, updater_input_mapping_count> entries{};
    if (!memory::read(mappings, entries)) return false;
    for (std::size_t index = 0; index != entries.size(); ++index) {
        if (entries[index].register_offset != updater_input_register_offsets[index] ||
            (entries[index].native_metadata & 0xffU) != 0)
            return false;
    }
    return true;
}

const char* gate_name(GateStatus gate) noexcept {
    switch (gate) {
    case GateStatus::not_attempted: return "not_attempted";
    case GateStatus::image_mismatch: return "image_mismatch";
    case GateStatus::data_mismatch: return "data_mismatch";
    case GateStatus::binder_fingerprint_mismatch: return "binder_fingerprint_mismatch";
    case GateStatus::callsite_fingerprint_mismatch: return "callsite_fingerprint_mismatch";
    case GateStatus::create_failed: return "create_failed";
    case GateStatus::rollback_failed: return "rollback_failed";
    case GateStatus::enable_failed: return "enable_failed";
    case GateStatus::enable_failed_disable_failed: return "enable_failed_disable_failed";
    case GateStatus::prepared: return "prepared";
    }
    return "unknown";
}

void expression_argument_binder_hook(
    std::uintptr_t serialized_graph, std::uintptr_t runtime_graph,
    void** outputs, void** inputs, std::uintptr_t allocator) {
    const auto incoming_error = GetLastError();
    auto& state = neighborhood_unlock_state();
    const auto original = state.original.load(std::memory_order_acquire);
    ++state.invocations;
    if (const auto observer = state.observer.load(std::memory_order_acquire)) {
        DINGO_PROFILE_ZONE("hooks/argument binder observer");
        try { observer(serialized_graph, runtime_graph, inputs); } catch (...) {}
    }

    void** forwarded_inputs = inputs;
    std::array<void*, 2> replacement{};
    if (state.enabled.load(std::memory_order_acquire)) {
        DINGO_PROFILE_ZONE("hooks/argument binder unlock check");
        std::array<unsigned char, updater_serialized_header.size()> header{};
        std::uintptr_t resource_template{};
        // Every bound graph passes here: peeked rather than one system call per read.
        if (!memory::peek(serialized_graph + resource_template_offset, resource_template) ||
            !valid_range(resource_template, 0x50) ||
            !memory::peek(resource_template + serialized_header_offset, header)) {
            ++state.unreadable_headers;
        } else if (header == updater_serialized_header) {
            ++state.target_matches;
            if (!exact_updater_input_mapping(serialized_graph)) {
                ++state.rejected_input_mappings;
            } else {
                std::array<void*, 2> native_inputs{};
                if (memory::read(reinterpret_cast<std::uintptr_t>(inputs), native_inputs)) {
                    replacement = {native_inputs[0], const_cast<bool*>(&forced_unlocked)};
                    forwarded_inputs = replacement.data();
                    ++state.overrides;
                } else {
                    ++state.unreadable_inputs;
                }
            }
        }
    } else {
        ++state.preactive_forwards;
    }
    if (forwarded_inputs == inputs) ++state.native_forwards;

    SetLastError(incoming_error);
    try {
        original(serialized_graph, runtime_graph, outputs, forwarded_inputs, allocator);
    } catch (...) {
        const auto native_error = GetLastError();
        SetLastError(native_error);
        throw;
    }
    const auto native_error = GetLastError();
    SetLastError(native_error);
}

} // namespace

void set_expression_binder_observer(ExpressionBinderObserver observer) noexcept {
    neighborhood_unlock_state().observer.store(observer, std::memory_order_release);
}

bool prepare_neighborhood_unlock_override(std::uintptr_t base) noexcept {
    PreserveError preserve;
    auto& state = neighborhood_unlock_state();
    try {
        std::lock_guard lock(state.initialization_mutex);
        if (state.attempted)
            return state.base == base &&
                state.gate.load(std::memory_order_acquire) == GateStatus::prepared;
        state.attempted = true;
        state.base = base;
        if (!state.verify_image(base)) {
            state.gate.store(GateStatus::image_mismatch, std::memory_order_release);
            return false;
        }
        if (!state.verify_data(base)) {
            state.gate.store(GateStatus::data_mismatch, std::memory_order_release);
            return false;
        }
        if (!fingerprint_matches(base + unlock::binder, unlock::binder_prefix)) {
            state.gate.store(GateStatus::binder_fingerprint_mismatch, std::memory_order_release);
            return false;
        }
        if (!fingerprint_matches(base + unlock::cached_bind_call_a,
                unlock::cached_bind_call_a_prefix) ||
            !fingerprint_matches(base + unlock::cached_bind_call_b,
                unlock::cached_bind_call_b_prefix) ||
            !fingerprint_matches(base + unlock::one_shot_bind_call,
                unlock::one_shot_bind_call_prefix)) {
            state.gate.store(GateStatus::callsite_fingerprint_mismatch, std::memory_order_release);
            return false;
        }

        auto* target = reinterpret_cast<void*>(base + unlock::binder);
        void* trampoline{};
        const auto created = hook_prepare(target,
            reinterpret_cast<void*>(&expression_argument_binder_hook), &trampoline);
        if (created != HookOk || !trampoline) {
            const bool rollback_failed = created == HookOk && hook_remove(target) != HookOk;
            state.gate.store(rollback_failed ? GateStatus::rollback_failed :
                GateStatus::create_failed, std::memory_order_release);
            return false;
        }
        state.original.store(reinterpret_cast<ExpressionArgumentBinder>(trampoline),
            std::memory_order_release);
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

bool set_neighborhood_unlock_override_enabled(bool enabled) noexcept {
    PreserveError preserve;
    auto& state = neighborhood_unlock_state();
    if (state.gate.load(std::memory_order_acquire) != GateStatus::prepared) return false;
    state.enabled.store(enabled, std::memory_order_release);
    return true;
}

NeighborhoodUnlockOverrideObservation neighborhood_unlock_override_observation() {
    PreserveError preserve;
    auto& state = neighborhood_unlock_state();
    const auto gate = state.gate.load(std::memory_order_acquire);
    const bool enabled = state.enabled.load(std::memory_order_acquire);
    const auto invocations = state.invocations.load(std::memory_order_relaxed);
    const auto matches = state.target_matches.load(std::memory_order_relaxed);
    const auto overrides = state.overrides.load(std::memory_order_relaxed);
    const auto native = state.native_forwards.load(std::memory_order_relaxed);
    const auto bad_headers = state.unreadable_headers.load(std::memory_order_relaxed);
    const auto bad_mappings = state.rejected_input_mappings.load(std::memory_order_relaxed);
    const auto bad_inputs = state.unreadable_inputs.load(std::memory_order_relaxed);
    const auto preactive = state.preactive_forwards.load(std::memory_order_relaxed);

    NeighborhoodUnlockOverrideObservation observation;
    observation.prepared = gate == GateStatus::prepared;
    observation.enabled = enabled;
    observation.invocations = invocations;
    observation.target_matches = matches;
    observation.overrides = overrides;

    std::ostringstream json;
    json << "{\"event\":\"neighborhood_unlock_override_observation\",\"gate\":\""
         << gate_name(gate) << "\",\"enabled\":" << (enabled ? "true" : "false")
         << ",\"binder_rva\":\"0x" << std::hex << unlock::binder << std::dec << "\""
         << ",\"serialized_hash\":\"0x22cc3916\""
         << ",\"invocations\":" << invocations
         << ",\"target_matches\":" << matches
         << ",\"overrides\":" << overrides
         << ",\"native_forwards\":" << native
         << ",\"unreadable_headers\":" << bad_headers
         << ",\"rejected_input_mappings\":" << bad_mappings
         << ",\"unreadable_inputs\":" << bad_inputs
         << ",\"preactive_forwards\":" << preactive << '}';

    std::ostringstream detail;
    detail << "Neighborhood unlock override: " << gate_name(gate)
           << " | " << (enabled ? "enabled" : "native")
           << " | matches " << matches << " | overrides " << overrides;
    observation.json = json.str();
    observation.detail = detail.str();
    return observation;
}

} // namespace dingosdk

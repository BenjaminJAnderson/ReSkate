#include "fixed_stop_entitlement_provider.h"
#include "fixed_stop_entitlement_internal.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/image_identity.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/fixed_stop_entitlement.h"

#include "fast_travel_unlock.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace dingosdk {
using namespace fixed_stop_detail;
namespace {
namespace provider = addr::fixed_stop_entitlement;

constexpr std::uintptr_t provider_image_size = supported_build::game_image_size;
constexpr std::size_t maximum_request_identifiers = 512;

static_assert(provider::group_enumeration +
    provider::group_enumeration_prefix.size() <= provider_image_size);

struct ReentryGuard {
    bool& active;
    explicit ReentryGuard(bool& value) noexcept : active(value) { active = true; }
    ~ReentryGuard() { active = false; }
};

thread_local bool provider_active{};

template<std::size_t N>
bool fingerprint_matches(
    std::uintptr_t address, const std::array<unsigned char, N>& expected) noexcept {
    std::array<unsigned char, N> actual{};
    return memory::read(address, actual) && actual == expected;
}

bool complete_native_api(const NativeApi& api) noexcept {
    return api.result_vector_init && api.array_allocate && api.result_element_init &&
        api.assign_string && api.construct_error && api.complete_promise &&
        api.destroy_vector && api.destroy_string && api.assign_future &&
        api.eid_key && api.find_member_eid && api.client_game_manager_global;
}

void record_attempt(FixedStopEntitlementAttempt attempt,
    std::uint32_t requested_count = 0,
    std::uint32_t fixed_count = 0,
    std::uint32_t eid_count = 0) noexcept {
    auto& state = fixed_stop_entitlement_provider_state();
    state.attempts.fetch_add(1, std::memory_order_relaxed);
    switch (attempt) {
    case FixedStopEntitlementAttempt::forwarded_unprepared:
        state.forwarded_unprepared.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_disabled:
        state.forwarded_disabled.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_malformed_request:
        state.forwarded_malformed_request.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_no_fixed_stop:
        state.forwarded_no_fixed_stop.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_unsupported_batch:
        state.forwarded_unsupported_batch.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_no_group:
        state.forwarded_no_group.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_malformed_group:
        state.forwarded_malformed_group.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_no_eid:
        state.forwarded_no_eid.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_allocation_failure:
        state.forwarded_allocation_failure.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::forwarded_reentrant:
        state.forwarded_reentrant.fetch_add(1, std::memory_order_relaxed); break;
    case FixedStopEntitlementAttempt::completed:
        state.completions.fetch_add(1, std::memory_order_relaxed);
        if (fixed_count != requested_count)
            state.mixed_completions.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    std::lock_guard lock(state.latest_mutex);
    state.latest_attempt = attempt;
    state.latest_requested_count = requested_count;
    state.latest_fixed_stop_count = fixed_count;
    state.latest_eid_count = eid_count;
    state.latest_mixed = fixed_count != 0 && fixed_count != requested_count;
}

bool classify_request(const void* wrapper,
    std::uint32_t& requested_count, std::uint32_t& fixed_count,
    std::vector<std::string>& owned_ids) {
    requested_count = fixed_count = 0;
    owned_ids.clear();
    std::uintptr_t data{};
    if (!wrapper || !memory::read(reinterpret_cast<std::uintptr_t>(wrapper), data) ||
        data < 4)
        return false;
    std::uint32_t encoded_count{};
    if (!memory::read(data - 4, encoded_count)) return false;
    requested_count = encoded_count & 0x7fffffffU;
    if (requested_count > maximum_request_identifiers ||
        (requested_count && !valid_range(data,
            static_cast<std::size_t>(requested_count) * sizeof(std::uintptr_t))))
        return false;
    std::array<char, maximum_identifier_bytes + 1> text{};
    const auto ownership = fixed_stop_entitlement_provider_state().ownership.load(std::memory_order_acquire);
    for (std::uint32_t index = 0; index != requested_count; ++index) {
        std::uintptr_t identifier{};
        if (!memory::read(data + static_cast<std::uintptr_t>(index) * sizeof(identifier),
                identifier))
            return false;
        std::size_t length{};
        if (!read_c_string(identifier, text, length)) return false;
        const std::string_view id{text.data(), length};
        if (classify_fixed_bus_stop_entitlement(id) && (!ownership || ownership(id))) {
            ++fixed_count;
            if (std::find(owned_ids.begin(), owned_ids.end(), id) == owned_ids.end())
                owned_ids.emplace_back(id);
        }
    }
    return true;
}

} // namespace

namespace fixed_stop_detail {
ProviderState& fixed_stop_entitlement_provider_state() {
    static auto* value = new ProviderState;
    return *value;
}
} // namespace fixed_stop_detail

bool prepare_fixed_stop_entitlement_provider(std::uintptr_t image_base) noexcept {
    PreserveLastError preserve;
    auto& state = fixed_stop_entitlement_provider_state();
    std::scoped_lock lock(state.initialization_mutex);
    if (state.attempted)
        return state.prepared.load(std::memory_order_acquire) &&
            state.base.load(std::memory_order_relaxed) == image_base;
    state.attempted = true;
    state.base.store(image_base, std::memory_order_relaxed);
    auto fail = [&](ProviderGate gate) {
        state.gate.store(gate, std::memory_order_release);
        return false;
    };
    if (!state.verify_image || !state.verify_image(image_base))
        return fail(ProviderGate::image_mismatch);
    if (!fingerprint_matches(image_base + provider::check_entitlements,
            provider::check_entitlements_prefix))
        return fail(ProviderGate::entry_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::group_enumeration,
            provider::group_enumeration_prefix))
        return fail(ProviderGate::group_contract_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::result_vector_init,
            provider::result_vector_init_prefix))
        return fail(ProviderGate::result_vector_init_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::array_allocate,
            provider::array_allocate_prefix))
        return fail(ProviderGate::array_allocate_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::result_element_init,
            provider::result_element_init_prefix))
        return fail(ProviderGate::result_element_init_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + addr::engine::string_assign,
            provider::assign_string_prefix))
        return fail(ProviderGate::assign_string_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::construct_error,
            provider::construct_error_prefix))
        return fail(ProviderGate::construct_error_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::complete_promise,
            provider::complete_promise_prefix))
        return fail(ProviderGate::complete_promise_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::destroy_vector,
            provider::destroy_vector_prefix))
        return fail(ProviderGate::destroy_vector_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + addr::engine::native_text_release,
            provider::destroy_string_prefix))
        return fail(ProviderGate::destroy_string_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::assign_future,
            provider::assign_future_prefix))
        return fail(ProviderGate::assign_future_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::eid_key, provider::eid_key_prefix))
        return fail(ProviderGate::eid_key_fingerprint_mismatch);
    if (!fingerprint_matches(image_base + provider::find_member_eid,
            provider::find_member_eid_prefix))
        return fail(ProviderGate::find_member_eid_fingerprint_mismatch);
    if (!state.resolve_native)
        return fail(ProviderGate::native_resolution_failed);
    const auto native = state.resolve_native(image_base);
    if (!complete_native_api(native))
        return fail(ProviderGate::native_resolution_failed);
    state.native = native;
    state.prepared.store(true, std::memory_order_release);
    state.gate.store(ProviderGate::prepared, std::memory_order_release);
    return true;
}

void set_fixed_stop_entitlement_ownership(FixedStopOwnership lookup) noexcept {
    fixed_stop_entitlement_provider_state().ownership.store(lookup, std::memory_order_release);
}

void set_fixed_stop_entitlement_provider_enabled(bool enabled) noexcept {
    PreserveLastError preserve;
    auto& state = fixed_stop_entitlement_provider_state();
    state.enabled.store(enabled && state.prepared.load(std::memory_order_acquire),
        std::memory_order_release);
}

FixedStopEntitlementAttempt try_complete_fixed_stop_entitlement_request(
    void* future_out, const void* requested_identifier_array,
    void* result_promise) {
    PreserveLastError preserve;
    auto& state = fixed_stop_entitlement_provider_state();
    try {
        if (!state.prepared.load(std::memory_order_acquire)) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_unprepared);
            return FixedStopEntitlementAttempt::forwarded_unprepared;
        }
        if (!state.enabled.load(std::memory_order_acquire)) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_disabled);
            return FixedStopEntitlementAttempt::forwarded_disabled;
        }
        if (provider_active) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_reentrant);
            return FixedStopEntitlementAttempt::forwarded_reentrant;
        }
        ReentryGuard guard(provider_active);
        if (!future_out || !result_promise) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_malformed_request);
            return FixedStopEntitlementAttempt::forwarded_malformed_request;
        }
        std::uint32_t requested_count{};
        std::uint32_t fixed_count{};
        std::vector<std::string> owned_ids;
        if (!classify_request(requested_identifier_array, requested_count, fixed_count, owned_ids)) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_malformed_request,
                requested_count, fixed_count);
            return FixedStopEntitlementAttempt::forwarded_malformed_request;
        }
        if (!fixed_count) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_no_fixed_stop,
                requested_count, fixed_count);
            return FixedStopEntitlementAttempt::forwarded_no_fixed_stop;
        }
        // The updated native reducer reports all-requested ownership, not the
        // previous existential result. Never manufacture results for other IDs.
        if (requested_count != fixed_count) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_unsupported_batch,
                requested_count, fixed_count);
            return FixedStopEntitlementAttempt::forwarded_unsupported_batch;
        }
        std::array<EidBuffer, maximum_group_members> eids{};
        std::uint32_t eid_count{};
        auto failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
        if (!gather_group_eids(state.native, eids, eid_count, failure)) {
            record_attempt(failure, requested_count, fixed_count, eid_count);
            return failure;
        }
        if (!complete_request(state.native, future_out, result_promise, eids, eid_count, owned_ids)) {
            record_attempt(FixedStopEntitlementAttempt::forwarded_allocation_failure,
                requested_count, fixed_count, eid_count);
            return FixedStopEntitlementAttempt::forwarded_allocation_failure;
        }
        record_attempt(FixedStopEntitlementAttempt::completed,
            requested_count, fixed_count, eid_count);
        return FixedStopEntitlementAttempt::completed;
    } catch (...) {
        preserve.capture_current();
        state.native_exceptions.fetch_add(1, std::memory_order_relaxed);
        throw;
    }
}

} // namespace dingosdk

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk {

enum class FixedStopEntitlementAttempt : unsigned char {
    forwarded_unprepared,
    forwarded_disabled,
    forwarded_malformed_request,
    forwarded_no_fixed_stop,
    forwarded_unsupported_batch,
    forwarded_no_group,
    forwarded_malformed_group,
    forwarded_no_eid,
    forwarded_allocation_failure,
    forwarded_reentrant,
    completed,
};

struct FixedStopEntitlementProviderObservation {
    bool prepared{};
    bool enabled{};
    std::uint64_t attempts{};
    std::uint64_t completions{};
    std::uint64_t mixed_completions{};
    std::uint64_t forwarded_unprepared{};
    std::uint64_t forwarded_disabled{};
    std::uint64_t forwarded_malformed_request{};
    std::uint64_t forwarded_no_fixed_stop{};
    std::uint64_t forwarded_unsupported_batch{};
    std::uint64_t forwarded_no_group{};
    std::uint64_t forwarded_malformed_group{};
    std::uint64_t forwarded_no_eid{};
    std::uint64_t forwarded_allocation_failure{};
    std::uint64_t forwarded_reentrant{};
    std::uint64_t native_exceptions{};
    std::uint32_t latest_requested_count{};
    std::uint32_t latest_fixed_stop_count{};
    std::uint32_t latest_eid_count{};
    bool latest_mixed{};
    FixedStopEntitlementAttempt latest_attempt{
        FixedStopEntitlementAttempt::forwarded_unprepared};
    std::string detail;
    std::string json;
};

// Validates the exact retail image and the native result-construction ABI.
// This module deliberately does not install a hook: the entitlement trace owns
// the sole CheckUsersEntitlements detour and calls the delegate below.
bool prepare_fixed_stop_entitlement_provider(std::uintptr_t image_base) noexcept;

// Enabling is process-local and reversible. A true request is ignored until
// prepare_fixed_stop_entitlement_provider has passed every exact-build gate.
// Disable affects calls entering after this setter returns; an in-flight call
// can finish the decision it already made.
void set_fixed_stop_entitlement_provider_enabled(bool enabled) noexcept;
// Every requested identifier must be a saved, owned stop ID. The native result
// reports all-requested ownership; mixed, unknown, or revoked batches forward.
using FixedStopOwnership = bool (*)(std::string_view) noexcept;
void set_fixed_stop_entitlement_ownership(FixedStopOwnership lookup) noexcept;

// Completes the existing promise and writes a null future only when the request
// is valid, contains only owned strict fixed-stop IDs, and has a readable current group
// with at least one eID. Every other case must be forwarded by the caller.
FixedStopEntitlementAttempt try_complete_fixed_stop_entitlement_request(
    void* future_out,
    const void* requested_identifier_array,
    void* result_promise);

const char* fixed_stop_entitlement_attempt_name(
    FixedStopEntitlementAttempt attempt) noexcept;

FixedStopEntitlementProviderObservation
fixed_stop_entitlement_provider_observation();

} // namespace dingosdk

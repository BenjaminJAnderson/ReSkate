#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {

struct EntitlementRequestTraceObservation {
    bool prepared{};
    std::uint64_t entry_calls{};
    std::uint64_t valid_request_lists{};
    std::uint64_t malformed_request_lists{};
    std::uint64_t all_fixed_stop_requests{};
    std::uint64_t mixed_requests{};
    std::uint64_t requests_without_fixed_stops{};
    std::uint64_t exact_builder_calls{};
    std::uint64_t service_list_matches{};
    std::uint64_t service_list_mismatches{};
    std::uint64_t synchronous_entry_errors{};
    std::uint64_t asynchronous_submissions{};
    std::uint64_t local_provider_completions{};
    std::uint64_t local_provider_exceptions{};
    std::uint64_t associated_completions{};
    std::uint64_t successful_completions{};
    std::uint64_t error_completions{};
    std::uint64_t unknown_completions{};
    std::uint64_t orphan_completions{};
    std::uint64_t active_requests{};
    std::uint64_t dropped_requests{};
    std::uint64_t native_exceptions{};
    std::uint64_t latest_sequence{};
    std::uint32_t latest_requested_count{};
    std::uint32_t latest_fixed_stop_count{};
    std::uint32_t latest_service_entitlement_count{};
    std::uint32_t latest_group_eid_count{};
    bool latest_all_fixed_stop{};
    bool latest_mixed{};
    bool latest_service_list_matches{};
    bool latest_completion_observed{};
    bool latest_completion_error{};
    std::string json;
};

// Installs an exact-build observer for the retail build's
// CheckUsersEntitlements entry, its exact ProgressionService request builder,
// and the unique result reducer. Requests not completed by the separately
// prepared fixed-stop provider retain the native request and continuation.
bool prepare_entitlement_request_trace(std::uintptr_t image_base) noexcept;

EntitlementRequestTraceObservation entitlement_request_trace_observation();

} // namespace dingosdk

#include "fixed_stop_entitlement_provider.h"
#include "fixed_stop_entitlement_internal.h"

#include <mutex>
#include <sstream>

namespace dingosdk {
using namespace fixed_stop_detail;
namespace {
const char* gate_name(ProviderGate gate) noexcept {
    switch (gate) {
    case ProviderGate::not_attempted: return "not_attempted";
    case ProviderGate::image_mismatch: return "image_mismatch";
    case ProviderGate::entry_fingerprint_mismatch: return "entry_fingerprint_mismatch";
    case ProviderGate::group_contract_fingerprint_mismatch:
        return "group_contract_fingerprint_mismatch";
    case ProviderGate::result_vector_init_fingerprint_mismatch:
        return "result_vector_init_fingerprint_mismatch";
    case ProviderGate::array_allocate_fingerprint_mismatch:
        return "array_allocate_fingerprint_mismatch";
    case ProviderGate::result_element_init_fingerprint_mismatch:
        return "result_element_init_fingerprint_mismatch";
    case ProviderGate::assign_string_fingerprint_mismatch:
        return "assign_string_fingerprint_mismatch";
    case ProviderGate::construct_error_fingerprint_mismatch:
        return "construct_error_fingerprint_mismatch";
    case ProviderGate::complete_promise_fingerprint_mismatch:
        return "complete_promise_fingerprint_mismatch";
    case ProviderGate::destroy_vector_fingerprint_mismatch:
        return "destroy_vector_fingerprint_mismatch";
    case ProviderGate::destroy_string_fingerprint_mismatch:
        return "destroy_string_fingerprint_mismatch";
    case ProviderGate::assign_future_fingerprint_mismatch:
        return "assign_future_fingerprint_mismatch";
    case ProviderGate::eid_key_fingerprint_mismatch: return "eid_key_fingerprint_mismatch";
    case ProviderGate::find_member_eid_fingerprint_mismatch:
        return "find_member_eid_fingerprint_mismatch";
    case ProviderGate::native_resolution_failed: return "native_resolution_failed";
    case ProviderGate::prepared: return "prepared";
    }
    return "unknown";
}
} // namespace

const char* fixed_stop_entitlement_attempt_name(
    FixedStopEntitlementAttempt attempt) noexcept {
    switch (attempt) {
    case FixedStopEntitlementAttempt::forwarded_unprepared: return "forwarded_unprepared";
    case FixedStopEntitlementAttempt::forwarded_disabled: return "forwarded_disabled";
    case FixedStopEntitlementAttempt::forwarded_malformed_request:
        return "forwarded_malformed_request";
    case FixedStopEntitlementAttempt::forwarded_no_fixed_stop:
        return "forwarded_no_fixed_stop";
    case FixedStopEntitlementAttempt::forwarded_unsupported_batch:
        return "forwarded_unsupported_batch";
    case FixedStopEntitlementAttempt::forwarded_no_group: return "forwarded_no_group";
    case FixedStopEntitlementAttempt::forwarded_malformed_group:
        return "forwarded_malformed_group";
    case FixedStopEntitlementAttempt::forwarded_no_eid: return "forwarded_no_eid";
    case FixedStopEntitlementAttempt::forwarded_allocation_failure:
        return "forwarded_allocation_failure";
    case FixedStopEntitlementAttempt::forwarded_reentrant: return "forwarded_reentrant";
    case FixedStopEntitlementAttempt::completed: return "completed";
    }
    return "unknown";
}

FixedStopEntitlementProviderObservation
fixed_stop_entitlement_provider_observation() {
    PreserveLastError preserve;
    auto& state = fixed_stop_entitlement_provider_state();
    FixedStopEntitlementProviderObservation observation;
    observation.prepared = state.prepared.load(std::memory_order_acquire);
    observation.enabled = state.enabled.load(std::memory_order_acquire);
    observation.attempts = state.attempts.load(std::memory_order_relaxed);
    observation.completions = state.completions.load(std::memory_order_relaxed);
    observation.mixed_completions = state.mixed_completions.load(std::memory_order_relaxed);
    observation.forwarded_unprepared = state.forwarded_unprepared.load(std::memory_order_relaxed);
    observation.forwarded_disabled = state.forwarded_disabled.load(std::memory_order_relaxed);
    observation.forwarded_malformed_request =
        state.forwarded_malformed_request.load(std::memory_order_relaxed);
    observation.forwarded_no_fixed_stop =
        state.forwarded_no_fixed_stop.load(std::memory_order_relaxed);
    observation.forwarded_unsupported_batch =
        state.forwarded_unsupported_batch.load(std::memory_order_relaxed);
    observation.forwarded_no_group = state.forwarded_no_group.load(std::memory_order_relaxed);
    observation.forwarded_malformed_group =
        state.forwarded_malformed_group.load(std::memory_order_relaxed);
    observation.forwarded_no_eid = state.forwarded_no_eid.load(std::memory_order_relaxed);
    observation.forwarded_allocation_failure =
        state.forwarded_allocation_failure.load(std::memory_order_relaxed);
    observation.forwarded_reentrant = state.forwarded_reentrant.load(std::memory_order_relaxed);
    observation.native_exceptions = state.native_exceptions.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(state.latest_mutex);
        observation.latest_requested_count = state.latest_requested_count;
        observation.latest_fixed_stop_count = state.latest_fixed_stop_count;
        observation.latest_eid_count = state.latest_eid_count;
        observation.latest_mixed = state.latest_mixed;
        observation.latest_attempt = state.latest_attempt;
    }
    observation.detail = observation.prepared
        ? "exact-build process-local fixed-stop entitlement provider"
        : gate_name(state.gate.load(std::memory_order_acquire));
    std::ostringstream json;
    json << "{\"event\":\"fixed_stop_entitlement_provider\",\"gate\":\""
         << gate_name(state.gate.load(std::memory_order_acquire))
         << "\",\"prepared\":" << (observation.prepared ? "true" : "false")
         << ",\"enabled\":" << (observation.enabled ? "true" : "false")
         << ",\"attempts\":" << observation.attempts
         << ",\"completions\":" << observation.completions
         << ",\"mixed_completions\":" << observation.mixed_completions
         << ",\"forwarded_unprepared\":" << observation.forwarded_unprepared
         << ",\"forwarded_disabled\":" << observation.forwarded_disabled
         << ",\"forwarded_malformed_request\":"
         << observation.forwarded_malformed_request
         << ",\"forwarded_no_fixed_stop\":" << observation.forwarded_no_fixed_stop
         << ",\"forwarded_unsupported_batch\":" << observation.forwarded_unsupported_batch
         << ",\"forwarded_no_group\":" << observation.forwarded_no_group
         << ",\"forwarded_malformed_group\":"
         << observation.forwarded_malformed_group
         << ",\"forwarded_no_eid\":" << observation.forwarded_no_eid
         << ",\"forwarded_allocation_failure\":"
         << observation.forwarded_allocation_failure
         << ",\"forwarded_reentrant\":" << observation.forwarded_reentrant
         << ",\"native_exceptions\":" << observation.native_exceptions
         << ",\"latest_attempt\":\""
         << fixed_stop_entitlement_attempt_name(observation.latest_attempt)
         << "\",\"latest_requested_count\":" << observation.latest_requested_count
         << ",\"latest_fixed_stop_count\":" << observation.latest_fixed_stop_count
         << ",\"latest_eid_count\":" << observation.latest_eid_count
         << ",\"latest_mixed\":" << (observation.latest_mixed ? "true" : "false")
         << '}';
    observation.json = json.str();
    return observation;
}

} // namespace dingosdk

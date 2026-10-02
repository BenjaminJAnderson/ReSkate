#include "mission_progression_override.h"
#include "mission_progression_internal.h"

#include <mutex>
#include <sstream>

namespace dingosdk {
using namespace mission_progression_detail;
namespace {
const char* gate_name(GateStatus gate) noexcept {
    switch (gate) {
    case GateStatus::not_attempted: return "not_attempted";
    case GateStatus::image_mismatch: return "image_mismatch";
    case GateStatus::lookup_fingerprint_mismatch: return "lookup_fingerprint_mismatch";
    case GateStatus::query_fingerprint_mismatch: return "query_fingerprint_mismatch";
    case GateStatus::callsite_fingerprint_mismatch: return "callsite_fingerprint_mismatch";
    case GateStatus::consumer_fingerprint_mismatch: return "consumer_fingerprint_mismatch";
    case GateStatus::profile_fingerprint_mismatch: return "profile_fingerprint_mismatch";
    case GateStatus::lookup_create_failed: return "lookup_create_failed";
    case GateStatus::query_create_failed: return "query_create_failed";
    case GateStatus::trampoline_missing: return "trampoline_missing";
    case GateStatus::lookup_enable_failed: return "lookup_enable_failed";
    case GateStatus::query_enable_failed: return "query_enable_failed";
    case GateStatus::rollback_failed: return "rollback_failed";
    case GateStatus::preparing: return "preparing";
    case GateStatus::prepared: return "prepared";
    case GateStatus::route_required: return "route_required";
    case GateStatus::restoring: return "restoring";
    case GateStatus::active: return "active";
    }
    return "unknown";
}
} // namespace

MainMissionOverrideObservation main_mission_override_observation() {
    auto& state = main_mission_override_state();
    MainMissionOverrideObservation result;
    const auto gate = state.gate.load(std::memory_order_acquire);
    result.prepared = gate == GateStatus::prepared || gate == GateStatus::route_required ||
        gate == GateStatus::restoring || gate == GateStatus::active;
    result.active = gate == GateStatus::active;
    result.authored_offline_route =
        state.authored_offline_route.load(std::memory_order_acquire);
    result.lookup_calls = state.lookup_calls.load();
    result.allowlisted_lookups = state.allowlisted_lookups.load();
    result.tagged_handles = state.tagged_handles.load();
    result.query_calls = state.query_calls.load();
    result.exact_query_matches = state.exact_query_matches.load();
    result.state_writes = state.state_writes.load();
    result.state_restores = state.state_restores.load();
    result.identity_releases = state.identity_releases.load();
    {
        std::lock_guard lock(state.lease_mutex);
        result.pending_leases = pending_lease_count(state);
        for (std::size_t index = 0; index != state.leases.size(); ++index) {
            if (!state.leases[index].active) continue;
            if (index < main_mission_ids.size()) ++result.claimed_main_mission_count;
            else if (index < main_mission_ids.size() +
                    progression_unlock_quest_ids.size()) {
                ++result.claimed_progression_unlock_count;
            } else {
                ++result.claimed_onboarding_dependency_count;
            }
        }
    }
    result.claimed_override_count = result.claimed_main_mission_count +
        result.claimed_progression_unlock_count +
        result.claimed_onboarding_dependency_count;
    result.pending_restore_count = gate == GateStatus::restoring
        ? result.claimed_override_count : 0;
    result.rejected_matches = state.rejected_matches.load();
    result.native_exceptions = state.native_exceptions.load();
    result.detail = gate_name(gate);
    std::ostringstream json;
    json << "{\"event\":\"main_mission_override_observation\",\"gate\":\""
         << gate_name(gate) << "\",\"prepared\":" << (result.prepared ? "true" : "false")
         << ",\"active\":" << (result.active ? "true" : "false")
         << ",\"authored_offline_route\":"
         << (result.authored_offline_route ? "true" : "false")
         << ",\"mission_count\":" << main_mission_ids.size()
         << ",\"progression_unlock_quest_count\":"
         << progression_unlock_quest_ids.size()
         << ",\"onboarding_dependency_quest_count\":"
         << onboarding_dependency_quest_ids.size()
         << ",\"override_quest_count\":" << override_quest_count
         << ",\"lookup_calls\":" << result.lookup_calls
         << ",\"allowlisted_lookups\":" << result.allowlisted_lookups
         << ",\"tagged_handles\":" << result.tagged_handles
         << ",\"query_calls\":" << result.query_calls
         << ",\"exact_query_matches\":" << result.exact_query_matches
         << ",\"state_writes\":" << result.state_writes
         << ",\"state_restores\":" << result.state_restores
         << ",\"identity_releases\":" << result.identity_releases
         << ",\"pending_leases\":" << result.pending_leases
         << ",\"claimed_override_count\":" << result.claimed_override_count
         << ",\"claimed_main_mission_count\":"
         << result.claimed_main_mission_count
         << ",\"claimed_progression_unlock_count\":"
         << result.claimed_progression_unlock_count
         << ",\"claimed_onboarding_dependency_count\":"
         << result.claimed_onboarding_dependency_count
         << ",\"pending_restore_count\":" << result.pending_restore_count
         << ",\"conditional_restore_skips\":"
         << state.conditional_restore_skips.load()
         << ",\"rejected_matches\":" << result.rejected_matches
         << ",\"native_exceptions\":" << result.native_exceptions
         << ",\"profile_active\":" << (state.profile_lookup.load() ? "true" : "false")
         << ",\"profile_resolve_attempts\":" << state.profile_resolve_attempts.load()
         << ",\"profile_resolve_matches\":" << state.profile_resolve_matches.load()
         << ",\"profile_state_writes\":" << state.profile_state_writes.load()
         << ",\"profile_rejected_resolves\":" << state.profile_rejected_resolves.load()
         << ",\"profile_resolved_quests\":[";
    const auto resolved = state.profile_resolved_mask.load();
    bool comma{};
    for (std::size_t i = 0; i < onboarding_dependency_quest_ids.size(); ++i) {
        if (!(resolved & (1U << i))) continue;
        json << (comma ? ",\"" : "\"") << onboarding_dependency_quest_ids[i] << '"';
        comma = true;
    }
    json << "]}";
    result.json = json.str();
    return result;
}

} // namespace dingosdk

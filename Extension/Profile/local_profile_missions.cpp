#include "Engine/Core/Log/logging.h"
#include "runtime_internal.h"
#include "Extension/Progression/mission_progression_override.h"

namespace dingosdk {
using namespace profile_runtime;
LocalProfileAccess local_profile_access() noexcept {
    PreserveError preserve;
    auto& s = local_runtime();
    if (!s.active.load(std::memory_order_acquire)) return {};
    try {
        return {s.store->bool_option(profile::unlock_neighborhoods_option).value_or(false),
            s.store->bool_option(profile::unlock_preset_slots_option).value_or(false),
            s.store->bool_option(profile::unlock_bus_stops_option).value_or(false),
            s.store->bool_option(profile::max_neighborhood_ranks_option).value_or(false),
            s.store->bool_option(profile::unlock_cosmetics_option).value_or(false)};
    } catch (...) { return {}; }
}

bool local_profile_owns_entitlement(std::string_view id) noexcept {
    PreserveError preserve;
    auto& s = local_runtime();
    if (!s.active.load(std::memory_order_acquire)) return false;
    try { return s.store->entitlement(id).value_or(false); }
    catch (...) { return false; }
}

LocalMissions local_profile_missions() {
    auto& s = local_runtime();
    LocalMissions result;
    if (!s.active.load(std::memory_order_acquire)) return result;
    std::lock_guard lock(s.native_mutex);
    const auto snapshot = s.store->mission_state();
    if (s.mission_source != snapshot) {
        result.available = true;
        const auto append = [&](std::span<const std::string_view> ids, const char* group) {
            for (const auto id : ids) {
                const auto saved = snapshot->quests.find(id);
                result.rows.push_back({std::string(id), group,
                    saved == snapshot->quests.end() ? -1 : saved->second == 5 ? 1 : 0});
            }
        };
        append(current_main_mission_ids(), "Story");
        append(current_onboarding_dependency_quest_ids(), "Onboarding");
        append(current_progression_unlock_quest_ids(), "Districts");
        for (const auto id : profile::onboarding_ids) {
            const auto saved = snapshot->play_events.find(std::string(id) + ":complete");
            result.rows.push_back({std::string(id), "Onboarding",
                saved == snapshot->play_events.end() ? -1 : saved->second.count > 0 ? 1 : 0});
        }
        s.mission_cache = std::move(result);
        s.mission_source = snapshot;
    }
    result = s.mission_cache;
    result.feedback = s.mission_feedback;
    return result;
}

bool set_local_mission_completed(std::string_view id, bool completed) {
    auto& s = local_runtime();
    if (!s.active.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(s.native_mutex);
    try {
        if (is_current_main_mission_id(id) || is_current_onboarding_dependency_quest_id(id) ||
            is_current_progression_unlock_quest_id(id)) {
            s.store->set_quest_state(id, completed ? 5 : 0);
            reset_local_mission_hydration(id);
        }
        else if (std::find(profile::onboarding_ids.begin(), profile::onboarding_ids.end(), id) != profile::onboarding_ids.end())
            s.store->set_onboarding_completed(id, completed);
        else { s.mission_feedback = "Unknown mission; no changes saved."; return false; }
        s.mission_feedback = std::string(id) + (completed ? ": completed." : ": not completed.") +
            " Saved. Reload the level to refresh an active mission.";
        dingosdk::logging::event(dingosdk::logging::Channel::profile, "{\"event\":\"local_profile_mission_saved\"}");
        return true;
    } catch (...) {
        s.mission_feedback = "Save failed. The previous mission state was retained.";
        dingosdk::logging::event(dingosdk::logging::Channel::profile, "{\"event\":\"local_profile_mission_save_failed\"}");
        return false;
    }
}
}

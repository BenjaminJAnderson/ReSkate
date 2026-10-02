#include "local_profile.h"
#include "profile_internal.h"

namespace dingosdk::profile {
using namespace detail;
namespace detail {
void merge_missing(Json& destination, const Json& defaults) {
    for (const auto& [key, value] : defaults.items()) {
        if (!destination.contains(key)) destination[key] = value;
        else if (destination[key].is_object() && value.is_object()) merge_missing(destination[key], value);
    }
}
}

void seed_completed_onboarding(Snapshot& s, const Snapshot& defaults) {
    if (!s.offline_rank_cap) s.offline_rank_cap = defaults.offline_rank_cap;
    if (s.onboarding_seed >= defaults.onboarding_seed) return;
    // Defaults are versioned data. Merge missing keys once per data version;
    // preserve explicit false values, zeroed events, mission resets and ranks.
    for (const auto& [key, value] : defaults.bool_options) s.bool_options.try_emplace(key, value);
    for (const auto& [key, value] : defaults.entitlements) s.entitlements.try_emplace(key, value);
    for (const auto& [key, value] : defaults.quests) s.quests.try_emplace(key, value);
    for (const auto& [key, value] : defaults.play_events) s.play_events.try_emplace(key, value);
    for (const auto& [key, value] : defaults.neighborhood_ranks) s.neighborhood_ranks.try_emplace(key, value);
    merge_missing(s.customization, defaults.customization);
    merge_missing(s.settings, defaults.settings);
    merge_missing(s.extensions, defaults.extensions);
    s.onboarding_seed = defaults.onboarding_seed;
}
}

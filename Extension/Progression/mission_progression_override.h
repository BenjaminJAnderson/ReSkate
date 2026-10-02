#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace dingosdk {

struct MainMissionOverrideObservation {
    bool prepared{};
    bool active{};
    bool authored_offline_route{};
    std::uint64_t lookup_calls{};
    std::uint64_t allowlisted_lookups{};
    std::uint64_t tagged_handles{};
    std::uint64_t query_calls{};
    std::uint64_t exact_query_matches{};
    std::uint64_t state_writes{};
    std::uint64_t state_restores{};
    std::uint64_t identity_releases{};
    std::uint64_t pending_leases{};
    std::uint64_t claimed_override_count{};
    std::uint64_t claimed_main_mission_count{};
    std::uint64_t claimed_progression_unlock_count{};
    std::uint64_t claimed_onboarding_dependency_count{};
    std::uint64_t pending_restore_count{};
    std::uint64_t rejected_matches{};
    std::uint64_t native_exceptions{};
    std::string detail;
    std::string json;
};

// Exact, case-sensitive IDs authored by the current retail build's 20 main
// mission assets. Auxiliary, event, entitlement and unknown quest IDs are not
// included and always retain their native state.
std::span<const std::string_view> current_main_mission_ids() noexcept;
bool is_current_main_mission_id(std::string_view quest_id) noexcept;

// Two installed neighborhood prerequisite quests are kept separate so the UI
// can report 20 story missions and two map/shop unlocks independently.
std::span<const std::string_view> current_progression_unlock_quest_ids() noexcept;
bool is_current_progression_unlock_quest_id(std::string_view quest_id) noexcept;

// Two installed intro quests are direct dependencies of the current rank
// onboarding and Chapter-1 pre-gate graphs. They remain a separate category
// so they are never reported as CH01-CH03 main missions.
std::span<const std::string_view> current_onboarding_dependency_quest_ids() noexcept;
bool is_current_onboarding_dependency_quest_id(std::string_view quest_id) noexcept;

// Installs the two exact-build hooks in forwarding-only mode. Detours hook service must
// already be initialized. Preparation verifies the executable hash, both hook
// prologues, the refresh query/consumer, expression lookup, and query constructor.
bool prepare_main_mission_override(std::uintptr_t image_base) noexcept;

// A durable offline profile can complete or reset individual catalog quests
// independently of the blanket debug override. Resets are applied once per
// freshly resolved identity when a completion saver is installed, allowing
// native progression to continue and its eventual completion to be saved.
using LocalQuestStateLookup = bool (*)(std::string_view, std::int32_t&) noexcept;
using LocalQuestCompletionSave = bool (*)(std::string_view) noexcept;
void set_main_mission_profile_provider(std::uintptr_t image_base, LocalQuestStateLookup,
    LocalQuestCompletionSave save_completion = nullptr) noexcept;
void reset_local_mission_hydration(std::string_view id) noexcept;

// Activation is accepted only after the caller confirms that the authored
// offline quest route is in use. This avoids changing live/AMP quest state.
bool arm_main_mission_override(
    std::uintptr_t image_base, bool authored_offline_route) noexcept;

// Requests forwarding-only restoration. A leased value is restored only when
// the same quest ID, ECS handle and component address are freshly observed
// again at the exact query boundary. Failed memory access remains pending for
// retry; replaced identities are released without sweeping old pointers.
void restore_main_mission_override() noexcept;

MainMissionOverrideObservation main_mission_override_observation();

} // namespace dingosdk

#pragma once
#include "mission_progression_override.h"
#include "Engine/Game/Build/image_identity.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

// Override state shared by the hooks and the observation report.
namespace dingosdk::mission_progression_detail {
using QuestLookup = std::uint64_t* (*)(
    std::uintptr_t, std::uint64_t*, const void*);
using ComponentQuery = std::uint64_t (*)(
    const void*, const void*, void*, std::uint32_t);
using QueryContextConstructor = void* (*)(void*, std::uint64_t);
using ImageVerifier = bool (*)(std::uintptr_t) noexcept;
using CallerClassifier = bool (*)(std::uintptr_t, std::uintptr_t) noexcept;

inline constexpr std::array<std::string_view, 20> main_mission_ids{
    "Base_CH01_M01",
    "Base_CH01_M02",
    "Base_CH01_M02_B",
    "Base_CH01_M03",
    "Base_CH01_M03_B",
    "Base_CH01_M04",
    "Base_CH01_M05",
    "Base_CH02_M01",
    "Base_CH02_M01_B",
    "Base_CH02_M01_C",
    "Base_CH02_M02",
    "Base_CH02_M03",
    "Base_CH02_M03_B",
    "Base_CH02_M04",
    "Base_CH03_M01",
    "Base_CH03_M02",
    "Base_CH03_M03A",
    "Base_CH03_M03B",
    "Base_CH03_M04",
    "Base_CH03_M05",
};

inline constexpr std::array<std::string_view, 2> progression_unlock_quest_ids{
    "Neighborhood_Unlock_Quest_Historica",
    "Neighborhood_Unlock_Quest_Financiala",
};

inline constexpr std::array<std::string_view, 2> onboarding_dependency_quest_ids{
    "Ent_Intro_Q03_A",
    "Ent_Intro_Q03_B",
};

inline constexpr std::size_t override_quest_count =
    main_mission_ids.size() + progression_unlock_quest_ids.size() +
    onboarding_dependency_quest_ids.size();

enum class GateStatus : unsigned char {
    not_attempted,
    image_mismatch,
    lookup_fingerprint_mismatch,
    query_fingerprint_mismatch,
    callsite_fingerprint_mismatch,
    consumer_fingerprint_mismatch,
    profile_fingerprint_mismatch,
    lookup_create_failed,
    query_create_failed,
    trampoline_missing,
    lookup_enable_failed,
    query_enable_failed,
    rollback_failed,
    preparing,
    prepared,
    route_required,
    restoring,
    active,
};

struct QuestStateLease {
    std::uintptr_t component{};
    std::uint64_t handle{};
    std::int32_t original{};
    bool active{};
};

bool exact_status_query_caller(
    std::uintptr_t return_address, std::uintptr_t base) noexcept;

struct MainMissionOverrideState {
    struct ResetIdentity { std::uint64_t handle{}; std::uintptr_t component{}; };
    std::array<ResetIdentity, override_quest_count> profile_resets{};
    std::atomic<LocalQuestCompletionSave> save_completion{};
    std::uintptr_t base{};
    std::atomic<QuestLookup> original_lookup{};
    std::atomic<ComponentQuery> original_query{};
    std::atomic<GateStatus> gate{GateStatus::not_attempted};
    std::atomic<std::uint64_t> generation{1};
    std::atomic<std::uint64_t> lookup_calls{};
    std::atomic<std::uint64_t> allowlisted_lookups{};
    std::atomic<std::uint64_t> tagged_handles{};
    std::atomic<std::uint64_t> query_calls{};
    std::atomic<std::uint64_t> exact_query_matches{};
    std::atomic<std::uint64_t> state_writes{};
    std::atomic<std::uint64_t> state_restores{};
    std::atomic<std::uint64_t> identity_releases{};
    std::atomic<std::uint64_t> rejected_matches{};
    std::atomic<std::uint64_t> native_exceptions{};
    std::atomic<std::uint64_t> conditional_restore_skips{};
    std::mutex initialization_mutex;
    std::mutex lease_mutex;
    std::array<QuestStateLease, override_quest_count> leases{};
    ImageVerifier verify_image{&supported_build::running_image_matches};
    CallerClassifier classify_caller{&exact_status_query_caller};
    bool attempted{};
    std::atomic<bool> authored_offline_route{};
    std::atomic<LocalQuestStateLookup> profile_lookup{};
    QueryContextConstructor construct_query{};
    std::atomic<std::uint64_t> profile_resolve_attempts{}, profile_resolve_matches{},
        profile_state_writes{}, profile_rejected_resolves{};
    std::atomic<unsigned> profile_resolved_mask{};
};

MainMissionOverrideState& main_mission_override_state();
std::size_t pending_lease_count(const MainMissionOverrideState& state) noexcept;
} // namespace dingosdk::mission_progression_detail

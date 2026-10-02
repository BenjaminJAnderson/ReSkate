#pragma once
#include "gameplay_settings_override.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/gameplay_settings.h"
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Settings tables, leases and state shared by the gameplay_settings_*.cpp files.
namespace dingosdk::gameplay_settings_detail {
namespace gameplay = addr::gameplay_settings;
inline constexpr std::size_t feature_group_count = 6;

enum class SettingsType : std::size_t {
    activity,
    advance,
    progression,
    game,
    ui,
    rank,
    count,
};

struct TypeSpec {
    std::uintptr_t handle_rva;
    std::uintptr_t vtable_rva;
    std::size_t object_size;
    const char* name;
};

inline constexpr std::array<TypeSpec, static_cast<std::size_t>(SettingsType::count)> type_specs{{
    {gameplay::activity_settings_handle, gameplay::activity_settings_vtable, 0x98, "DingoActivitySettings"},
    {gameplay::advance_settings_handle, gameplay::advance_settings_vtable, 0x30, "DingoAdvanceSettings"},
    {gameplay::player_progression_settings_handle, gameplay::player_progression_settings_vtable, 0x30,
        "PlayerProgressionSystemSettings"},
    {addr::engine::game_settings_handle, addr::engine::game_settings_vtable, 0x80, "DelMarGameSettings"},
    {gameplay::ui_settings_handle, gameplay::ui_settings_vtable, 0xb8, "DelMarUISettings"},
    {gameplay::progression_settings_handle, gameplay::progression_settings_vtable, 0x28, "DingoProgressionSettings"},
}};

struct FieldSpec {
    overlay::OfflineFeatureGroup group;
    SettingsType type;
    std::uintptr_t metadata_rva;
    std::uint64_t name_hash;
    std::size_t offset;
    const char* name;
};

inline constexpr std::array<FieldSpec, 24> field_specs{{
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::force_all_activities_enabled_member, 0xa6b42b0c, 0x72, "ForceAllActivitiesEnabled"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::use_dev_enabled_activities_member, 0xf6b26249, 0x86, "UseDevEnabledActivities"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::enable_quest_system_member, 0xbf0371a9, 0x73, "EnableQuestSystem"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::enable_action_system_member, 0x75f7696e, 0x83, "EnableActionSystem"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::enable_narrative_member, 0x5beee67f, 0x8c, "EnableNarrative"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::enable_activity_spawned_waypoints_member, 0xe2c57a52, 0x8a, "EnableActivitySpawnedWaypoints"},
    {overlay::OfflineFeatureGroup::fast_travel, SettingsType::advance, gameplay::fast_travel_points_enabled_member, 0x44aaefd6, 0x29, "FastTravelPointsEnabled"},
    {overlay::OfflineFeatureGroup::progression, SettingsType::progression, gameplay::enable_player_progression_member, 0xd7454aeb, 0x28, "EnablePlayerProgression"},
    {overlay::OfflineFeatureGroup::progression, SettingsType::progression, gameplay::enable_graphs_member, 0xdd0aafd4, 0x29, "EnableGraphs"},
    {overlay::OfflineFeatureGroup::developer_menus, SettingsType::ui, gameplay::enable_dev_only_menu_member, 0x65417461, 0xa4, "EnableDevOnlyMenu"},
    {overlay::OfflineFeatureGroup::developer_menus, SettingsType::ui, gameplay::enable_content_browser_ui_member, 0x8558f36c, 0x9e, "EnableContentBrowserUI"},
    {overlay::OfflineFeatureGroup::developer_menus, SettingsType::ui, gameplay::enable_legacy_progression_menu_member, 0x9a5c8c69, 0xb3, "EnableLegacyProgressionMenu"},
    {overlay::OfflineFeatureGroup::developer_menus, SettingsType::ui, gameplay::enable_new_map_member, 0xd4f78d23, 0xae, "EnableNewMap"},
    {overlay::OfflineFeatureGroup::developer_menus, SettingsType::ui, gameplay::enable_old_buildkit_object_browser_member, 0xc7e18264, 0x9a, "EnableOldBuildkitObjectBrowser"},
    // The retail client retains reflected quest-debug and local-workflow
    // switches. These are exact DingoActivitySettings records; the settings
    // transaction does not infer that they synthesize completion or replace a
    // quest-service result.
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::enable_quest_system_debug_member, 0xd64c3afe, 0x7f, "EnableQuestSystemDebug"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::force_use_mock_quest_member, 0xbf233e54, 0x7d, "ForceUseMockQuest"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::show_local_quests_member, 0xac8e489c, 0x75, "ShowLocalQuests"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::auto_claim_quests_member, 0x7fe60124, 0x7e, "AutoClaimQuests"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::disable_quest_order_filtering_member, 0x007642a1, 0x6f, "DisableQuestOrderFiltering"},
    {overlay::OfflineFeatureGroup::activities, SettingsType::activity, gameplay::dev_workflows_enabled_member, 0x63dde121, 0x7c, "DevWorkflowsEnabled"},
    // Rank systems are disabled by default in the retail settings constructor.
    // Enabling them lets installed neighborhood and competitive progression
    // data participate in the authored offline route.
    {overlay::OfflineFeatureGroup::progression, SettingsType::rank, gameplay::enable_neighborhood_rank_member, 0x0567af2b, 0x20, "EnableNeighborhoodRank"},
    {overlay::OfflineFeatureGroup::progression, SettingsType::rank, gameplay::enable_competitive_rank_member, 0x36fe569b, 0x21, "EnableCompetitiveRank"},
    // The authored SkaterObserverStateMachine UpdateBoardWear graph returns
    // immediately unless this byte is set, so the deck never accumulates the
    // six wear morph values. Enabling it only lets the shipped graph run.
    {overlay::OfflineFeatureGroup::board_wear, SettingsType::advance, gameplay::board_wear_enabled_member, 0x6397e070, 0x21, "BoardWearEnabled"},
    // Its only reader is the settings predicate that hides the game's own
    // "Enable Party Collision" option (profile key EnablePartyCollision).
    {overlay::OfflineFeatureGroup::player_collision, SettingsType::advance, gameplay::player_vs_player_collision_enabled_member, 0x1638d4cf, 0x24, "PlayerVsPlayerCollisionEnabled"},
    // This build removed EnableStoreV2; no replacement switch is assumed.
}};

struct Lease {
    bool owned{};
    std::uintptr_t object{};
    std::uint8_t original{};
    std::uint8_t applied{};
};

struct TypeResolution {
    bool attempted{};
    bool available{};
    std::uintptr_t object{};
};

struct FieldSnapshot {
    std::size_t index{};
    std::uintptr_t object{};
    std::uint8_t value{};
};

struct SettingsGuard {
    std::string message;
};

struct SettingsState {
    std::mutex mutex;
    std::uintptr_t base{};
    DWORD engine_thread{};
    bool attempted{};
    bool active{};
    bool registry_ready{};
    std::array<bool, feature_group_count> requested{};
    std::array<Lease, field_specs.size()> leases{};
    overlay::OfflineFeatureModel model;
    std::string detail;
    std::uint64_t refreshes{};
    std::uint64_t requests{};
    std::uint64_t rejected{};
    std::uint64_t lookups{};
    std::uint64_t writes{};
    std::uint64_t restores{};
    std::uint64_t abandoned{};
};

SettingsState& settings_state();

inline std::size_t group_index(overlay::OfflineFeatureGroup group) {
    return static_cast<std::size_t>(group);
}

inline void settings_require(bool condition, std::string message) {
    if (!condition) throw SettingsGuard{std::move(message)};
}

// gameplay_settings_leases.cpp
FieldSnapshot snapshot_field(SettingsState& state, std::size_t index,
    std::array<TypeResolution, static_cast<std::size_t>(SettingsType::count)>& cache);
bool release_replaced_lease(SettingsState& state, const FieldSnapshot& snapshot);
void enable_group(SettingsState& state, overlay::OfflineFeatureGroup group);
void restore_group(SettingsState& state, overlay::OfflineFeatureGroup group);
void restore_variable(SettingsState& state, std::size_t index);
void set_variable(SettingsState& state, std::size_t index, bool enabled);

// gameplay_settings_model.cpp
void reset_variable_model(SettingsState& state);
void refresh_model(SettingsState& state);
GameplaySettingsOverrideObservation observation_locked(const SettingsState& state);
} // namespace dingosdk::gameplay_settings_detail

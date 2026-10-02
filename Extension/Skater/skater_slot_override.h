#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {
enum class SkaterSlotOverrideAction {
    tick,
    enable,
    restore,
};

struct SkaterSlotOverrideObservation {
    bool initialized{};
    bool available{};
    bool requested{};
    bool settings_owned{};
    bool manager_available{};
    bool selectors_enabled{};
    std::uint32_t slot_count{};
    std::uint32_t manager_state{};
    bool ui_ready{};
    std::int32_t selected_slot{-1};
    std::uint64_t initial_selections{};
    bool initial_binding_ready{}, initial_appearance_ready{};
    std::uint32_t initial_category_count{};
    std::uint32_t target_slot_count{10};
    std::uint64_t writes{};
    std::uint64_t restores{};
    std::uint64_t native_resizes{};
    std::uint64_t rejected{};
    std::string status;
    std::string detail;
    std::string json;
};

// Valid only for the inspected Skate.exe build and the authored-offline route.
// Initialization validates immutable PE, function and reflection identities;
// it performs no registry lookup, write or native call.
bool initialize_skater_slot_override(
    std::uintptr_t image_base, bool authored_offline_route_active) noexcept;

// Call from one game update thread. Once enabled, tick reapplies the reversible
// settings leases and asks the game's own loadout manager to restore ten slots
// if an inventory/profile response reduces the process-local count.
SkaterSlotOverrideObservation update_skater_slot_override(
    SkaterSlotOverrideAction action = SkaterSlotOverrideAction::tick,
    std::uint32_t initial_preset = 0) noexcept;

SkaterSlotOverrideObservation skater_slot_override_observation();
}

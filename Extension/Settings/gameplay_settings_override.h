#pragma once

#include "Extension/UI/Overlay/overlay.h"

#include <cstdint>
#include <string>

namespace dingosdk {
struct EngineVariableDefinition { std::string name, qualified_name, description; };
std::vector<EngineVariableDefinition> gameplay_engine_variable_definitions();
enum class EngineVariableAction {
    set,
    restore,
};

// Only exact names published in OfflineFeatureModel::variables are accepted.
// Name matching is case-insensitive ASCII; addresses and unvalidated fields
// cannot be supplied through this request.
struct EngineVariableRequest {
    std::string name;
    EngineVariableAction action = EngineVariableAction::set;
    bool value = false;
};

struct GameplaySettingsOverrideObservation {
    overlay::OfflineFeatureModel model;
    std::string json;
    std::string detail;
};

// This control is valid only after the exact-build authored-offline route and
// its dependent progression guard are both active. Initialization validates
// the inspected settings lookup and reflection records but makes no native
// calls and performs no writes.
bool initialize_gameplay_settings_override(
    std::uintptr_t image_base, bool authored_offline_route_active) noexcept;

// Call from the game's update thread. A null request refreshes the effective
// state. Requests capture original bytes and can restore only bytes still
// owned by this module on the same freshly resolved settings object.
GameplaySettingsOverrideObservation update_gameplay_settings_override(
    const overlay::OfflineFeatureRequest* request = nullptr) noexcept;

// Call from the same recorded game update thread as the group API. A set owns
// one reversible byte lease; restore releases only that exact variable's lease.
GameplaySettingsOverrideObservation update_gameplay_engine_variable(
    const EngineVariableRequest& request) noexcept;

GameplaySettingsOverrideObservation gameplay_settings_override_observation();
}

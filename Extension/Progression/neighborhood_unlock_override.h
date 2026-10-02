#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {

struct NeighborhoodUnlockOverrideObservation {
    bool prepared{};
    bool enabled{};
    std::uint64_t invocations{};
    std::uint64_t target_matches{};
    std::uint64_t overrides{};
    std::string json;
    std::string detail;
};

// Installs an exact-build hook on the common expression argument binder in a
// forwarding-only state. Detours hook service must already be initialized.
bool prepare_neighborhood_unlock_override(std::uintptr_t image_base) noexcept;

// Read-only diagnostic observer shared by cached and one-shot expression calls.
using ExpressionBinderObserver = void (*)(std::uintptr_t, std::uintptr_t, void**);
void set_expression_binder_observer(ExpressionBinderObserver observer) noexcept;

// Enables or restores the process-local override after preparation. Restore
// keeps the hook installed; detour entries that begin after it returns forward
// native inputs. An invocation already in flight may finish its prior decision.
bool set_neighborhood_unlock_override_enabled(bool enabled) noexcept;

NeighborhoodUnlockOverrideObservation neighborhood_unlock_override_observation();

} // namespace dingosdk

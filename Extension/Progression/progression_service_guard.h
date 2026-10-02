#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {

struct ProgressionServiceGuardObservation {
    std::string json;
    std::string detail;
};

// Installs the exact-build post-spawn hook in a forwarding-only state. This
// must succeed before the global offline experiment can activate.
bool prepare_progression_service_guard(std::uintptr_t image_base) noexcept;
// Arms the already-prepared hook after global offline activation succeeds.
// Only then does it skip a null service pointer from the inspected caller;
// every other invocation is forwarded unchanged.
bool arm_progression_service_guard(std::uintptr_t image_base) noexcept;
ProgressionServiceGuardObservation progression_service_guard_observation();

} // namespace dingosdk

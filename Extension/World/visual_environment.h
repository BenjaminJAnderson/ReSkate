#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
// Observes parent ownership for fog/sky/wind components. Lighting selection is
// owned by world layers; this observer never writes visibility or day/night flags.
struct VisualEnvironment {
    std::uintptr_t entity{}, owner{}, data{}, reference{}, blueprint{};
    WorldMap map{WorldMap::none};
    std::string name;
};
struct VisualEnvironments {
    using Construct = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t);
    using Destroy = std::uintptr_t (*)(std::uintptr_t, unsigned);
    Construct construct{};
    Destroy destroy{};
    std::atomic<bool> active{};
    // Acquire parent lifetime before component locks; erase before destruction.
    std::recursive_mutex mutex;
    std::map<std::uintptr_t, VisualEnvironment> nodes;
};
VisualEnvironments& visual_environments();
bool visual_environment_identity(VisualEnvironment& node, bool capture);
void start_visual_environments() noexcept;
}

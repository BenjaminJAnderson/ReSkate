#pragma once
#include "Engine/Core/Log/logging.h"
#include "Extension/Profile/runtime_internal.h"
#include "local_world_controls.h"

namespace dingosdk::profile_runtime {
struct PopulationLease { float original{}, applied{}; bool owned{}; };

struct PopulationConfig { std::array<PopulationLease, 5> fields{}; };

struct PopulationNode {
    std::uintptr_t config{};
    int realm{};
    std::array<bool, 2> original{}, owned{};
};

struct PopulationRuntime {
    using Construct = std::uintptr_t (*)(std::uintptr_t, int, std::uintptr_t);
    using Destroy = std::uintptr_t (*)(std::uintptr_t, unsigned);
    using Tick = void (*)(std::uintptr_t);
    using Enable = void (*)(bool, int);
    Construct construct{}; Destroy destroy{};
    std::array<Tick, 2> tick{};
    std::array<Enable, 2> enable{};
    std::map<std::uintptr_t, PopulationNode> nodes;
    std::map<std::uintptr_t, PopulationConfig> configs;
    std::array<std::uint64_t, 2> observed_at{};
};

PopulationRuntime& population_runtime();

inline constexpr std::array<unsigned, 5> population_offsets{0x70,0x58,0x54,0x5c,0x64};

bool population_identity(std::uintptr_t manager, int realm);

bool population_config_identity(std::uintptr_t config);

bool population_value(std::uintptr_t config, unsigned i, float& out);

bool population_write(std::uintptr_t config, unsigned i, float value);

void population_release_config(std::uintptr_t manager);

std::uintptr_t population_construct(std::uintptr_t manager, int realm, std::uintptr_t allocator);

std::uintptr_t population_destroy(std::uintptr_t manager, unsigned flags);

int population_choice(unsigned kind);

template<unsigned Kind> void population_enable(bool enabled, int realm) {
    auto& r = world_control_runtime(); auto& p = population_runtime();
    std::lock_guard lock(r.mutex);
    const auto choice = population_choice(Kind);
    for (auto& [_, node] : p.nodes) if (node.realm == realm) {
        node.original[Kind] = enabled; node.owned[Kind] = choice >= 0;
    }
    // The server must keep ticking at density zero to remove existing actors.
    // Client visibility can turn off immediately.
    p.enable[Kind](choice < 0 ? enabled : realm == 1 || choice != 0, realm);
}

template<unsigned Kind> void population_tick(std::uintptr_t manager) {
    auto& r = world_control_runtime(); auto& p = population_runtime();
    {
        PreserveError preserve;
        std::lock_guard lock(r.mutex);
        try {
            const auto found = p.nodes.find(manager);
            if (found != p.nodes.end() && population_identity(manager, found->second.realm)) {
                auto& node = found->second; std::uintptr_t config{}; std::uint8_t running{};
                if (memory::peek(manager + 0x108, config) && memory::peek(manager + 0x152, running) && running == 1) {
                    config &= ~std::uintptr_t{4};
                    if (config != node.config) { population_release_config(manager); node.config = config; }
                    if (population_config_identity(config)) {
                        auto& saved = p.configs[config]; const auto choice = population_choice(Kind);
                        bool ok = true;
                        for (unsigned i = Kind == 0 ? 0 : 3; i < (Kind == 0 ? 3u : 5u); ++i) {
                            float current{}; auto& f = saved.fields[i];
                            if (!population_value(config, i, current)) { ok = false; continue; }
                            if (f.owned && current != f.applied) f.owned = false;
                            if (!f.owned) f.original = current;
                            if (choice < 0 && !f.owned) continue;
                            constexpr std::array<float, 4> factors{0,.5f,1,2};
                            const float desired = choice < 0 ? f.original :
                                std::min((i == 0 || i == 3) ? 100.0f : 1000.0f, f.original * factors[choice]);
                            const float rounded = i == 0 || i == 3 ? desired : std::floor(desired);
                            if (current != rounded && !population_write(config, i, rounded)) { ok = false; continue; }
                            f.applied = rounded; f.owned = choice >= 0;
                        }
                        p.observed_at[Kind] = GetTickCount64(); r.model.population_ready[Kind] = ok;
                        std::uintptr_t begin{}, end{};
                        const unsigned offset = Kind == 0 ? 0xa8 : 0xe8;
                        if (memory::peek(manager + offset, begin) && memory::peek(manager + offset + 8, end) && end >= begin && end - begin <= 8000)
                            r.model.population[Kind] = static_cast<unsigned>((end - begin) / 8);
                    }
                }
            }
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::world, "{\"event\":\"world_population_update_failed\"}"); }
    }
    p.tick[Kind](manager);
}

void update_population_controls();
}

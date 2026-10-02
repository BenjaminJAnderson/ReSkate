#include "Engine/Core/Log/logging.h"
#include "visual_environment.h"
#include "local_world_layers.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/visual_environment.h"

namespace dingosdk::profile_runtime {
using namespace addr::visual_environment;
namespace {
bool visual_environment_name(std::string_view name, VisualEnvironment& node) {
    for (const auto map : {WorldMap::bam, WorldMap::grom, WorldMap::mpr}) {
        const std::string_view prefix = map == WorldMap::bam ? "Lighting/VE/TOD/BAM/ve_bam_" :
            map == WorldMap::grom ? "Lighting/VE/TOD/Grom/ve_grom_" : "Lighting/VE/TOD/Megapark/ve_mpr_";
        if (name.size() <= prefix.size() || !seasonal_name_equal(name.substr(0, prefix.size()), prefix)) continue;
        node.map = map;
        return true;
    }
    return false;
}
}
VisualEnvironments& visual_environments() { static auto* r = new VisualEnvironments; return *r; }

bool visual_environment_identity(VisualEnvironment& node, bool capture) {
    const auto base = local_runtime().base;
    std::uintptr_t vt{}, owner{}, data{}, reference{}, type{}, blueprint{}, object{};
    std::uint32_t flags{};
    std::string name;
    if (!read(node.entity, vt) || vt != base + entity_vtable ||
        !read(node.entity + 0x40, owner) || !owner || !read(node.entity + 0x48, data) ||
        !read(data + 8, type) || type != base + entity_data_type ||
        !read(owner + 0x44, flags)) return false;
    // Same tagged reference-data selection as the native reader.
    if (flags & (1u << 27)) {
        if (!read(owner + 0x48, reference)) return false;
        reference &= ~std::uintptr_t{4};
    } else {
        if (!read(owner + 0x70, reference)) return false;
        reference &= ~std::uintptr_t{3};
    }
    if (!read(reference + 8, type) || type != base + reference_data_type ||
        !read(reference + 0x70, blueprint) || !read(blueprint + 0x68, object) || object != data ||
        !identifier(reinterpret_cast<const void*>(blueprint + 0x18), name)) return false;
    if (!capture) return node.owner == owner && node.data == data && node.reference == reference &&
        node.blueprint == blueprint && node.name == name;
    if (!visual_environment_name(name, node)) return false;
    node.owner = owner; node.data = data; node.reference = reference; node.blueprint = blueprint;
    node.name = std::move(name);
    return true;
}

std::uintptr_t visual_environment_construct(std::uintptr_t entity, std::uintptr_t info, std::uintptr_t data) {
    auto& r = visual_environments();
    const auto result = r.construct(entity, info, data);
    if (r.active.load(std::memory_order_acquire)) {
        PreserveError preserve;
        try {
            VisualEnvironment node; node.entity = entity;
            if (visual_environment_identity(node, true)) {
                std::lock_guard lock(r.mutex);
                if (r.nodes.size() < 96) {
                    r.nodes[entity] = node;
                    dingosdk::logging::event(dingosdk::logging::Channel::world, dingosdk::Json{{"event", "visual_environment_observed"},
                        {"asset", node.name}, {"entity", entity}}.dump().c_str());
                }
            }
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::world, "{\"event\":\"visual_environment_capture_failed\"}"); }
    }
    return result;
}

std::uintptr_t visual_environment_destroy(std::uintptr_t entity, unsigned flags) {
    auto& r = visual_environments();
    { PreserveError preserve; std::lock_guard lock(r.mutex); r.nodes.erase(entity); }
    return r.destroy(entity, flags);
}

void start_visual_environments() noexcept {
    auto& environments = visual_environments();
    if (environments.active.load(std::memory_order_acquire)) return;
    const auto base = local_runtime().base;
    std::vector<void*> created;
    try {
        created.reserve(visual_environment_sites.size());
        for (const auto& site : visual_environment_sites) {
            std::array<unsigned char, 32> bytes{};
            if (!read(base + site.rva, bytes) || bytes != site.bytes)
                throw std::runtime_error("Visual environment fingerprint mismatch");
        }
        const auto hook = [&](unsigned i, auto replacement, auto& original) {
            auto* target = reinterpret_cast<void*>(base + visual_environment_sites[i].rva);
            if (hook_prepare(target, reinterpret_cast<void*>(replacement), reinterpret_cast<void**>(&original)) != HookOk)
                throw std::runtime_error("Cannot prepare visual environment observer");
            created.push_back(target);
        };
        hook(0, &visual_environment_construct, environments.construct);
        hook(1, &visual_environment_destroy, environments.destroy);
        for (auto* target : created)
            if (hook_enable(target) != HookOk) throw std::runtime_error("Cannot enable visual environment observer");
        environments.active.store(true, std::memory_order_release);
        dingosdk::logging::event(dingosdk::logging::Channel::world, "{\"event\":\"visual_environment_observer_initialized\",\"active\":true}");
    } catch (...) {
        for (auto* target : created) hook_remove(target);
        dingosdk::logging::event(dingosdk::logging::Channel::world, "{\"event\":\"visual_environment_observer_initialized\",\"active\":false}");
    }
}
}

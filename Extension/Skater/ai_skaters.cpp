#include "ai_skaters.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Extension/Multiplayer/Remote/native_pose_layout.h"
#include "Extension/Multiplayer/Remote/native_skater.h"
#include "Engine/Game/Abi/native_data.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/ai_skaters.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/skater_entities.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <intrin.h>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace dingosdk::ai_skaters {
namespace {
namespace ai = addr::ai_skaters;
namespace entities = addr::skater_entities;
constexpr std::uintptr_t highest = memory::highest_user_address;
constexpr std::string_view player_blueprint = "Gameplay/Characters/CharacterBlueprint_Physics_Skater_RSP_CAS";
constexpr std::string_view ai_blueprint = "Gameplay/Characters/CharacterBlueprint_Physics_Skater_AI";
constexpr std::size_t max_skaters = 16;
// Milliseconds after creation at which each skater's native state is logged.
constexpr std::array<ULONGLONG, 6> sample_ms{500, 1000, 2000, 4000, 8000, 15000};

struct Unavailable : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void require(bool value, const std::string &reason) {
    if (!value)
        throw Unavailable(reason);
}
bool readable(std::uintptr_t address, void *out, std::size_t size) noexcept {
    if (address < 0x10000 || size > highest || address > highest - size)
        return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool write(std::uintptr_t address, const void *data, std::size_t size) noexcept {
    if (address < 0x10000 || size > highest || address > highest - size)
        return false;
    __try {
        std::memcpy(reinterpret_cast<void *>(address), data, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T> T read(std::uintptr_t object, std::uintptr_t offset = 0) {
    T value{};
    // The message is formatted only on failure, not for every successful read.
    if (!(object >= 0x10000 && object <= highest && offset <= highest - object &&
          readable(object + offset, &value, sizeof(value))))
        throw Unavailable(std::format("AI skater read failed at {:#x}+{:#x}.", object, offset));
    return value;
}
std::uintptr_t ptr(std::uintptr_t object, std::uintptr_t offset = 0) {
    return read<std::uintptr_t>(object, offset);
}
bool code(std::uintptr_t base, std::uintptr_t address) {
    return address >= base && address < base + supported_build::game_image_size;
}
template <std::size_t N>
void prefix(std::uintptr_t base, std::uintptr_t rva, const std::array<std::uint8_t, N> &expected) {
    std::array<std::uint8_t, 32> bytes{};
    require(expected.size() <= bytes.size() && readable(base + rva, bytes.data(), expected.size()) &&
                std::equal(expected.begin(), expected.end(), bytes.begin()),
            std::format("AI skater native function {:#x} fingerprint changed.", rva));
}
std::string name_at(std::uintptr_t address) {
    std::string result;
    for (std::size_t i = 0; i < 256; ++i) {
        char c{};
        require(readable(address + i, &c, 1), "Asset name unreadable.");
        if (!c)
            return result;
        result += c;
    }
    throw Unavailable("Asset name exceeds 255 bytes.");
}
bool same_name(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}
bool derives(std::uintptr_t object, std::uintptr_t expected) {
    auto type = ptr(object, 8);
    for (int depth = 0; depth < 16 && type; ++depth) {
        if (type == expected)
            return true;
        type = ptr(type, 0x20);
    }
    return false;
}

using Matrix = std::array<float, 16>;
Matrix root_matrix(std::uintptr_t entity) {
    const auto collection = ptr(entity, 0x70);
    require(ptr(collection) == entity, "Skater transform owner differs.");
    const auto first = read<std::uint8_t>(collection, 9), extra = read<std::uint8_t>(collection, 10);
    require(first <= 128 && extra <= 32, "Skater transform layout exceeds bounds.");
    return read<Matrix>(collection, 0x10 + (std::uintptr_t{first} + 2 * std::uintptr_t{extra}) * 0x20);
}
std::string position_text(const Matrix &m) {
    return std::format("({:.2f}, {:.2f}, {:.2f})", m[12], m[13], m[14]);
}

struct Local {
    std::uintptr_t context{}, player{}, entity{};
    Matrix root{};
};
Local local_skater(std::uintptr_t base, std::uintptr_t client) {
    require(ptr(client) == base + addr::engine::client_vtable, "Local client unavailable.");
    require(read<std::uint32_t>(client, 0xc0) <= 1, "AI skaters need a local single-player or Hosted map.");
    Local local;
    local.context = ptr(client, 8);
    const auto offset = read<std::uint32_t>(base, addr::engine::context_player_manager_offset);
    require(offset <= 0x1000000, "Player manager offset changed.");
    const auto manager = ptr(local.context, offset);
    require(ptr(manager) == base + addr::engine::local_player_manager_vtable, "Local player manager unavailable.");
    const auto begin = ptr(manager, 0x4c8), end = ptr(manager, 0x4d0);
    require(end >= begin && end - begin == 8, "Expected one local player.");
    local.player = ptr(begin);
    require(ptr(local.player) == base + addr::engine::local_player_vtable &&
                read<std::uint8_t>(local.player, 0x45) == 1 &&
                !read<std::uint8_t>(local.player, 0x44),
            "Local human player unavailable.");
    local.entity = ptr(local.player, 0xb8);
    require(local.entity && ptr(local.entity) == base + addr::engine::skater_entity_vtable &&
                ptr(local.entity, 0xf8) == local.player &&
                ptr(local.entity, 0x20) == local.context,
            "Local skater is not spawned.");
    local.root = root_matrix(local.entity);
    return local;
}

// Client skater sources (engine::client_skater_source_vtable). Spawn creates +0x68
// through client_source_spawn::source_create from data+0xd0; the local player's source
// provides the parent bus.
struct Source {
    std::uintptr_t address{}, data{}, parent{}, blueprint{}, entity{};
    std::string name;
};
std::vector<Source> read_sources(std::uintptr_t base, std::uintptr_t context) {
    const auto partition_offset = read<std::uint32_t>(base, addr::engine::context_partition_offset);
    require(partition_offset <= 0x1000000, "Source partition offset changed.");
    const auto partition = read<std::uint32_t>(context, partition_offset);
    require(partition <= 4, "Source partition unavailable.");
    std::vector<Source> result;
    for (unsigned kind = 0; kind < 7; ++kind) {
        const auto manager = ptr(base, addr::engine::source_managers + (std::uintptr_t{partition} * 7 + kind) * 8);
        if (!manager)
            continue;
        require(ptr(manager) == base + addr::engine::source_manager_vtable && ptr(manager, 0x18) == context,
                "Source manager differs.");
        const auto begin = ptr(manager, 0xa0), end = ptr(manager, 0xa8);
        require(begin <= end && (end - begin) % 0x18 == 0 && (end - begin) / 0x18 <= 64,
                "Source collection exceeds bounds.");
        for (auto at = begin; at < end && result.size() < 64; at += 0x18) {
            const auto address = ptr(at, 8);
            if (!address || ptr(address) != base + addr::engine::client_skater_source_vtable)
                continue;
            const auto data = ptr(address, 0x38), parent = ptr(address, 0x30);
            if (!data || !parent || ptr(address, 0x20) != context || ptr(parent, 0x20) != context)
                continue;
            const auto blueprint = ptr(data, 0xd0) & ~std::uintptr_t{4};
            if (!blueprint)
                continue;
            result.push_back(
                {address, data, parent, blueprint, ptr(address, 0x68), name_at(ptr(blueprint, 0x18))});
        }
    }
    return result;
}

// Loaded named assets are registered per asset domain. Search the player
// blueprint's domain and its parents, as the native lookup (ai::asset_domain_lookup) does.
std::uintptr_t find_blueprint(std::uintptr_t base, std::uintptr_t anchor, std::string_view name) {
    const auto find = game::native_data().find_asset;
    require(find != nullptr, "Native asset lookup is unavailable.");
    const std::string text(name);
    auto domain = read<std::uint16_t>(anchor, 0x16);
    std::set<std::uint16_t> visited;
    while (domain < 0xbbf && visited.size() < 32 && visited.insert(domain).second) {
        const auto owner = ptr(base, addr::engine::domain_owners + domain * std::uintptr_t{8});
        if (!owner)
            return 0;
        if (const auto asset = find(domain, text.c_str())) {
            require(derives(asset, base + entities::blueprint_type), "Named asset is not a blueprint.");
            require(same_name(name_at(ptr(asset, 0x18)), name), "Named blueprint differs.");
            return asset;
        }
        domain = read<std::uint16_t>(owner, 0x42);
    }
    return 0;
}

struct Skater {
    unsigned id{};
    std::uintptr_t entity{}, component{}, parent{}, context{};
    Matrix spawn{};
    ULONGLONG created{};
    std::size_t next_sample{};
};
struct Request {
    enum class Kind { spawn, clear, report, brain } kind{};
    unsigned count{};
    bool physics{}, brain{};
};
struct State {
    std::mutex mutex;
    std::vector<Skater> skaters;
    std::vector<Request> requests;
    std::uintptr_t context{};
    unsigned next_id = 1;
    bool listening{};
    // Read by the destruction hook without the mutex; updated under it.
    std::atomic<std::size_t> tracked{};
    void publish() noexcept { tracked.store(skaters.size(), std::memory_order_release); }
};
State &state() {
    static auto *s = new State;
    return *s;
}
void entity_destroyed(std::uintptr_t entity) noexcept {
    auto &s = state();
    if (!s.tracked.load(std::memory_order_acquire))
        return; // Every native entity destruction passes through here.
    std::lock_guard lock(s.mutex);
    const auto found = std::find_if(s.skaters.begin(), s.skaters.end(),
                                    [&](const Skater &skater) { return skater.entity == entity; });
    if (found == s.skaters.end())
        return;
    logging::log(logging::Level::info, logging::Channel::skater, "AI skater #{} was destroyed by the engine.",
                 found->id);
    s.skaters.erase(found);
    s.publish();
}

std::uintptr_t create_actor(std::uintptr_t base, std::uintptr_t parent, std::uintptr_t blueprint) {
    // Mirrors the source creator (client_source_spawn::source_create). Each skater gets its own child bus:
    // the entity constructor binds properties such as +0x578 on that bus, and
    // the shared parent bus leaves them null (a crash once physics runs).
    using CreateBus = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                                         std::uint8_t, std::uintptr_t);
    const auto bus =
        reinterpret_cast<CreateBus>(base + ai::create_bus)(ptr(ptr(parent, 8), 0x48), parent, 0, 0, 0, 0);
    require(bus != 0, "Engine did not create an AI skater entity bus.");
    reinterpret_cast<int (*)(std::uintptr_t)>(base + ai::acquire_bus)(bus);
    struct BusScope {
        std::uintptr_t base, bus;
        ~BusScope() { reinterpret_cast<void (*)(std::uintptr_t)>(base + entities::release_reference)(bus); }
    } bus_scope{base, bus};
    // Local id 255 (no player association), no creation list and an identity
    // creation transform; placement applies the real one. The descriptor owns
    // internal list state and must be constructed and destroyed in place.
    alignas(16) std::array<std::uint8_t, 0x190> descriptor{};
    alignas(16) constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    using Init = void *(*)(void *, std::uintptr_t, std::uintptr_t, const void *);
    reinterpret_cast<Init>(base + entities::descriptor_init)(descriptor.data(), 0, bus, identity.data());
    struct DescriptorScope {
        std::uintptr_t base;
        void *data;
        ~DescriptorScope() {
            reinterpret_cast<void (*)(void *)>(base + entities::descriptor_destroy)(
                static_cast<std::uint8_t *>(data) + 0x10);
        }
    } scope{base, descriptor.data()};
    const std::uint32_t id = 255;
    const std::uintptr_t no_list = 0;
    std::memcpy(descriptor.data() + 0x38, &id, sizeof(id));
    descriptor[0x151] = 0;
    std::memcpy(descriptor.data() + 0x158, &no_list, sizeof(no_list));
    std::array<std::uintptr_t, 3> result{};
    using Create = void *(*)(void *, void *, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    reinterpret_cast<Create>(base + entities::create_entity)(result.data(), descriptor.data(), blueprint, 0, 0);
    if (result[1])
        reinterpret_cast<void (*)(std::uintptr_t)>(base + entities::release_reference)(result[1]);
    return result[0];
}
// Mode 1 is the default appearance, as requested by entities::source_apply_appearance when the source
// data has no recipe; the multiplayer remote actors request the same mode.
void initialize_appearance(std::uintptr_t base, std::uintptr_t entity, std::uintptr_t local_entity) {
    const auto getter = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(base + entities::customization_component);
    const auto customization = getter(entity), local_customization = getter(local_entity);
    require(customization && local_customization && customization != local_customization &&
                ptr(customization) == ptr(local_customization) && code(base, ptr(customization)) &&
                ptr(ptr(customization, 0x18)) == entity,
            "AI customization component unavailable.");
    const std::uint8_t enabled = 1;
    const std::uint32_t mode = 1;
    require(write(customization + 0x110, &enabled, 1), "Cannot enable AI customization.");
    if (ptr(customization, 0x120))
        reinterpret_cast<void (*)(std::uintptr_t, const void *, std::uint32_t)>(
            base + addr::engine::set_customization_flag)(customization + 0x120, &enabled, 1);
    require(write(customization + 0x134, &mode, 4) && write(customization + 0x10a, &enabled, 1) &&
                write(customization + 0x10d, &enabled, 1),
            "Cannot request AI appearance.");
}
bool owned(std::uintptr_t base, const Skater &skater) {
    return ptr(skater.entity) == base + addr::engine::skater_entity_vtable &&
           ptr(skater.entity, 0x20) == skater.context &&
           ptr(skater.entity, 0x40) == skater.parent && !ptr(skater.entity, 0xf8);
}
void destroy(std::uintptr_t base, const Skater &skater) {
    // The shared destruction hook reports this entity to entity_destroyed().
    if (owned(base, skater))
        reinterpret_cast<void (*)(std::uintptr_t, std::uintptr_t)>(base + entities::destroy_entity)(
            skater.entity, skater.parent);
}
void forget(std::uintptr_t entity) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    std::erase_if(s.skaters, [&](const Skater &skater) { return skater.entity == entity; });
    s.publish();
}

void verify_native(std::uintptr_t base) {
    // install_entity_hooks has verified the creation, placement, appearance,
    // physics-flag (entities::physics_c7_setter) and destruction functions used here.
    prefix(base, ai::physics_source_setter, ai::physics_source_setter_prefix);
    prefix(base, entities::physics_cb_setter, ai::physics_cb_setter_prefix);
    prefix(base, ai::create_bus, ai::create_bus_prefix);
    prefix(base, ai::acquire_bus, ai::acquire_bus_prefix);
    prefix(base, ai::set_property_binding, ai::set_property_binding_prefix);
    // Skater entity +0x458 (ai::binding_reader) reads the +0x578 binding unchecked once
    // the skater's physics exists.
    prefix(base, ai::binding_read_site, ai::binding_read_site_prefix);
    require(ptr(base + addr::engine::skater_entity_vtable, 0x158) == base + entities::place_entity &&
                ptr(base + addr::engine::skater_entity_vtable, 0x458) == base + ai::binding_reader,
            "Skater entity interface differs.");
    // Brain tree and skater AI component updates gated by the run flags.
    require(ptr(base + ai::brain_tree_vtable, 0x138) == base + ai::brain_tree_update &&
                ptr(base + ai::skater_ai_vtable, 0x138) == base + ai::skater_ai_update,
            "AI component interfaces differ.");
    prefix(base, ai::skater_ai_update, ai::skater_ai_update_prefix);
}
void require_job_context(std::uintptr_t base, std::uintptr_t context) {
    require(read<std::uint8_t>(base, addr::engine::entity_creation_ready) != 0,
            "Native entity creation is not ready.");
    const auto tls_array = static_cast<std::uintptr_t>(__readgsqword(0x58));
    const auto tls_index = read<std::uint32_t>(base, addr::engine::tls_index);
    require(tls_index <= 4095, "Native TLS index changed.");
    const auto tls = ptr(tls_array, std::uintptr_t{tls_index} * 8);
    require(read<std::uint8_t>(tls, 0xb19) != 0 && ptr(tls, 0x550) == context,
            "Waiting for the native client job context.");
}

// EATBrainTree component, collection slot = its ClientIndex (12) in the player
// blueprint. Factory ai::brain_tree_factory creates the bt_default instance (+0x60) at once
// through holder +0x40, but copies EnabledByDefault (data+0x82, false) into the
// run flag +0x70; its update ai::brain_tree_update ticks the tree only while that is set.
std::uintptr_t brain_component(std::uintptr_t base, std::uintptr_t entity) {
    constexpr unsigned slot = 12;
    const auto collection = ptr(entity, 0x70);
    require(ptr(collection) == entity && read<std::uint8_t>(collection, 8) > slot,
            "Skater component collection differs.");
    const auto component = ptr(collection, 0x20 + slot * 0x20);
    require(component && ptr(component) == base + ai::brain_tree_vtable && ptr(component, 0x18) == collection,
            "Brain tree component differs.");
    const auto data = ptr(component, 8);
    require(ptr(component, 0x40) == base + ai::brain_tree_holder_vtable && data &&
                (ptr(component, 0x50) & ~std::uintptr_t{4}) == (ptr(data, 0x70) & ~std::uintptr_t{4}),
            "Brain tree holder differs.");
    return component;
}
// SkaterAIComponent, slot 14 (factory ai::skater_ai_factory, 0x50 bytes). Its update
// ai::skater_ai_update (vtable +0x138) creates the 0x2c90-byte AI skater controller at +0x40
// (ai::ai_controller_create, bound to the skater's physics body) and runs it (ai::ai_controller_run) only
// while the byte at +0x48 is set; otherwise it destroys the controller. The
// constructor clears +0x48 and nothing in the player blueprint sets it.
std::uintptr_t driver_component(std::uintptr_t base, std::uintptr_t entity) {
    constexpr unsigned slot = 14;
    const auto collection = ptr(entity, 0x70);
    require(ptr(collection) == entity && read<std::uint8_t>(collection, 8) > slot,
            "Skater component collection differs.");
    const auto component = ptr(collection, 0x20 + slot * 0x20);
    require(component && ptr(component) == base + ai::skater_ai_vtable && ptr(component, 0x18) == collection,
            "Skater AI component differs.");
    return component;
}
void set_brain(std::uintptr_t base, std::uintptr_t entity, bool enabled) {
    const auto component = brain_component(base, entity);
    const auto driver = driver_component(base, entity);
    require(!enabled || ptr(component, 0x60) != 0, "Brain tree instance was not created.");
    const std::uint8_t value = enabled ? 1 : 0;
    require(write(component + 0x70, &value, 1), "Cannot change the brain tree run flag.");
    require(write(driver + 0x48, &value, 1), "Cannot change the skater AI controller flag.");
}
std::string brain_text(std::uintptr_t base, std::uintptr_t entity) {
    const auto component = brain_component(base, entity);
    const auto driver = driver_component(base, entity);
    return std::format("brain {} (tree {}), AI controller {} ({})",
                       read<std::uint8_t>(component, 0x70) ? "running" : "paused",
                       ptr(component, 0x60) ? "created" : "missing",
                       read<std::uint8_t>(driver, 0x48) ? "on" : "off", ptr(driver, 0x40) ? "created" : "none");
}

Skater spawn_one(std::uintptr_t base, const Local &local, const Source &source, const Matrix &at, bool physics,
                 unsigned id) {
    const auto entity = create_actor(base, source.parent, source.blueprint);
    require(entity && entity != local.entity, "Engine did not create an AI skater.");
    Skater skater;
    skater.id = id;
    skater.entity = entity;
    skater.context = local.context;
    skater.parent = ptr(entity, 0x40);
    skater.spawn = at;
    skater.created = GetTickCount64();
    {
        // Track before any further native call so an engine destruction is seen.
        auto &s = state();
        std::lock_guard lock(s.mutex);
        s.skaters.push_back(skater);
        s.publish();
    }
    try {
        require(ptr(entity) == base + addr::engine::skater_entity_vtable,
                std::format("AI entity class differs (vtable RVA {:#x}).", ptr(entity) - base));
        require(ptr(entity, 0x20) == local.context && !ptr(entity, 0xf8), "AI entity ownership differs.");
        // The source creator writes its data+0xe1 flag into this binding, which
        // the player blueprint routes to its CAS loader entities. Entity +0x458
        // reads it unchecked once physics exists, so never start without it.
        require(ptr(entity, 0x578) != 0, "Skater property binding (+0x578) is missing.");
        reinterpret_cast<void (*)(std::uintptr_t, std::uint8_t)>(base + ai::set_property_binding)(
            entity, read<std::uint8_t>(source.data, 0xe1));
        skater.component = multiplayer::read_native_skater_component(readable, base, entity);
        // Runs the entity's lifecycle callbacks and initializes its owned
        // components, including the client-only brain tree and navigator.
        using InitializePlacement = void (*)(std::uintptr_t, const void *, std::uintptr_t, std::uint8_t);
        alignas(16) const auto matrix = at;
        reinterpret_cast<InitializePlacement>(base + entities::initialize_placement)(entity, matrix.data(), 0, 1);
        require((read<std::uint32_t>(entity, 0x28) & 8) != 0 && ptr(entity, 0x628) == skater.component,
                "AI skater initialization did not finish.");
        try {
            initialize_appearance(base, entity, local.entity);
        } catch (const std::exception &e) {
            logging::log(logging::Level::warning, logging::Channel::skater, "AI skater #{} appearance: {}", id,
                         e.what());
        }
        // Source creation order: animation physics flags after placement.
        using Setter = void (*)(std::uintptr_t, std::uint8_t);
        reinterpret_cast<Setter>(base + entities::physics_c7_setter)(skater.component, 1);
        reinterpret_cast<Setter>(base + ai::physics_source_setter)(
            skater.component, read<std::uint8_t>(source.data, 0xe0));
        if (physics) {
            // The same pair of setters enables the local player's physics after
            // its source creates and places it (client_source_spawn.cpp).
            reinterpret_cast<Setter>(base + entities::physics_c7_setter)(skater.component, 0);
            reinterpret_cast<Setter>(base + entities::physics_cb_setter)(skater.component, 0);
        }
        auto &s = state();
        std::lock_guard lock(s.mutex);
        const auto found = std::find_if(s.skaters.begin(), s.skaters.end(),
                                        [&](const Skater &tracked) { return tracked.entity == entity; });
        require(found != s.skaters.end(), "AI skater was destroyed during initialization.");
        found->component = skater.component;
        return skater;
    } catch (...) {
        try {
            destroy(base, skater);
        } catch (...) {
        }
        forget(entity);
        throw;
    }
}

// Owned components in collection order, as vtable RVAs. The blueprint's
// ClientIndex values (EATBrainTree 12, SkaterAI 14, AIGoals 17) identify them.
std::string component_list(std::uintptr_t base, std::uintptr_t entity) {
    const auto collection = ptr(entity, 0x70);
    require(ptr(collection) == entity, "Skater component collection owner differs.");
    const auto count = read<std::uint8_t>(collection, 8);
    require(count <= 128, "Skater component count exceeds bounds.");
    std::string result;
    for (unsigned i = 0; i < count; ++i) {
        const auto component = ptr(collection, 0x20 + std::uintptr_t{i} * 0x20);
        const auto vtable = component ? ptr(component) : 0;
        result += std::format("{}{}={:#x}", i ? " " : "", i, code(base, vtable) ? vtable - base : vtable);
    }
    return result;
}

void spawn(std::uintptr_t base, std::uintptr_t client, unsigned count, bool physics, bool brain) {
    auto &s = state();
    std::string detail;
    require(multiplayer::install_entity_hooks(base, detail), "Native entity hooks unavailable: " + detail);
    if (!s.listening) {
        multiplayer::set_entity_destroyed_listener(&entity_destroyed);
        s.listening = true;
    }
    verify_native(base);
    const auto local = local_skater(base, client);
    require_job_context(base, local.context);
    const auto sources = read_sources(base, local.context);
    // Prefer the source that created the local skater. A natively spawned
    // skater need not have one; any player-blueprint source then supplies the
    // parent bus and asset domain, as for multiplayer remote actors.
    auto player_source = std::find_if(sources.begin(), sources.end(), [&](const Source &source) {
        return source.entity == local.entity && same_name(source.name, player_blueprint);
    });
    if (player_source == sources.end())
        player_source = std::find_if(sources.begin(), sources.end(),
                                     [](const Source &source) { return same_name(source.name, player_blueprint); });
    if (player_source == sources.end()) {
        std::string listed;
        for (const auto &source : sources)
            listed += std::format("{}{} ({})", listed.empty() ? "" : "; ", source.name,
                                  source.entity == local.entity ? "local player"
                                  : source.entity              ? "created"
                                                               : "not created");
        throw Unavailable(std::format("No player skater source is loaded; {} source(s): {}", sources.size(),
                                      listed.empty() ? "none" : listed));
    }
    logging::log(logging::Level::info, logging::Channel::skater, "AI skaters: using {} source {:#x} ({}).",
                 player_source->entity == local.entity ? "the local player's" : "a player", player_source->address,
                 player_source->name);
    // Gameplay/Characters/CharacterBlueprint_Physics_Skater_AI is stale in the
    // shipped game: it wires none of the entity properties (+0x568..+0x580)
    // the current skater code binds, and +0x578 is read unchecked. The player
    // blueprint carries the same AI stack (bt_default brain tree, client-only
    // EATBrainTree, SkaterAIComponent), so AI skaters are built from it.
    const auto blueprint = player_source->blueprint;
    const auto entity_data = ptr(blueprint, 0x68) & ~std::uintptr_t{4};
    const auto spawn_type = entity_data ? read<std::uint32_t>(entity_data, 0xc4) : ~0u;
    std::size_t existing{};
    {
        std::lock_guard lock(s.mutex);
        existing = s.skaters.size();
        s.context = local.context;
    }
    for (unsigned i = 0; i < count; ++i) {
        require(existing + i < max_skaters, std::format("At most {} AI skaters can be active.", max_skaters));
        // Line them up to the player's right, facing the same way.
        auto at = local.root;
        const float distance = 2.0f * static_cast<float>(existing + i + 1);
        for (int axis = 0; axis < 3; ++axis)
            at[12 + axis] += local.root[axis] * distance;
        const auto id = s.next_id++;
        const auto skater = spawn_one(base, local, *player_source, at, physics, id);
        logging::log(logging::Level::info, logging::Channel::skater,
                     "AI skater #{} spawned at {}; entity={:#x}, component={:#x}, blueprint spawn type={}, "
                     "physics {}.",
                     id, position_text(at), skater.entity, skater.component, spawn_type,
                     physics ? "enabled" : "left disabled");
        if (brain) {
            try {
                set_brain(base, skater.entity, true);
                logging::log(logging::Level::info, logging::Channel::skater, "AI skater #{} {}.", id,
                             brain_text(base, skater.entity));
            } catch (const std::exception &e) {
                logging::log(logging::Level::warning, logging::Channel::skater, "AI skater #{} brain: {}", id,
                             e.what());
            }
        }
        try {
            logging::log(logging::Level::info, logging::Channel::skater, "AI skater #{} components: {}", id,
                         component_list(base, skater.entity));
        } catch (const std::exception &e) {
            logging::log(logging::Level::warning, logging::Channel::skater, "AI skater #{} components: {}", id,
                         e.what());
        }
    }
}

void clear(std::uintptr_t base) {
    std::vector<Skater> skaters;
    {
        auto &s = state();
        std::lock_guard lock(s.mutex);
        skaters = s.skaters;
    }
    std::size_t destroyed{};
    for (const auto &skater : skaters) {
        try {
            destroy(base, skater);
            ++destroyed;
        } catch (const std::exception &e) {
            logging::log(logging::Level::warning, logging::Channel::skater, "AI skater #{} removal: {}", skater.id,
                         e.what());
        }
        forget(skater.entity);
    }
    logging::log(logging::Level::info, logging::Channel::skater, "Removed {} AI skater(s).", destroyed);
}

void toggle_brain(std::uintptr_t base, bool enabled) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (s.skaters.empty())
        logging::log(logging::Level::info, logging::Channel::skater, "No AI skaters are active.");
    for (auto &skater : s.skaters) {
        try {
            require(owned(base, skater), "Ownership changed.");
            set_brain(base, skater.entity, enabled);
            // Restart the movement samples from the current position.
            skater.spawn = root_matrix(skater.entity);
            skater.created = GetTickCount64();
            skater.next_sample = 0;
            logging::log(logging::Level::info, logging::Channel::skater, "AI skater #{} {}.", skater.id,
                         brain_text(base, skater.entity));
        } catch (const std::exception &e) {
            logging::log(logging::Level::warning, logging::Channel::skater, "AI skater #{} brain: {}", skater.id,
                         e.what());
        }
    }
}

std::string describe(std::uintptr_t base, const Skater &skater) {
    if (!owned(base, skater))
        return std::format("AI skater #{}: ownership changed.", skater.id);
    const auto root = root_matrix(skater.entity);
    float moved{};
    for (int axis = 0; axis < 3; ++axis)
        moved += (root[12 + axis] - skater.spawn[12 + axis]) * (root[12 + axis] - skater.spawn[12 + axis]);
    std::string physics = "unknown";
    if (skater.component && ptr(skater.component) == base + addr::engine::skater_component_vtable)
        physics = std::format("body={}, board={}, flags c7/cb={}/{}", ptr(skater.component, 0x70) ? "yes" : "no",
                              ptr(skater.component, 0x60) ? "yes" : "no",
                              read<std::uint8_t>(skater.component, 0xc7), read<std::uint8_t>(skater.component, 0xcb));
    std::string brain;
    try {
        brain = brain_text(base, skater.entity);
    } catch (const std::exception &e) {
        brain = e.what();
    }
    return std::format("AI skater #{} t={:.1f}s at {}, moved {:.2f} m, {}, {}.", skater.id,
                       static_cast<double>(GetTickCount64() - skater.created) / 1000.0, position_text(root),
                       std::sqrt(moved), physics, brain);
}
void report(std::uintptr_t base, std::uintptr_t client) {
    const auto local = local_skater(base, client);
    const auto sources = read_sources(base, local.context);
    logging::log(logging::Level::info, logging::Channel::skater, "AI report: {} skater source(s) on this map.",
                 sources.size());
    for (const auto &source : sources)
        logging::log(logging::Level::info, logging::Channel::skater, "  source {:#x}: {} ({}).", source.address,
                     source.name,
                     source.entity == local.entity ? "local player" : source.entity ? "created" : "not created");
    const auto player_source = std::find_if(sources.begin(), sources.end(),
                                            [](const Source &source) { return same_name(source.name, player_blueprint); });
    if (player_source != sources.end()) {
        const auto blueprint = find_blueprint(base, player_source->blueprint, ai_blueprint);
        logging::log(logging::Level::info, logging::Channel::skater, "  {} {}.", ai_blueprint,
                     blueprint ? std::format("is loaded at {:#x}", blueprint) : std::string("was not found"));
    }
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (s.skaters.empty())
        logging::log(logging::Level::info, logging::Channel::skater, "  No AI skaters are active.");
    for (const auto &skater : s.skaters) {
        try {
            logging::log(logging::Level::info, logging::Channel::skater, "  {}", describe(base, skater));
        } catch (const std::exception &e) {
            logging::log(logging::Level::warning, logging::Channel::skater, "  AI skater #{}: {}", skater.id,
                         e.what());
        }
    }
}

void sample(std::uintptr_t base) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    const auto now = GetTickCount64();
    for (auto &skater : s.skaters) {
        if (skater.next_sample >= sample_ms.size() || now - skater.created < sample_ms[skater.next_sample])
            continue;
        ++skater.next_sample;
        try {
            logging::log(logging::Level::info, logging::Channel::skater, "{}", describe(base, skater));
        } catch (const std::exception &e) {
            logging::log(logging::Level::warning, logging::Channel::skater, "AI skater #{} sample: {}", skater.id,
                         e.what());
        }
    }
}
} // namespace

void request_spawn(unsigned count, bool physics, bool brain) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    s.requests.push_back({Request::Kind::spawn, count, physics, brain});
}
void request_brain(bool enabled) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    s.requests.push_back({Request::Kind::brain, 0, false, enabled});
}
void request_clear() {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    s.requests.push_back({Request::Kind::clear});
}
void request_report() {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    s.requests.push_back({Request::Kind::report});
}
std::size_t active_count() noexcept {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    return s.skaters.size();
}
void tick(std::uintptr_t base, std::uintptr_t client, bool ready) noexcept {
    auto &s = state();
    std::vector<Request> requests;
    std::uintptr_t context{};
    {
        std::lock_guard lock(s.mutex);
        requests.swap(s.requests);
        context = s.context;
    }
    try {
        // The destruction hook normally reports level teardown. If the client
        // context changed without it, drop the stale pointers untouched.
        std::uintptr_t current{};
        if (context && (!readable(client + 8, &current, sizeof(current)) || current != context)) {
            std::lock_guard lock(s.mutex);
            if (!s.skaters.empty())
                logging::log(logging::Level::info, logging::Channel::skater,
                             "Forgetting {} AI skater(s) after the map changed.", s.skaters.size());
            s.skaters.clear();
            s.publish();
            s.context = 0;
        }
        for (const auto &request : requests) {
            if (!ready) {
                logging::log(logging::Level::warning, logging::Channel::skater,
                             "AI skaters: load a map and spawn your skater first.");
                break;
            }
            try {
                switch (request.kind) {
                case Request::Kind::spawn:
                    spawn(base, client, request.count, request.physics, request.brain);
                    break;
                case Request::Kind::brain:
                    toggle_brain(base, request.brain);
                    break;
                case Request::Kind::clear:
                    clear(base);
                    break;
                case Request::Kind::report:
                    report(base, client);
                    break;
                }
            } catch (const std::exception &e) {
                logging::log(logging::Level::error, logging::Channel::skater, "AI skaters: {}", e.what());
            }
        }
        if (ready)
            sample(base);
    } catch (const std::exception &e) {
        logging::log(logging::Level::error, logging::Channel::skater, "AI skaters: {}", e.what());
    } catch (...) {
    }
}
} // namespace dingosdk::ai_skaters

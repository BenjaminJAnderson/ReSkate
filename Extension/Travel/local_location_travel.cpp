#include "Engine/Core/Log/logging.h"
#include "Engine/Game/Abi/native_data.h"
#include "Extension/Profile/runtime_internal.h"
#include "Extension/Progression/local_challenge_runtime.h"
#include "Extension/Progression/local_entitlement_trigger_runtime.h"
#include "local_location_travel.h"
#include "Engine/Game/Build/20260929/local_location_travel.h"

namespace dingosdk::profile_runtime {
// Offline access-point and destination data consumed by the authored
// LocationTravelPoint graphs. Native publication copies all borrowed values.

LocationTravelRuntime& location_travel_runtime() { static auto* r = new LocationTravelRuntime; return *r; }

std::atomic<LocalLocationTravelQueue> location_travel_queue{};

std::string local_travel_destination(const void* value) {
    std::string id;
    if (!identifier(value, id)) return {};
    const auto* found = find_travel_destination(location_travel_runtime().policy, id);
    return found ? found->map : std::string{};
}

void* prepare_location_travel_hook(void* output, const void* identifier_value) {
    auto& r = location_travel_runtime();
    const auto result = r.prepare_request(output, identifier_value);
    PreserveError preserve;
    if (!local_runtime().active.load(std::memory_order_acquire) || !r.policy.enabled) return result;
    try {
        std::string id;
        if (!identifier(identifier_value, id) || !find_travel_destination(r.policy, id)) return result;
        // The native lookup returns two empty owned CStrings without the
        // matchmaking catalog. Keep the local destination in that same ABI so
        // the eventual submit does not depend on the caller's VM stack layout.
        auto assign = game::native_data().values.assign;
        if (!assign || !output) return result;
        assign(output, id.c_str(), static_cast<std::uint32_t>(id.size()));
        // The menu has two submission branches: scenario + attributes, or
        // attributes alone. Both must carry a resolvable local destination.
        assign(static_cast<std::byte*>(output) + 8, id.c_str(), static_cast<std::uint32_t>(id.size()));
        dingosdk::logging::event(dingosdk::logging::Channel::level, dingosdk::Json{{"event", "local_location_travel_prepared"}, {"destination", id}}.dump().c_str());
    } catch (...) {}
    return result;
}

std::string local_travel_request_map(std::uintptr_t vm) {
    std::uintptr_t instance{}, resource{}, current{}, page{};
    std::uint32_t key{};
    std::array<std::uint32_t, 10> layout{};
    if (!vm || !read(vm + 0x30, instance) || !read(instance, resource) ||
        !read(vm + 0x38, current) || resource != current || !read(resource + 0x10, key) ||
        !read(resource + 0x20, layout)) return {};
    std::uint32_t id_offset{};
    if (key == 0xbbd6f228 && layout == std::array<std::uint32_t, 10>{48,304,101,174,3,0,1,15,1507328,0})
        id_offset = 0x10; // Matchmaker/OnInitiateLocationTravel
    else if (key == 0x7812457a && layout == std::array<std::uint32_t, 10>{64,912,341,689,7,0,9,655414,3211287,264448})
        id_offset = 0x68; // NeighborhoodRank/OnEnterLocationSolo, inlined request
    else return {};
    const auto pages = instance + ((layout[0] + 15ULL) & ~15ULL);
    if (!read(pages + 2 * sizeof(std::uintptr_t), page) || !page) return {};
    std::string id;
    if (!identifier(reinterpret_cast<const void*>(page + id_offset), id)) return {};
    const auto* found = find_travel_destination(location_travel_runtime().policy, id);
    return found ? found->map : std::string{};
}

bool queue_local_travel(const std::string& map, const char* route) {
    const auto queue = location_travel_queue.load(std::memory_order_acquire);
    bool queued{};
    try { if (queue) queued = queue(map.c_str()); } catch (...) {}
    try { dingosdk::logging::event(dingosdk::logging::Channel::level, dingosdk::Json{{"event", "local_location_travel_request"}, {"map", map},
        {"route", route}, {"queued", queued}}.dump().c_str()); } catch (...) {}
    return queued;
}

void location_travel_attribute_request_hook(const void* attributes, std::uintptr_t options) {
    const auto incoming_error = GetLastError();
    bool local{};
    try {
        if (local_runtime().active.load(std::memory_order_acquire) && location_travel_runtime().policy.enabled) {
            const auto map = local_travel_destination(attributes);
            local = !map.empty();
            if (local) queue_local_travel(map, "attributes");
        }
    } catch (...) {}
    SetLastError(incoming_error);
    // A local ID is not a server attribute string, including when the loader
    // is busy. A refused queue can be retried without entering matchmaking.
    if (!local) location_travel_runtime().attribute_request(attributes, options);
}

void location_travel_request_hook(const void* first, const void* second) {
    const auto incoming_error = GetLastError();
    bool local{};
    try {
        if (local_runtime().active.load(std::memory_order_acquire) && location_travel_runtime().policy.enabled) {
            auto map = local_travel_destination(first);
            if (map.empty()) map = local_travel_request_map(executing_expression);
            local = !map.empty();
            if (local) queue_local_travel(map, "scenario");
        }
    } catch (...) {}
    SetLastError(incoming_error);
    if (!local) location_travel_runtime().request(first, second);
}

TravelReferences travel_locations, travel_access_points;

bool travel_catalog_matches(std::uintptr_t model, std::uint64_t list, const TravelReferences& expected,
    bool allow_empty) {
    const auto value = game::native_data().models.value(model, list, 0, 0);
    std::uintptr_t elements{}; std::uint32_t count{};
    if (!read(value, elements) || !elements || !read(elements - 4, count)) return false;
    count &= 0x7fffffff;
    if (!count) return allow_empty;
    if (count != expected.count) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        TravelReference actual;
        if (!read(elements + i * sizeof(actual), actual) || actual.context != expected.values[i].context)
            return false;
    }
    return true;
}

void notify_location_travel() {
    auto& r = location_travel_runtime();
    if (r.notified) return;
    const auto entity_owner = challenge_owner();
    std::uintptr_t context{};
    if (!entity_owner || !read(entity_owner + 0x18, context) || !context) return;
    const std::uint8_t event{};
    const std::array<std::uint32_t, 3> options{0, 1, 0};
    for (const auto type : addr::local_location_travel::notify_event_types)
        challenge_runtime().f.dispatch(context, local_runtime().base + type, &event, options.data(), 0);
    r.notified = true;
    dingosdk::logging::event(dingosdk::logging::Channel::level, "{\"event\":\"local_location_travel_notified\"}");
}

bool location_travel_type_contract(std::uintptr_t base) {
    // Full field tables are pinned by the build audit. These live headers also
    // reject incompatible records before any native value is constructed.
    const auto valid = [&](std::uintptr_t rva, std::uint32_t hash, std::uint16_t size) {
        std::uint32_t actual{}; std::uint16_t actual_size{};
        return read(base + rva, actual) && actual == hash &&
            read(base + rva + 6, actual_size) && actual_size == size;
    };
    namespace travel = addr::local_location_travel;
    return valid(travel::access_point_record, 0x3d9c0623, 0x28) && valid(travel::location_record, 0xf2b75989, 0xc0) &&
        valid(travel::byte_records[0], 0x4a69d31e, 1) && valid(travel::byte_records[1], 0x1c5a8a68, 1);
}

void update_location_travel() {
    auto& r = location_travel_runtime(); auto& s = local_runtime(); auto& n = game::native_data().models;
    if (!s.active.load(std::memory_order_acquire) || !r.policy.enabled) return;
    const auto now = GetTickCount64();
    if (now < r.next_poll) return;
    r.next_poll = now + 1000;
    std::uintptr_t owner{}, vtable{}, descriptor{}, array_type{};
    std::uint64_t location_list{}, access_list{};
    if (!read(s.base + addr::engine::location_owner, owner) || !owner || !read(owner, vtable) ||
        vtable != s.base + addr::local_location_travel::owner_vtable ||
        !read(owner + 0x20, location_list) || !location_list || !read(owner + 0x40, access_list) || !access_list ||
        !read(s.base + addr::engine::reference_type, descriptor) || !read(descriptor + 0x20, array_type)) return;
    const auto model = n.get_model(0xbf0f9789);
    if (!model) return;
    if (r.owner != owner || r.model != model || r.location_list != location_list || r.access_list != access_list) {
        r.ready = r.notified = r.started = false;
        r.owner = owner; r.model = model; r.location_list = location_list; r.access_list = access_list;
        travel_locations = {}; travel_access_points = {};
    }
    const auto hydrate = [&]() {
        game::ModelWriteLock model_lock(model);
        // Only claim catalogs that are empty offline. Existing server data belongs
        // to its native provider and must not be replaced with the local defaults.
        if (r.ready && travel_catalog_matches(model, location_list, travel_locations, false) &&
            travel_catalog_matches(model, access_list, travel_access_points, false)) return;
        r.ready = r.notified = false;
        const TravelReferences empty;
        if (!travel_catalog_matches(model, location_list, r.started ? travel_locations : empty, true) ||
            !travel_catalog_matches(model, access_list, r.started ? travel_access_points : empty, true)) return;
        TravelReferences locations;
        for (const auto& [id, destination] : r.policy.destinations) {
            const auto type = s.base + addr::local_location_travel::location_type;
            const auto native_id = travel_native_id(destination);
            const auto hash = game::native_name_hash(native_id);
            auto context = n.find(model, type, hash, 0, 0);
            if (!context) context = n.create(model, type, hash, 0, false, 2);
            if (!context) return;
            TravelValue<0xc0> value;
            r.construct_location(&value);
            value.set(0x18, native_id.data());
            value.set(0x20, id.c_str()); // Matchmaking catalog key, resolved locally.
            value.set(0x80, destination.description.c_str());
            value.set(0x90, destination.name.c_str());
            value.set(0x28, destination.name.c_str()); // Short title.
            value.set(0x30, destination.black_icon.c_str());
            value.set(0x98, destination.white_icon.c_str());
            value.set(0x40, destination.large_image.c_str());
            value.set(0x88, destination.large_image.c_str()); // Default large image.
            value.set(0x68, destination.small_image.c_str());
            value.set(0x10, destination.small_image.c_str()); // Default small image.
            value.set(0xa8, destination.medium);
            value.set(0xb0, hash);
            value.set<std::uint8_t>(0xb4, 1); // unlocked; no required entitlements
            if (!n.publish(model, context, type, &value)) return;
            locations.values[locations.count++] = {native_id.data(), context};
        }
        TravelReferences access_points;
        for (const auto& [id, destinations] : r.policy.access_points) {
            const auto type = s.base + addr::local_location_travel::access_point_type;
            auto context = n.find(model, type, game::native_name_hash(id), 0, 0);
            if (!context) context = n.create(model, type, game::native_name_hash(id), 0, false, 2);
            if (!context) return;
            TravelValue<0x28> value;
            r.construct_access(&value);
            TravelArray<std::uint32_t, 16> hashes;
            for (const auto& destination : destinations)
                hashes.values[hashes.count++] = game::native_name_hash(travel_native_id(r.policy.destinations.at(destination)));
            value.set(0x10, id.c_str());
            value.set(0x18, hashes.values.data());
            if (!n.publish(model, context, type, &value)) return;
            access_points.values[access_points.count++] = {id.c_str(), context};
        }
        const auto location_values = locations.values.data();
        const auto access_values = access_points.values.data();
        // Remember owned contexts before publishing either list so a partial
        // publication can be retried without replacing another provider's data.
        travel_locations = locations; travel_access_points = access_points; r.started = true;
        if (!n.publish(model, location_list, array_type, &location_values) ||
            !n.publish(model, access_list, array_type, &access_values)) return;
        // These are the catalog-ready bytes set at the end of the native
        // location/access response converters, not backend connection flags.
        *reinterpret_cast<std::uint8_t*>(owner + 0x180) = 1;
        *reinterpret_cast<std::uint8_t*>(owner + 0x182) = 1;
        r.owner = owner; r.model = model; r.ready = true;
        dingosdk::logging::event(dingosdk::logging::Channel::level, dingosdk::Json{{"event", "local_location_travel_hydrated"}, {"destinations", locations.count},
            {"access_points", access_points.count}}.dump().c_str());
    };
    hydrate();
    // Publish events outside the data-model lock. If ECS is not ready yet,
    // retry notification on the next tick after the catalogs are available.
    if (r.ready) notify_location_travel();
}
}
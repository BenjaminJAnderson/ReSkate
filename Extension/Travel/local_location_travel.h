#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct LocationTravelRuntime {
    dingosdk::LocationTravelPolicy policy;
    void* (*construct_location)(void*){};
    void* (*construct_access)(void*){};
    void (*request)(const void*, const void*){};
    void (*attribute_request)(const void*, std::uintptr_t){};
    void* (*prepare_request)(void*, const void*){};
    std::uintptr_t owner{}, model{};
    std::uint64_t location_list{}, access_list{};
    std::uint64_t next_poll{};
    bool ready{}, notified{}, started{};
};

LocationTravelRuntime& location_travel_runtime();

extern std::atomic<LocalLocationTravelQueue> location_travel_queue;

std::string local_travel_destination(const void* value);

void* prepare_location_travel_hook(void* output, const void* identifier_value);

std::string local_travel_request_map(std::uintptr_t vm);

bool queue_local_travel(const std::string& map, const char* route);

void location_travel_attribute_request_hook(const void* attributes, std::uintptr_t options);

void location_travel_request_hook(const void* first, const void* second);

template<std::size_t N> struct alignas(8) TravelValue {
    std::array<std::byte, N> bytes{};
    template<class T> void set(std::size_t offset, T value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); }
};

template<class T, std::size_t N> using TravelArray = game::NativeArrayStorage<T, N>;

struct TravelReference { const char* name{}; std::uint64_t context{}; };

using TravelReferences = TravelArray<TravelReference, 16>;

static_assert(sizeof(TravelReference) == 16 && offsetof(TravelReferences, values) == 8);

extern TravelReferences travel_locations;

bool travel_catalog_matches(std::uintptr_t model, std::uint64_t list, const TravelReferences& expected,
    bool allow_empty);

void notify_location_travel();

bool location_travel_type_contract(std::uintptr_t base);

void update_location_travel();
}

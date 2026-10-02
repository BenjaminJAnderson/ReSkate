#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
using UpdateNeighborhoods = void (*)(std::uintptr_t);

using ConstructNeighborhood = void* (*)(void*);



struct NeighborhoodRuntime {
    UpdateNeighborhoods update{};
    ConstructNeighborhood construct{};
    std::uint64_t next_poll{};
    std::uintptr_t last_owner{};
    std::mutex observation_mutex;
    std::map<std::string, std::uint32_t, std::less<>> logged_ranks;
    std::atomic<bool> logged_pending{};
};

NeighborhoodRuntime& neighborhood_runtime();

struct alignas(8) NeighborhoodRecord {
    std::array<std::byte, 0xc0> bytes{};
    template<class T> T get(std::size_t offset) const {
        T value{}; std::memcpy(&value, bytes.data() + offset, sizeof(value)); return value;
    }
    template<class T> void set(std::size_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
};

bool neighborhood_type_contract(std::uintptr_t base);


bool known_neighborhood(const NeighborhoodRecord& record, std::string& id);

std::optional<std::uint32_t> neighborhood_cap(const NeighborhoodRecord& record);

bool apply_neighborhood_profile(NeighborhoodRecord& record);

bool publish_neighborhood_hook(std::uintptr_t manager, std::uint64_t context,
    std::uintptr_t type, const void* source);

void hydrate_neighborhoods(std::uintptr_t owner);

void update_neighborhoods_hook(std::uintptr_t owner);
}

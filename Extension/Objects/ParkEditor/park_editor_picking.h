#pragma once
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/park_editor.h"
#include <algorithm>
#include <array>
#include <optional>

namespace dingosdk::editor {
struct NativePickingApi {
    void *(*hit_body)(const void *, void *){};
    bool (*body_valid)(const void *){};
    void *(*body_entity)(void *, const void *){};
    std::uintptr_t (*mapping)(){};
    void *(*map_entity)(std::uintptr_t, void *, std::uint64_t, int){};
    bool ready{};
    void *(*buildkit_query)(const void *, void *){};
};
struct BuildKitIdentity {
    std::uint32_t drop_id{};
    std::uint64_t instance{};
    bool operator==(const BuildKitIdentity &) const = default;
};
inline std::optional<BuildKitIdentity> buildkit_identity(const NativePickingApi &api, std::uint64_t root) {
    if (!api.ready || !api.buildkit_query || !root)
        return {};
    std::array<std::uint64_t, 5> component{};
    api.buildkit_query(&root, component.data());
    BuildKitIdentity identity;
    if (!component[1] || !component[2] || component[3] != root ||
        !memory::read(component[2] + 0x24, identity.drop_id) || identity.drop_id == UINT32_MAX ||
        !memory::read(component[2] + 8, identity.instance))
        return {};
    return identity;
}
inline std::uint64_t mapped_entity(const NativePickingApi &api, std::uint64_t entity, int direction) {
    if (!api.ready || !entity)
        return 0;
    const auto manager = api.mapping();
    const auto offset = direction == 1 ? 0x70 : 0x48;
    std::uintptr_t buckets{};
    std::uint32_t count{}, entries{};
    if (!manager || !memory::read(manager + offset, buckets) || !buckets ||
        !memory::read(manager + offset + 8, count) || !count || count > 0x100000 ||
        !memory::read(manager + offset + 12, entries) || !entries)
        return 0; // Native lookup takes entity % count; an unready table is not callable.
    std::uint64_t mapped{};
    api.map_entity(manager, &mapped, entity, direction);
    return mapped;
}
inline NativePickingApi picking_api(std::uintptr_t base) {
    const auto &picking_contracts = addr::park_editor::picking_contracts;
    NativePickingApi api;
    api.ready = std::all_of(picking_contracts.begin(), picking_contracts.end(), [&](const auto &contract) {
        std::array<unsigned char, 32> bytes{};
        return memory::read_bytes(base + contract.rva, bytes.data(), bytes.size()) && bytes == contract.bytes;
    });
    if (api.ready) {
        api.hit_body = reinterpret_cast<decltype(api.hit_body)>(base + picking_contracts[0].rva);
        api.body_valid = reinterpret_cast<decltype(api.body_valid)>(base + picking_contracts[1].rva);
        api.body_entity = reinterpret_cast<decltype(api.body_entity)>(base + picking_contracts[2].rva);
        api.mapping = reinterpret_cast<decltype(api.mapping)>(base + picking_contracts[3].rva);
        api.map_entity = reinterpret_cast<decltype(api.map_entity)>(base + picking_contracts[4].rva);
        api.buildkit_query = reinterpret_cast<decltype(api.buildkit_query)>(base + picking_contracts[5].rva);
    }
    return api;
}
} // namespace dingosdk::editor

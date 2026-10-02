#pragma once
#include "Engine/Game/World/bus_stop_world.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dingosdk {
enum class FixedBusStopEntitlementKind : std::uint8_t {
    none,
    visibility,
    collected,
};

struct FixedBusStopEntitlement {
    std::uint8_t number{};
    FixedBusStopEntitlementKind kind{FixedBusStopEntitlementKind::none};

    explicit constexpr operator bool() const noexcept {
        return kind != FixedBusStopEntitlementKind::none;
    }
};

struct FixedBusStopCatalogEntry {
    std::uint8_t number{};
    FixedBusStopWorld world{FixedBusStopWorld::bam};
    std::string_view visibility_entitlement;
    std::string_view collected_entitlement;
    std::string_view collect_event;
};

inline constexpr std::size_t fixed_bus_stop_count = 56;

// Exact, installed identifiers for the current Skate build. Entries 1-39 and
// 48-50 are authored in BAM; entries 40-47 are authored on Isle of Grom;
// 63-68 are authored in MPR. Missing numbers are not implicitly unlocked.
std::span<const FixedBusStopCatalogEntry> fixed_bus_stop_catalog() noexcept;
const FixedBusStopCatalogEntry* fixed_bus_stop_catalog_entry(
    std::uint8_t number) noexcept;

// Matching is deliberately case-sensitive and accepts only the exact
// `unlock_busstop_NNN` and `unlock_busstop_NNN_collect` forms in the catalog.
FixedBusStopEntitlement classify_fixed_bus_stop_entitlement(
    std::string_view entitlement) noexcept;
}

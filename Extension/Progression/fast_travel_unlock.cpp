#include "fast_travel_unlock.h"

#include <array>

namespace dingosdk {
namespace {
constexpr std::array<FixedBusStopCatalogEntry, fixed_bus_stop_count> catalog{{
    {1, FixedBusStopWorld::bam, "unlock_busstop_001", "unlock_busstop_001_collect", "Collect_BusStop_001"},
    {2, FixedBusStopWorld::bam, "unlock_busstop_002", "unlock_busstop_002_collect", "Collect_BusStop_002"},
    {3, FixedBusStopWorld::bam, "unlock_busstop_003", "unlock_busstop_003_collect", "Collect_BusStop_003"},
    {4, FixedBusStopWorld::bam, "unlock_busstop_004", "unlock_busstop_004_collect", "Collect_BusStop_004"},
    {5, FixedBusStopWorld::bam, "unlock_busstop_005", "unlock_busstop_005_collect", "Collect_BusStop_005"},
    {6, FixedBusStopWorld::bam, "unlock_busstop_006", "unlock_busstop_006_collect", "Collect_BusStop_006"},
    {7, FixedBusStopWorld::bam, "unlock_busstop_007", "unlock_busstop_007_collect", "Collect_BusStop_007"},
    {8, FixedBusStopWorld::bam, "unlock_busstop_008", "unlock_busstop_008_collect", "Collect_BusStop_008"},
    {9, FixedBusStopWorld::bam, "unlock_busstop_009", "unlock_busstop_009_collect", "Collect_BusStop_009"},
    {10, FixedBusStopWorld::bam, "unlock_busstop_010", "unlock_busstop_010_collect", "Collect_BusStop_010"},
    {11, FixedBusStopWorld::bam, "unlock_busstop_011", "unlock_busstop_011_collect", "Collect_BusStop_011"},
    {12, FixedBusStopWorld::bam, "unlock_busstop_012", "unlock_busstop_012_collect", "Collect_BusStop_012"},
    {13, FixedBusStopWorld::bam, "unlock_busstop_013", "unlock_busstop_013_collect", "Collect_BusStop_013"},
    {14, FixedBusStopWorld::bam, "unlock_busstop_014", "unlock_busstop_014_collect", "Collect_BusStop_014"},
    {15, FixedBusStopWorld::bam, "unlock_busstop_015", "unlock_busstop_015_collect", "Collect_BusStop_015"},
    {16, FixedBusStopWorld::bam, "unlock_busstop_016", "unlock_busstop_016_collect", "Collect_BusStop_016"},
    {17, FixedBusStopWorld::bam, "unlock_busstop_017", "unlock_busstop_017_collect", "Collect_BusStop_017"},
    {18, FixedBusStopWorld::bam, "unlock_busstop_018", "unlock_busstop_018_collect", "Collect_BusStop_018"},
    {19, FixedBusStopWorld::bam, "unlock_busstop_019", "unlock_busstop_019_collect", "Collect_BusStop_019"},
    {20, FixedBusStopWorld::bam, "unlock_busstop_020", "unlock_busstop_020_collect", "Collect_BusStop_020"},
    {21, FixedBusStopWorld::bam, "unlock_busstop_021", "unlock_busstop_021_collect", "Collect_BusStop_021"},
    {22, FixedBusStopWorld::bam, "unlock_busstop_022", "unlock_busstop_022_collect", "Collect_BusStop_022"},
    {23, FixedBusStopWorld::bam, "unlock_busstop_023", "unlock_busstop_023_collect", "Collect_BusStop_023"},
    {24, FixedBusStopWorld::bam, "unlock_busstop_024", "unlock_busstop_024_collect", "Collect_BusStop_024"},
    {25, FixedBusStopWorld::bam, "unlock_busstop_025", "unlock_busstop_025_collect", "Collect_BusStop_025"},
    {26, FixedBusStopWorld::bam, "unlock_busstop_026", "unlock_busstop_026_collect", "Collect_BusStop_026"},
    {27, FixedBusStopWorld::bam, "unlock_busstop_027", "unlock_busstop_027_collect", "Collect_BusStop_027"},
    {28, FixedBusStopWorld::bam, "unlock_busstop_028", "unlock_busstop_028_collect", "Collect_BusStop_028"},
    {29, FixedBusStopWorld::bam, "unlock_busstop_029", "unlock_busstop_029_collect", "Collect_BusStop_029"},
    {30, FixedBusStopWorld::bam, "unlock_busstop_030", "unlock_busstop_030_collect", "Collect_BusStop_030"},
    {31, FixedBusStopWorld::bam, "unlock_busstop_031", "unlock_busstop_031_collect", "Collect_BusStop_031"},
    {32, FixedBusStopWorld::bam, "unlock_busstop_032", "unlock_busstop_032_collect", "Collect_BusStop_032"},
    {33, FixedBusStopWorld::bam, "unlock_busstop_033", "unlock_busstop_033_collect", "Collect_BusStop_033"},
    {34, FixedBusStopWorld::bam, "unlock_busstop_034", "unlock_busstop_034_collect", "Collect_BusStop_034"},
    {35, FixedBusStopWorld::bam, "unlock_busstop_035", "unlock_busstop_035_collect", "Collect_BusStop_035"},
    {36, FixedBusStopWorld::bam, "unlock_busstop_036", "unlock_busstop_036_collect", "Collect_BusStop_036"},
    {37, FixedBusStopWorld::bam, "unlock_busstop_037", "unlock_busstop_037_collect", "Collect_BusStop_037"},
    {38, FixedBusStopWorld::bam, "unlock_busstop_038", "unlock_busstop_038_collect", "Collect_BusStop_038"},
    {39, FixedBusStopWorld::bam, "unlock_busstop_039", "unlock_busstop_039_collect", "Collect_BusStop_039"},
    {40, FixedBusStopWorld::isle_of_grom, "unlock_busstop_040", "unlock_busstop_040_collect", "Collect_BusStop_040"},
    {41, FixedBusStopWorld::isle_of_grom, "unlock_busstop_041", "unlock_busstop_041_collect", "Collect_BusStop_041"},
    {42, FixedBusStopWorld::isle_of_grom, "unlock_busstop_042", "unlock_busstop_042_collect", "Collect_BusStop_042"},
    {43, FixedBusStopWorld::isle_of_grom, "unlock_busstop_043", "unlock_busstop_043_collect", "Collect_BusStop_043"},
    {44, FixedBusStopWorld::isle_of_grom, "unlock_busstop_044", "unlock_busstop_044_collect", "Collect_BusStop_044"},
    {45, FixedBusStopWorld::isle_of_grom, "unlock_busstop_045", "unlock_busstop_045_collect", "Collect_BusStop_045"},
    {46, FixedBusStopWorld::isle_of_grom, "unlock_busstop_046", "unlock_busstop_046_collect", "Collect_BusStop_046"},
    {47, FixedBusStopWorld::isle_of_grom, "unlock_busstop_047", "unlock_busstop_047_collect", "Collect_BusStop_047"},
    {48, FixedBusStopWorld::bam, "unlock_busstop_048", "unlock_busstop_048_collect", "Collect_BusStop_048"},
    {49, FixedBusStopWorld::bam, "unlock_busstop_049", "unlock_busstop_049_collect", "Collect_BusStop_049"},
    {50, FixedBusStopWorld::bam, "unlock_busstop_050", "unlock_busstop_050_collect", "Collect_BusStop_050"},
    {63, FixedBusStopWorld::mpr, "unlock_busstop_063", "unlock_busstop_063_collect", "Collect_BusStop_063"},
    {64, FixedBusStopWorld::mpr, "unlock_busstop_064", "unlock_busstop_064_collect", "Collect_BusStop_064"},
    {65, FixedBusStopWorld::mpr, "unlock_busstop_065", "unlock_busstop_065_collect", "Collect_BusStop_065"},
    {66, FixedBusStopWorld::mpr, "unlock_busstop_066", "unlock_busstop_066_collect", "Collect_BusStop_066"},
    {67, FixedBusStopWorld::mpr, "unlock_busstop_067", "unlock_busstop_067_collect", "Collect_BusStop_067"},
    {68, FixedBusStopWorld::mpr, "unlock_busstop_068", "unlock_busstop_068_collect", "Collect_BusStop_068"},
}};

constexpr bool catalog_is_ordered() noexcept {
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        if (!catalog[index].number || (index && catalog[index - 1].number >= catalog[index].number)) return false;
    }
    return true;
}
static_assert(catalog_is_ordered());

constexpr std::string_view visibility_prefix = "unlock_busstop_";
constexpr std::string_view collected_suffix = "_collect";

FixedBusStopEntitlement parse_number(
    std::string_view digits, FixedBusStopEntitlementKind kind) noexcept {
    if (digits.size() != 3 || digits[0] < '0' || digits[0] > '9' ||
        digits[1] < '0' || digits[1] > '9' ||
        digits[2] < '0' || digits[2] > '9') return {};
    const auto number = static_cast<unsigned>(digits[0] - '0') * 100u +
        static_cast<unsigned>(digits[1] - '0') * 10u +
        static_cast<unsigned>(digits[2] - '0');
    if (number > UINT8_MAX || !fixed_bus_stop_catalog_entry(static_cast<std::uint8_t>(number))) return {};
    return {static_cast<std::uint8_t>(number), kind};
}
}

std::span<const FixedBusStopCatalogEntry> fixed_bus_stop_catalog() noexcept {
    return catalog;
}

const FixedBusStopCatalogEntry* fixed_bus_stop_catalog_entry(
    std::uint8_t number) noexcept {
    for (const auto& entry : catalog) {
        if (entry.number == number) return &entry;
        if (entry.number > number) break;
    }
    return nullptr;
}

FixedBusStopEntitlement classify_fixed_bus_stop_entitlement(
    std::string_view entitlement) noexcept {
    if (!entitlement.starts_with(visibility_prefix)) return {};
    auto value = entitlement.substr(visibility_prefix.size());
    auto kind = FixedBusStopEntitlementKind::visibility;
    if (value.ends_with(collected_suffix)) {
        value.remove_suffix(collected_suffix.size());
        kind = FixedBusStopEntitlementKind::collected;
    }
    return parse_number(value, kind);
}
}

#pragma once
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "Engine/Core/Json/json.h"
#include "Engine/Vfs/content_catalogs.h"

namespace dingosdk {
struct TravelDestination {
    std::string id, map, name, description;
    std::string white_icon, black_icon, small_image, large_image;
    unsigned medium{};
};
struct LocationTravelPolicy {
    bool enabled{};
    std::map<std::string, TravelDestination, std::less<>> destinations;
    std::map<std::string, std::vector<std::string>, std::less<>> access_points;
};
inline std::string_view travel_level_asset(std::string_view map) {
    if (map == "bam") return "Levels/Game/BAM_LevelRoot/BAM_LevelRoot";
    if (map == "grom") return "Levels/Game/DingoLevel_Isle_of_Grom/DingoLevel_Isle_of_Grom";
    if (map == "mpr") return "Levels/Game/DingoLevel_MPR/DingoLevel_MPR";
    if (map == "stadium_1") return "Levels/Game/DingoLevel_SDM/DingoLevel_SDM_Int_001/DingoLevel_SDM_Int_001";
    return {};
}
// The map a cached location's level tag names; empty for levels ReSkate cannot travel to.
inline std::string_view travel_map(std::string_view level) {
    if (level == "defaultDsub") return "bam";
    if (level == "grom" || level == "mpr") return level;
    if (level == "stadium01") return "stadium_1";
    return {};
}
// Destination ids are the game's own location ids (location_bam, ...).
inline std::string_view travel_native_id(const TravelDestination& destination) { return destination.id; }
inline const TravelDestination* find_travel_destination(const LocationTravelPolicy& policy, std::string_view id) {
    const auto saved = policy.destinations.find(id);
    return saved == policy.destinations.end() ? nullptr : &saved->second;
}
// The live service's artwork links name the game's CDN; offline they are
// fetched from it directly (cdn:/<name>-{platform}-{size}?sha1=... is the
// Windows default rendition of <name>).
inline std::string travel_artwork_url(std::string_view link) {
    constexpr std::string_view scheme = "cdn:/";
    if (!link.starts_with(scheme)) return {};
    std::string name(link.substr(scheme.size(), link.find('?') == std::string_view::npos ? std::string_view::npos
                                                                                        : link.find('?') - scheme.size()));
    for (const auto& [from, to] : {std::pair{std::string_view("{platform}"), std::string_view("windows")},
                                   std::pair{std::string_view("{size}"), std::string_view("default")}})
        if (const auto at = name.find(from); at != std::string::npos) name.replace(at, from.size(), to);
    return "https://dingo-dev-assets.akamaized.net/cdn/production/" + name;
}
// Whether the player left location travel on (profile location_travel.enabled).
inline bool location_travel_enabled(const dingosdk::Json& extensions) {
    if (!extensions.contains("location_travel")) return true;
    const auto& root = extensions.at("location_travel");
    if (!root.is_object() || (root.contains("enabled") && !root.at("enabled").is_boolean()))
        throw std::invalid_argument("Invalid location travel settings");
    return root.value("enabled", true);
}
// Destinations and access points are the game's own records from the content cache.
inline LocationTravelPolicy location_travel_policy(const dingosdk::Json& extensions,
                                                   const content_cache::Catalogs& catalogs = content_cache::catalogs()) {
    LocationTravelPolicy result;
    result.enabled = location_travel_enabled(extensions);
    const auto medium = [](std::string_view value) -> unsigned { return value == "Water" ? 1 : value == "Door" ? 2 : 0; };
    for (const auto& location : catalogs.travel_locations) {
        const auto map = travel_map(location.level);
        if (map.empty() || location.name.empty() || result.destinations.size() >= 16) continue;
        result.destinations.emplace(location.id, TravelDestination{location.id, std::string(map), location.name,
            location.description.empty() ? location.name : location.description,
            travel_artwork_url(location.white_icon), travel_artwork_url(location.black_icon), {},
            travel_artwork_url(location.image), medium(location.medium)});
    }
    for (const auto& [id, targets] : catalogs.travel_access_points) {
        std::vector<std::string> kept;
        for (const auto& target : targets)
            if (result.destinations.contains(target) && std::find(kept.begin(), kept.end(), target) == kept.end())
                kept.push_back(target);
        if (!kept.empty()) result.access_points.emplace(id, std::move(kept));
    }
    return result;
}
}

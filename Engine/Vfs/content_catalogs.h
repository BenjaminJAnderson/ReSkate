#pragma once
#include "Engine/Core/Json/json.h"
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

// Game catalogues read from the installed content cache (content_cache.h).
namespace dingosdk::content_cache {
// One tile of the native Object Browser: a family of object variants.
struct ObjectGroup {
    std::string id, title;
    std::uint32_t priority{};
};
// A build-kit category of the game's category service.
struct ObjectCategory {
    std::string id, title;
    std::string icon;       // a cdn:/ link, which cannot load offline
    std::uint32_t priority{};
    bool quick_drop{};      // listed by the Object Browser ("qdbuildkit")
    std::vector<ObjectGroup> groups;
};
// A travel destination (location_*): the level it is (field 3), its title,
// description, travel medium ("Water", "Door") and cdn:/ artwork.
struct TravelLocation {
    std::string id, level, name, description, medium, white_icon, black_icon, image;
};
struct Catalogs {
    bool available{};  // the pack is installed and was read
    // Lower-case owned asset id -> {"title", "description", "rarity_id", "group",
    // "object_type"} (each optional). Objects name their ObjectGroup in "group".
    Json items = Json::object();
    // Challenge id -> {"type", "asset", "available", "title_key", "goals", "neighborhood"}.
    Json challenges = Json::object();
    std::vector<std::string> entitlements;
    std::set<std::string> open_items;
    // Build-kit categories in record order.
    std::vector<ObjectCategory> object_categories;
    std::vector<TravelLocation> travel_locations;
    // Access point id (accesspoint_*) -> the location ids it offers.
    std::vector<std::pair<std::string, std::vector<std::string>>> travel_access_points;
    bool reserved(const std::string& key) const;
};
// Read once, on first use. Empty (available == false) when no pack is installed.
const Catalogs& catalogs();
Catalogs read_catalogs(const std::filesystem::path& folder);
}

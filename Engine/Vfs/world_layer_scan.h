#pragma once
// Builds the world-layer catalog from the installed level data: every
// SubWorldReferenceObjectData a map's bundles carry, minus the references the
// game drives itself (activity presentations, community-park lots) and ones
// whose bundle is not shipped.
#include "Engine/Game/World/world_layer_types.h"
#include <filesystem>
#include <string>

namespace dingosdk::world_layer_scan {
// Reads the level TOCs under `gameRoot`\Data. Takes a few seconds.
WorldLayerCatalog scan(const std::filesystem::path& gameRoot);

// The catalog cached for this game install in `file`, or a fresh scan written
// there when the cache is missing or its level TOCs changed.
WorldLayerCatalog load_or_scan(const std::filesystem::path& gameRoot, const std::filesystem::path& file);
// A catalog written by load_or_scan, read as it is (no game install needed):
// the dedicated server takes a copy of the players' cache file.
WorldLayerCatalog read(const std::filesystem::path& file);
// %LOCALAPPDATA%\ReSkate\cache\<build>\world-layers.json
std::filesystem::path cache_file();

std::string to_json(const WorldLayerCatalog& catalog, const std::string& stamp);
}

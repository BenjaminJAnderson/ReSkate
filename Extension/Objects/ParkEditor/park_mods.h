#pragma once
#include "park_document.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Parks shipped as mods: <Mods>/<folder>/parks/<map>.park.json beside the
// mod's manifest.json. A mod can hold one park per base map, installs and
// shares like any other mod, and loads as a preset from the overlay.
namespace dingosdk::editor {

struct ParkModDetails {
    std::string title, author, version, description; // manifest.json: name, author, version_number, description
};

struct ParkMod {
    std::string folder;
    ParkModDetails details;
    bool enabled{};
    std::vector<std::string> maps; // base maps it has a park for
};

// Every mod folder with a parks/ directory, in load order.
std::vector<ParkMod> list_park_mods(const std::filesystem::path &data_root);

std::filesystem::path park_mod_file(const std::filesystem::path &mods_root, std::string_view folder,
                                    std::string_view map);
ParkDocument load_mod_park(const std::filesystem::path &mods_root, std::string_view folder, std::string_view map);
void save_mod_park(const std::filesystem::path &mods_root, std::string_view folder, const ParkDocument &park);
// Writes manifest.json (name, author, version_number, description), keeping
// any other fields already in it.
void save_park_mod_details(const std::filesystem::path &mods_root, std::string_view folder,
                           const ParkModDetails &details);
// A new mod folder named after the title (made unique); returns the folder.
std::string create_park_mod(const std::filesystem::path &mods_root, const ParkModDetails &details);

} // namespace dingosdk::editor

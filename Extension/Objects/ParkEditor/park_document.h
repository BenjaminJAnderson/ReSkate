#pragma once
#include "Extension/Objects/object_placements.h"

namespace dingosdk::editor {
bool valid_park_name(std::string_view name);
struct ParkDocument {
    std::string map;
    profile::ObjectLayout objects;
};
std::string encode_park(const ParkDocument &);
ParkDocument decode_park(std::string_view);
void save_park(const std::filesystem::path &directory, std::string_view name, const ParkDocument &);
ParkDocument load_park(const std::filesystem::path &directory, std::string_view name);
std::vector<std::string> list_parks(const std::filesystem::path &directory);
} // namespace dingosdk::editor

#pragma once
#include "Engine/Core/Json/json.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::custom_scripts {
struct Script {
    std::string id;
    std::filesystem::path file;
    bool enabled{true};
    int order{1000};
};
std::vector<Script> discover(const std::filesystem::path& game_directory);
void save_enabled(const std::filesystem::path& game_directory, std::string_view id, bool enabled);
void save_order(const std::filesystem::path& game_directory, const std::vector<Script>& ordered);
std::string read_source(const Script& script);
Json read_config(const Script& script);
// Encode JSON as a Lua literal; strings are escaped independently of Lua source.
std::string lua_value(const Json& value);
void report(std::string_view id, std::string message);
std::string status(std::string_view id);
}

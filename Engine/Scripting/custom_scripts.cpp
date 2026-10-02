#include "custom_scripts.h"
#include "Engine/Vfs/initfs.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>

namespace dingosdk::custom_scripts {
namespace {
namespace fs = std::filesystem;
std::mutex settings_mutex, report_mutex;
std::map<std::string, std::string> reports;
std::string lower(std::string value) {
    for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 'a' - 'A');
    return value;
}
std::string utf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::string read_text(const fs::path& path, std::size_t maximum) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open " + utf8(path.filename()));
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > maximum)
        throw std::runtime_error("File exceeds size limit: " + utf8(path.filename()));
    std::string text(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!text.empty() && !input.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Cannot read " + utf8(path.filename()));
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    return text;
}
Json preferences(const fs::path& root) {
    const auto path = root / L"ReSkate.custom.json";
    Json value = fs::exists(path) ? Json::parse(read_text(path, 65536)) : Json::object();
    if (!value.is_object()) throw std::runtime_error("ReSkate.custom.json must contain an object");
    if (!value.contains("scripts")) value["scripts"] = Json::object();
    if (!value["scripts"].is_object()) throw std::runtime_error("ReSkate.custom.json: scripts must be an object");
    return value;
}
void write_preferences(const fs::path& root, const Json& value) {
    const auto text = value.dump(2) + '\n';
    if (text.size() > 65536) throw std::runtime_error("Custom script preferences exceed 64 KiB");
    const auto destination = root / L"ReSkate.custom.json";
    static std::atomic<unsigned> sequence{};
    const auto temporary = root / (L"ReSkate.custom-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(sequence.fetch_add(1)) + L".tmp");
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot save custom script preferences");
    DWORD written{};
    const bool saved = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
        written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Cannot publish custom script preferences");
    }
}
Json& preference_row(Json& settings, std::string_view id) {
    auto& rows = settings["scripts"];
    const auto key = lower(std::string(id));
    if (!rows.contains(key)) rows[key] = Json::object();
    if (!rows[key].is_object()) throw std::runtime_error("Invalid preferences for " + std::string(id));
    return rows[key];
}
bool valid_id(std::string_view id) {
    return lower(std::string(id)).ends_with(".lua") &&
        initfs::loose_relative_path("Scripts/Custom/" + std::string(id)).has_value();
}
}

std::vector<Script> discover(const fs::path& game_directory) {
    std::lock_guard lock(settings_mutex);
    auto settings = preferences(game_directory);
    const auto root = game_directory / L"scripts" / L"Custom";
    std::vector<Script> scripts;
    if (!fs::exists(root)) return scripts;
    for (const auto& entry : fs::directory_iterator(root)) {
        const auto name = utf8(entry.path().filename());
        if (name.empty() || name[0] == '_' || name[0] == '.') continue;
        auto file = entry.path();
        if (entry.is_directory()) file /= L"init.lua";
        else if (lower(utf8(file.extension())) != ".lua") continue;
        if (!fs::is_regular_file(file)) continue;
        const auto id = utf8(file.lexically_relative(root));
        if (!valid_id(id)) continue;
        if (scripts.size() >= 256) throw std::runtime_error("Custom script limit reached (256 entries)");
        auto& row = preference_row(settings, id);
        if (row.contains("order") && (!row["order"].is_number_integer() ||
                row["order"] < -100000 || row["order"] > 100000))
            throw std::runtime_error("Load order must be an integer between -100000 and 100000 for " + id);
        if (row.contains("enabled") && !row["enabled"].is_boolean())
            throw std::runtime_error("Enabled must be true or false for " + id);
        const auto order = row.value<std::int64_t>("order", 1000);
        scripts.push_back({id, file, row.value("enabled", true), static_cast<int>(order)});
    }
    std::sort(scripts.begin(), scripts.end(), [](const Script& a, const Script& b) {
        return a.order != b.order ? a.order < b.order : lower(a.id) < lower(b.id);
    });
    return scripts;
}
void save_enabled(const fs::path& root, std::string_view id, bool enabled) {
    if (!valid_id(id)) throw std::runtime_error("Invalid custom script name");
    std::lock_guard lock(settings_mutex);
    auto settings = preferences(root);
    preference_row(settings, id)["enabled"] = enabled;
    write_preferences(root, settings);
}
void save_order(const fs::path& root, const std::vector<Script>& ordered) {
    std::lock_guard lock(settings_mutex);
    auto settings = preferences(root);
    int index = 0;
    for (const auto& script : ordered) {
        if (!valid_id(script.id)) throw std::runtime_error("Invalid custom script name");
        preference_row(settings, script.id)["order"] = ++index;
    }
    write_preferences(root, settings);
}
std::string read_source(const Script& script) { return read_text(script.file, 1024 * 1024); }
Json read_config(const Script& script) {
    const auto path = script.file.parent_path() / (script.file.stem().wstring() + L".settings.json");
    if (!fs::exists(path)) return Json::object();
    auto result = Json::parse(read_text(path, 65536));
    if (!result.is_object()) throw std::runtime_error("Script settings must contain a JSON object");
    return result;
}
std::string lua_value(const Json& value) {
    if (value.is_null()) return "nil";
    if (value.is_boolean() || value.is_number()) return value.dump();
    if (value.is_string()) {
        std::string result = "\"";
        for (const unsigned char ch : value.string()) {
            if (ch >= 32 && ch <= 126 && ch != '"' && ch != '\\') result += static_cast<char>(ch);
            else {
                result += '\\';
                result += static_cast<char>('0' + ch / 100);
                result += static_cast<char>('0' + (ch / 10) % 10);
                result += static_cast<char>('0' + ch % 10);
            }
        }
        return result + '"';
    }
    std::string result = "{";
    if (value.is_array()) {
        std::size_t index = 0;
        for (const auto& item : value) result += '[' + std::to_string(++index) + "]=" + lua_value(item) + ',';
    } else {
        for (const auto& [key, item] : value.items()) result += '[' + lua_value(Json(key)) + "]=" + lua_value(item) + ',';
    }
    return result + '}';
}
void report(std::string_view id, std::string message) {
    std::lock_guard lock(report_mutex);
    reports[lower(std::string(id))] = std::move(message);
}
std::string status(std::string_view id) {
    std::lock_guard lock(report_mutex);
    const auto found = reports.find(lower(std::string(id)));
    return found == reports.end() ? "Not loaded this session" : found->second;
}
}

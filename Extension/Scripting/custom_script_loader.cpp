#include "custom_script_loader.h"
#include "lua_startup.h"
#include "Engine/Vfs/initfs.h"
#include "Engine/Scripting/custom_scripts.h"
#include "Engine/Core/Log/logging.h"
#include "embedded_custom_entry.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <filesystem>

namespace dingosdk {
namespace {
std::filesystem::path game_root;
std::atomic<bool> ran{};
bool load_scripts(const lua_startup::Context& context) {
    if (ran.exchange(true)) return false;
    std::vector<custom_scripts::Script> scripts;
    try {
        scripts = custom_scripts::discover(game_root);
        custom_scripts::report("", "Startup discovery complete");
    } catch (const std::exception& error) {
        logging::log(logging::Level::error, logging::Channel::assets, "Custom script discovery failed: {}", error.what());
        custom_scripts::report("", error.what());
        return false;
    }
    bool executed = false;
    std::size_t loaded = 0, failed = 0, disabled = 0;
    for (const auto& script : scripts) {
        if (!script.enabled) { ++disabled; custom_scripts::report(script.id, "Disabled at launch"); continue; }
        try {
            const auto source = custom_scripts::read_source(script);
            const auto settings = custom_scripts::read_config(script);
            std::string wrapper = "local id=" + custom_scripts::lua_value(script.id) +
                "\nlocal source=" + custom_scripts::lua_value(source) +
                "\nlocal settings=" + custom_scripts::lua_value(settings) + '\n';
            wrapper += custom_entry_lua;
            custom_scripts::report(script.id, "Loading");
            logging::log(logging::Level::info, logging::Channel::assets, "Loading custom script: {}", script.id);
            executed = true;
            if (context.execute(wrapper)) {
                ++loaded; custom_scripts::report(script.id, "Loaded");
                logging::log(logging::Level::info, logging::Channel::assets, "Custom script loaded: {}", script.id);
            } else {
                ++failed; custom_scripts::report(script.id, "Lua error; see Lua console output");
                logging::log(logging::Level::error, logging::Channel::assets,
                    "Custom script failed: {}. Lua reported a compilation or execution error; continuing with other scripts.", script.id);
            }
        } catch (const std::exception& error) {
            ++failed; custom_scripts::report(script.id, error.what());
            logging::log(logging::Level::error, logging::Channel::assets, "Custom script {} failed: {}", script.id, error.what());
        }
    }
    logging::log(logging::Level::info, logging::Channel::assets,
        "Custom scripts: {} loaded, {} failed, {} disabled.", loaded, failed, disabled);
    custom_scripts::report("", std::to_string(loaded) + " loaded, " + std::to_string(failed) +
        " failed, " + std::to_string(disabled) + " disabled at startup");
    return executed;
}
}
bool start_custom_script_loader(std::uintptr_t base, std::string& error) {
    error.clear();
    try {
        std::array<wchar_t, 32768> path{};
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) { error = "Cannot locate the custom script directory"; return false; }
        game_root = std::filesystem::canonical(std::filesystem::path(std::wstring(path.data(), length))).parent_path();
        if (!initfs::loose_files_session_enabled(game_root)) {
            custom_scripts::report("", "Custom scripts disabled for this session");
            return true;
        }
        return lua_startup::add_callback(base, &load_scripts, error);
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
}

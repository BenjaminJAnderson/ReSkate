// ReSkate dedicated server: a headless session host that players find in the
// in-game server browser. Runs from its own folder, next to steam_api64.dll and
// the Steam client files (steamclient64.dll, tier0_s64.dll, vstdlib_s64.dll).
#include "server_config.h"
#include "server_host.h"
#include "server_update.h"
#include "steam_server.h"
#include "Extension/Multiplayer/Session/monotonic_clock.h"
#include "Engine/Core/Text/word_filter.h"
#include "Engine/Game/World/world_layer_catalog.h"
#include "Engine/Game/World/world_names.h"
#include "Engine/Vfs/world_layer_scan.h"
#include <Windows.h>
#include <timeapi.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace dingosdk;
using namespace dingosdk::server;

namespace {
std::atomic<bool> stopping{};
// run() returns this when a newer server release should be installed.
constexpr int restart_for_update = -2;
std::string update_version;
std::mutex log_mutex;
std::ofstream log_file;
void write_log(const std::string &text) {
    std::lock_guard lock(log_mutex);
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32]{};
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);
    std::printf("[%s] %s\n", stamp + 11, text.c_str());
    if (log_file) log_file << '[' << stamp << "] " << text << std::endl;
}
std::atomic<bool> finished{};
BOOL WINAPI on_console(DWORD event) {
    stopping = true;
    // Closing the window ends the process as soon as this returns. Wait (Windows
    // allows about 5 s) for the server to sign out of Steam, so its entry leaves
    // the server list instead of lingering there.
    if (event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT)
        for (int i = 0; i < 90 && !finished; ++i) Sleep(50);
    return TRUE;
}
std::filesystem::path folder() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Cannot find ReSkateServer.exe");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}
// Console lines, read on their own thread so the network loop never waits for typing.
// One reader for the whole process: run() can start again after a failed update.
struct Input {
    std::mutex mutex;
    std::deque<std::string> lines;
    void run() {
        std::string line;
        while (!stopping && std::getline(std::cin, line)) {
            std::lock_guard lock(mutex);
            lines.push_back(line);
        }
    }
    std::deque<std::string> take() {
        std::lock_guard lock(mutex);
        return std::exchange(lines, {});
    }
};
Input &console_input() {
    static Input input;
    static std::once_flag started;
    std::call_once(started, [] { std::thread([] { input.run(); }).detach(); });
    return input;
}
constexpr auto update_interval = std::chrono::minutes(30);
} // namespace

int run(int argc, wchar_t **argv, bool skip_update) {
    // Unbuffered, so a hosting panel reading the pipe sees each line at once.
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(on_console, TRUE);
    const auto here = folder();
    // Release builds: ReSkateServer --export-world-layers "<Skate folder>" "<file>" writes the
    // players' world-layer catalog from the game's level data (release\build_release.bat).
    if (argc == 4 && std::wstring(argv[1]) == L"--export-world-layers") {
        try {
            const auto catalog = world_layer_scan::scan(argv[2]);
            if (catalog.layers.empty()) throw std::runtime_error("no world layers found; is that the Skate folder?");
            std::ofstream out(argv[3], std::ios::binary | std::ios::trunc);
            out << world_layer_scan::to_json(catalog, "server");
            if (!out) throw std::runtime_error("cannot write the file");
            std::printf("Wrote %zu world layers.\n", catalog.layers.size());
            return 0;
        } catch (const std::exception &e) {
            std::printf("World layer export failed: %s\n", e.what());
            return 1;
        }
    }
    auto config_file = here / L"ReSkateServer.json";
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wstring(argv[i]) == L"--config") config_file = argv[++i];
    if (!log_file.is_open()) log_file.open(here / L"ReSkateServer.log", std::ios::app);

    ServerConfig config;
    try {
        const bool fresh = !std::filesystem::exists(config_file);
        std::vector<std::string> added;
        config = load_config(config_file, &added);
        if (fresh) write_log("Wrote a default " + config_file.filename().string() + ". Edit it to name the server and add admins.");
        if (!added.empty()) {
            std::string names;
            for (const auto &name : added) names += (names.empty() ? "" : ", ") + name;
            write_log("Added new settings to " + config_file.filename().string() + " with their defaults: " + names + ".");
        }
    } catch (const std::exception &e) {
        write_log("Cannot read " + config_file.string() + ": " + e.what());
        return 1;
    }
    // Maps: the retail ones and custom maps from Mods\<mod>\reskate-levels.json.
    for (const auto &problem : load_levels(here / L"Mods")) write_log("Mods: skipped " + problem);
    if (levels().size() > 6) write_log("Mods: " + std::to_string(levels().size() - 6) + " custom map(s).");
    // Older configs name the map by its full destination; keep the plain name instead.
    if (const auto setting = map_setting(config.map); setting != config.map && !setting.empty()) {
        config.map = setting;
        try { save_config(config); } catch (...) {}
    }
    if (const auto error = config_error(config); !error.empty()) {
        write_log("Config problem: " + error);
        return 1;
    }
    // Updates: at startup, then every half hour while running (installed once
    // nobody is on). --no-update or "auto_update": false turns them off.
    remove_previous_update(here);
    bool auto_update = config.auto_update && updates_enabled() && !skip_update;
    for (int i = 1; i < argc; ++i)
        if (std::wstring(argv[i]) == L"--no-update") auto_update = false;
    if (auto_update) {
        write_log("Checking for updates...");
        const auto check = check_for_update();
        if (check.available) {
            update_version = check.version;
            write_log("Server update " + update_version + " found.");
            return restart_for_update;
        }
        write_log(check.problem.empty() ? "The server is up to date (" + check.version + ")."
                                        : "Update check skipped: " + check.problem + ".");
    }
    // World layers are optional: without the players' catalog every player keeps their own.
    if (const auto catalog = here / L"world-layers.json"; std::filesystem::exists(catalog)) {
        try {
            install_world_layer_catalog(world_layer_scan::read(catalog));
            write_log("World layers: " + std::to_string(world_layers().size()) + " from world-layers.json.");
        } catch (const std::exception &e) {
            write_log(std::string("world-layers.json is unreadable; world layer sync is off: ") + e.what());
        }
    }

    SteamServer steam;
    std::string error;
    if (!steam.start(here, config.port, config.query_port, error)) {
        write_log(error);
        return 1;
    }
    write_log("Signing in to Steam...");
    const auto login_started = std::chrono::steady_clock::now();
    while (!steam.logged_on() && !stopping) {
        steam.run_callbacks();
        if (std::chrono::steady_clock::now() - login_started > std::chrono::seconds(60)) {
            write_log("Steam sign-in timed out after 60 s. Check the internet connection and try again.");
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (stopping) return 0;

    multiplayer::SteamTransport transport;
    if (!transport.open_game_server(steam.module())) {
        write_log("Steam networking failed: " + transport.status().detail);
        return 1;
    }
    Host host(config, transport, write_log);
    if (!host.start(error)) {
        write_log("Could not open the server: " + error);
        return 1;
    }
    write_log(config.name + " is up on " + host.map_name() + " for " + std::to_string(config.max_players) + " players.");
    write_log("Steam ID " + std::to_string(steam.steam_id()) + ", public IP " + steam.public_ip() + ".");
    write_log("Join code: " + host.invite() + (config.password.empty() ? "" : " (password required)"));
    write_log(config.admins.empty() ? "No admins yet: type \"admin add <SteamID64>\" to add one."
                              : std::to_string(config.admins.size()) + " admin(s). Type help for commands.");

    auto &input = console_input();
    timeBeginPeriod(1);
    auto next_advertise = std::chrono::steady_clock::now();
    std::optional<bool> name_allowed; // last seen: whether the name may be listed
    auto next_update_check = next_advertise + update_interval;
    std::future<UpdateCheck> update_check;
    bool update_now{}, update_waiting{}, restart{};
    while (!stopping && !restart) {
        steam.run_callbacks();
        try {
            host.tick(multiplayer::now_us());
        } catch (const std::exception &e) {
            write_log(std::string("Server error: ") + e.what());
        }
        for (const auto &line : input.take()) {
            if (line == "quit" || line == "exit" || line == "stop") {
                stopping = true;
                break;
            }
            if (line.empty()) continue;
            // "update": check now and install straight away, even with players on.
            if (line == "update") {
                if (!updates_enabled()) {
                    write_log("This is a local build; it doesn't update itself.");
                } else if (update_waiting) {
                    write_log("Restarting to install server update " + update_version + ".");
                    restart = true;
                } else {
                    update_now = true;
                    if (!update_check.valid()) update_check = std::async(std::launch::async, check_for_update);
                    write_log("Checking for updates...");
                }
                continue;
            }
            try {
                write_log(host.command(line));
            } catch (const std::exception &e) {
                write_log(std::string("Command failed: ") + e.what());
            }
        }
        const auto now_time = std::chrono::steady_clock::now();
        if (auto_update && !update_waiting && !update_check.valid() && now_time >= next_update_check)
            update_check = std::async(std::launch::async, check_for_update);
        if (update_check.valid() && update_check.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto check = update_check.get();
            next_update_check = now_time + update_interval;
            if (check.available) {
                update_version = check.version;
                update_waiting = true;
                if (!update_now)
                    write_log("Server update " + update_version + " is ready; the server restarts to install it when nobody is on.");
            } else if (update_now) {
                write_log(check.problem.empty() ? "The server is up to date (" + check.version + ")."
                                                : "Update check failed: " + check.problem + ".");
            }
            if (update_waiting && update_now) {
                write_log("Restarting to install server update " + update_version + ".");
                restart = true;
            }
            update_now = false;
        }
        if (update_waiting && !restart && host.players() == 0) {
            write_log("Nobody is on; restarting to install server update " + update_version + ".");
            restart = true;
        }
        if (const auto now = std::chrono::steady_clock::now(); now >= next_advertise) {
            next_advertise = now + std::chrono::seconds(2);
            // A name with a bad word in it is never listed (clients hide one too); the
            // server still runs and players can join with its code.
            const bool allowed = !text::contains_bad_words(config.name);
            if (name_allowed != allowed) {
                if (!allowed)
                    write_log("The server name \"" + config.name + "\" contains blocked words, so the server is not listed "
                              "in the server browser. Rename it with: name <new name>");
                else if (name_allowed)
                    write_log(config.listed ? "The server name is allowed again; the server is listed."
                                            : "The server name is allowed again (the server is still set to unlisted).");
                name_allowed = allowed;
            }
            steam.advertise({config.name, host.map_name(), host.players(), config.max_players, !config.password.empty(),
                             config.listed && allowed, host.secret()});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    timeEndPeriod(1);
    if (update_check.valid()) update_check.wait();
    write_log(restart ? "Restarting for an update." : "Shutting down.");
    host.stop(restart ? "The server is restarting for an update. Rejoin in a minute." : "The server is shutting down.");
    // Leaving scope closes the networking before Steam itself shuts down.
    return restart ? restart_for_update : 0;
}

int wmain(int argc, wchar_t **argv) {
    bool skip_update{};
    int code{};
    for (;;) {
        code = run(argc, argv, skip_update);
        if (code != restart_for_update) break;
        // Steam has shut down, so every server file can be replaced now.
        try {
            write_log("Installing server update " + update_version + "...");
            install_update(folder());
            write_log("Server update " + update_version + " installed; starting it.");
            if (relaunch()) {
                code = 0;
                break;
            }
            write_log("Could not start the updated server; start ReSkateServer.exe again.");
            code = 1;
            break;
        } catch (const std::exception &e) {
            write_log(std::string("Server update failed (") + e.what() + "); carrying on with this version.");
            skip_update = true;
        }
    }
    finished = true;
    return code;
}

#pragma once

#include "launch.h"
#include "text_encoding.h"
#include "thunderstore.h"

#include "Engine/Vfs/mod_list.h"
#include "Extension/UI/skate_theme.h"

#include <Windows.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Shared by the launcher window's files (gui*.cpp).
namespace dingosdk::launcher_gui::detail {

namespace fs = std::filesystem;
namespace update = launcher_update;

// ---------------------------------------------------------------- look

constexpr float design_width = 1180.0f;
constexpr float design_height = 680.0f;

inline ImU32 rgba(int r, int g, int b, float a = 1.0f) {
    return IM_COL32(r, g, b, static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f));
}
namespace color {
inline const ImU32 background_top = rgba(9, 11, 15);
inline const ImU32 background_bottom = rgba(17, 20, 27);
inline const ImU32 text = rgba(236, 239, 244);
inline const ImU32 muted = rgba(128, 137, 151);
inline const ImU32 danger = rgba(255, 92, 92);
inline const ImU32 panel = rgba(26, 26, 26, 0.97f);
// Sampled from skate.'s own menus (HUB screen).
inline const ImU32 tile = skate_theme::tile;
inline const ImU32 tile_grey = skate_theme::tile_light;
inline const ImU32 blue = skate_theme::blue;
inline const ImU32 good = skate_theme::good;
inline const ImU32 warning = skate_theme::warning;
inline const ImU32 avatar = skate_theme::avatar;
inline const ImU32 ink = skate_theme::black;
inline const ImU32 outline = rgba(255, 255, 255, 0.12f);
}

// Blue button with black text, the colours of skate.'s selected tile.
using skate_theme::push_primary_button;
using skate_theme::pop_primary_button;

struct Fonts {
    ImFont* body{};     // Montserrat SemiBold
    ImFont* caption{};
    ImFont* bold{};     // Montserrat ExtraBold
    ImFont* heading{};
    ImFont* tile{};     // tile headers, like the HUB's "BOUNTIES"
    ImFont* action{};   // the PLAY tile
    ImFont* title{};    // brushed page title, like the HUB's "HUB"
};

inline float g_scale = 1.0f;
inline float S(float value) { return value * g_scale; }

// Optional photo behind the launcher; zero id means the drawn background.
struct Background {
    ImTextureID id{};
    float width{}, height{};
};
inline Background g_background;
// White tile icons (assets/launcher/icon_*.png), drawn faded like the HUB's.
inline Background g_icon_mods, g_icon_settings;

using launcher_text::utf8;
using launcher_text::wide;

class Renderer;
// The window's renderer, for textures made after startup (package icons).
inline Renderer* g_renderer{};

void panel_title(const Fonts& fonts, const char* text);
// A centred modal panel; returns its size.
ImVec2 begin_panel(const char* id, ImVec2 size, ImVec2 panel);
void open_path(const fs::path& path);
// Opens an https:// page in the default browser; anything else is ignored.
void open_url(std::string_view url);

// ---------------------------------------------------------------- window

inline bool g_drag_allowed = true;
// Set while Settings waits for a key to bind; the window procedure stores the
// next key press here instead of letting ImGui see it.
inline std::atomic<bool> g_capturing_key{};
inline std::atomic<unsigned> g_captured_key{};
// Paths dropped on the window, picked up by the next frame.
inline std::mutex g_dropped_mutex;
inline std::vector<fs::path> g_dropped;

// ---------------------------------------------------------------- settings

struct Settings {
    bool windowed{};
    int width{1920};
    int height{1080};
    bool loose_files{true};
    bool gpu_diagnostics{};
    bool offline{};
    int menu_key{static_cast<int>(launcher::default_menu_key)};
    int console_key{static_cast<int>(launcher::default_console_key)};
    int log_level{2};
    std::string arguments;
    bool close_on_launch{true};
    // Off: never replace ReSkate.dll or the launcher (keeps a test build someone handed out).
    bool updates{true};
    // Off: a crash uploads nothing (RESKATE_CRASH_REPORTING=0 for the launcher and the game).
    bool crash_reports{true};
    std::string steam_username;   // never the password
    bool steam_remember{true};
    bool steam_prefer_code{};
};

inline constexpr std::array<const char*, 7> log_levels{"trace", "debug", "info", "warning", "error", "critical", "off"};

// ---------------------------------------------------------------- state

enum class Phase { checking, update_available, updating, game_missing, game_outdated, downloading,
                   ready, launching, failed };

struct State {
    Phase phase{Phase::checking};
    std::string status{"Checking for updates"};
    std::string detail;
    float progress{-1};
    std::vector<std::string> qr;
    std::optional<update::Prompt> prompt;
    std::optional<update::Config> config;
};

// Update checks, the Steam download and the game launch, one at a time on a worker thread.
class Launcher {
public:
    Launcher(const launcher_app::Session& session, std::vector<std::wstring> arguments);
    ~Launcher();

    State snapshot();
    bool busy() const { return busy_; }
    bool restart_requested() const { return restart_; }
    // Process id of the Skate this launcher started, until game_exited().
    DWORD game() const { return game_; }
    // Injection finished and the game is running on its own.
    bool launched() const { return launched_; }
    // Called once the started game has exited; `seen` if its splash screen ever opened.
    void game_exited(bool seen);
    Settings& settings() { return settings_; }
    const launcher_app::Session& session() const { return session_; }
    void save();

    void check();
    void apply_updates();
    // `qr` signs in with a QR code; otherwise the saved Steam username is used.
    // The password only lives in memory until DepotDownloader asks for it; it
    // may be empty when DepotDownloader remembers the login.
    void download(bool validate, bool qr, std::string password);
    void play();
    void cancel();
    // Answers the pending Steam prompt; nothing cancels the download.
    void answer(std::optional<std::string> value);

    void restart();

private:
    launcher_app::Session session_;
    std::vector<std::wstring> arguments_;
    Settings settings_;
    bool relaunched_{};
    bool binaries_{true};
    std::mutex mutex_;
    std::condition_variable answered_;
    State state_;
    std::string password_;
    bool qr_login_{true};
    std::optional<std::string> answer_;
    bool has_answer_{};
    std::thread worker_;
    std::atomic<bool> busy_{};
    std::atomic<bool> cancel_{};
    std::atomic<bool> restart_{};
    std::atomic<DWORD> game_{};
    std::atomic<bool> launched_{};

    template<class Task> void start(Task task);
    static void wipe(std::string& value);
    std::optional<std::string> prompt(const update::Prompt& prompt);
    void set(Phase phase, std::string status, std::string detail = {}, float progress = -1);
    void set_progress(std::string detail, float progress);
    void fail(const std::string& message);
    std::optional<update::Config> config();
    update::Progress progress_for(std::string label);
    bool launcher_outdated(const update::Config& config) const;
    bool runtime_outdated(const update::Config& config) const;
    void run_check();
    void run_updates();
    void run_download(bool validate);
    void run_play();
};

// Panels the main screen can show; one at a time.
struct Ui {
    bool settings{};
    int settings_tab{};         // GAME, DISPLAY, KEYS, ADVANCED
    int binding{};              // 1 = menu key, 2 = console key, while waiting for a press
    std::string key_error;
    bool mods{};
    bool sign_in{};
    bool sign_in_validate{};
    bool focus{};
    std::array<char, 65> username{};
    std::array<char, 256> password{};
    std::array<char, 16> code{};
    // The MODS tile's "2 of 3 enabled", re-read every few seconds.
    std::string mods_detail;
    double mods_checked{-100};
    // Steam display name for the name plate, re-read every few seconds.
    std::string steam_name;
    double steam_checked{-100};
};

// The Thunderstore listing, fetched in the background when the launcher
// starts and again on Refresh. `packages` belongs to the UI thread; the worker
// hands a finished fetch over through `incoming`.
struct Store {
    std::thread worker;
    std::atomic<bool> loading{};
    std::atomic<bool> stop{};
    std::mutex mutex;
    bool arrived{};                                  // worker -> UI, under mutex
    std::vector<thunderstore::Package> incoming;
    std::string incoming_error;

    bool loaded{};                                   // a listing arrived (it may be empty)
    std::string error;                               // why the last fetch failed
    std::vector<thunderstore::Package> packages;
    double fetched{-1e9};                            // ImGui time the last fetch started

    // The BROWSE page.
    std::array<char, 96> search{};
    std::string category;                            // empty = all
    int sort{};
    std::string selected;                            // full_name

    ~Store() {
        stop = true;
        if (worker.joinable()) worker.join();
    }
};

// Package icons: fetched and decoded on a worker (cached on disk under
// %LOCALAPPDATA%\ReSkate\thunderstore\icons), uploaded on the UI thread.
struct Icons {
    struct Entry { ImTextureID id{}; bool queued{}; bool failed{}; int used{}; };
    std::map<std::string, Entry, std::less<>> entries;   // by icon URL; UI thread
    struct Decoded { std::string url; std::vector<unsigned char> pixels; unsigned width{}, height{}; };

    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> queue;
    std::vector<Decoded> done;
    bool stop{};

    ~Icons() {
        { std::lock_guard lock(mutex); stop = true; }
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
};

// The Mods panel's list plus one background install at a time.
struct ModsPanel {
    bool scanned{};
    fs::path root;
    mods::ModList list;
    int selected{-1};
    int tab{};                           // 0 INSTALLED, 1 BROWSE
    std::string message;
    bool message_error{};
    std::string confirm_remove;          // folder awaiting "Remove" confirmation
    fs::path conflict_source;            // install waiting for "Replace" confirmation
    std::string conflict_name;

    std::thread worker;
    std::atomic<bool> installing{};
    std::atomic<bool> cancel{};
    std::atomic<float> progress{-1};
    std::mutex mutex;
    std::string activity;                // what the worker is doing, under mutex
    bool finished{};
    std::string finished_name, finished_error, finished_conflict, finished_note;
    fs::path finished_source;

    Store store;
    Icons icons;

    ~ModsPanel() {
        cancel = true;
        if (worker.joinable()) worker.join();
    }
};

void scan(ModsPanel& panel, const launcher_app::Session& session);
void start_install(ModsPanel& panel, const fs::path& source, bool replace);

// ---------------------------------------------------------------- Thunderstore (gui_mods_browse.cpp)

// Starts a listing fetch when none ran yet, or when `force`.
void refresh_listing(ModsPanel& panel, double time, bool force = false);
// Takes over a fetch the worker finished; call once a frame.
void collect_listing(ModsPanel& panel);
thunderstore::Installed installed_versions(const mods::ModList& list);
// The package an installed mod folder came from, if the listing has it.
const thunderstore::Package* package_for(const Store& store, std::string_view folder);
std::vector<const thunderstore::Package*> updates(const Store& store, const thunderstore::Installed& installed);
// Downloads and installs (or updates) each package in turn on the panel's worker.
void start_store_install(ModsPanel& panel, std::vector<thunderstore::Package> packages);
// The package's icon texture, or empty while it loads; uploads finished icons.
ImTextureID package_icon(ModsPanel& panel, const thunderstore::Package& package);
void pump_icons(ModsPanel& panel);
// The BROWSE page body, between the tabs and the footer.
void browse_page(const Fonts& fonts, ModsPanel& panel, float height, bool installing);

void open_sign_in(Launcher& launcher, Ui& ui, bool validate);
void sign_in_window(Launcher& launcher, const Fonts& fonts, ImVec2 size, Ui& ui);
void prompt_window(Launcher& launcher, const Fonts& fonts, ImVec2 size, const update::Prompt& prompt, Ui& ui);
void qr_window(Launcher& launcher, const Fonts& fonts, ImVec2 size, const std::vector<std::string>& rows);
void settings_window(Launcher& launcher, const Fonts& fonts, ImVec2 size, Ui& ui);
void mods_window(Launcher& launcher, const Fonts& fonts, ImVec2 size, Ui& ui, ModsPanel& panel, HWND window);

// The main screen: background, tiles, status and whichever panel is open.
void frame(Launcher& launcher, const Fonts& fonts, HWND window, Ui& ui, ModsPanel& mods_panel);

} // namespace dingosdk::launcher_gui::detail

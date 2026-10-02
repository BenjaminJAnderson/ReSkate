#include "gui_internal.h"

#include "depot_output.h"

#include "Engine/Core/Json/json.h"
#include "Engine/Core/Log/logging.h"

#include <shellapi.h>

#include <format>
#include <fstream>
#include <stdexcept>

namespace dingosdk::launcher_gui::detail {
namespace {

std::wstring widen(std::string_view value) {
    if (value.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring output(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length);
    return output;
}

std::string format_bytes(std::uint64_t bytes) {
    if (bytes >= 1024ull * 1024 * 1024) return std::format("{:.1f} GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024));
}

fs::path settings_path(const launcher_app::Session& session) {
    return session.self.parent_path() / L"ReSkateLauncher.settings.json";
}

Settings load_settings(const fs::path& path) {
    Settings settings;
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) return settings;
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto json = Json::parse(text);
        settings.windowed = json.value("windowed", settings.windowed);
        settings.width = std::clamp(json.value("width", settings.width), 320, 16384);
        settings.height = std::clamp(json.value("height", settings.height), 200, 16384);
        settings.loose_files = json.value("loose_files", settings.loose_files);
        settings.gpu_diagnostics = json.value("gpu_diagnostics", settings.gpu_diagnostics);
        settings.offline = json.value("offline", settings.offline);
        settings.menu_key = json.value("menu_key", settings.menu_key);
        settings.console_key = json.value("console_key", settings.console_key);
        if (!launcher::bindable_key(static_cast<unsigned>(settings.menu_key)) ||
            !launcher::bindable_key(static_cast<unsigned>(settings.console_key)) ||
            settings.menu_key == settings.console_key) {
            settings.menu_key = static_cast<int>(launcher::default_menu_key);
            settings.console_key = static_cast<int>(launcher::default_console_key);
        }
        const auto level = json.value("log_level", std::string("info"));
        for (std::size_t index = 0; index < log_levels.size(); ++index)
            if (level == log_levels[index]) settings.log_level = static_cast<int>(index);
        settings.arguments = json.value("arguments", std::string());
        settings.close_on_launch = json.value("close_on_launch", settings.close_on_launch);
        settings.updates = json.value("updates", settings.updates);
        settings.crash_reports = json.value("crash_reports", settings.crash_reports);
        settings.steam_username = json.value("steam_username", std::string());
        settings.steam_remember = json.value("steam_remember", settings.steam_remember);
        settings.steam_prefer_code = json.value("steam_prefer_code", settings.steam_prefer_code);
    } catch (const std::exception& exception) {
        logging::log(logging::Level::warning, logging::Channel::launcher, "Ignoring launcher settings: {}", exception.what());
    }
    return settings;
}

void save_settings(const fs::path& path, const Settings& settings) {
    auto json = Json::object();
    json["windowed"] = settings.windowed;
    json["width"] = settings.width;
    json["height"] = settings.height;
    json["loose_files"] = settings.loose_files;
    json["gpu_diagnostics"] = settings.gpu_diagnostics;
    json["offline"] = settings.offline;
    json["menu_key"] = settings.menu_key;
    json["console_key"] = settings.console_key;
    json["log_level"] = log_levels[static_cast<std::size_t>(settings.log_level)];
    json["arguments"] = settings.arguments;
    json["close_on_launch"] = settings.close_on_launch;
    json["updates"] = settings.updates;
    json["crash_reports"] = settings.crash_reports;
    json["steam_username"] = settings.steam_username;
    json["steam_remember"] = settings.steam_remember;
    json["steam_prefer_code"] = settings.steam_prefer_code;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << json.dump(2) << '\n';
}

// Turns saved settings into the same flags the command line accepts, so both
// paths share parse_launch_options' validation.
launcher::LaunchOptions launch_options(const Settings& settings) {
    std::vector<std::wstring> arguments;
    if (settings.windowed) {
        arguments.push_back(std::format(L"--width={}", settings.width));
        arguments.push_back(std::format(L"--height={}", settings.height));
    }
    if (!settings.loose_files) arguments.emplace_back(L"--no-loose-files");
    if (settings.gpu_diagnostics) arguments.emplace_back(L"--gpu-diagnostics");
    if (settings.offline) arguments.emplace_back(L"-offline");
    arguments.push_back(std::format(L"--menu-key={}", settings.menu_key));
    arguments.push_back(std::format(L"--console-key={}", settings.console_key));
    arguments.push_back(L"--log-level=" + widen(log_levels[static_cast<std::size_t>(settings.log_level)]));
    if (settings.arguments.find_first_not_of(" \t") != std::string::npos) {
        int count{};
        const auto line = L"skate " + widen(settings.arguments);
        if (auto** split = CommandLineToArgvW(line.c_str(), &count)) {
            for (int index = 1; index < count; ++index) arguments.emplace_back(split[index]);
            LocalFree(split);
        }
    }
    return launcher::parse_launch_options(arguments);
}

} // namespace

Launcher::Launcher(const launcher_app::Session& session, std::vector<std::wstring> arguments)
    : session_(session), arguments_(std::move(arguments)), settings_(load_settings(settings_path(session))) {
    relaunched_ = std::find(arguments_.begin(), arguments_.end(), L"--reskate-updated") != arguments_.end();
    binaries_ = std::find(arguments_.begin(), arguments_.end(), L"--no-update") == arguments_.end();
}

Launcher::~Launcher() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

State Launcher::snapshot() {
    std::lock_guard lock(mutex_);
    return state_;
}

void Launcher::game_exited(bool seen) {
    game_ = 0;
    launched_ = false;
    if (busy_ || snapshot().phase == Phase::failed) return;  // the failed launch explains itself
    if (seen) check();
    else fail("Skate closed while it was starting.");
}

void Launcher::save() {
    save_settings(settings_path(session_), settings_);
    // The game inherits the launcher's environment, so this reaches the next launch; the
    // launcher's own crash reporting follows the setting from its next start.
    SetEnvironmentVariableW(L"RESKATE_CRASH_REPORTING", settings_.crash_reports ? nullptr : L"0");
}

template<class Task> void Launcher::start(Task task) {
    if (busy_) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    cancel_ = false;
    worker_ = std::thread([this, task] {
        try { task(); }
        catch (const std::exception& exception) { fail(exception.what()); }
        busy_ = false;
    });
}

void Launcher::check() { start([this] { run_check(); }); }
void Launcher::apply_updates() { start([this] { run_updates(); }); }

void Launcher::download(bool validate, bool qr, std::string password) {
    {
        std::lock_guard lock(mutex_);
        wipe(password_);
        password_ = std::move(password);
        qr_login_ = qr;
    }
    start([this, validate] { run_download(validate); });
}

void Launcher::play() { start([this] { run_play(); }); }

void Launcher::cancel() {
    cancel_ = true;
    answered_.notify_all();
}

void Launcher::answer(std::optional<std::string> value) {
    std::lock_guard lock(mutex_);
    if (answer_) wipe(*answer_);
    answer_ = std::move(value);
    if (!answer_) cancel_ = true;
    has_answer_ = true;
    answered_.notify_all();
}

void Launcher::restart() {
    auto arguments = arguments_;
    if (std::find(arguments.begin(), arguments.end(), L"--reskate-updated") == arguments.end())
        arguments.emplace_back(L"--reskate-updated");
    launcher_app::relaunch(session_.self, arguments);
}

void Launcher::wipe(std::string& value) {
    SecureZeroMemory(value.data(), value.size());
    value.clear();
}

std::optional<std::string> Launcher::prompt(const update::Prompt& prompt) {
    std::unique_lock lock(mutex_);
    // Use the password from the sign-in panel once; ask again only if Steam re-asks.
    if (prompt.kind == update::PromptKind::password && !password_.empty()) {
        auto password = std::move(password_);
        password_.clear();
        return password;
    }
    state_.prompt = prompt;
    has_answer_ = false;
    answered_.wait(lock, [this] { return has_answer_ || cancel_; });
    state_.prompt.reset();
    if (cancel_) return std::nullopt;
    auto value = std::move(answer_);
    answer_.reset();
    return value;
}

void Launcher::set(Phase phase, std::string status, std::string detail, float progress) {
    std::lock_guard lock(mutex_);
    state_.phase = phase;
    state_.status = std::move(status);
    state_.detail = std::move(detail);
    state_.progress = progress;
}

void Launcher::set_progress(std::string detail, float progress) {
    std::lock_guard lock(mutex_);
    state_.detail = std::move(detail);
    state_.progress = progress;
}

void Launcher::fail(const std::string& message) {
    logging::write(logging::Level::error, logging::Channel::launcher, message);
    std::lock_guard lock(mutex_);
    state_.phase = Phase::failed;
    state_.status = message;
    state_.detail = "See logs\\ReSkate.log for details.";
    state_.progress = -1;
    state_.qr.clear();
}

std::optional<update::Config> Launcher::config() {
    std::lock_guard lock(mutex_);
    return state_.config;
}

update::Progress Launcher::progress_for(std::string label) {
    return [this, label](std::uint64_t received, std::uint64_t total) {
        set_progress(std::format("{}  {} of {}", label, format_bytes(received), format_bytes(total)),
            total ? static_cast<float>(static_cast<double>(received) / static_cast<double>(total)) : -1.0f);
    };
}

bool Launcher::launcher_outdated(const update::Config& config) const {
    return !relaunched_ && !config.launcher.url.empty() && !update::file_matches(session_.self, config.launcher);
}

bool Launcher::runtime_outdated(const update::Config& config) const {
    return !config.runtime.url.empty() && !update::file_matches(session_.paths.dll, config.runtime);
}

void Launcher::run_check() {
    set(Phase::checking, "Checking for updates");
    update::remove_previous_launcher(session_.self);
    auto config = update::fetch_config();
    {
        std::lock_guard lock(mutex_);
        state_.config = config;
    }
    const bool newer = config && (launcher_outdated(*config) || runtime_outdated(*config));
    if (newer && update::binary_updates_enabled() && binaries_ && settings_.updates) {
        set(Phase::update_available, "A ReSkate update is ready",
            std::format("Version {} will be downloaded and installed.",
                config->runtime.version.empty() ? config->launcher.version : config->runtime.version));
        return;
    }
    set(Phase::checking, "Checking game files");
    if (!launcher_app::game_files_supported(session_.paths)) {
        std::error_code error;
        const bool installed = fs::is_regular_file(session_.paths.game, error);
        if (!config) {
            fail(installed ? "This Skate version is not supported, and the update server could not be reached."
                           : "Skate is not installed here, and the update server could not be reached.");
            return;
        }
        if (!launcher_app::config_matches_build(*config)) {
            fail("This launcher is out of date. Download the latest ReSkate release.");
            return;
        }
        const auto build = config->game.build_id.empty() ? config->game.manifest_id : config->game.build_id;
        if (installed) set(Phase::game_outdated, "Steam updated Skate",
            std::format("ReSkate needs build {}. Only changed files are downloaded.", build));
        else set(Phase::game_missing, "Skate is not installed here",
            std::format("Install build {} from Steam (about 14 GB). Your account must own skate.", build));
        return;
    }
    std::error_code error;
    if (!fs::is_regular_file(session_.paths.dll, error)) {
        fail("ReSkate.dll is missing from the game folder.");
        return;
    }
    set(Phase::ready, "Ready to skate", !config ? "Offline: update check skipped."
        : !settings_.updates ? (newer ? "ReSkate updates are off; keeping your ReSkate files." : "ReSkate updates are off.")
        : "Up to date.");
}

void Launcher::run_updates() {
    const auto config = this->config();
    if (!config) return run_check();
    set(Phase::updating, "Updating ReSkate");
    if (runtime_outdated(*config))
        update::replace_file(session_.paths.dll, config->runtime, progress_for("ReSkate.dll"));
    if (launcher_outdated(*config)) {
        update::replace_running_launcher(session_.self, config->launcher, progress_for("Launcher"));
        logging::flush();
        restart_ = true;
        return;
    }
    run_check();
}

void Launcher::run_download(bool validate) {
    const auto config = this->config();
    if (!config) return run_check();
    set(Phase::downloading, "Preparing Steam download", {}, 0);
    const auto depot_downloader = update::ensure_depot_downloader(config->depot_downloader,
        progress_for("DepotDownloader"));
    set(Phase::downloading, "Connecting to Steam");
    DepotOutput output;
    update::SteamLogin login;
    login.username = settings_.steam_username;
    login.remember = settings_.steam_remember;
    login.prefer_code = settings_.steam_prefer_code;
    {
        std::lock_guard lock(mutex_);
        if (qr_login_) login.username.clear();
    }
    if (!login.username.empty()) set(Phase::downloading, "Signing in to Steam", login.username);
    const auto code = update::run_depot_downloader(depot_downloader, config->game,
        session_.paths.directory, validate, login, [&](std::string_view line) {
            // DepotDownloader names the account it signs in ("Logging 'name' into Steam3"); the log
            // travels with crash reports, so the name is left out.
            std::string logged(line);
            if (!login.username.empty())
                for (auto at = logged.find(login.username); at != std::string::npos; at = logged.find(login.username, at + 8))
                    logged.replace(at, login.username.size(), "<hidden>");
            logging::write(logging::Level::debug, logging::Channel::launcher, logged);
            output.feed(line);
            std::lock_guard lock(mutex_);
            if (line.find("Use the Steam Mobile App to confirm") != std::string_view::npos) {
                state_.status = "Approve the sign-in in the Steam app";
                state_.detail = "Open Steam on your phone and confirm. Waiting...";
                return;
            }
            if (output.qr_update) {
                state_.qr = *output.qr_update;
                if (state_.qr.empty()) state_.status = "Signed in to Steam";
            }
            if (output.percent) {
                state_.status = validate ? "Verifying Skate" : "Downloading Skate";
                state_.detail = std::format("{:.1f}%  {}", *output.percent, output.file);
                state_.progress = *output.percent / 100.0f;
            } else if (state_.qr.empty() && !output.message.empty() && output.message != state_.detail) {
                state_.detail = output.message;
            }
        }, [this](const update::Prompt& prompt) { return this->prompt(prompt); }, cancel_);
    {
        std::lock_guard lock(mutex_);
        state_.qr.clear();
        state_.prompt.reset();
        wipe(password_);
    }
    if (cancel_) {
        run_check();
        std::lock_guard lock(mutex_);
        state_.detail = "Download cancelled. It will resume where it stopped.";
        return;
    }
    if (launcher_app::game_files_supported(session_.paths)) {
        set(Phase::ready, "Ready to skate", "Skate build installed.");
        return;
    }
    fail(std::format("The Steam download did not finish (exit code {}). {}", code, output.message));
}

void Launcher::run_play() {
    set(Phase::launching, "Starting Skate");
    save();
    auto options = launch_options(settings_);
    // The launcher's own command line can request offline mode too.
    if (std::find(arguments_.begin(), arguments_.end(), L"-offline") != arguments_.end() ||
        std::find(arguments_.begin(), arguments_.end(), L"--offline") != arguments_.end())
        options.offline = true;
    launched_ = false;
    launcher_app::start_game(session_, options, [this](DWORD id) { game_ = id; });
    launched_ = true;
    set(Phase::launching, "Skate is starting", options.offline || !launcher_app::steam_signed_in() ?
        "Offline mode: playing without Steam as Unknown Player; multiplayer is hidden." :
        "The launcher hides when the splash screen opens.");
}

} // namespace dingosdk::launcher_gui::detail

namespace dingosdk::launcher_gui {
bool updates_enabled(const launcher_app::Session& session) {
    return detail::load_settings(detail::settings_path(session)).updates;
}
void apply_crash_report_setting() noexcept {
    try {
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) return;
        path.resize(length);
        if (!detail::load_settings(std::filesystem::path(path).parent_path() / L"ReSkateLauncher.settings.json").crash_reports)
            SetEnvironmentVariableW(L"RESKATE_CRASH_REPORTING", L"0");
    } catch (...) {}
}
} // namespace dingosdk::launcher_gui

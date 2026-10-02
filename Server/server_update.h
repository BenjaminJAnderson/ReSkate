#pragma once
#include <filesystem>
#include <string>

// Self-update for release builds of the dedicated server. The latest GitHub
// release's launcher.json pins the server zip (size and SHA-256) and the
// SHA-256 of the ReSkateServer.exe inside it; a server whose own exe differs
// installs that zip over its folder and starts again. Its own files
// (ReSkateServer.json, Mods, logs) are not in the zip, so they are kept.
namespace dingosdk::server {
// Release builds only: a local build never replaces itself.
bool updates_enabled() noexcept;

struct UpdateCheck {
    bool available{};
    std::string version; // the release's server version, e.g. "0.3.8"
    std::string problem; // why the check could not finish, or empty
};
// Asks GitHub for the latest release; blocks for a few seconds at most.
UpdateCheck check_for_update();
// Downloads the pinned zip into `folder` and unpacks it there. Throws on any
// failure, before anything in the folder has changed.
void install_update(const std::filesystem::path& folder);
// Starts a new copy of this exe with the same command line.
bool relaunch();
// Deletes the files a previous update renamed aside.
void remove_previous_update(const std::filesystem::path& folder) noexcept;
} // namespace dingosdk::server

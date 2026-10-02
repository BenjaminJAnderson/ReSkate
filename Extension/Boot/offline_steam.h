#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace dingosdk::offline_steam {

// Offline mode (RESKATE_OFFLINE=1): Skate.exe falls back to the EA app when
// Steam fails to start, so instead Steam "starts" against stand-in interfaces.
// The player is signed in as Unknown Player with no friends, lobbies or
// overlay; no call ever reaches the real Steam client.
bool install(HMODULE steam_module, std::string& error) noexcept;

// The SteamID the stand-in reports (launcher::offline_steam_id).
std::uint64_t steam_id() noexcept;

} // namespace dingosdk::offline_steam

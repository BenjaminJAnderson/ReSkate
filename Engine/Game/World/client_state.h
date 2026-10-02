#pragma once
#include <iterator>

namespace dingosdk {
// ClientState names verified against the supported image's native name table.
// Ingame (13) also describes the splash scene; it does not prove player control.
inline constexpr const char* client_state_name(unsigned state) noexcept {
    constexpr const char* names[]{
        "Static bundles", "Profile options", "Lost connection", "Unloading",
        "Startup", "Starting server", "Waiting for server", "Waiting for level",
        "Starting level load", "Loading level", "Waiting for link", "Level linked",
        "Waiting for objects", "Level active", "Leaving level", "Connecting",
        "Starting sublevel server", "Waiting for sublevel server", "Waiting for sequence",
        "Waiting for baseline", "Waiting for sublevel objects", "Sublevel active",
        "Leaving sublevel", "Connecting to sublevel", "Shutting down", "Shutdown", "None"};
    return state < std::size(names) ? names[state] : "Unknown";
}
}

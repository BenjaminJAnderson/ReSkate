#pragma once

#include <Windows.h>

#include <string>

namespace dingosdk {

// Installs the single Steam restart detour in an already validated module.
// Exposed separately so the forwarding behavior can be tested in an owned DLL.
bool install_steam_restart_guard(HMODULE steam_module, std::string& error) noexcept;

// Resolves ReSkate.dll's exact sibling steam_api64.dll, validates the original
// file identity, loads it if necessary, then installs only the restart detour.
bool start_steam_restart_guard(std::string& error) noexcept;

} // namespace dingosdk

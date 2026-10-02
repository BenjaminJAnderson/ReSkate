#pragma once

#include <string>

namespace dingosdk {

// Stops anything inside Skate.exe from starting the EA app / Origin client
// (EADesktop.exe, EALauncher.exe, link2ea:/origin: URLs). Always on.
bool start_ea_app_block(std::string& error) noexcept;

} // namespace dingosdk

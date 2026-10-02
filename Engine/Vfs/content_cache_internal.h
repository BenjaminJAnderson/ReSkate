#pragma once

namespace dingosdk::content_cache::detail {
// Holds the installed pack's ZIP SHA-256; present only after a complete install.
inline constexpr const wchar_t* marker_name = L".reskate-pack";
}

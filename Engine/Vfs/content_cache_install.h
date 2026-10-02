#pragma once
#include "content_cache.h"

namespace dingosdk::content_cache {
enum class InstallStatus { installed, downloaded, busy, failed };
struct InstallResult {
    InstallStatus status{InstallStatus::failed};
    unsigned long http_status{}, error{};
};
// Downloads, verifies and unpacks `pack` into `folder` unless it is already
// there. A failed attempt leaves any previous install untouched.
InstallResult ensure_installed(const std::filesystem::path& folder, const Pack& pack) noexcept;
InstallResult ensure_installed() noexcept;
}

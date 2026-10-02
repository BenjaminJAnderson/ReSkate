#include "content_cache.h"
#include "content_cache_internal.h"
#include "Engine/Game/Build/supported_build.h"
#include <Windows.h>
#include <array>
#include <fstream>
#include <stdexcept>

namespace dingosdk::content_cache {
const Pack& supported_pack() {
    static const Pack pack{std::wstring(supported_build::content_cache_url),
        std::string(supported_build::content_cache_sha256), supported_build::content_cache_bytes,
        std::wstring(supported_build::steam_build_id.begin(), supported_build::steam_build_id.end())};
    return pack;
}

std::filesystem::path directory(const Pack& pack) {
    std::array<wchar_t, 32768> local{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (!length || length >= local.size()) throw std::runtime_error("LOCALAPPDATA is unavailable");
    return std::filesystem::path(local.data()) / L"ReSkate" / L"cache" / pack.build;
}

bool installed(const std::filesystem::path& folder, const Pack& pack) {
    // The marker is written last, after every file is in place.
    std::ifstream marker(folder / detail::marker_name, std::ios::binary);
    std::string recorded;
    return marker && std::getline(marker, recorded) && recorded == pack.sha256;
}

bool installed() {
    try { return installed(directory()); }
    catch (...) { return false; }
}
}

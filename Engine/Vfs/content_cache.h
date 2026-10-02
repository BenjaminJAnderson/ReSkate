#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

// The live game's HTTP content cache (catalogue chunks and CDN images) for the
// supported Skate.exe build. ReSkate ships none of it: the launcher installs the
// matching ReSkateCache release into Local AppData, and the runtime reads it.
namespace dingosdk::content_cache {
struct Pack {
    std::wstring url;
    std::string sha256;   // of the ZIP
    std::uint64_t bytes{};
    std::wstring build;   // Steam build id; names the install folder
};
const Pack& supported_pack();

// %LOCALAPPDATA%\ReSkate\cache\<build>
std::filesystem::path directory(const Pack& pack = supported_pack());
// True once a complete, verified copy of `pack` is in `folder`.
bool installed(const std::filesystem::path& folder, const Pack& pack = supported_pack());
bool installed();
}

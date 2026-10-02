#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct MusicSong {
    std::string id, artist, title;
    std::vector<std::string> playlists;
};

struct MusicPlaylist {
    std::string id;
    std::vector<std::string> songs;
};

struct MusicCatalog {
    std::vector<MusicSong> songs;
    std::vector<MusicPlaylist> playlists;
};

struct MusicAssetFunctions {
    game::NativeModelFunctions::Lock lock{}, unlock{};
};

MusicAssetFunctions& music_asset_functions();

bool music_asset_type(std::uintptr_t asset, std::uintptr_t vtable_rva);

bool read_music_catalog(MusicCatalog& result);
}

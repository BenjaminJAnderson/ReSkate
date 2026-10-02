#include "Extension/Customization/local_customization_runtime.h"
#include "local_music_assets.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/local_music.h"
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
// Snapshot the game's registered music assets, as the cosmetic catalog does

// for OwnableData. No generated song table or invented playlist membership.

MusicAssetFunctions& music_asset_functions() {
    // The game reaches these Win32 APIs through its IAT thunks.
    // This is not the UI model's different, custom lock implementation.
    static MusicAssetFunctions functions{
        [](std::uintptr_t p) { EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(p)); },
        [](std::uintptr_t p) { LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(p)); }};
    return functions;
}

bool music_asset_type(std::uintptr_t asset, std::uintptr_t vtable_rva) {
    std::uintptr_t vtable{};
    return asset && read(asset, vtable) && vtable == local_runtime().base + vtable_rva;
}

bool read_music_catalog(MusicCatalog& result) {
    const auto base = local_runtime().base;
    auto& native = music_asset_functions();
    std::uintptr_t manager{};
    if (!native.lock || !native.unlock || !read(base + addr::local_music::asset_manager, manager) || !manager) return false;
    // Native registration/unregistration uses this same lock.
    // Copy data while held; never retain a borrowed asset pointer in the UI.
    native.lock(manager + 0x20);
    struct Unlock {
        game::NativeModelFunctions::Lock function;
        std::uintptr_t address;
        ~Unlock() { function(address); }
    } unlock{native.unlock, manager + 0x20};
    std::uintptr_t current{}, buckets{}, sentinel{};
    std::uint32_t bucket_count{}, count{};
    if (!read(base + addr::local_music::asset_manager, current) || current != manager ||
        !read(manager + 0x78, buckets) || !buckets ||
        !read(manager + 0x80, bucket_count) || !bucket_count || bucket_count > 16384 ||
        !read(manager + 0x84, count) || !count || count > 8192 ||
        !read(buckets + bucket_count * 8ULL, sentinel)) return false;

    MusicCatalog snapshot;
    snapshot.songs.reserve(count);
    std::map<std::string, std::vector<std::string>, std::less<>> playlists;
    std::set<std::uintptr_t> visited;
    std::set<std::string, std::less<>> song_ids;
    for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
        std::uintptr_t node{};
        if (!read(buckets + bucket * 8ULL, node)) return false;
        while (node && node != sentinel) {
            if (visited.size() >= count || !visited.insert(node).second) return false;
            std::uint32_t hash{};
            std::uintptr_t graph{}, metadata{}, artist{}, title{}, tags{};
            if (!read(node, hash) || hash % bucket_count != bucket ||
                !read(node + 8, graph)) return false;
            graph &= ~std::uintptr_t{4};
            if (!music_asset_type(graph, addr::local_music::graph_asset_vtable) || !read(graph + 0x20, metadata)) return false;
            metadata &= ~std::uintptr_t{4};
            if (!music_asset_type(metadata, addr::local_music::metadata_vtable) || !read(metadata + 0x18, artist) ||
                !read(metadata + 0x20, title) || !read(graph + 0x28, tags)) return false;
            MusicSong song;
            if (!cosmetic_text(artist, song.artist) || !cosmetic_text(title, song.title) ||
                song.artist.empty() || song.title.empty()) return false;
            // Exact native registration input, NOT MusicGraph.NameHash.
            // The native map already resolves duplicate artist/title pairs.
            song.id = song.artist + " - " + song.title;
            if (cosmetic_hash(song.id) != hash || !song_ids.insert(song.id).second) return false;

            std::uint32_t tag_count{};
            if (tags && !cosmetic_array(tags, 8, 128, tag_count)) return false;
            for (std::uint32_t index = 0; index < tag_count; ++index) {
                std::uintptr_t tag{}, enumeration{}, selection{}, path{}, name{};
                if (!read(tags + index * 8ULL, tag)) return false;
                tag &= ~std::uintptr_t{4};
                if (!music_asset_type(tag, addr::local_music::tag_vtable) || !read(tag + 0x18, enumeration) || !read(tag + 0x20, selection)) return false;
                enumeration &= ~std::uintptr_t{4}; selection &= ~std::uintptr_t{4};
                std::string enum_path, playlist;
                if (!enumeration || !read(enumeration + 0x18, path) ||
                    !cosmetic_text(path, enum_path)) return false;
                // Other music tags are not playlist membership declarations.
                if (_stricmp(enum_path.c_str(), "Audio/Music/Playlist/DGO_MUS_Playlist_FilterTags") != 0)
                    continue;
                if (!music_asset_type(selection, addr::local_music::tag_selection_vtable) || !read(selection + 0x18, name) ||
                    !cosmetic_text(name, playlist) || playlist.empty()) return false;
                if (playlist == "Invalid" || playlist == "ChallengeMusic_Test") continue;
                if (std::find(song.playlists.begin(), song.playlists.end(), playlist) == song.playlists.end()) {
                    song.playlists.push_back(playlist);
                    playlists[playlist].push_back(song.id);
                }
            }
            snapshot.songs.push_back(std::move(song));
            if (!read(node + 0x10, node)) return false;
        }
    }
    std::uint32_t current_count{};
    std::uintptr_t current_buckets{};
    if (visited.size() != count || !read(base + addr::local_music::asset_manager, current) || current != manager ||
        !read(manager + 0x78, current_buckets) || current_buckets != buckets ||
        !read(manager + 0x84, current_count) || current_count != count) return false;
    // Registry order is bucket order, not authored ordering. Use stable IDs for
    // deterministic UI order; do not claim this is the original service order.
    std::sort(snapshot.songs.begin(), snapshot.songs.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    for (auto& [id, songs] : playlists) {
        std::sort(songs.begin(), songs.end());
        snapshot.playlists.push_back({id, std::move(songs)});
    }
    result = std::move(snapshot);
    return true;
}
}
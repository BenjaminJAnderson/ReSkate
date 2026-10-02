#include "mod_merge_internal.h"

#include <stdexcept>

namespace dingosdk::mods::detail {

AssetOverrides collect_asset_overrides(const std::vector<const Mod*>& mods,
                                       const std::map<const Mod*, RelativeFiles>& modFiles,
                                       const CasStore& store, const fs::path& baseRoot,
                                       const fs::path& gameRoot, MergeReport& report) {
    AssetOverrides out;
    // Highest priority first: an asset a higher mod already changed keeps that change.
    for (const auto* mod : mods) {
        // A map's edits to the game's assets serve its own levels; asset mods
        // (cameras, tuning, cosmetics) are the ones meant to apply everywhere.
        if (mod->provides_levels) continue;
        const auto files = modFiles.find(mod);
        if (files == modFiles.end()) continue;
        std::size_t changed{};
        for (const auto& relative : files->second.tocs) {
            std::error_code error;
            const auto baseToc = baseRoot / fs::path(relative);
            if (!fs::is_regular_file(baseToc, error)) continue;
            try {
                const auto own = fb::read_toc(read_file(mod->directory / fs::path(relative)));
                const auto game = fb::read_toc(read_file(baseToc));
                std::map<std::string, const fb::TocBundle*, std::less<>> gameBundles;
                for (const auto& bundle : game.bundles) gameBundles.emplace(lower(bundle.name), &bundle);
                for (const auto& bundle : own.bundles) {
                    const auto shipped = gameBundles.find(lower(bundle.name));
                    if (shipped == gameBundles.end()) continue;
                    const auto modListing = list_bundle(store, mod->directory, baseRoot, bundle, gameRoot);
                    const auto gameListing = list_bundle(store, baseRoot, baseRoot, *shipped->second, gameRoot);
                    if (!modListing || !gameListing) continue;
                    std::map<std::string, fb::Sha1, std::less<>> original;
                    for (const auto& asset : gameListing->manifest.ebx) original.emplace(lower(asset.name), asset.sha1);
                    for (std::size_t index = 0; index < modListing->manifest.ebx.size(); ++index) {
                        const auto& asset = modListing->manifest.ebx[index];
                        const auto name = lower(asset.name);
                        const auto game_copy = original.find(name);
                        if (game_copy == original.end() || game_copy->second == asset.sha1) continue;
                        auto& versions = out[name];
                        if (versions.contains(game_copy->second)) continue;
                        const auto at = modListing->first + index;
                        if (at >= modListing->files.size()) continue;
                        const auto& file = modListing->files[at];
                        versions.emplace(game_copy->second,
                            AssetOverride{mod->name, asset.sha1, asset.originalSize,
                                          store.read(file.location.patch ? mod->directory : baseRoot,
                                                     file.location, file.offset, file.size)});
                        ++changed;
                    }
                }
            } catch (const std::exception& failure) {
                report.notes.push_back(mod->name + ": " + relative +
                    ": its changes could not be read for other mods' copies (" + failure.what() + ")");
            }
        }
        if (changed)
            report.notes.push_back(mod->name + ": " + std::to_string(changed) +
                " changed asset(s) also apply to the copies other mods carry");
    }
    return out;
}

} // namespace dingosdk::mods::detail

#pragma once
#include "Engine/Vfs/item_thumbnails.h"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <imgui.h>
#include <span>
#include <string_view>

namespace dingosdk::overlay {
struct ParkPreviewImage {
    ImTextureID texture{};
    ImVec2 uv0{}, uv1{};
};
// Starts reading the game's build-kit thumbnails from its data files on a
// background thread. Call once, early; the read takes well under a second.
void start_park_previews(std::filesystem::path game_root);
// Render-thread setup, before the ImGui atlas is uploaded to the GPU: adds the
// background read's thumbnails, waiting up to `wait` for it to finish.
std::size_t load_park_previews(ImFontAtlas &, std::chrono::milliseconds wait) noexcept;
// Adds 128x128 build-kit thumbnails to the atlas; returns how many were added.
std::size_t load_park_previews(ImFontAtlas &, std::span<const vfs::ItemThumbnail>) noexcept;
bool park_preview_image(std::string_view key, ParkPreviewImage &image);
void clear_park_previews() noexcept;
} // namespace dingosdk::overlay

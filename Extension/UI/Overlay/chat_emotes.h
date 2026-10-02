#pragma once
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

// Chat emotes, written in chat as :name:. The pack is built into ReSkate.dll from the
// repository's assets/emotes/emotes.json and emotes.png (cmake/Runtime.cmake), in Better Chat's
// format (github.com/codecat/tm-better-chat):
//   {"name": "...", "texture": "...", "emotes": {
//       "Name": {"pos": [x, y], "size": [w, h]},
//       "Animated": {"size": [w, h], "frames": [{"time": ms, "pos": [x, y]}, ...]}}}
// ("texture" is ignored: the built-in emotes.png is the texture). Players on an older build
// without an emote see its :name: text.
namespace dingosdk::overlay {
// Starts decoding the built-in pack on a background thread. Call once, early.
void start_chat_emotes();
// Render-thread setup, before the ImGui atlas is built: reserves room in it for every emote
// frame, waiting up to `wait` for the background read. Returns how many emotes there are.
std::size_t reserve_chat_emotes(ImFontAtlas &, std::chrono::milliseconds wait) noexcept;
// After the atlas is built: copies the emote pixels into it.
void fill_chat_emotes(ImFontAtlas &) noexcept;
bool chat_emotes_loaded() noexcept;
struct ChatEmote {
    ImTextureID texture{};
    ImVec2 uv0{}, uv1{};
    float aspect = 1; // width / height
};
// The emote called `name` (as written between the colons), at its current animation frame.
bool chat_emote(std::string_view name, ChatEmote &emote);
// Emote names starting with `prefix` (any case), for completion; at most `limit`, sorted.
std::vector<std::string> chat_emote_names(std::string_view prefix, std::size_t limit);
} // namespace dingosdk::overlay

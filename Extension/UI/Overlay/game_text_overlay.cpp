#include "overlay_internal.h"

// Text the game draws through its debug text natives, which retail reduced to stubs
// (the S.K.A.T.E. throwdown's whole HUD is drawn that way). The game side hands over
// one frame of lines at a time in its own virtual screen; they are scaled to the
// window here and drawn under the menus, taking no input.

namespace dingosdk::overlay {
namespace {
std::atomic<GameTextFeed> game_text_feed{};
}
void set_game_text_feed(GameTextFeed feed) noexcept { game_text_feed.store(feed); }
} // namespace dingosdk::overlay

using namespace dingosdk::overlay::detail;
namespace dingosdk::overlay::detail {
namespace {
GameText& game_text() {
    static GameText value;
    return value;
}
} // namespace

bool game_text_pending() {
    auto& text = game_text();
    text = {};
    if (const auto feed = game_text_feed.load()) {
        try { text = feed(); } catch (...) { text = {}; }
    }
    return !text.lines.empty() && text.width > 0 && text.height > 0;
}

void draw_game_text() {
    const auto& text = game_text();
    if (text.lines.empty() || text.width <= 0 || text.height <= 0) return;
    const auto display = ImGui::GetIO().DisplaySize;
    if (display.x <= 0 || display.y <= 0) return;
    auto& s = state();
    auto* font = s.menu.bold ? s.menu.bold : ImGui::GetFont();
    auto* draw = ImGui::GetBackgroundDrawList();
    const float sx = display.x / text.width, sy = display.y / text.height;
    for (const auto& line : text.lines) {
        if (line.text.empty()) continue;
        const float size = std::clamp(12.0f * line.scale * sy, 8.0f, 96.0f);
        ImVec2 at(line.x * sx, line.y * sy);
        if (line.centered) at.x -= font->CalcTextSizeA(size, FLT_MAX, 0.0f, line.text.c_str()).x * 0.5f;
        const ImU32 colour = line.color;
        const auto alpha = (colour >> IM_COL32_A_SHIFT) & 0xff;
        const float shadow = std::max(1.0f, size / 14.0f);
        draw->AddText(font, size, ImVec2(at.x + shadow, at.y + shadow), IM_COL32(0, 0, 0, alpha * 3 / 4), line.text.c_str());
        draw->AddText(font, size, at, colour, line.text.c_str());
    }
}
} // namespace dingosdk::overlay::detail

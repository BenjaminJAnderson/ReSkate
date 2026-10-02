#include "overlay_internal.h"
#include "nametag_gradient.h"
#include "role_badge.h"
#include <cmath>

// ReSkate's nametags. The game side hands over each other player's head position,
// name, colour and distance with the camera the client last used; they are placed here
// on the background draw list, under ReSkate's own menus and chat, taking no input.

namespace dingosdk::overlay {
namespace {
std::atomic<NametagFeed> nametag_feed{};
}
void set_nametag_feed(NametagFeed feed) noexcept { nametag_feed.store(feed); }
} // namespace dingosdk::overlay

using namespace dingosdk::overlay::detail;
namespace dingosdk::overlay::detail {
namespace {
// Nearer than this a player gets their name; further, only a dot.
constexpr float name_range = 150.0f;
// How far in from the screen's edge the dots of players off screen sit (1080p pixels).
constexpr float edge_margin = 28.0f;

Nametags &nametags() {
    static Nametags value;
    return value;
}
using Vec3 = std::array<float, 3>;
float dot(const Vec3 &a, const Vec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
std::string distance_text(float metres) {
    char text[32]{};
    if (metres < 1000.0f) std::snprintf(text, sizeof text, "%.0f m", std::floor(metres));
    else std::snprintf(text, sizeof text, "%.1f km", metres / 1000.0f);
    return text;
}
ImU32 with_alpha(ImU32 colour, float alpha) {
    const auto a = static_cast<unsigned>(((colour >> IM_COL32_A_SHIFT) & 0xff) * std::clamp(alpha, 0.0f, 1.0f));
    return (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}
void outlined_dot(ImDrawList *draw, ImVec2 at, float radius, ImU32 colour) {
    draw->AddCircleFilled(at, radius + std::max(1.0f, radius * 0.35f), with_alpha(IM_COL32(0, 0, 0, 255), 0.55f), 16);
    draw->AddCircleFilled(at, radius, colour, 16);
}
// A small speaker with one sound wave, its left edge at `at`, centred on `at.y`.
void speaker(ImDrawList *draw, ImVec2 at, float size, ImU32 colour) {
    const float h = size * 0.5f;
    draw->AddRectFilled(ImVec2(at.x, at.y - h * 0.35f), ImVec2(at.x + size * 0.28f, at.y + h * 0.35f), colour);
    draw->AddTriangleFilled(ImVec2(at.x + size * 0.28f, at.y - h * 0.35f), ImVec2(at.x + size * 0.62f, at.y - h),
                            ImVec2(at.x + size * 0.62f, at.y + h), colour);
    draw->AddTriangleFilled(ImVec2(at.x + size * 0.28f, at.y - h * 0.35f), ImVec2(at.x + size * 0.62f, at.y + h),
                            ImVec2(at.x + size * 0.28f, at.y + h * 0.35f), colour);
    draw->PathArcTo(ImVec2(at.x + size * 0.62f, at.y), size * 0.42f, -0.9f, 0.9f, 10);
    draw->PathStroke(colour, 0, std::max(1.0f, size * 0.11f));
}
} // namespace

bool nametags_pending() {
    auto &value = nametags();
    value = {};
    if (const auto feed = nametag_feed.load()) {
        try { value = feed(); } catch (...) { value = {}; }
    }
    return !value.tags.empty() && value.vertical_fov > 1 && value.vertical_fov < 175;
}

void draw_nametags() {
    auto &value = nametags();
    if (value.tags.empty() || !(value.vertical_fov > 1 && value.vertical_fov < 175)) return;
    const auto display = ImGui::GetIO().DisplaySize;
    if (display.x <= 0 || display.y <= 0) return;
    const auto &m = value.camera;
    const Vec3 right{m[0], m[1], m[2]}, up{m[4], m[5], m[6]}, back{m[8], m[9], m[10]}, origin{m[12], m[13], m[14]};
    const float k = display.y / 1080.0f;
    const float focal = display.y / (2.0f * std::tan(value.vertical_fov * 3.14159265f / 360.0f));
    const ImVec2 centre(display.x * 0.5f, display.y * 0.5f);
    const float margin = edge_margin * k;
    auto &s = state();
    auto *font = s.menu.bold ? s.menu.bold : ImGui::GetFont();
    auto *draw = ImGui::GetBackgroundDrawList();
    const double animation_time = ImGui::GetTime();
    // Far first, so nearer names draw over them.
    std::sort(value.tags.begin(), value.tags.end(), [](const Nametag &a, const Nametag &b) { return a.distance > b.distance; });
    for (const auto &tag : value.tags) {
        const auto animated_colour = animated_nametag_colour(tag.color, animation_time);
        const Vec3 delta{tag.position[0] - origin[0], tag.position[1] - origin[1], tag.position[2] - origin[2]};
        const float depth = -dot(delta, back), side = dot(delta, right), height = dot(delta, up);
        bool on_screen = false;
        ImVec2 at;
        if (depth > 0.1f) {
            at = ImVec2(centre.x + side * focal / depth, centre.y - height * focal / depth);
            on_screen = at.x >= margin && at.x <= display.x - margin && at.y >= margin && at.y <= display.y - margin;
        }
        if (!on_screen) {
            // The direction to them from the middle of the screen, pushed out to the edge.
            float dx = depth > 0.1f ? at.x - centre.x : side, dy = depth > 0.1f ? at.y - centre.y : -height;
            if (depth <= 0.1f && std::abs(dx) < 1e-3f && std::abs(dy) < 1e-3f) dy = 1.0f; // straight behind
            const float hx = centre.x - margin, hy = centre.y - margin;
            const float scale = std::min(std::abs(dx) > 1e-6f ? hx / std::abs(dx) : FLT_MAX,
                                         std::abs(dy) > 1e-6f ? hy / std::abs(dy) : FLT_MAX);
            outlined_dot(draw, ImVec2(centre.x + dx * scale, centre.y + dy * scale), 5.0f * k, animated_colour);
            continue;
        }
        if (tag.distance > name_range) {
            outlined_dot(draw, at, 4.0f * k, animated_colour);
            continue;
        }
        // Names shrink a little with distance and fade slightly towards the name range.
        const float nearness = 1.0f - std::clamp(tag.distance / name_range, 0.0f, 1.0f);
        const float size = std::clamp((15.0f + 7.0f * nearness) * k, 10.0f, 44.0f);
        const float alpha = 0.7f + 0.3f * nearness;
        const auto colour = with_alpha(tag.color, alpha);
        const auto shadow = with_alpha(IM_COL32(0, 0, 0, 255), 0.75f * alpha);
        const float offset = std::max(1.0f, size / 14.0f);
        const auto name_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, tag.name.c_str());
        const auto distance = distance_text(tag.distance);
        const float detail = size * 0.72f;
        const auto distance_size = font->CalcTextSizeA(detail, FLT_MAX, 0.0f, distance.c_str());
        // The name sits above the head point, the distance under it, and the role badge (as in
        // chat) before the name, the two centred together.
        const float badge = role_badge_width(font, size, tag.tag);
        const float badge_gap = badge > 0.0f ? size * 0.3f : 0.0f;
        const float left = at.x - (badge + badge_gap + name_size.x) * 0.5f;
        const ImVec2 name_at(left + badge + badge_gap, at.y - name_size.y - distance_size.y);
        draw_role_badge(draw, font, size, ImVec2(left, name_at.y), name_size.y, tag.tag, tag.color, alpha);
        draw->AddText(font, size, ImVec2(name_at.x + offset, name_at.y + offset), shadow, tag.name.c_str());
        const int name_vertices = draw->VtxBuffer.Size;
        draw->AddText(font, size, name_at, colour, tag.name.c_str());
        shade_nametag_gradient(draw, name_vertices, name_at.x, name_size.x, tag.color, animation_time);
        const ImVec2 distance_at(at.x - distance_size.x * 0.5f, at.y - distance_size.y);
        const auto dim = with_alpha(IM_COL32(230, 230, 230, 255), 0.85f * alpha);
        draw->AddText(font, detail, ImVec2(distance_at.x + offset, distance_at.y + offset), shadow, distance.c_str());
        draw->AddText(font, detail, distance_at, dim, distance.c_str());
        if (tag.talking) speaker(draw, ImVec2(left - size * 1.05f, name_at.y + name_size.y * 0.5f), size * 0.8f,
                                 with_alpha(animated_colour, alpha));
    }
}
} // namespace dingosdk::overlay::detail

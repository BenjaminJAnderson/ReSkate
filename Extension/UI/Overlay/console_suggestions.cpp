#include "console_suggestions.h"
namespace dingosdk::overlay::detail {
namespace {
ImU32 console_group_color(dingosdk::console::Group group) {
    using G = dingosdk::console::Group;
    switch (group) {
    case G::movement:
        return IM_COL32(74, 202, 255, 255);
    case G::gameplay:
        return IM_COL32(77, 211, 133, 255);
    case G::world:
        return IM_COL32(240, 187, 67, 255);
    case G::graphics:
        return IM_COL32(186, 136, 255, 255);
    case G::progression:
        return IM_COL32(255, 145, 87, 255);
    case G::objects:
        return IM_COL32(65, 213, 204, 255);
    case G::engine:
        return IM_COL32(239, 111, 172, 255);
    default:
        return IM_COL32(180, 190, 202, 255);
    }
}

} // namespace
bool draw_console_suggestion(const console::Suggestion &row, ImU32 text_color, ImU32 muted_color) {
    ImGui::PushID(row.text.c_str());
    std::string label = row.text;
    if (row.argument)
        label += "  | " + row.name;
    if (row.state.value)
        label += " = [" + *row.state.value + "]";
    else if (row.kind == console::Kind::variable && row.state.available)
        label += " = [?]";
    if (!row.state.available)
        label += " [unavailable]";
    if (!row.description.empty())
        label += " - " + row.description;
    if (!row.state.available && !row.state.reason.empty())
        label += " | " + row.state.reason;
    const auto at = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();
    const float badge = std::max(8.0f, height - 4);
    const float width = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(label.c_str()).x + badge + 10);
    const bool selected = ImGui::Selectable("##suggestion", false, 0, ImVec2(width, height));
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(at.x, at.y + 2), ImVec2(at.x + badge, at.y + 2 + badge), console_group_color(row.group));
    draw->AddText(ImVec2(at.x + badge + 8, at.y), ImGui::GetColorU32(row.state.available ? text_color : muted_color),
                  label.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(console::group_name(row.group));
        ImGui::TextUnformatted(row.usage.c_str());
        if (!row.state.detail.empty())
            ImGui::TextUnformatted(row.state.detail.c_str());
        if (!row.state.available)
            ImGui::TextWrapped("Unavailable: %s", row.state.reason.c_str());
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    return selected;
}
} // namespace dingosdk::overlay::detail

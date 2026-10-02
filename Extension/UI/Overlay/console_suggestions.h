#pragma once
#include "Engine/Core/Console/command_registry.h"
#include <imgui.h>
namespace dingosdk::overlay::detail {
bool draw_console_suggestion(const console::Suggestion &row, ImU32 text_color, ImU32 muted_color);
}

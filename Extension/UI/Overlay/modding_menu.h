#pragma once
namespace dingosdk::overlay {
struct SkateMenu;
// The MODS page: tab 0 lists installed mods, tab 1 custom Lua scripts.
void draw_modding_menu(SkateMenu& menu, int tab);
}

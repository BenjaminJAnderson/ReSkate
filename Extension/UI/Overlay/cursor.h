#pragma once
#include <Windows.h>
namespace dingosdk::overlay::detail {
bool game_window_foreground(HWND window);
void sync_menu_cursor(bool release = false);
UINT cursor_sync_message();
void request_window_cursor_sync(HWND window);
void window_cursor_input(HWND window, UINT message, WPARAM wp);
// The same, when the caller already knows whether the overlay owns the pointer
// (owns_menu_cursor), as the window procedure does once per mouse message.
void window_cursor_input(HWND window, UINT message, WPARAM wp, bool overlay_owns_pointer);
void update_menu_pointer();
BOOL WINAPI clip_cursor(const RECT* rectangle);
BOOL WINAPI set_cursor_pos(int x, int y);
}

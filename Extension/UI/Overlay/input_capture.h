#pragma once
#include <Windows.h>
namespace dingosdk::overlay::detail {
extern thread_local unsigned overlay_input_access;
struct OverlayInputAccess {
    OverlayInputAccess() { ++overlay_input_access; }
    ~OverlayInputAccess() { --overlay_input_access; }
};
// The WM_INPUT packet the window procedure has already noted (note_raw_mouse_packet)
// while that message is handled on this thread; the game reading the same packet
// through GetRawInputData is then not noted a second time.
extern thread_local HRAWINPUT noted_raw_input;
bool block_polled_input();
bool install_input_capture();
// Skate's controller mode registers the mouse with RIDEV_NOLEGACY, which stops
// the Windows cursor from moving. While the overlay owns the pointer the mouse
// is registered without it; the game's own registration returns on close.
// Call on the game window's thread; does nothing when nothing changed.
void sync_raw_mouse_registration();
// The same, when the caller already knows whether the overlay owns the pointer
// (owns_menu_cursor), as the window procedure does once per mouse message.
void sync_raw_mouse_registration(bool overlay_owns_pointer);
// One raw mouse packet, from whichever path the game read it (WM_INPUT,
// GetRawInputData or GetRawInputBuffer).
void note_raw_mouse_packet(const RAWMOUSE& mouse);
void note_raw_mouse_packet(const RAWMOUSE& mouse, bool overlay_owns_pointer);
// The game window's input messages (keys, mouse), for the input-mode log (input_capture.cpp).
void note_input_message(UINT message, WPARAM wp) noexcept;
}

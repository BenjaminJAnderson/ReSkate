#include "Extension/UI/Overlay/held_input.h"

#include <cstdio>

namespace {
using dingosdk::overlay::detail::HeldInput;
int failures = 0;

void expect(bool condition, const char* what) {
    if (condition) return;
    ++failures;
    std::printf("FAIL: %s\n", what);
}

RAWINPUT key(USHORT scan, bool release, USHORT flags = 0) {
    RAWINPUT record{};
    record.header.dwType = RIM_TYPEKEYBOARD;
    record.data.keyboard.MakeCode = scan;
    record.data.keyboard.Flags = static_cast<USHORT>(flags | (release ? RI_KEY_BREAK : RI_KEY_MAKE));
    record.data.keyboard.VKey = 0x54;
    return record;
}

RAWINPUT mouse(USHORT buttons, LONG x = 0, LONG y = 0) {
    RAWINPUT record{};
    record.header.dwType = RIM_TYPEMOUSE;
    record.data.mouse.usButtonFlags = buttons;
    record.data.mouse.lLastX = x;
    record.data.mouse.lLastY = y;
    return record;
}
} // namespace

int main() {
    {
        // T goes down before the chat takes input; its release must still reach the game, once.
        HeldInput held;
        held.seen(key(0x14, false));
        auto up = key(0x14, true);
        expect(held.release_only(up), "release of a key the game holds passes");
        auto again = key(0x14, true);
        expect(!held.release_only(again), "a second release is dropped");
    }
    {
        // Keys typed into the chat: the game never saw them go down, so nothing passes.
        HeldInput held;
        auto down = key(0x1e, false);
        expect(!held.release_only(down), "a press while the overlay owns input is dropped");
        auto up = key(0x1e, true);
        expect(!held.release_only(up), "the release of a key pressed in the overlay is dropped");
    }
    {
        // A key the game already saw come up is not held any more.
        HeldInput held;
        held.seen(key(0x11, false));
        held.seen(key(0x11, true));
        auto up = key(0x11, true);
        expect(!held.release_only(up), "a key released before the overlay opened stays released");
    }
    {
        // Extended keys are their own keys: right Ctrl (E0 1D) is not left Ctrl (1D).
        HeldInput held;
        held.seen(key(0x1d, false));
        auto right = key(0x1d, true, RI_KEY_E0);
        expect(!held.release_only(right), "right Ctrl does not release left Ctrl");
        auto left = key(0x1d, true);
        expect(held.release_only(left), "left Ctrl releases");
    }
    {
        // Mouse: only the held button's release passes, and nothing else rides along with it.
        HeldInput held;
        held.seen(mouse(RI_MOUSE_RIGHT_BUTTON_DOWN));
        auto packet = mouse(RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_LEFT_BUTTON_DOWN, 12, -4);
        expect(held.release_only(packet), "release of a held mouse button passes");
        expect(packet.data.mouse.usButtonFlags == RI_MOUSE_RIGHT_BUTTON_UP, "only the held button's release is kept");
        expect(packet.data.mouse.lLastX == 0 && packet.data.mouse.lLastY == 0, "the movement is dropped");
        auto move = mouse(0, 5, 5);
        expect(!held.release_only(move), "plain movement is dropped");
        auto click = mouse(RI_MOUSE_LEFT_BUTTON_UP);
        expect(!held.release_only(click), "a click made in the overlay never reaches the game");
    }
    if (failures == 0) std::printf("held input: all checks passed\n");
    return failures == 0 ? 0 : 1;
}

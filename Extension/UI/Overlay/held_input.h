#pragma once
#include <Windows.h>
#include <bitset>
#include <cstddef>
#include <mutex>

namespace dingosdk::overlay::detail {
// The keys and mouse buttons the game has seen go down and not yet come up, from the raw input
// it reads. While the overlay owns input every press is kept from the game, but the release of
// something the game holds must still reach it: the T that opens chat goes down just before the
// overlay takes over, and without its release the game keeps T held, so it drops out of
// controller mode on its own and menus stop taking the controller.
class HeldInput {
  public:
    // A record the game reads while the overlay does not own input.
    void seen(const RAWINPUT& record) {
        std::lock_guard lock(mutex_);
        if (record.header.dwType == RIM_TYPEKEYBOARD) {
            const auto& key = record.data.keyboard;
            keys_.set(key_id(key), !(key.Flags & RI_KEY_BREAK));
        } else if (record.header.dwType == RIM_TYPEMOUSE) {
            const auto flags = record.data.mouse.usButtonFlags;
            for (unsigned button = 0; button < buttons; ++button) {
                if (flags & down(button)) held_buttons_ |= 1u << button;
                if (flags & up(button)) held_buttons_ &= ~(1u << button);
            }
        }
    }
    // While the overlay owns input: whether `record` releases a key or button the game holds. A
    // mouse record is rewritten to just those releases; the caller drops every other record.
    bool release_only(RAWINPUT& record) {
        std::lock_guard lock(mutex_);
        if (record.header.dwType == RIM_TYPEKEYBOARD) {
            const auto& key = record.data.keyboard;
            const auto id = key_id(key);
            if (!(key.Flags & RI_KEY_BREAK) || !keys_.test(id)) return false;
            keys_.reset(id);
            return true;
        }
        if (record.header.dwType != RIM_TYPEMOUSE) return false;
        USHORT released{};
        for (unsigned button = 0; button < buttons; ++button)
            if ((record.data.mouse.usButtonFlags & up(button)) && (held_buttons_ & (1u << button))) {
                released |= up(button);
                held_buttons_ &= ~(1u << button);
            }
        if (!released) return false;
        RAWMOUSE mouse{};
        mouse.usFlags = MOUSE_MOVE_RELATIVE;
        mouse.usButtonFlags = released;
        record.data.mouse = mouse;
        return true;
    }

  private:
    static constexpr unsigned buttons = 5; // left, right, middle, 4, 5: down/up flag pairs
    static constexpr USHORT down(unsigned button) { return static_cast<USHORT>(1u << (2 * button)); }
    static constexpr USHORT up(unsigned button) { return static_cast<USHORT>(2u << (2 * button)); }
    // The physical key: its scan code with the E0/E1 prefixes; the virtual key when there is none.
    static std::size_t key_id(const RAWKEYBOARD& key) {
        if (!key.MakeCode) return 0x300 | (key.VKey & 0xff);
        return (key.MakeCode & 0xff) | (key.Flags & RI_KEY_E0 ? 0x100 : 0) | (key.Flags & RI_KEY_E1 ? 0x200 : 0);
    }
    std::mutex mutex_;
    std::bitset<0x400> keys_;
    unsigned held_buttons_{};
};
} // namespace dingosdk::overlay::detail

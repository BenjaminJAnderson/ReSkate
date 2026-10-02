#pragma once
#include "Engine/Game/Multiplayer/voice_settings.h"
#include <Windows.h>
#include <array>
#include <string>

namespace dingosdk {
inline std::string voice_key_name(int key) {
    if (!key) return "Not bound";
    if (key == VK_LBUTTON) return "Left mouse";
    if (key == VK_RBUTTON) return "Right mouse";
    if (key == VK_XBUTTON1) return "Mouse 4";
    if (key == VK_XBUTTON2) return "Mouse 5";
    if (key == VK_MBUTTON) return "Middle mouse";
    const auto scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC_EX);
    const auto parameter = static_cast<LONG>((scan & 0xffU) << 16 | ((scan & 0xff00U) ? 1U << 24 : 0));
    std::array<char, 64> name{};
    if (GetKeyNameTextA(parameter, name.data(), static_cast<int>(name.size()))) return name.data();
    return "Key " + std::to_string(key);
}
struct VoicePushToTalk {
    int key{};
    std::uint32_t combo{};
    unsigned device{};
    bool keyboard_armed{}, controller_armed{};
    bool update(const VoiceSettings &settings, bool key_down, ControllerInput input, bool inhibited) noexcept {
        if (key != settings.push_to_talk) { key = settings.push_to_talk; keyboard_armed = false; }
        if (combo != settings.controller_combo || device != input.device) {
            combo = settings.controller_combo; device = input.device; controller_armed = false;
        }
        if (inhibited) { keyboard_armed = controller_armed = false; return false; }
        if (!key_down) keyboard_armed = true;
        if (!input.available) controller_armed = false;
        else if (!(input.buttons & combo)) controller_armed = true;
        return (key && keyboard_armed && key_down) ||
            (input.available && combo && valid_controller_combo(combo) && controller_armed &&
             (input.buttons & combo) == combo);
    }
};
}

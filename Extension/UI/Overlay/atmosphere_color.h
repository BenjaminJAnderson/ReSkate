#pragma once
#include "Engine/Game/World/atmosphere_controls.h"

namespace dingosdk::overlay::menu {
inline constexpr double atmosphere_color_scale = 2500.0;

// The picker uses normalized RGB. Persisted/console values and native caches
// remain in game units so existing saves are never rescaled on load.
inline std::array<float, 3> atmosphere_color_to_ui(const AtmosphereValue& value) {
    std::array<float, 3> color{};
    for (unsigned i = 0; i < color.size(); ++i)
        color[i] = static_cast<float>(std::clamp(value.number[i] / atmosphere_color_scale, 0.0, 1.0));
    return color;
}
inline AtmosphereValue atmosphere_color_from_ui(const std::array<float, 3>& color, const AtmosphereValue& previous) {
    auto value = previous;
    for (unsigned i = 0; i < color.size(); ++i)
        value.number[i] = std::clamp(static_cast<double>(color[i]), 0.0, 1.0) * atmosphere_color_scale;
    return value;
}
}

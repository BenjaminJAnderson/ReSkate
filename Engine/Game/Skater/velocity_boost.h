#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace dingosdk {
enum class VelocityBoostDirection { forward, up };

inline std::optional<std::array<float, 3>> velocity_boost_delta(
    const std::array<float, 16>& transform, float speed, VelocityBoostDirection direction) noexcept {
    const float maximum = direction == VelocityBoostDirection::up ? 25.0f : 300.0f;
    if (!std::isfinite(speed) || speed < 1.0f || speed > maximum) return {};
    if (direction == VelocityBoostDirection::up) return std::array<float, 3>{0, speed, 0};
    std::array<float, 3> delta{transform[8], transform[9], transform[10]};
    float length{};
    for (float component : delta) length += component * component;
    if (!std::isfinite(length) || length <= .01f) return {};
    const auto scale = speed / std::sqrt(length);
    for (auto& component : delta) component *= scale;
    return delta;
}
}

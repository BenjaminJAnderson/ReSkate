#pragma once
#include "Extension/UI/Overlay/overlay.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dingosdk {
inline overlay::FlightInput controller_flight_input(std::int16_t x, std::int16_t y,
    std::uint8_t lower, std::uint8_t raise, bool boost) {
    overlay::FlightInput input;
    input.active = true;
    input.boost = boost;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    const float magnitude = std::sqrt(fx * fx + fy * fy);
    constexpr float deadzone = 7849.0f;
    if (magnitude > deadzone) {
        const float scale = std::clamp((magnitude - deadzone) / (32767.0f - deadzone), 0.0f, 1.0f) / magnitude;
        input.right = fx * scale;
        input.forward = fy * scale;
    }
    const auto trigger = [](std::uint8_t value) {
        return std::max(0.0f, (static_cast<float>(value) - 30.0f) / 225.0f);
    };
    input.up = trigger(raise) - trigger(lower);
    return input;
}

inline bool valid_flight_transform(const std::array<float, 16>& matrix) {
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t col = 0; col < 3; ++col)
            if (!std::isfinite(matrix[row * 4 + col]) || std::abs(matrix[row * 4 + col]) > 1000000) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        float length = 0;
        for (std::size_t j = 0; j < 3; ++j) length += matrix[i * 4 + j] * matrix[i * 4 + j];
        if (std::abs(length - 1) > .05f) return false;
        for (std::size_t k = i + 1; k < 3; ++k) {
            float dot = 0;
            for (std::size_t j = 0; j < 3; ++j) dot += matrix[i * 4 + j] * matrix[k * 4 + j];
            if (std::abs(dot) > .05f) return false;
        }
    }
    return true;
}

inline std::array<float, 3> flight_velocity(const std::array<float, 16>& view,
    const overlay::FlightInput& input, float speed) {
    if (!input.active || !valid_flight_transform(view) || !std::isfinite(speed) ||
        !std::isfinite(input.right) || !std::isfinite(input.up) || !std::isfinite(input.forward)) return {};
    std::array<float, 3> velocity{};
    float length = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        velocity[i] = view[i] * std::clamp(input.right, -1.0f, 1.0f) -
            view[8 + i] * std::clamp(input.forward, -1.0f, 1.0f) +
            (i == 1 ? std::clamp(input.up, -1.0f, 1.0f) : 0);
        length += velocity[i] * velocity[i];
    }
    const float scale = std::clamp(speed, .6f, 1500.0f) * (input.boost ? 4 : 1) / std::max(1.0f, std::sqrt(length));
    for (auto& component : velocity) component *= scale;
    return velocity;
}

inline std::array<float, 16> step_free_flight(std::array<float, 16> matrix,
    const overlay::FlightInput& input, float seconds, float speed) {
    if (!input.active || !valid_flight_transform(matrix) || !std::isfinite(seconds) || !std::isfinite(speed) ||
        !std::isfinite(input.right) || !std::isfinite(input.up) || !std::isfinite(input.forward) ||
        !std::isfinite(input.look_x) || !std::isfinite(input.look_y)) return matrix;
    const auto original = matrix;
    // Native camera basis is right/up/backward. Keep SIMD fourth lanes intact.
    if (input.look_x != 0 || input.look_y != 0) {
        float yaw = std::atan2(matrix[8], matrix[10]);
        float pitch = std::asin(std::clamp(matrix[9], -1.0f, 1.0f));
        yaw -= std::clamp(input.look_x, -250.0f, 250.0f) * .0025f;
        pitch = std::clamp(pitch + std::clamp(input.look_y, -250.0f, 250.0f) * .0025f, -1.55f, 1.55f);
        const float sy = std::sin(yaw), cy = std::cos(yaw), sp = std::sin(pitch), cp = std::cos(pitch);
        matrix[0] = cy; matrix[1] = 0; matrix[2] = -sy;
        matrix[4] = -sy * sp; matrix[5] = cp; matrix[6] = -cy * sp;
        matrix[8] = sy * cp; matrix[9] = sp; matrix[10] = cy * cp;
    }
    const auto velocity = flight_velocity(matrix, input, speed);
    for (std::size_t i = 0; i < 3; ++i) matrix[12 + i] += velocity[i] * std::clamp(seconds, 0.0f, .05f);
    return valid_flight_transform(matrix) ? matrix : original;
}
}

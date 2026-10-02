#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace dingosdk {
// The active camera as the client last updated it: its world matrix (rows: right, up,
// back, position) and vertical field of view in degrees. Written on the client thread,
// read by the overlay to place things over the world. `camera` is the camera object itself:
// the game moves its own camera after the client update, so the overlay reads the matrix
// from it again when it draws (a freecam or noclip camera is already final here).
struct GameView {
    std::array<float, 16> world{};
    float vertical_fov{};
    std::uintptr_t camera{};
};
namespace game_view_detail {
inline std::mutex mutex;
inline GameView latest;
inline std::chrono::steady_clock::time_point at;
} // namespace game_view_detail
inline void publish_game_view(const GameView &view) noexcept {
    std::lock_guard lock(game_view_detail::mutex);
    game_view_detail::latest = view;
    game_view_detail::at = std::chrono::steady_clock::now();
}
// Nothing while the camera has not updated recently (loading, no local camera).
inline std::optional<GameView> latest_game_view(std::chrono::milliseconds max_age = std::chrono::milliseconds(250)) noexcept {
    std::lock_guard lock(game_view_detail::mutex);
    if (game_view_detail::at == std::chrono::steady_clock::time_point{} ||
        std::chrono::steady_clock::now() - game_view_detail::at > max_age)
        return std::nullopt;
    return game_view_detail::latest;
}
} // namespace dingosdk

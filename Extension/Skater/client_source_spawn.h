#pragma once
#include <cstdint>
#include <array>
#include <string>
#include "Extension/UI/Overlay/overlay.h"

namespace dingosdk {
// Binds the native camera functions the debug controls use. Installs no hooks.
void initialize_client_source_spawn(std::uintptr_t base);
// Installs the exact camera-reset handler guard; forwarding is unchanged until a Noclip request is queued.
bool start_client_noclip_velocity(std::uintptr_t base) noexcept;
// Engine-thread-only interactive controls. Presentation callbacks only queue requests.
overlay::DebugModel on_client_debug_tick(std::uintptr_t base, std::uintptr_t client,
    bool can_control, bool camera_phase_observed, const overlay::DebugRequest* request = nullptr,
    const overlay::FlightInput* flight_input = nullptr);
bool restore_client_debug(std::uintptr_t base, std::uintptr_t client, bool camera_phase_observed);
// Native camera lease for the party's Spectate action. Null restores only the
// camera acquired by this function; it never changes another debug camera. A positive
// `fov` is applied to the spectate camera while it is held; its own comes back after.
bool update_party_camera(std::uintptr_t base, std::uintptr_t client, bool ready, bool phase,
    const std::array<float, 16>* transform, std::string& detail, float fov = 0) noexcept;
bool read_local_camera_transform(std::uintptr_t base, std::uintptr_t client, std::array<float, 16>& transform) noexcept;
// Publishes the local view's camera (Engine/Game/UI/game_view.h) for things drawn over the
// world. Client thread, every tick while something needs it.
bool publish_local_camera_view(std::uintptr_t base, std::uintptr_t client) noexcept;
}

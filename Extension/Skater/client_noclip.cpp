#include "client_source_spawn.h"
#include "client_source_spawn_internal.h"
#include "no_bail.h"
#include "offboard_flight.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/no_bail.h"
#include "Engine/Game/Build/20260929/offboard_flight.h"
#include "free_flight.h"
#include <cmath>

namespace dingosdk::client_source::detail {
namespace {
// Physics bodies already verified writable (a VirtualQuery each), so the
// every-frame and every-simulation-step body reads below skip that system call.
// A body at a new address, and every body once a second, is checked again.
// Only touched with SourceState::busy held, which every caller holds.
struct WritableBodies {
    std::array<std::uintptr_t, 32> bodies{};
    ULONGLONG until{};
};
WritableBodies& writable_bodies() { static WritableBodies value; return value; }
bool body_writable(std::size_t index, std::uintptr_t body) {
    auto& cache = writable_bodies();
    const auto now = GetTickCount64();
    if (now >= cache.until) {
        cache.bodies = {};
        cache.until = now + 1000;
    }
    if (cache.bodies[index] == body) return true;
    if (!source_writable(body + 0x60, 0x20)) return false;
    cache.bodies[index] = body;
    return true;
}
bool copy_to(std::uintptr_t address, const void* data, std::size_t size) noexcept {
    __try {
        std::memcpy(reinterpret_cast<void*>(address), data, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// debug_write for a physics body field inside the region debug_noclip_bodies
// verified writable: a guarded copy and read-back instead of VirtualQuery,
// WriteProcessMemory and a ReadProcessMemory on every simulation step.
template<class T> void body_write(std::uintptr_t address, const T& value) {
    source_require(source_range(address, sizeof(T)) && copy_to(address, &value, sizeof(T)), "Debug setting write failed.");
    T written{};
    source_require(memory::peek(address, written) && written == value, "Debug setting write could not be verified.");
}
}
void debug_stop_noclip(InteractiveDebug& debug) noexcept {
    debug.noclip = false;
    debug.noclip_entity = 0;
    debug.noclip_velocity.valid = false;
    clear_no_bail_flight();
}
NoclipBodies debug_noclip_bodies(std::uintptr_t base, std::uintptr_t client, std::uintptr_t entity) {
    overlay::DebugModel skater;
    (void)debug_skater(base, client, skater);
    source_require(skater.skater_identity == entity, "Skater changed; flight stopped.");
    SourceReader reader;
    const auto collection = reader.pointer(entity, 0x70);
    const auto component = reader.pointer(entity, 0x628);
    source_require(reader.pointer(component) == base + addr::engine::skater_component_vtable && reader.pointer(component, 0x18) == collection,
        "Skater physics ownership changed.");
    NoclipBodies result;
    result.root = skater.skater_position;
    result.core = reader.pointer(component, 0x70);
    source_require(reader.pointer(result.core) == base + addr::no_bail::bail_core_vtable, "Skater physics is unavailable.");
    const auto board = reader.pointer(reader.pointer(result.core, 0x430), 0x18);
    result.rig_wrapper = reader.pointer(result.core, 0x438);
    const auto rig = reader.pointer(result.rig_wrapper, 0x2f10);
    result.context = reader.pointer(result.core, 0x3c0);
    source_require(reader.pointer(result.rig_wrapper) == result.context && reader.pointer(result.rig_wrapper, 0x4630) == result.core,
        "Skater motion ownership changed.");
    result.seconds = reader.value<float>(result.context, 0x17ec);
    source_require(std::isfinite(result.seconds) && result.seconds >= 0 && result.seconds <= .1f, "Invalid physics timestep.");
    result.offboard = reader.pointer(reader.pointer(result.core, 0x3b0)) == base + addr::offboard_flight::offboard_flight_vtable;
    source_require(reader.pointer(board) == base + spawn::board_physics_vtable && reader.pointer(rig) == base + spawn::rig_physics_vtable,
        "Unsupported board or skeleton physics.");
    const auto board_parts = reader.pointer(board, 0x20), rig_parts = reader.pointer(rig, 0x20);
    source_require(reader.value<std::uint32_t>(board_parts, 0) == 9 && reader.value<std::uint32_t>(rig_parts, 0) == 26,
        "Unsupported physics body layout.");
    for (std::size_t i = 0; i < result.parts.size(); ++i) {
        const auto body = i < 9 ? board_parts + i * 0x130 : rig_parts + (i - 8) * 0x130;
        source_require(reader.pointer(body, 0x10) == (i < 9 ? board : rig) && body_writable(i, body),
            "Physics body ownership changed.");
        const auto velocity = reader.value<std::array<float, 3>>(body, 0x70);
        for (const auto v : velocity) source_require(std::isfinite(v) && std::abs(v) <= 100000,
            "Invalid physics velocity.");
        result.parts[i] = body;
    }
    result.board_height = reader.value<float>(board_parts, 0x54);
    source_require(std::isfinite(result.board_height) && std::abs(result.board_height) <= 1000000, "Invalid board altitude.");
    reader.verify();
    return result;
}
namespace {
void noclip_apply_velocity(std::uintptr_t core) noexcept {
    SourceLastError error;
    auto& state = source_state();
    if (!state.initialized.load(std::memory_order_acquire) || !state.velocity_guard_active.load(std::memory_order_acquire) ||
        state.busy.test_and_set(std::memory_order_acquire)) return;
    SourceBusyScope scope{state.busy};
    auto& debug = state.trial.debug;
    const auto apply_boost = [&](InteractiveDebug::VelocityRequest& boost, std::uint64_t& updates, bool up) {
        if (!boost.valid || boost.core != core) return;
        try {
            source_require(GetTickCount64() < boost.expires, "Velocity boost timed out.");
            const auto bodies = debug_noclip_bodies(state.trial.base, boost.client, boost.entity);
            source_require(bodies.core == core, "Skater physics was replaced before the velocity boost could apply.");
            source_require(!bodies.offboard, "Velocity boosts require the skater to be on the board.");
            source_require(!debug.noclip && !debug.park_editor, "Velocity boost cancelled while editing or flying.");
            SourceReader reader;
            source_require(reader.value<std::uint8_t>(boost.entity, 0x7e0) == 0,
                "Wait for the current teleport before using velocity boosts.");
            std::array<std::array<float, 3>, 32> velocities{};
            std::array<std::uint32_t, 32> flags{};
            for (std::size_t i = 0; i < bodies.parts.size(); ++i) {
                velocities[i] = reader.value<std::array<float, 3>>(bodies.parts[i], 0x70);
                flags[i] = reader.value<std::uint32_t>(bodies.parts[i], 0x60);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    velocities[i][axis] += boost.velocity[axis];
                    source_require(std::isfinite(velocities[i][axis]) && std::abs(velocities[i][axis]) <= 100000,
                        "Velocity boost produced an invalid physics velocity.");
                }
            }
            reader.verify();
            boost.valid = false;
            for (std::size_t i = 0; i < bodies.parts.size(); ++i) {
                body_write(bodies.parts[i] + 0x70, velocities[i]);
                body_write(bodies.parts[i] + 0x60, flags[i] | 8u);
            }
            ++updates;
            debug.status = up ? "Up velocity added." : "Forward velocity added.";
        } catch (const SourceGuard& issue) { boost.valid = false; debug.status = issue.message; }
          catch (...) { boost.valid = false; debug.status = "Velocity boost failed during the physics update."; }
    };
    apply_boost(debug.forward_velocity, debug.forward_velocity_updates, false);
    apply_boost(debug.up_velocity, debug.up_velocity_updates, true);
    const auto& request = debug.noclip_velocity;
    if (!debug.noclip || !request.valid || request.core != core) return;
    try {
        if (GetTickCount64() >= request.expires) { debug_stop_noclip(debug); debug.status = "Flight input timed out."; return; }
        const auto bodies = debug_noclip_bodies(state.trial.base, request.client, request.entity);
        source_require(bodies.core == core, "Skater physics was replaced; flight stopped.");
        SourceReader reader;
        source_require(reader.value<std::uint8_t>(request.entity, 0x7e0) == 0,
            "A teleport started; flight stopped.");
        for (float v : request.velocity) source_require(std::isfinite(v) && std::abs(v) <= 6000, "Invalid flight input.");
        reader.verify();
        if (bodies.seconds == 0) { debug.noclip_altitude_valid = false; return; }
        // Off-board the native drive only chases the motion target through a
        // spring, at a capped walking speed while grounded. Give the skeleton
        // bodies the flight velocity directly so the root (and the camera) keeps
        // up at any speed and stops dead on release. The carried/detached board
        // is left alone; its bodies only receive velocity while riding.
        const float height = bodies.offboard ? bodies.root[1] : bodies.board_height;
        source_require(std::isfinite(height) && std::abs(height) <= 1000000, "Invalid flight altitude.");
        if (!debug.noclip_altitude_valid || debug.noclip_altitude_offboard != bodies.offboard) {
            debug.noclip_altitude = height;
            debug.noclip_altitude_offboard = bodies.offboard;
        }
        debug.noclip_altitude_valid = true;
        debug.noclip_altitude = std::clamp(debug.noclip_altitude, height - 3.0f, height + 3.0f);
        auto velocity = request.velocity;
        // Correct gravity drift without changing world gravity or teleporting.
        velocity[1] += std::clamp((debug.noclip_altitude - height) * 12.0f, -8.0f, 8.0f);
        debug.noclip_altitude += request.velocity[1] * bodies.seconds;
        // Match native velocity writers: XYZ at +70 and dirty bit 8 at +60.
        // Preserve W, angular velocity, transforms, contacts and other flags.
        // Runs after the simulation's movement-state update.
        for (std::size_t i = bodies.offboard ? 9 : 0; i < bodies.parts.size(); ++i) {
            const auto body = bodies.parts[i];
            const auto flags = reader.value<std::uint32_t>(body, 0x60);
            body_write(body + 0x70, velocity);
            body_write(body + 0x60, flags | 8u);
        }
        ++debug.noclip_velocity_updates;
    } catch (const SourceGuard& issue) { debug_stop_noclip(debug); debug.status = issue.message; }
      catch (...) { debug_stop_noclip(debug); debug.status = "Flight stopped after a physics error."; }
}
void noclip_physics_update(std::uintptr_t core) {
    const auto original = source_state().velocity_update_original.load(std::memory_order_acquire);
    if (original) original(core);
    noclip_apply_velocity(core);
}
bool noclip_motion_target(std::uintptr_t rig, std::uintptr_t context,
    const std::array<float,16>* supplied, std::array<float,16>& target) noexcept {
    SourceLastError error;
    auto& state = source_state();
    if (!state.initialized.load(std::memory_order_acquire) || !state.velocity_guard_active.load(std::memory_order_acquire) ||
        state.busy.test_and_set(std::memory_order_acquire)) return false;
    SourceBusyScope scope{state.busy};
    auto& debug = state.trial.debug;
    const auto& request = debug.noclip_velocity;
    if (!debug.noclip || !request.valid) return false;
    try {
        SourceReader reader;
        // Foreign native calls forward without altering the local flight session.
        if (reader.pointer(request.core, 0x438) != rig || reader.pointer(request.core, 0x3c0) != context) return false;
        if (GetTickCount64() >= request.expires) { debug_stop_noclip(debug); debug.status = "Flight input timed out."; return false; }
        const auto bodies = debug_noclip_bodies(state.trial.base, request.client, request.entity);
        source_require(bodies.core == request.core && bodies.rig_wrapper == rig && bodies.context == context,
            "Skater motion changed; flight stopped.");
        if (!bodies.offboard || bodies.seconds == 0) return false;
        source_require(reader.value<std::uint8_t>(request.entity, 0x7e0) == 0, "A teleport started; flight stopped.");
        std::array<float,16> previous{};
        source_require(reader.raw(reinterpret_cast<std::uintptr_t>(supplied), target.data(), sizeof(target)) &&
            reader.raw(rig + 0x4720, previous.data(), sizeof(previous)) &&
            valid_flight_transform(target) && valid_flight_transform(previous), "Invalid skater motion target.");
        // The new build moved the simulation counter to +180. +170 now holds
        // state flags and can stay constant, suppressing every subsequent move.
        (void)reader.value<std::uint32_t>(request.core, 0x180);
        // Lead the skater's current root by one step instead of integrating the
        // previous target. The native drive is a spring toward this target: a
        // free-running target stretched it without bound at high speed (the
        // skater outran the camera) and the ragdoll snapped back on release.
        // Anchoring to the root bounds the stretch to one step and leaves no
        // stored energy when the input stops.
        for (std::size_t i = 0; i < 3; ++i) {
            source_require(std::isfinite(request.velocity[i]) && std::abs(request.velocity[i]) <= 6000, "Invalid flight input.");
            source_require(std::isfinite(bodies.root[i]) && std::abs(bodies.root[i]) <= 1000000, "Invalid skater root position.");
            target[12+i] = bodies.root[i] + request.velocity[i] * bodies.seconds;
        }
        source_require(valid_flight_transform(target), "Flight motion target is out of bounds.");
        reader.verify();
        source_require(sync_offboard_flight_velocity(state.trial.base, bodies.core, context, rig, request.velocity),
            "Off-board flight velocity ownership changed; flight stopped.");
        ++debug.noclip_motion_updates;
        return true;
    } catch (const SourceGuard& issue) { debug_stop_noclip(debug); debug.status = issue.message; }
      catch (...) { debug_stop_noclip(debug); debug.status = "Flight stopped after a motion error."; }
    return false;
}
void noclip_skater_motion(std::uintptr_t rig, std::uintptr_t context,
    const std::array<float,16>* supplied, std::uint8_t flags) {
    const auto original = source_state().motion_original.load(std::memory_order_acquire);
    alignas(16) std::array<float,16> target{};
    const auto* motion = noclip_motion_target(rig, context, supplied, target) ? &target : supplied;
    if (original) original(rig, context, motion, flags);
}
}
}

namespace dingosdk {
using namespace client_source::detail;

bool start_client_noclip_velocity(std::uintptr_t base) noexcept {
    SourceLastError error;
    try {
        auto& state = source_state();
        std::lock_guard lock(state.initialization_mutex);
        if (!state.initialized.load() || state.trial.base != base) return false;
        if (state.velocity_guard_attempted) return state.velocity_guard_active.load();
        state.velocity_guard_attempted = true;
        SourceReader reader;
        std::array<unsigned char, 32> bytes{};
        if (!reader.raw(base + spawn::physics_update, bytes.data(), bytes.size()) || bytes != spawn::physics_update_prefix ||
            reader.pointer(base + addr::no_bail::bail_core_vtable, 0x58) != base + spawn::physics_update ||
            !reader.raw(base + spawn::skater_motion, bytes.data(), bytes.size()) || bytes != spawn::skater_motion_prefix ||
            !offboard_flight_compatible(base)) return false;
        reader.verify();
        auto* target = reinterpret_cast<void*>(base + spawn::physics_update);
        auto* motion_target = reinterpret_cast<void*>(base + spawn::skater_motion);
        void* original{};
        if (hook_prepare(target, reinterpret_cast<void*>(&noclip_physics_update), &original) != HookOk) return false;
        if (!original) { (void)hook_remove(target); return false; }
        state.velocity_update_original.store(reinterpret_cast<SourcePhysicsUpdate>(original), std::memory_order_release);
        original = nullptr;
        if (hook_prepare(motion_target, reinterpret_cast<void*>(&noclip_skater_motion), &original) != HookOk) {
            (void)hook_remove(target); state.velocity_update_original.store(nullptr); return false;
        }
        if (!original) {
            (void)hook_remove(motion_target); (void)hook_remove(target);
            state.velocity_update_original.store(nullptr); return false;
        }
        state.motion_original.store(reinterpret_cast<SourceSkaterMotion>(original), std::memory_order_release);
        // Keep trampolines on uncertain enable results; handlers still forward.
        if (hook_enable(target) != HookOk || hook_enable(motion_target) != HookOk) return false;
        state.velocity_guard_active.store(true, std::memory_order_release);
        return true;
    } catch (...) { return false; }
}
}

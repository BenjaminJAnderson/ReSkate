#include "puppet_cost.h"
#include "native_skater_internal.h"
#include "native_pose_layout.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/puppet_cost.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

// Native work a remote puppet does not need (Engine/Game/Build/20260929/puppet_cost.h):
// - driven_placement: AntDriven no longer re-places the skater entity every frame;
// - ecs_retry: the ECS attach that never completes on a remote actor runs once a second;
// - far_components: past 200 m the crowd agent, stimulus and pedestrian collider update
//   4 times a second, with the elapsed game time.
// Within 200 m of the local skater or the camera every native update stays as it was.
namespace dingosdk::multiplayer {
using namespace native_skater_detail;
namespace {
namespace cost = addr::puppet_cost;
using Address = std::uintptr_t;
using ComponentUpdate = void (*)(Address component, const void *info);
using ManagerRegister = void (*)(Address manager, Address item, std::uint16_t interval);
using ManagerUnregister = void (*)(Address manager, Address item);

// The far component rate applies past live_distance.
constexpr float live_distance = 200.0f;
constexpr float far_update_period = 0.25f; // game seconds between far component updates
constexpr ULONGLONG ecs_retry_ms = 1000, far_linger_ms = 1000;

constexpr unsigned bit(PuppetSaving saving) noexcept { return 1u << static_cast<unsigned>(saving); }
constexpr unsigned all_savings =
    bit(PuppetSaving::driven_placement) | bit(PuppetSaving::ecs_retry) | bit(PuppetSaving::far_components);
std::atomic<unsigned> &enabled_savings() noexcept {
    static std::atomic<unsigned> value{all_savings};
    return value;
}
bool on(PuppetSaving saving) noexcept {
    return (enabled_savings().load(std::memory_order_relaxed) & bit(saving)) != 0;
}

enum Kind : std::size_t { navigator, stimulus, collider, ecs, kinds };
constexpr std::array<const game::build::Fingerprint *, kinds> contracts{
    &cost::navigator_update, &cost::stim_update, &cost::collider_update, &cost::ecs_update};
// The class vtable slot each handler must occupy.
constexpr std::array<std::pair<Address, Address>, kinds> handler_slots{{
    {cost::navigator_vtable, cost::presim_handler_slot},
    {cost::stim_vtable, cost::presim_handler_slot},
    {cost::collider_vtable, cost::postsim_handler_slot},
    {cost::ecs_vtable, cost::presim_handler_slot},
}};

// Read by the update hooks on whichever thread runs the pass.
struct UpdateHookSlot {
    std::atomic<bool> distant{}; // past live_distance with far_components on
    // Game seconds not yet handed to the navigator, stimulus and collider updates.
    std::array<std::atomic<float>, 3> deferred{};
    // Earliest next ECS attach attempt for the skater and the skateboard.
    std::array<std::atomic<ULONGLONG>, 2> next_ecs{};
};
struct UpdateHooks {
    std::array<std::atomic<ComponentUpdate>, kinds> original{};
    std::atomic<bool> installed{};
    std::atomic<int> far_slots{};
    // After the last player comes back within range the hooks keep resolving owners a
    // little longer, so each update hands over its deferred time once.
    std::atomic<ULONGLONG> linger_until{};
    std::array<UpdateHookSlot, max_remote_players> slots{};
};
UpdateHooks &update_hooks() noexcept {
    static auto *h = new UpdateHooks{};
    return *h;
}

// What this module applied to the current slot's actor. Client thread.
struct Applied {
    Address driven{}, driven_manager{}; // unregistered AntDriven component and its manager
    bool driven_done{};                 // tried for this actor (success or not)
    unsigned failed{};                  // savings disabled for this actor after an error
};
Applied &applied() noexcept {
    static auto *a = new PeerStorage<Applied>;
    return a->current();
}
std::atomic<int> undriven_actors{};

void set_far(bool value) noexcept {
    auto &h = update_hooks();
    if (h.slots[peer_slot].distant.exchange(value, std::memory_order_acq_rel) == value)
        return;
    h.far_slots.fetch_add(value ? 1 : -1, std::memory_order_acq_rel);
    if (!value)
        h.linger_until.store(GetTickCount64() + far_linger_ms, std::memory_order_release);
}

// The slot whose skater (or skateboard, when boards) owns component. Two reads and a scan of
// the watched keys: the only work the hooks add for every other actor.
std::size_t owner_slot(Address component, bool boards, bool *board = nullptr) noexcept {
    Address collection{}, owner{};
    if (!memory::peek(component + 0x18, collection) || !memory::peek(collection, owner) || !owner)
        return max_remote_players;
    auto &keys = watched();
    const auto bound = std::min(keys.bound.load(std::memory_order_acquire), max_remote_players);
    for (std::size_t slot = 0; slot < bound; ++slot) {
        if (keys.entity[slot].load(std::memory_order_acquire) == owner)
            return slot;
        if (boards && keys.board[slot].load(std::memory_order_acquire) == owner) {
            if (board)
                *board = true;
            return slot;
        }
    }
    return max_remote_players;
}

// Navigator, stimulus and pedestrian collider updates (component, const UpdateInfo *): past
// 200 m they run every far_update_period with the game time elapsed since their last run,
// so the collider's velocity (position change / dt) and the timers stay right.
template <Kind K> void far_update_hook(Address component, const void *info) noexcept {
    auto &h = update_hooks();
    const auto original = h.original[K].load(std::memory_order_acquire);
    if (h.far_slots.load(std::memory_order_acquire) <= 0 &&
        GetTickCount64() >= h.linger_until.load(std::memory_order_acquire)) {
        original(component, info);
        return;
    }
    const auto error = GetLastError();
    const auto slot = owner_slot(component, false);
    const auto at = reinterpret_cast<Address>(info);
    float dt{};
    if (slot >= max_remote_players || !memory::peek(at + cost::update_info_dt, dt) || !std::isfinite(dt) ||
        dt < 0.0f) {
        SetLastError(error);
        original(component, info);
        return;
    }
    auto &state = h.slots[slot];
    auto &deferred = state.deferred[K];
    const float total = deferred.load(std::memory_order_relaxed) + dt;
    if (state.distant.load(std::memory_order_acquire) && on(PuppetSaving::far_components) &&
        total < far_update_period) {
        deferred.store(total, std::memory_order_relaxed);
        SetLastError(error);
        return;
    }
    deferred.store(0.0f, std::memory_order_relaxed);
    alignas(16) std::array<std::uint8_t, cost::update_info_size> copy{};
    if (total == dt || !memory::peek_bytes(at, copy.data(), copy.size())) {
        SetLastError(error);
        original(component, info);
        return;
    }
    std::memcpy(copy.data() + cost::update_info_dt, &total, sizeof(total));
    SetLastError(error);
    original(component, copy.data());
}
// The ECS attach retry of a remote skater or skateboard: at most once per ecs_retry_ms.
// The native handler itself only queues the attempt while the attach is incomplete.
void ecs_update_hook(Address component, const void *info) noexcept {
    auto &h = update_hooks();
    const auto original = h.original[ecs].load(std::memory_order_acquire);
    if (!on(PuppetSaving::ecs_retry) || !watched().bound.load(std::memory_order_acquire)) {
        original(component, info);
        return;
    }
    const auto error = GetLastError();
    bool board{};
    const auto slot = owner_slot(component, true, &board);
    if (slot < max_remote_players) {
        auto &next = h.slots[slot].next_ecs[board ? 1 : 0];
        const auto now = GetTickCount64();
        if (now < next.load(std::memory_order_relaxed)) {
            SetLastError(error);
            return;
        }
        next.store(now + ecs_retry_ms, std::memory_order_relaxed);
    }
    SetLastError(error);
    original(component, info);
}
void *replacement(std::size_t kind) noexcept {
    switch (kind) {
    case navigator:
        return reinterpret_cast<void *>(&far_update_hook<navigator>);
    case stimulus:
        return reinterpret_cast<void *>(&far_update_hook<stimulus>);
    case collider:
        return reinterpret_cast<void *>(&far_update_hook<collider>);
    default:
        return reinterpret_cast<void *>(&ecs_update_hook);
    }
}
constexpr std::array<const char *, 3> saving_names{"AntDriven placement removal", "ECS retry throttle",
                                                   "far component throttle"};
constexpr std::array<const char *, kinds> hook_names{"navigator", "stimulus broadcast", "pedestrian collider",
                                                     "ECS attach"};

bool matches(Address base, const game::build::Fingerprint &contract) noexcept {
    std::array<unsigned char, 32> bytes{};
    return readable(base + contract.rva, bytes.data(), bytes.size()) && bytes == contract.bytes;
}

// AntDriven re-places the skater entity from a joint of the pose buffer every frame; the
// SDK already places it (place_actor) with the same network pose. Its own deinit uses the
// same unregister call; register restores it.
void set_undriven(Address base, Remote &r, Applied &a, bool want) {
    if (want && !a.driven_done) {
        a.driven_done = true;
        require(matches(base, cost::manager_register) && matches(base, cost::manager_unregister),
                "Native component manager functions differ.");
        const auto component = read_native_component(readable, r.entity, base + cost::ant_driven_vtable);
        const auto manager = ptr(component, cost::ant_driven_manager);
        if (!manager)
            return;
        require(ptr(manager) == base + addr::engine::source_manager_vtable &&
                    ptr(manager, cost::manager_hooks) == manager + cost::manager_hooks_storage &&
                    ptr(manager, cost::manager_hooks_storage) == base + cost::ant_driven_hooks_vtable &&
                    read<std::uint32_t>(manager, cost::manager_kind) == cost::ant_driven_kind,
                "AntDriven registration differs.");
        reinterpret_cast<ManagerUnregister>(base + cost::manager_unregister.rva)(manager,
                                                                                component + cost::ant_driven_item);
        a.driven = component;
        a.driven_manager = manager;
        undriven_actors.fetch_add(1, std::memory_order_relaxed);
    } else if (!want && a.driven_done) {
        const auto component = std::exchange(a.driven, 0), manager = std::exchange(a.driven_manager, 0);
        a.driven_done = false;
        if (!component)
            return;
        undriven_actors.fetch_add(-1, std::memory_order_relaxed);
        // Registered again with its native interval (1), unless its own deinit released it.
        if (manager && ptr(component, cost::ant_driven_manager) == manager)
            reinterpret_cast<ManagerRegister>(base + cost::manager_register.rva)(
                manager, component + cost::ant_driven_item, 1);
    }
}
void report(Applied &a, PuppetSaving saving, const std::exception &e) noexcept {
    if (a.failed & bit(saving))
        return;
    a.failed |= bit(saving);
    logging::log(logging::Level::warning, logging::Channel::runtime,
                 "Multiplayer: {} unavailable for the player in slot {}: {}",
                 saving_names[static_cast<std::size_t>(saving)], peer_slot, e.what());
}
} // namespace

namespace puppet_cost {
PreparedHooks prepare_hooks(std::uintptr_t base) noexcept {
    auto &h = update_hooks();
    PreparedHooks prepared;
    if (h.installed.load(std::memory_order_acquire))
        return prepared;
    for (std::size_t kind = 0; kind < kinds; ++kind) {
        const auto &contract = *contracts[kind];
        Address slot{};
        if (!matches(base, contract) ||
            !readable(base + handler_slots[kind].first + handler_slots[kind].second, &slot, sizeof(slot)) ||
            slot != base + contract.rva) {
            logging::log(logging::Level::warning, logging::Channel::runtime,
                         "Multiplayer: the {} update differs from the supported build; its saving stays off.",
                         hook_names[kind]);
            continue;
        }
        auto *target = reinterpret_cast<void *>(base + contract.rva);
        void *original{};
        if (const auto status = hook_prepare(target, replacement(kind), &original); status != HookOk || !original) {
            if (status == HookOk)
                hook_remove(target);
            logging::log(logging::Level::warning, logging::Channel::runtime,
                         "Multiplayer: cannot prepare the {} update hook ({}).", hook_names[kind],
                         hook_status_string(status));
            continue;
        }
        h.original[kind].store(reinterpret_cast<ComponentUpdate>(original), std::memory_order_release);
        prepared.targets[prepared.count++] = target;
    }
    return prepared;
}
void hooks_applied(const PreparedHooks &prepared, bool enabled) noexcept {
    update_hooks().installed.store(enabled && prepared.count != 0, std::memory_order_release);
}
void forget_board() noexcept {
    update_hooks().slots[peer_slot].next_ecs[1].store(0, std::memory_order_relaxed);
}
void forget_skater() noexcept {
    forget_board();
    auto &a = applied();
    if (a.driven)
        undriven_actors.fetch_add(-1, std::memory_order_relaxed);
    a = {};
    set_far(false);
    auto &slot = update_hooks().slots[peer_slot];
    for (auto &deferred : slot.deferred)
        deferred.store(0.0f, std::memory_order_relaxed);
    slot.next_ecs[0].store(0, std::memory_order_relaxed);
}
} // namespace puppet_cost

void set_puppet_saving(PuppetSaving saving, bool enabled) noexcept {
    if (enabled)
        enabled_savings().fetch_or(bit(saving), std::memory_order_relaxed);
    else
        enabled_savings().fetch_and(~bit(saving), std::memory_order_relaxed);
}
bool puppet_saving(PuppetSaving saving) noexcept { return on(saving); }

void note_remote_distance(float metres) noexcept {
    auto &r = remote();
    auto &a = applied();
    auto &s = shared();
    if (!s.hooks || !r.entity || GetCurrentThreadId() != s.engine_thread) {
        set_far(false);
        return;
    }
    // An unknown distance counts as near.
    const bool known = std::isfinite(metres) && metres >= 0.0f;
    set_far(known && metres > live_distance && on(PuppetSaving::far_components));
    {
        std::lock_guard lock(r.mutex);
        if (r.failed)
            return;
    }
    // After an error a saving is not applied again to this actor, but whatever it already
    // applied is still undone when no longer wanted.
    try {
        set_undriven(s.base, r, a, on(PuppetSaving::driven_placement) &&
                                       !(a.failed & bit(PuppetSaving::driven_placement)));
    } catch (const std::exception &e) {
        report(a, PuppetSaving::driven_placement, e);
    }
}

std::string puppet_saving_status() {
    const auto state = [](PuppetSaving saving) { return on(saving) ? "on" : "off"; };
    return std::format("Far-player savings: AntDriven placement removal {} ({} players), ECS retry throttle {}, "
                       "far components {} ({} players past 200 m); update hooks {}.",
                       state(PuppetSaving::driven_placement), undriven_actors.load(),
                       state(PuppetSaving::ecs_retry), state(PuppetSaving::far_components),
                       update_hooks().far_slots.load(), update_hooks().installed.load() ? "installed" : "not installed");
}
} // namespace dingosdk::multiplayer

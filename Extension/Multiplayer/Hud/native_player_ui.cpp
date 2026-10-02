#include "native_player_ui.h"
#include "Extension/Multiplayer/Session/peer_slots.h"
#include "native_indicators.h"
#include "native_party.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/native_player_ui.h"
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>

namespace dingosdk::multiplayer {
namespace {
namespace player_ui = addr::native_player_ui;
// Map display, location manager and render message records: guarded copies, not a
// system call per read (the map tick and its copy run every frame the map ticks).
template <class T> T read(std::uintptr_t address) {
    T result{};
    if (!memory::peek(address, result))
        throw std::runtime_error("Native player UI memory unavailable.");
    return result;
}
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Location {
    std::array<float, 2> position{};
    std::uint8_t fields = 1;
    std::array<std::uint8_t, 3> padding{};
};
struct MapEntry {
    std::uintptr_t manager{};
    std::uint16_t handle{};
};
struct Shared {
    std::recursive_mutex maps_mutex;
    std::uintptr_t base{};
    std::atomic<bool> installed{};
    void (*map_tick)(std::uintptr_t){};
    std::uintptr_t (*copy)(std::uintptr_t, std::uintptr_t){};
    std::uintptr_t (*destroy)(std::uintptr_t, unsigned){};
    void (*shutdown)(std::uint8_t){};
    bool attempted{};
};
Shared &shared() {
    static auto *s = new Shared;
    return *s;
}
struct UiState {
    std::mutex snapshot_mutex;
    // visible, readable without the lock: a map tick skips a slot that shows
    // nothing and has no map entry to release.
    std::atomic<bool> shown{};
    bool visible{};
    ULONGLONG sampled{}, next_projection{};
    std::array<float, 3> position{};
    std::string name, status = "Waiting for the native player map.";
    std::map<std::uintptr_t, MapEntry> maps;
    std::atomic<std::uint64_t> map_updates{};
};
UiState &state() {
    static auto *s = new PeerStorage<UiState>;
    return s->current();
}
using MapSelection = std::array<MapEntry *, max_remote_players>;
thread_local MapSelection current_maps{};
void status(std::string message) {
    auto &s = state();
    std::lock_guard lock(s.snapshot_mutex);
    s.status = std::move(message);
}
bool manager_matches(std::uintptr_t manager) {
    const auto base = shared().base;
    return manager && read<std::uintptr_t>(base + player_ui::location_manager) == manager &&
           read<std::uintptr_t>(manager) == base + player_ui::location_manager_vtable;
}
void release(MapEntry &entry) {
    if (entry.handle && manager_matches(entry.manager))
        reinterpret_cast<void (*)(std::uintptr_t, std::uint16_t)>(shared().base + player_ui::location_release)(entry.manager,
                                                                                            entry.handle);
    entry = {};
}
// display: the map display's type checked out (tick_hook checks it once per tick).
MapEntry *update_map(std::uintptr_t interface_address, bool display) {
    auto &s = state();
    if (s.maps.empty() && !s.shown.load(std::memory_order_acquire))
        return nullptr;
    MapEntry *selected{};
    try {
        const auto object = interface_address - 0x40;
        require(display, "Native map display type differs.");
        std::array<float, 3> position{};
        bool visible{};
        {
            std::lock_guard snapshot(s.snapshot_mutex);
            visible = s.visible && GetTickCount64() - s.sampled < 1000 &&
                      !native_party_map_icon_visible(peer_slot);
            position = s.position;
        }
        auto found = s.maps.find(object);
        if (!visible) {
            if (found != s.maps.end()) {
                release(found->second);
                s.maps.erase(found);
            }
        } else {
            const auto now = GetTickCount64();
            if (found != s.maps.end() && now < s.next_projection)
                return &found->second;
            s.next_projection = now + 50;
            require(found != s.maps.end() || s.maps.size() < 8, "Native player map instance limit reached.");
            auto &entry = s.maps[object];
            const auto manager = read<std::uintptr_t>(shared().base + player_ui::location_manager);
            require(manager_matches(manager), "Native player-location manager is unavailable.");
            if (entry.manager != manager)
                entry = {};
            // Same map projection inputs the native map tick uses for native players.
            const auto size = read<std::array<float, 2>>(object + 0x5320);
            const auto origin = read<std::array<float, 3>>(object + 0x5310);
            require(std::isfinite(size[0]) && std::isfinite(size[1]) && size[0] > 0 && size[1] > 0 &&
                        size[0] < 100000 && size[1] < 100000 && std::isfinite(origin[0]) &&
                        std::isfinite(origin[1]) && std::isfinite(origin[2]) && std::abs(origin[1]) > .01f &&
                        std::abs(origin[1] - position[1]) > .01f,
                    "Waiting for native map projection.");
            alignas(16) const std::array<float, 4> world{position[0], position[1], position[2], 0};
            Location location;
            reinterpret_cast<void *(*)(void *, const void *, const void *, const void *)>(
                shared().base + player_ui::map_project)(location.position.data(), world.data(), size.data(), origin.data());
            require(std::isfinite(location.position[0]) && std::isfinite(location.position[1]) &&
                        std::abs(location.position[0]) < 10000000 &&
                        std::abs(location.position[1]) < 10000000,
                    "Native player map projection is invalid.");
            if (!entry.handle) {
                const std::uint64_t key = 0x5265536b6174654dULL + peer_slot;
                const auto scope = read<std::uint8_t>(object + 0x5370);
                entry.manager = manager;
                entry.handle = reinterpret_cast<std::uint16_t (*)(std::uintptr_t, const void *, const void *,
                                                                  std::uint8_t)>(shared().base + player_ui::location_create)(
                    manager, &key, &location, scope);
                if (entry.handle < 2) {
                    entry = {};
                    throw std::runtime_error("Native player map allocation failed.");
                }
            } else {
                reinterpret_cast<void (*)(std::uintptr_t, std::uint16_t, const void *)>(
                    shared().base + player_ui::location_update)(manager, entry.handle, &location);
            }
            selected = &entry;
        }
    } catch (const std::exception &e) {
        status(e.what());
    }
    return selected;
}
void tick_hook(std::uintptr_t interface_address) {
    if (!shared().installed.load(std::memory_order_acquire)) {
        shared().map_tick(interface_address);
        return;
    }
    std::lock_guard lock(shared().maps_mutex);
    const auto previous = current_maps;
    // The display's type is the same for every slot: checked once per tick.
    std::uintptr_t display_type{}, interface_type{};
    const bool display = memory::peek(interface_address - 0x40, display_type) &&
                         memory::peek(interface_address, interface_type) &&
                         display_type == shared().base + player_ui::map_display_vtable &&
                         interface_type == shared().base + player_ui::map_display_interface_vtable;
    each_peer([&] { current_maps[peer_slot] = update_map(interface_address, display); });
    shared().map_tick(interface_address);
    current_maps = previous;
}
std::uintptr_t copy_hook(std::uintptr_t destination, std::uintptr_t source) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto result = shared().copy(destination, source);
    // This first-256-byte move helper is folded across many UI types. Only the
    // exact player-map call may touch the remaining, type-specific fields.
    if (caller != shared().base + player_ui::copy_return)
        return result;
    each_peer([&] {
        auto &s = state();
        if (!current_maps[peer_slot])
            return;
        try {
            auto &entry = *current_maps[peer_slot];
            if (!entry.handle || !manager_matches(entry.manager) || !(read<std::uint8_t>(source + 0x114) & 1))
                return;
            const auto array = read<std::uintptr_t>(source + 0x108);
            const auto count = read<std::uint32_t>(array - 4) & 0x7fffffff;
            require(count < 128, "Native player map list exceeds bound.");
            // One read of the whole list (4-byte entries, handle in the low half).
            std::array<std::uint32_t, 128> handles{};
            require(!count || memory::peek_bytes(array, handles.data(), count * sizeof(std::uint32_t)),
                    "Native player UI memory unavailable.");
            for (unsigned i = 0; i < count; ++i)
                if (static_cast<std::uint16_t>(handles[i]) == entry.handle)
                    return;
            auto *slot = reinterpret_cast<std::uint32_t *(*)(std::uintptr_t, std::uintptr_t)>(
                shared().base + player_ui::list_append)(source + 0x108, 0);
            require(slot != nullptr, "Native player map list allocation failed.");
            // The caller moves this owned array into its render message after the
            // common prefix. Native message destruction releases this added ref.
            reinterpret_cast<void (*)(std::uintptr_t, std::uint16_t)>(shared().base + player_ui::location_retain)(entry.manager,
                                                                                                entry.handle);
            *slot = entry.handle;
            ++s.map_updates;
            status("Native player map icon receiving positions.");
        } catch (const std::exception &e) {
            status(e.what());
        }
    });
    return result;
}
std::uintptr_t destroy_hook(std::uintptr_t object, unsigned flags) {
    {
        std::lock_guard lock(shared().maps_mutex);
        each_peer([&] {
            auto &s = state();
            const auto found = s.maps.find(object);
            if (found != s.maps.end()) {
                try {
                    release(found->second);
                } catch (...) {
                }
                s.maps.erase(found);
            }
        });
    }
    return shared().destroy(object, flags);
}
void shutdown_hook(std::uint8_t unregister) {
    {
        std::lock_guard lock(shared().maps_mutex);
        each_peer([&] {
            auto &s = state();
            for (auto &[object, entry] : s.maps) {
                try {
                    release(entry);
                } catch (...) {
                }
            }
            s.maps.clear();
        });
    }
    shared().shutdown(unregister);
}
void install(std::uintptr_t base) {
    require(!shared().attempted, "Native player UI installation requires a restart.");
    shared().attempted = true;
    shared().base = base;
    auto prefix = [base](std::uintptr_t rva, const auto &bytes) {
        std::array<std::uint8_t, 32> actual{};
        require(bytes.size() <= actual.size() &&
                    memory::read_bytes(base + rva, actual.data(), bytes.size()) &&
                    std::equal(bytes.begin(), bytes.end(), actual.begin()),
                "Native player UI function fingerprint differs.");
    };
    prefix(player_ui::map_tick, player_ui::map_tick_prefix);
    prefix(player_ui::ui_copy, player_ui::ui_copy_prefix);
    prefix(player_ui::map_destroy, player_ui::map_destroy_prefix);
    prefix(player_ui::location_shutdown, player_ui::location_shutdown_prefix);
    prefix(player_ui::location_create, player_ui::location_create_prefix);
    prefix(player_ui::location_update, player_ui::location_update_prefix);
    prefix(player_ui::location_release, player_ui::location_release_prefix);
    prefix(player_ui::location_retain, player_ui::location_retain_prefix);
    prefix(player_ui::list_append, player_ui::list_append_prefix);
    prefix(player_ui::map_project, player_ui::map_project_prefix);
    prefix(player_ui::copy_call, player_ui::copy_call_prefix);
    require(read<std::uintptr_t>(base + player_ui::map_display_interface_vtable + 8) == base + player_ui::map_tick &&
                read<std::uintptr_t>(base + player_ui::location_manager_vtable) == base + player_ui::location_retain &&
                read<std::uintptr_t>(base + player_ui::location_manager_vtable + 0x18) == base + player_ui::location_update,
            "Native player map method table differs.");
    struct Hook {
        std::uintptr_t rva;
        void *replacement;
        void **original;
    };
    const std::array hooks{
        Hook{player_ui::map_tick, reinterpret_cast<void *>(&tick_hook), reinterpret_cast<void **>(&shared().map_tick)},
        Hook{player_ui::ui_copy, reinterpret_cast<void *>(&copy_hook), reinterpret_cast<void **>(&shared().copy)},
        Hook{player_ui::map_destroy, reinterpret_cast<void *>(&destroy_hook), reinterpret_cast<void **>(&shared().destroy)},
        Hook{player_ui::location_shutdown, reinterpret_cast<void *>(&shutdown_hook),
             reinterpret_cast<void **>(&shared().shutdown)}};
    std::size_t prepared{};
    for (const auto &hook : hooks) {
        if (hook_prepare(reinterpret_cast<void *>(base + hook.rva), hook.replacement, hook.original) !=
            HookOk) {
            while (prepared)
                hook_remove(reinterpret_cast<void *>(base + hooks[--prepared].rva));
            throw std::runtime_error("Cannot prepare native player map hooks.");
        }
        ++prepared;
    }
    // Keep forwarding trampolines on uncertain enable results, with no map
    // mutations until every hook, including manager teardown, is active.
    // One transaction (one suspension of every game thread) for all of them.
    for (const auto &hook : hooks)
        require(hook_queue_enable(reinterpret_cast<void *>(base + hook.rva)) == HookOk,
                "Cannot enable native player map hooks; restart ReSkate.");
    require(hook_apply_queued() == HookOk, "Cannot enable native player map hooks; restart ReSkate.");
    shared().installed.store(true, std::memory_order_release);
}
} // namespace
void prepare_player_ui(std::uintptr_t base) noexcept {
    try {
        if (!shared().attempted) install(base);
    } catch (const std::exception &e) {
        status(e.what());
    }
}
void update_player_ui(std::uintptr_t base, const Pose *pose, std::string name) noexcept {
    update_party_position(pose);
    update_native_indicators(base, pose, name);
    auto &s = state();
    try {
        {
            std::lock_guard lock(s.snapshot_mutex);
            // The map consumes only root position. Full pose validation occurs
            // once when the packet enters PoseBuffer.
            s.visible = pose && valid_transform(pose->root);
            s.shown.store(s.visible, std::memory_order_release);
            s.sampled = GetTickCount64();
            s.name = std::move(name);
            if (s.visible)
                s.position = pose->root.position;
        }
        if (pose && !shared().attempted)
            install(base);
    } catch (const std::exception &e) {
        status(e.what());
    }
}
std::string player_ui_status() {
    const auto indicators = native_indicators_status();
    auto &s = state();
    std::lock_guard lock(s.snapshot_mutex);
    return indicators + "\n" + s.status + "\n" + native_party_status();
}
std::uint64_t player_map_updates() noexcept { return state().map_updates.load(); }
} // namespace dingosdk::multiplayer

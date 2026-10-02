#include "ui_pointer_skip.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Core/Profiling/profiler.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/ui_pointer.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <format>

namespace dingosdk::ui_pointer {
namespace {
namespace up = addr::ui_pointer;
using EnableCursor = void (*)(std::uintptr_t mouse, std::uintptr_t enable, std::uintptr_t style, std::uintptr_t reason);
using ForceCursor = void (*)(std::uintptr_t mouse, std::uintptr_t delta);
using Construct = std::uintptr_t (*)(std::uintptr_t mouse, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c);
using Pointer = void (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
Construct original_construct{};
EnableCursor original_enable{};
ForceCursor original_force{};
Pointer original_still{}, original_moved{};
// The game's InputManMouse: built at engine start (after ReSkate's hooks), and handed over again
// whenever the UI turns its cursor on or off.
std::atomic<std::uintptr_t> mouse{};
std::atomic<bool> skipping{true}, installed{};
std::atomic<std::uint64_t> skipped{}, tested{};

// True while a cursor is or may be on screen: asked for, applied, or forced on. Unknown counts
// as on, so nothing is skipped until the game has shown which mouse it uses.
bool cursor_in_use() noexcept {
    const auto m = mouse.load(std::memory_order_acquire);
    std::uint8_t applied{}, requested{};
    std::int32_t forced{};
    if (!m || !memory::peek(m + up::mouse_cursor_applied, applied) || !memory::peek(m + up::mouse_cursor_requested, requested) ||
        !memory::peek(m + up::mouse_force_count, forced)) return true;
    return applied || requested || forced > 0;
}

std::uintptr_t construct_hook(std::uintptr_t m, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c) {
    const auto result = original_construct(m, a, b, c);
    mouse.store(m, std::memory_order_release);
    return result;
}

void enable_cursor_hook(std::uintptr_t m, std::uintptr_t enable, std::uintptr_t style, std::uintptr_t reason) {
    const auto error = GetLastError();
    const auto previous = mouse.exchange(m, std::memory_order_acq_rel);
    std::uint8_t before{};
    (void)memory::peek(m + up::mouse_cursor_requested, before);
    SetLastError(error);
    original_enable(m, enable, style, reason);
    const auto after_error = GetLastError();
    const bool on = (enable & 0xff) != 0;
    if (previous != m || static_cast<bool>(before) != on)
        logging::log(logging::Level::info, logging::Channel::ui, "Game cursor {} (style {:#x}){}.", on ? "on" : "off",
            static_cast<std::uint32_t>(style), previous && previous != m ? "; the mouse object changed" : "");
    SetLastError(after_error);
}
void force_cursor_hook(std::uintptr_t m, std::uintptr_t delta) {
    mouse.store(m, std::memory_order_release);
    original_force(m, delta);
}
void pointer_still_hook(std::uintptr_t ui, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c) {
    if (skipping.load(std::memory_order_relaxed) && !cursor_in_use()) {
        skipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    tested.fetch_add(1, std::memory_order_relaxed);
    DINGO_PROFILE_ZONE("engine/UI pointer hit-test");
    original_still(ui, a, b, c);
}
void pointer_moved_hook(std::uintptr_t ui, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c) {
    if (skipping.load(std::memory_order_relaxed) && !cursor_in_use()) {
        skipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    tested.fetch_add(1, std::memory_order_relaxed);
    DINGO_PROFILE_ZONE("engine/UI pointer hit-test");
    original_moved(ui, a, b, c);
}

bool matches(std::uintptr_t base, const game::build::Fingerprint& contract) noexcept {
    std::array<unsigned char, 32> bytes{};
    return memory::peek(base + contract.rva, bytes) && bytes == contract.bytes;
}
}

bool install(std::uintptr_t base) noexcept {
    if (installed.load()) return true;
    if (!base || !matches(base, up::mouse_construct) || !matches(base, up::enable_cursor_mode) || !matches(base, up::force_cursor) ||
        !matches(base, up::pointer_still) || !matches(base, up::pointer_moved)) {
        logging::write(logging::Level::warning, logging::Channel::ui,
            "UI pointer skip: this build's pointer or cursor functions differ; the game hit-tests as before.");
        return false;
    }
    struct Hook { std::uintptr_t rva; void* detour; void** original; };
    const std::array hooks{
        Hook{up::mouse_construct.rva, reinterpret_cast<void*>(&construct_hook), reinterpret_cast<void**>(&original_construct)},
        Hook{up::enable_cursor_mode.rva, reinterpret_cast<void*>(&enable_cursor_hook), reinterpret_cast<void**>(&original_enable)},
        Hook{up::force_cursor.rva, reinterpret_cast<void*>(&force_cursor_hook), reinterpret_cast<void**>(&original_force)},
        Hook{up::pointer_still.rva, reinterpret_cast<void*>(&pointer_still_hook), reinterpret_cast<void**>(&original_still)},
        Hook{up::pointer_moved.rva, reinterpret_cast<void*>(&pointer_moved_hook), reinterpret_cast<void**>(&original_moved)}};
    std::size_t prepared{};
    for (const auto& hook : hooks) {
        auto* target = reinterpret_cast<void*>(base + hook.rva);
        if (hook_prepare(target, hook.detour, hook.original) != HookOk) break;
        hook_queue_enable(target);
        ++prepared;
    }
    if (prepared != hooks.size() || hook_apply_queued() != HookOk) {
        for (std::size_t i = 0; i < prepared; ++i) (void)hook_remove(reinterpret_cast<void*>(base + hooks[i].rva));
        logging::write(logging::Level::warning, logging::Channel::ui, "UI pointer skip: the hooks could not be installed.");
        return false;
    }
    installed.store(true);
    return true;
}

void set_skip(bool enabled) noexcept { skipping.store(enabled, std::memory_order_relaxed); }
bool skip() noexcept { return skipping.load(std::memory_order_relaxed); }

std::string status() {
    if (!installed.load()) return "UI pointer skip is not installed for this build.";
    const auto m = mouse.load();
    std::int32_t forced{};
    std::uint8_t deferred{}, requested{}, applied{};
    if (m) {
        (void)memory::peek(m + up::mouse_force_count, forced);
        (void)memory::peek(m + up::mouse_forced, deferred);
        (void)memory::peek(m + up::mouse_cursor_requested, requested);
        (void)memory::peek(m + up::mouse_cursor_applied, applied);
    }
    return std::format("UI pointer skip {}: game cursor {}; {} hit-tests skipped, {} run since launch "
        "(mouse force {}, deferral {}, asked {}, applied {}).",
        skipping.load() ? "on" : "off", !m ? "not seen yet (nothing skipped)" : cursor_in_use() ? "on" : "off",
        skipped.load(), tested.load(), forced, deferred, requested, applied);
}
}

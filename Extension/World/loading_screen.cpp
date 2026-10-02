#include "loading_screen.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/level_loading.h"
#include "Engine/Game/Build/20260929/loading_screen.h"
#include "Engine/Game/UI/loading_screen_selection.h"
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>

namespace dingosdk::loading_screen {
namespace {
using Address = std::uintptr_t;
using Select = Address (*)(Address, bool);
struct State {
    Address base{};
    Select original{};
    std::atomic<bool> active{};
    std::mutex mutex;
    Pending pending;
    std::vector<LiveScreen> live;
    // Infos built for live screens, by level. They copy an authored info, so
    // the engine sees the same object layout; they are never freed.
    std::deque<std::string> bundles;
    std::vector<std::pair<std::string, Address>> built;
};
State& state() { static auto* value = new State; return *value; }
template<class T> T read(Address at) {
    T result{};
    return memory::read(at, result) ? result : T{};
}
std::string name(Address at) {
    std::array<char, 512> bytes{};
    if (!at || !memory::read(at, bytes)) return {};
    const auto end = std::find(bytes.begin(), bytes.end(), '\0');
    return end == bytes.end() ? std::string{} : std::string(bytes.begin(), end);
}
Address checked_info(Address info, Address base) {
    info &= ~Address{4};
    return read<Address>(info + 8) == base + addr::loading_screen::info_type ? info : 0;
}
// RimeLoadScreenInfo: BundleName (char*) at +0x20, WidgetAssetGuid after it.
constexpr Address info_bundle = 0x20;
constexpr Address info_widget_default = 0x28;
constexpr std::size_t info_copy = 0x100;
// Where WidgetAssetGuid sits: found in an authored row whose widget guid is
// known (a mod enabled at launch reports its rows too), else the default.
Address widget_offset(State &s, Address overrides, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        const auto entry = overrides + i * 48;
        const auto info = checked_info(read<Address>(entry + 32), s.base);
        if (!info) continue;
        const auto level = name(read<Address>(entry + 24));
        for (const auto &live : s.live) {
            if (!same_map(live.level, level)) continue;
            std::array<std::byte, 0x60> bytes{};
            if (!memory::read(info, bytes)) continue;
            for (Address at = info_bundle + 8; at + 16 <= bytes.size(); at += 4)
                if (std::memcmp(bytes.data() + at, live.widget.data(), 16) == 0) return at;
        }
    }
    return info_widget_default;
}
// An info for a live screen: a copy of the config's first authored info with
// this map's bundle and widget.
Address live_info(State &s, const std::string &destination, Address overrides, unsigned count) {
    const LiveScreen *wanted{};
    for (const auto &live : s.live)
        if (same_map(live.level, destination)) wanted = &live;
    if (!wanted) return 0;
    for (const auto &[level, info] : s.built)
        if (same_map(level, destination)) return info;
    Address donor{};
    for (unsigned i = 0; i < count && !donor; ++i) donor = checked_info(read<Address>(overrides + i * 48 + 32), s.base);
    if (!donor) return 0;
    std::array<std::byte, info_copy> bytes{};
    if (!memory::read(donor, bytes)) return 0;
    auto *copy = static_cast<std::byte *>(VirtualAlloc(nullptr, info_copy, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!copy) return 0;
    std::memcpy(copy, bytes.data(), bytes.size());
    s.bundles.push_back(wanted->bundle);
    const auto bundle = reinterpret_cast<Address>(s.bundles.back().c_str());
    const auto widget = widget_offset(s, overrides, count);
    std::memcpy(copy + info_bundle, &bundle, 8);
    std::memcpy(copy + widget, wanted->widget.data(), 16);
    const auto info = reinterpret_cast<Address>(copy);
    s.built.emplace_back(wanted->level, info);
    logging::log(logging::Level::info, logging::Channel::level, "Loading screen for {} built in memory (widget guid at +{:#x})",
                 wanted->level, widget);
    return info;
}
Address select(Address screen, bool boot) {
    auto& s = state();
    const auto original = s.original(screen, boot);
    struct LastError { DWORD value{GetLastError()}; ~LastError() { SetLastError(value); } } last_error;
    try {
        std::string destination;
        { std::lock_guard lock(s.mutex); destination = s.pending.take(screen, boot, GetTickCount64()); }
        logging::log(logging::Level::info, logging::Channel::level, "Loading screen chosen (boot {}) for {}", boot,
                     destination.empty() ? std::string("the game's own choice") : destination);
        if (destination.empty() || read<Address>(screen) != s.base + addr::loading_screen::screen_vtable) return original;
        const auto config = read<Address>(screen + 0x40);
        if (read<Address>(config + 8) != s.base + addr::loading_screen::config_type) return original;
        const auto overrides = read<Address>(config + 0x20);
        const auto count = read<unsigned>(overrides - 4) & 0x7fffffff;
        if (!overrides || count > 128) return original;
        for (unsigned i = 0; i < count; ++i) {
            const auto entry = overrides + i * 48;
            if (!same_map(name(read<Address>(entry + 24)), destination)) continue;
            const auto custom = read<Address>(entry);
            unsigned custom_count{};
            if (custom && !memory::read(custom - 4, custom_count)) return original;
            custom_count &= 0x7fffffff;
            std::array<std::uint8_t, 15> conditions{};
            if (!memory::read(entry + 8, conditions) || !unconditional(conditions, custom_count)) return original;
            if (const auto info = checked_info(read<Address>(entry + 32), s.base)) return info;
            return original;
        }
        // A map enabled since launch has no row in the configuration the game read.
        {
            std::lock_guard lock(s.mutex);
            if (const auto info = live_info(s, destination, overrides, count)) return info;
        }
        // Custom maps have no authored override; don't reuse the previous map.
        if (const auto fallback = checked_info(read<Address>(config + 0x30), s.base)) return fallback;
    } catch (...) { }
    return original;
}
}
bool start(Address base) noexcept {
    try {
        auto& s = state();
        if (s.active) return s.base == base;
        const auto& contract = addr::level_loading::loading_screen_select_contract;
        std::array<unsigned char, 32> bytes{};
        if (!base || !memory::read(base + contract.rva, bytes) || bytes != contract.bytes) return false;
        s.base = base;
        auto* target = reinterpret_cast<void*>(base + contract.rva);
        void* original{};
        if (hook_prepare(target, reinterpret_cast<void*>(&select), &original) != HookOk) return false;
        s.original = reinterpret_cast<Select>(original);
        if (hook_enable(target) != HookOk) { hook_remove(target); return false; }
        s.active = true;
        return true;
    } catch (...) { return false; }
}
void prepare(Address client, std::string_view root, std::string_view detached) noexcept {
    auto& s = state();
    if (!s.active) return;
    try {
        std::lock_guard lock(s.mutex);
        s.pending.clear();
        if (read<Address>(client) != s.base + addr::engine::client_vtable) return;
        const auto owner = read<Address>(client + 0x558);
        const auto screen = read<Address>(owner + 0x20);
        if (read<Address>(screen) != s.base + addr::loading_screen::screen_vtable) return;
        s.pending = {screen, GetTickCount64() + 10000, std::string(detached.empty() ? root : detached)};
    } catch (...) { }
}
void set_live_screens(std::vector<LiveScreen> screens) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    s.live = std::move(screens);
    // A rebuilt info would name the old bundle string; keep the built ones only
    // while their level is still offered.
    std::erase_if(s.built, [&](const auto &built) {
        return std::ranges::none_of(s.live, [&](const LiveScreen &live) { return same_map(live.level, built.first); });
    });
}
void cancel() noexcept {
    try { auto& s = state(); std::lock_guard lock(s.mutex); s.pending.clear(); } catch (...) { }
}
}

#include "preset_lookup_guard.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/native_cosmetics.h"
#include <atomic>
#include <mutex>
#include <set>
#include <string>

namespace dingosdk::preset_lookup_guard {
namespace {
namespace native = addr::native_cosmetics;
using Lookup = void **(*)(void **out, const char *name, void *table, std::uint8_t flag, void *context);
std::atomic<Lookup> original{};

void **lookup(void **out, const char *name, void *table, std::uint8_t flag, void *context) {
    if (table) return original.load()(out, name, table, flag, context);
    // The callers take an empty result as "not found", as for an empty name.
    *out = nullptr;
    static std::mutex logged_mutex;
    static std::set<std::string> logged;
    std::string text;
    if (name) {
        char buffer[160]{};
        memory::read_bytes(reinterpret_cast<std::uintptr_t>(name), buffer, sizeof(buffer) - 1);
        text = buffer;
    }
    std::lock_guard lock(logged_mutex);
    if (logged.size() < 64 && logged.insert(text).second)
        logging::log(logging::Level::warning, logging::Channel::customization,
                     "A cosmetic preset could not be found ({}); it is left out instead of loading.",
                     text.empty() ? "unnamed" : text);
    return out;
}
} // namespace

bool start(std::uintptr_t base) noexcept {
    try {
        std::array<unsigned char, native::named_lookup_prefix.size()> bytes{};
        if (!base || !memory::read_bytes(base + native::named_lookup, bytes.data(), bytes.size()) ||
            bytes != native::named_lookup_prefix)
            return false;
        auto *target = reinterpret_cast<void *>(base + native::named_lookup);
        void *previous{};
        if (hook_prepare(target, reinterpret_cast<void *>(&lookup), &previous) != HookOk) return false;
        original = reinterpret_cast<Lookup>(previous);
        if (hook_enable(target) != HookOk) {
            hook_remove(target);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace dingosdk::preset_lookup_guard

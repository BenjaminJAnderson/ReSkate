#include "lua_startup.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/lua_startup.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>

namespace dingosdk::lua_startup {
namespace {
using Startup = void (*)(void*);
using Execute = bool (*)(void*, const char*, unsigned, void*);
std::atomic<Startup> original{};
Execute execute_native{};
void* (*get_context)(){};
void (*reload_settings)(void*){};
void** settings_manager{};
std::mutex install_mutex;
std::uintptr_t installed_base{};
std::array<Callback, 4> callbacks{};
std::atomic<std::size_t> callback_count{};

void startup(void* native) {
    original.load(std::memory_order_acquire)(native);
    bool changed = false;
    const Context context{get_context()};
    if (!context.native) {
        logging::write(logging::Level::error, logging::Channel::runtime, "Post-startup Lua context is unavailable.");
        return;
    }
    const auto count = callback_count.load(std::memory_order_acquire);
    for (std::size_t index = 0; index < count; ++index) {
        try { changed = callbacks[index](context) || changed; }
        catch (const std::exception& error) {
            logging::log(logging::Level::error, logging::Channel::runtime, "Lua startup callback failed: {}", error.what());
        }
    }
    if (changed && *settings_manager) reload_settings(*settings_manager);
}
}
bool Context::execute(std::string_view source) const {
    return native && source.size() <= std::numeric_limits<unsigned>::max() &&
        execute_native(native, source.data(), static_cast<unsigned>(source.size()), nullptr);
}
bool add_callback(std::uintptr_t base, Callback callback, std::string& error) {
    std::lock_guard lock(install_mutex);
    error.clear();
    const auto count = callback_count.load(std::memory_order_relaxed);
    if (!callback || count >= callbacks.size() || (installed_base && installed_base != base)) {
        error = "Cannot register the Lua startup callback"; return false;
    }
    for (std::size_t index = 0; index < count; ++index) if (callbacks[index] == callback) return true;
    if (installed_base) {
        callbacks[count] = callback;
        callback_count.store(count + 1, std::memory_order_release);
        return true;
    }
    // Shared with the explicit display override so only one Detours hook owns
    // native startup. These are the previously inspected September 2026 ABIs.
    namespace lua = addr::lua_startup;
    constexpr game::build::Fingerprint contracts[]{
        lua::startup_contract, lua::execute_contract, lua::get_context_contract, lua::reload_settings_contract};
    for (const auto& contract : contracts) {
        std::array<unsigned char, 32> actual{};
        SIZE_T read{};
        if (!base || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(base + contract.rva),
                actual.data(), actual.size(), &read) || read != actual.size() || actual != contract.bytes) {
            error = "Native Lua startup contract does not match the supported game"; return false;
        }
    }
    auto* target = reinterpret_cast<void*>(base + contracts[0].rva);
    execute_native = reinterpret_cast<Execute>(base + contracts[1].rva);
    get_context = reinterpret_cast<decltype(get_context)>(base + contracts[2].rva);
    reload_settings = reinterpret_cast<decltype(reload_settings)>(base + contracts[3].rva);
    settings_manager = reinterpret_cast<void**>(base + addr::engine::settings_manager);
    Startup trampoline{};
    auto status = hook_prepare(target, reinterpret_cast<void*>(&startup), reinterpret_cast<void**>(&trampoline));
    if (status != HookOk || !trampoline) {
        if (status == HookOk) hook_remove(target);
        error = "Cannot prepare Lua startup hook: " + std::to_string(status); return false;
    }
    original.store(trampoline, std::memory_order_release);
    callbacks[0] = callback;
    callback_count.store(1, std::memory_order_release);
    status = hook_enable(target);
    if (status != HookOk) {
        hook_remove(target); callback_count.store(0, std::memory_order_release);
        error = "Cannot enable Lua startup hook: " + std::to_string(status); return false;
    }
    installed_base = base;
    return true;
}
}

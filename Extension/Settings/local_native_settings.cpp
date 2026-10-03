#include "Engine/Core/Log/logging.h"
#include "Extension/Profile/runtime_internal.h"
#include "local_native_settings.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/local_native_settings.h"
#include "Engine/Game/Build/20260929/named_settings.h"
#include "local_user_settings.h"

namespace dingosdk::profile_runtime {
namespace native_types = addr::named_settings;
// Modern DingoUISettingAsset controls use DingoProfileSettingsManager's boxed

// option groups, independently of the legacy DelMar/ECS settings scripts.

NativeSettingsFunctions& native_settings() { static NativeSettingsFunctions f; return f; }

unsigned native_settings_location(std::uintptr_t group) {
    auto& s = local_runtime();
    if (!group || !s.active.load(std::memory_order_acquire)) return 0;
    // Asked on every settings get the game's scripts make: peeked, no system call per read.
    std::uintptr_t manager{}, begin{}, end{}, candidate{};
    if (!memory::peek(s.base + addr::local_native_settings::profile_settings_manager, manager) || !manager ||
        !memory::peek(manager, begin) || !memory::peek(manager + 8, end) ||
        !begin || end < begin || end - begin != 3 * sizeof(std::uintptr_t)) return 0;
    // LocalPerDevice (0) already has working native disk storage. Only the two
    // cloud groups need the offline provider; retain their separate key spaces.
    for (unsigned location : {1u, 2u})
        if (memory::peek(begin + location * 8, candidate) && candidate == group) return location;
    return 0;
}

std::optional<dingosdk::Json> native_setting_json(const NativeSettingValue* input) {
    NativeSettingValue value{};
    if (!read(reinterpret_cast<std::uintptr_t>(input), value) || !value.data) return {};
    const auto type = value.type - local_runtime().base;
    switch (type) {
    case native_types::native_bool: {
        std::uint8_t byte{};
        if (read(reinterpret_cast<std::uintptr_t>(value.data), byte) && byte <= 1) return byte != 0;
        return {};
    }
    case native_types::native_uint32: return native_scalar<std::uint32_t>(value.data);
    case native_types::native_int32: return native_scalar<std::int32_t>(value.data);
    case native_types::native_float32: return native_scalar<float>(value.data);
    case native_types::native_cstring: {
        std::uintptr_t text{}; std::string result;
        if (!read(reinterpret_cast<std::uintptr_t>(value.data), text)) return {};
        for (std::size_t i = 0; i <= 4096; ++i) {
            char c{}; if (!read(text + i, c)) return {};
            if (!c) return result;
            result += c;
        }
    }
    }
    return {};
}

bool restore_native_setting(std::uintptr_t group, const char* key, std::uintptr_t type,
    const dingosdk::Json& saved) {
    const auto base = local_runtime().base;
    switch (type - base) {
    case native_types::native_bool: return restore_native_scalar<bool>(group, key, type, saved);
    case native_types::native_uint32: return restore_native_scalar<std::uint32_t>(group, key, type, saved);
    case native_types::native_int32: return restore_native_scalar<std::int32_t>(group, key, type, saved);
    case native_types::native_float32: return restore_native_scalar<float>(group, key, type, saved);
    case native_types::native_cstring: {
        if (!saved.is_string()) return false;
        const auto& value = saved.string();
        // CString cloning needs engine-owned storage, not a std::string buffer.
        const char* text = reinterpret_cast<const char*>(base + addr::engine::empty_cstring);
        user_values().assign_string(&text, value.c_str(), static_cast<std::uint32_t>(value.size()));
        struct Release { const char** text; ~Release() { local_runtime().destroy_string(text); } } release{&text};
        const NativeSettingValue borrowed{type, &text};
        return native_settings().set(group, key, type, &borrowed);
    }
    }
    return false;
}

const NativeSettingValue* native_setting_get(std::uintptr_t group, const char* key) {
    auto* result = native_settings().get(group, key);
    PreserveError preserve;
    try {
        const auto location = native_settings_location(group); std::string id;
        if (!location || !identifier(&key, id)) return result;
        const auto saved = local_runtime().store->native_profile_option(location, id);
        if (!saved) return result;
        const auto current = native_setting_json(result);
        NativeSettingValue native{};
        if (current && *current != *saved && read(reinterpret_cast<std::uintptr_t>(result), native) &&
            restore_native_setting(group, key, native.type, *saved))
            dingosdk::logging::event(dingosdk::logging::Channel::settings, dingosdk::Json{{"event","local_native_setting_restored"},{"location",location},{"key",id}}.dump().c_str());
    } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::settings, "{\"event\":\"local_native_setting_restore_failed\"}"); }
    return result;
}

bool native_setting_set(std::uintptr_t group, const char* key, std::uintptr_t type, const NativeSettingValue* value) {
    const bool changed = native_settings().set(group, key, type, value);
    PreserveError preserve;
    try {
        const auto location = native_settings_location(group); std::string id;
        if (changed && location && identifier(&key, id)) {
            // Read back the accepted native value, including type validation.
            if (const auto saved = native_setting_json(native_settings().get(group, key))) {
                local_runtime().store->set_native_profile_option(location, id, *saved);
                dingosdk::logging::event(dingosdk::logging::Channel::settings, dingosdk::Json{{"event","local_native_setting_saved"},{"location",location},{"key",id}}.dump().c_str());
            }
        }
    } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::settings, "{\"event\":\"local_native_setting_save_failed\"}"); }
    return changed;
}
}
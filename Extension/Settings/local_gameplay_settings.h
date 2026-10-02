#pragma once
#include "Engine/Core/Log/logging.h"
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct GameplaySettingFunctions {
    bool (*get_bool)(std::uintptr_t, const void*){};
    float (*get_number)(std::uintptr_t, const void*){};
    std::int32_t (*get_integer)(std::uintptr_t, const void*){};
    void* (*get_string)(std::uintptr_t, void*, const void*){};
    void (*set_bool)(std::uintptr_t, const void*, bool){};
    // Hook the expression wrapper: the cache setter's VEX prefix at +7 is
    // unsupported by Detours hook service's trampoline decoder even after its first 5 bytes.
    void (*set_number)(const void*, float){};
    void (*set_integer)(std::uintptr_t, const void*, std::int32_t){};
    void (*set_string)(std::uintptr_t, const void*, const void*){};
};

GameplaySettingFunctions& gameplay_settings();

unsigned gameplay_expression_kind(std::uintptr_t vm);

bool gameplay_user_edit();

bool gameplay_setting_key(const void* reference, std::string& key);

template<class T> std::optional<T> saved_gameplay_setting(const void* reference) {
    auto& s = local_runtime();
    if (!s.active.load(std::memory_order_acquire)) return {};
    PreserveError preserve;
    try {
        std::string key;
        if (!gameplay_setting_key(reference, key)) return {};
        const auto value = s.store->user_value(key);
        if (!value) return {};
        if constexpr (std::is_same_v<T, bool>) { if (value->is_boolean()) return value->get<bool>(); }
        if constexpr (std::is_same_v<T, float>) {
            if (value->is_number_float()) {
                const auto number = value->get<double>();
                if (std::isfinite(number) && std::abs(number) <= (std::numeric_limits<float>::max)())
                    return static_cast<float>(number);
            }
        }
        if constexpr (std::is_same_v<T, std::int32_t>) {
            if (value->is_number_integer()) {
                const auto number = value->get<std::int64_t>();
                if (number >= INT32_MIN && number <= INT32_MAX) return static_cast<std::int32_t>(number);
            }
        }
        if constexpr (std::is_same_v<T, std::string>) { if (value->is_string()) return value->get<std::string>(); }
    } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::settings, "{\"event\":\"local_gameplay_setting_read_failed\"}"); }
    return {};
}

void save_gameplay_setting(const void* reference, const dingosdk::Json& value);

bool gameplay_get_bool(std::uintptr_t m, const void* a);

float gameplay_get_number(std::uintptr_t m, const void* a);

std::int32_t gameplay_get_integer(std::uintptr_t m, const void* a);

void* gameplay_get_string(std::uintptr_t m, void* out, const void* a);

void gameplay_set_bool(std::uintptr_t m, const void* a, bool v);

void gameplay_set_number(const void* a, float v);

void gameplay_set_integer(std::uintptr_t m, const void* a, std::int32_t v);

void gameplay_set_string(std::uintptr_t m, const void* a, const void* v);
}

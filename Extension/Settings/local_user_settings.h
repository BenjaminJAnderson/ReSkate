#pragma once
#include "Engine/Core/Log/logging.h"
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct UserValueFunctions {
    using Get = void* (*)(std::uintptr_t, void*, const void*, void*);
    Get get_bool{}, get_number{}, get_integer{}, get_string{};
    void (*set_bool)(std::uintptr_t, const void*, bool, const void*){};
    void (*set_number)(std::uintptr_t, const void*, double, const void*){};
    void (*set_integer)(std::uintptr_t, const void*, std::int64_t, const void*){};
    void (*set_text)(std::uintptr_t, const void*, const char*, const void*){};
    void (*set_string)(std::uintptr_t, const void*, const void*, const void*){};
    void (*success_callback)(std::uintptr_t, const void*, void*){};
    void (*error_callback)(std::uintptr_t, const void*, void*){};
    void (*assign_string)(void*, const char*, std::uint32_t){};
};

UserValueFunctions& user_values();

template<class T> void* user_value_get(std::uintptr_t service, void* error, const void* key, void* output,
    UserValueFunctions::Get original) {
    auto& s = local_runtime();
    if (s.active.load(std::memory_order_acquire)) {
        PreserveError preserve;
        try {
            std::string id;
            if (identifier(key, id)) if (const auto value = s.store->user_value(id)) {
                bool matches{};
                if constexpr (std::is_same_v<T, bool>) matches = value->is_boolean();
                if constexpr (std::is_same_v<T, double>) matches = value->is_number_float();
                if constexpr (std::is_same_v<T, std::int64_t>) matches = value->is_number_integer();
                if constexpr (std::is_same_v<T, std::string>) matches = value->is_string();
                if (matches) {
                    if constexpr (std::is_same_v<T, std::string>) {
                        const auto& text = value->string();
                        user_values().assign_string(output, text.c_str(), static_cast<std::uint32_t>(text.size()));
                    } else *static_cast<T*>(output) = value->get<T>();
                    return s.success(error);
                }
            }
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::settings, "{\"event\":\"local_user_value_read_failed\"}"); }
    }
    return original(service, error, key, output);
}

void* user_get_bool(std::uintptr_t s, void* e, const void* k, void* o);

void* user_get_number(std::uintptr_t s, void* e, const void* k, void* o);

void* user_get_integer(std::uintptr_t s, void* e, const void* k, void* o);

void* user_get_string(std::uintptr_t s, void* e, const void* k, void* o);

bool user_value_save(std::uintptr_t service, const void* key, const dingosdk::Json& value, const void* callback);

void user_set_bool(std::uintptr_t s, const void* k, bool v, const void* c);

void user_set_number(std::uintptr_t s, const void* k, double v, const void* c);

void user_set_integer(std::uintptr_t s, const void* k, std::int64_t v, const void* c);

bool user_text_value(std::uintptr_t s, const void* k, const char* text, const void* c);

void user_set_text(std::uintptr_t s, const void* k, const char* v, const void* c);

void user_set_string(std::uintptr_t s, const void* k, const void* v, const void* c);
}

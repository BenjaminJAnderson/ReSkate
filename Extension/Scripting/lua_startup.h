#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk::lua_startup {
struct Context {
    void* native{};
    bool execute(std::string_view source) const;
};
// Callbacks run after stock startup, in registration order, on the same thread.
// Return true if settings should be reapplied. Register during gated bootstrap.
using Callback = bool (*)(const Context&);
bool add_callback(std::uintptr_t base, Callback callback, std::string& error);
}

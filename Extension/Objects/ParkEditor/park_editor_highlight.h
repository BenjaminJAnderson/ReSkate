#pragma once
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/park_editor.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace dingosdk::editor {
// Native Build Kit color override scope (addr::park_editor::highlight_contracts).
// The destructor publishes the dirty component notification.
struct NativeHighlightScope {
    std::uintptr_t vtable{}, type{}, component{}, handle{}, realm{}, notifications{};
    std::uint8_t dirty{};
    std::array<std::uint8_t, 3> padding{};
    std::uint32_t flags{};
};
static_assert(sizeof(NativeHighlightScope) == 0x38 && offsetof(NativeHighlightScope, dirty) == 0x30);
struct HighlightValue {
    std::int32_t color{};
    std::uint8_t enabled{};
    bool operator==(const HighlightValue &) const = default;
};
struct NativeHighlightApi {
    void *(*query)(const void *, NativeHighlightScope *){};
    void (*finish)(NativeHighlightScope *){};
    std::uintptr_t (*settings)(std::uintptr_t, const void *){};
    bool ready{};
};
inline NativeHighlightApi highlight_api(std::uintptr_t base) {
    const auto &highlight_contracts = addr::park_editor::highlight_contracts;
    NativeHighlightApi api;
    api.ready =
        std::all_of(highlight_contracts.begin(), highlight_contracts.end(), [&](const auto &contract) {
            std::array<unsigned char, 32> bytes{};
            return memory::read_bytes(base + contract.rva, bytes.data(), bytes.size()) &&
                   bytes == contract.bytes;
        });
    if (api.ready) {
        api.query = reinterpret_cast<decltype(api.query)>(base + highlight_contracts[0].rva);
        api.finish = reinterpret_cast<decltype(api.finish)>(base + highlight_contracts[1].rva);
        api.settings = reinterpret_cast<decltype(api.settings)>(base + highlight_contracts[2].rva);
    }
    return api;
}
inline std::optional<HighlightValue> highlight_value(const NativeHighlightApi &api, const void *root,
                                                     const std::optional<HighlightValue> &value = {},
                                                     const std::optional<HighlightValue> &expected = {}) {
    if (!api.ready)
        return {};
    NativeHighlightScope scope{};
    api.query(root, &scope);
    struct Finish {
        const NativeHighlightApi &api;
        NativeHighlightScope &scope;
        ~Finish() {
            api.finish(&scope);
        }
    } finish{api, scope};
    HighlightValue current;
    if (!scope.type || !scope.component || !memory::read(scope.component + 12, current.color) ||
        !memory::read(scope.component + 16, current.enabled))
        return {};
    if (value && (!expected || current == *expected) && current != *value) {
        std::memcpy(reinterpret_cast<void *>(scope.component + 12), &value->color, sizeof(value->color));
        std::memcpy(reinterpret_cast<void *>(scope.component + 16), &value->enabled, sizeof(value->enabled));
        scope.dirty = 1;
    }
    return current;
}
} // namespace dingosdk::editor

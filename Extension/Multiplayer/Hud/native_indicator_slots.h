#pragma once
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/native_indicators.h"
#include <cstdint>
#include <stdexcept>

namespace dingosdk::multiplayer {
// The native property setters use different arguments for reflected values
// and classes. Scalars/structs pass their value storage. Classes pass the
// referenced asset itself, not the address of a pointer-sized temporary.
template <class Reader>
std::uintptr_t indicator_property_argument(Reader &&reader, std::uintptr_t base, std::uintptr_t slot,
                                           std::uintptr_t type) {
    auto get = [&]<class T>(std::uintptr_t address) {
        T value{};
        if (!reader(address, &value, sizeof(value)))
            throw std::runtime_error("Native indicator value memory unavailable.");
        return value;
    };
    if (!slot || !type || get.template operator()<std::uintptr_t>(slot + 8) != type ||
        !(get.template operator()<std::uint32_t>(slot + 0x24) & 0x40))
        throw std::runtime_error("Native indicator value is unavailable or has a different type.");
    const auto storage = get.template operator()<std::uintptr_t>(slot);
    const auto metadata = get.template operator()<std::uintptr_t>(type);
    if (!storage || !metadata)
        throw std::runtime_error("Native indicator value storage unavailable.");
    const auto kind = get.template operator()<std::uint16_t>(metadata + 4) & 0x3e0;
    if (kind != 0x60)
        return storage;
    // Restrict the decoded reference representation to the verified native
    // class getter: *(out) = *(storage) & ~4.
    if (get.template operator()<std::uintptr_t>(metadata + 0x88) != base + addr::native_indicators::class_reference_getter)
        throw std::runtime_error("Native indicator asset-reference representation differs.");
    return get.template operator()<std::uintptr_t>(storage) & ~std::uintptr_t { 4 };
}

// Native property connections use separate input (40-byte) and output
// (24-byte) tables. Output slots may alias the source parent's input slots.
// A widget must be created without a source connection before writing them.
template <class Reader>
std::uintptr_t indicator_property(Reader &&reader, std::uintptr_t parent, std::uintptr_t data,
                                  std::uint32_t hash, bool output) {
    auto get = [&]<class T>(std::uintptr_t address) {
        T value{};
        if (!reader(address, &value, sizeof(value)))
            throw std::runtime_error("Native indicator property memory unavailable.");
        return value;
    };
    if (!parent || parent > UINTPTR_MAX - 0x98)
        throw std::runtime_error("Native indicator parent unavailable.");
    // Delegating sublevels use their parent's property context.
    for (unsigned depth = 0; get.template operator()<std::uint32_t>(parent + 0x44) & 0x08000000;) {
        if (++depth > 4)
            throw std::runtime_error("Native indicator parent chain exceeds bound.");
        parent = get.template operator()<std::uintptr_t>(parent + 0x10);
        if (!parent || parent > UINTPTR_MAX - 0x98)
            throw std::runtime_error("Native indicator parent chain is invalid.");
    }
    const auto metadata = get.template operator()<std::uintptr_t>(parent + 0x78);
    const auto slots = get.template operator()<std::uintptr_t>(parent + 0x58);
    if (!metadata || !slots || metadata > UINTPTR_MAX - 0x90)
        throw std::runtime_error("Native indicator property table unavailable.");
    const auto table = metadata + (output ? 0x80 : 0);
    const auto begin = get.template operator()<std::uintptr_t>(table);
    const auto end = get.template operator()<std::uintptr_t>(table + 8);
    const unsigned stride = output ? 24 : 40;
    if (end < begin || (end - begin) % stride || (end - begin) / stride > 8192)
        throw std::runtime_error("Native indicator property table exceeds bound.");
    for (auto record = begin; record != end; record += stride) {
        if (get.template operator()<std::uintptr_t>(record) != data ||
            get.template operator()<std::uint32_t>(record + 8) != hash)
            continue;
        const auto index =
            output ? (record - begin) / stride : get.template operator()<std::uint32_t>(record + 36);
        if (index >= 8192 || slots > UINTPTR_MAX - index * 8)
            throw std::runtime_error("Native indicator property index exceeds bound.");
        return get.template operator()<std::uintptr_t>(slots + index * 8) & ~std::uintptr_t { 3 };
    }
    return 0;
}
} // namespace dingosdk::multiplayer

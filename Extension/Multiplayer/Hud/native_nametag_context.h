#pragma once
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace dingosdk::multiplayer {
// PlayerNametag's getter expects the session-marker context (fc296756),
// not UIPlayerInfo (5c53e5e2). Its inherited field 0 / flattened field 2 is
// a Uint64 handle to UIPlayerInfo. All subsequent fields are authored UI state.
template <class Reader>
std::array<std::byte, 0x5a0> nametag_context_copy(Reader &&reader, std::uintptr_t base, std::uintptr_t type,
                                                  std::uintptr_t value, std::uint64_t player_info) {
    auto get = [&]<class T>(std::uintptr_t address) {
        T result{};
        if (!reader(address, &result, sizeof(result)))
            throw std::runtime_error("Native nametag context memory unavailable.");
        return result;
    };
    auto valid = [](bool condition) {
        if (!condition)
            throw std::runtime_error("Native nametag context schema differs.");
    };
    valid(type && value && player_info);
    const auto meta = get.template operator()<std::uintptr_t>(type);
    valid(get.template operator()<std::uint32_t>(meta) == 0xfc296756 &&
          get.template operator()<std::uint16_t>(meta + 6) == 0x5a0 &&
          get.template operator()<std::uint16_t>(meta + 0x2a) == 5);
    const auto fields = get.template operator()<std::uintptr_t>(meta + 0x60);
    valid(get.template operator()<std::uint32_t>(fields) == 0xbd35f1ed &&
          get.template operator()<std::uint16_t>(fields + 8) == 0);
    const auto inherited = get.template operator()<std::uintptr_t>(fields + 16);
    const auto inherited_meta = get.template operator()<std::uintptr_t>(inherited);
    valid(get.template operator()<std::uint32_t>(inherited_meta) == 0xf78df8b4 &&
          get.template operator()<std::uint16_t>(inherited_meta + 6) == 0x570 &&
          get.template operator()<std::uint16_t>(inherited_meta + 0x2a) == 11);
    const auto inherited_fields = get.template operator()<std::uintptr_t>(inherited_meta + 0x60);
    valid(get.template operator()<std::uint32_t>(inherited_fields) == 0x81d5aa57 &&
          get.template operator()<std::uint16_t>(inherited_fields + 8) == 0 &&
          get.template operator()<std::uintptr_t>(inherited_fields + 16) == base + addr::engine::uint64_type);
    std::array<std::byte, 0x5a0> result;
    if (!reader(value, result.data(), result.size()))
        throw std::runtime_error("Native nametag context value unavailable.");
    std::memcpy(result.data(), &player_info, sizeof(player_info));
    // Borrowed strings/assets/references are synchronously copied/retained by
    // native model publication while the caller holds ModelWriteLock.
    return result;
}
} // namespace dingosdk::multiplayer

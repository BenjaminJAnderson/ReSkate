#pragma once
#include "Engine/Game/Multiplayer/session_limits.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace dingosdk::multiplayer {
inline constexpr std::size_t max_players = multiplayer_player_limit;
inline constexpr std::size_t max_remote_players = max_players - 1;
inline constexpr std::uint64_t network_tick_us = 50000; // 20 updates per second.

// Native adapters share stable slot identities, never native objects or buffers.
// Client calls select their peer here; asynchronous hooks must select by their
// actual entity/component/provider before touching slot state. Scopes restore
// the previous selection across nested native callbacks and are thread-local.
inline thread_local std::size_t peer_slot = 0;
class PeerScope {
    std::size_t previous_ = peer_slot;

  public:
    explicit PeerScope(std::size_t slot) noexcept {
        assert(slot < max_remote_players);
        peer_slot = slot;
    }
    ~PeerScope() { peer_slot = previous_; }
    PeerScope(const PeerScope &) = delete;
    PeerScope &operator=(const PeerScope &) = delete;
};
template <class T> struct PeerStorage {
    std::array<T, max_remote_players> slots{};
    T &current() noexcept { return slots[peer_slot]; }
};
template <class F> void each_peer(F &&fn) {
    for (std::size_t i = 0; i < max_remote_players; ++i) {
        const PeerScope scope(i);
        fn();
    }
}
} // namespace dingosdk::multiplayer

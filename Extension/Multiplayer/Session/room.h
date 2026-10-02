#pragma once
#include "Extension/Multiplayer/Net/protocol.h"
#include <algorithm>
#include <stdexcept>

namespace dingosdk::multiplayer {
using MemberSlots = std::array<Member, max_remote_players>;

// Preserve slot identity for surviving players, including across joins/leaves.
// Callers retire native resources whenever a slot's id or epoch changes.
inline MemberSlots roster_slots(const MemberSlots &previous, std::span<const Member> members,
                                std::uint64_t local) {
    MemberSlots next{};
    for (std::size_t i = 0; i < previous.size(); ++i)
        for (const auto &m : members)
            if (m.id != local && m.id == previous[i].id)
                next[i] = m;
    for (const auto &m : members) {
        if (m.id == local ||
            std::any_of(next.begin(), next.end(), [&](const auto &v) { return v.id == m.id; }))
            continue;
        const auto free = std::find_if(next.begin(), next.end(), [](const auto &v) { return !v.id; });
        if (free == next.end())
            throw std::runtime_error("Session roster exceeds capacity.");
        *free = m;
    }
    return next;
}
inline bool trusted_roster(const Packet &p, std::uint64_t host, std::uint64_t host_epoch, std::uint64_t local,
                           std::uint64_t local_epoch) noexcept {
    if (p.kind != PacketKind::roster || p.source != host || p.epoch != host_epoch ||
        !valid_roster(p.members, p.capacity) || !p.distances.valid() || !valid_multiplayer_tps(p.tps))
        return false;
    bool found_host{}, found_local{};
    for (const auto &m : p.members) {
        if (m.id == host)
            found_host = m.epoch == host_epoch;
        if (m.id == local)
            found_local = m.epoch == local_epoch;
    }
    return found_host && found_local;
}
inline bool routed_source(const Packet &p, const Member &member, std::uint64_t connection, bool hosting,
                          std::uint64_t host, bool direct = false) noexcept {
    // Only the host may forward another Steam identity. A guest never chooses
    // its authority by putting a different sender ID in a packet.
    return member.id && member.epoch && p.source == member.id && p.epoch == member.epoch &&
           (hosting || direct ? connection == member.id : connection == host);
}
// A route report is a short lease: missing reports or a source reconnect restore relay.
inline bool needs_relay(std::span<const Member> routes, std::uint64_t reported, const Member &source,
                        std::uint64_t now) noexcept {
    return !reported || now < reported || now - reported > 1500000 ||
           std::none_of(routes.begin(), routes.end(),
                        [&](const auto &m) { return m.id == source.id && m.epoch == source.epoch; });
}
inline bool dial_peer(std::uint64_t local, std::uint64_t remote, std::uint64_t host) noexcept {
    return local && remote && remote != host && local != host && local < remote;
}
// Recent world positions only; uncertainty returns to full rate. Separate
// enter/exit thresholds avoid oscillating at a distance boundary.
inline std::uint32_t pose_interval(float distance_squared, std::uint32_t previous,
                                   const MultiplayerDistances &settings = {}, unsigned tps = 20) noexcept {
    const auto full = multiplayer_pose_interval(tps);
    if (!std::isfinite(distance_squared) || distance_squared < 0) return full;
    if (!settings.valid()) return full;
    const auto beyond = [&](int metres) { return distance_squared > static_cast<float>(metres * metres); };
    if (!beyond(settings.full_rate_return)) return full;
    if (beyond(settings.low_rate_start)) return 200000;
    if (previous == 200000 && beyond(settings.half_rate_return)) return 200000;
    if (beyond(settings.half_rate_start) || previous == 100000 || previous == 200000) return 100000;
    return full;
}
struct PoseDelivery {
    std::uint64_t source{}, epoch{}, last_sent{};
    std::uint32_t interval_us = 50000;
    std::uint64_t next_source_time{};
};
// Preserve the timer phase across variable client frames/packet arrivals. Only
// the newest state is sent; elapsed slots are skipped without catch-up packets.
inline void advance_pose_deadline(std::uint64_t &next, std::uint64_t now, std::uint64_t interval) {
    next = now + interval - (next && now >= next ? (now - next) % interval : 0);
}
// Reduce optional direct uploads when a guest cannot sustain fan-out. The host
// remains its first destination, and other receivers restore relay through leases.
struct DirectUploadBudget {
    unsigned limit = max_remote_players - 1;
    std::uint64_t next_check{}, recover_at{}, skipped{};
    void update(std::uint64_t now, std::uint64_t queue_us, std::uint64_t skipped_total, unsigned active) {
        if (now < next_check)
            return;
        next_check = now + 1000000;
        const auto new_skips = skipped_total >= skipped ? skipped_total - skipped : 0;
        skipped = skipped_total;
        if (active && (queue_us > 50000 || new_skips > 5)) {
            limit = std::min(limit, active);
            limit /= 2;
            recover_at = now + 10000000;
        } else if (now >= recover_at && queue_us < 10000 && !new_skips) {
            limit = std::min<unsigned>(max_remote_players - 1, limit + 1);
            recover_at = now + 5000000;
        }
    }
};
struct ReceiveBudget {
    std::uint64_t since{}, bytes{}, packets{};
    bool accept(std::uint64_t now, std::size_t size, unsigned sources = 1) {
        if (now - since >= 1000000) {
            since = now;
            bytes = packets = 0;
        }
        bytes += size;
        ++packets;
        return sources && sources <= max_remote_players && bytes <= 2ULL * 1024 * 1024 * sources &&
               packets <= (2ULL * multiplayer_tick_rates.back() + 80 + 32) * sources;
    }
};
} // namespace dingosdk::multiplayer

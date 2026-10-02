#include "session_internal.h"
#include "Extension/Multiplayer/Hud/native_party.h"
#include <algorithm>

// Parties. A listen host's lobby is always one party, led by the host; on a dedicated server
// the server owns the parties (Server/server_party.cpp) and every roster says who is in which.
// The native party UI, map markers and coop challenges read them from here.
namespace dingosdk::multiplayer::session_detail {
namespace {
// An invite the server passed on lapses there after a minute; drop ours a little later.
constexpr std::uint64_t invite_lifetime_us = 65000000;
} // namespace

std::uint32_t party_of(const Session &s, std::uint64_t id) {
    if (!id) return 0;
    if (id == s.transport.status().local_id) return s.local_party;
    for (const auto &peer : active_peers(s))
        if (peer.handshaken && peer.member.id == id) return peer.member.party;
    return 0;
}
bool party_member(const Session &s, std::uint64_t id) {
    return s.local_party && id != s.transport.status().local_id && party_of(s, id) == s.local_party;
}
std::uint64_t party_leader(const Session &s) {
    if (!s.local_party) return 0;
    if (s.local_party_leader) return s.transport.status().local_id;
    for (const auto &peer : active_peers(s))
        if (peer.handshaken && peer.member.party == s.local_party && peer.member.party_leader) return peer.member.id;
    return 0;
}
void set_local_party(Session &s, const Member &local) {
    const bool joined = local.party && local.party != s.local_party;
    if (local.party != s.local_party || local.party_leader != s.local_party_leader || local.party_open != s.local_party_open)
        ++s.party_revision;
    s.local_party = local.party;
    s.local_party_leader = local.party_leader;
    s.local_party_open = local.party_open;
    // Invites into the party we are now in are moot.
    if (joined)
        std::erase_if(s.party_invites, [&](const auto &invite) { return party_of(s, invite.from) == local.party; });
}
void receive_party(Session &s, const Packet &p, std::uint64_t now) {
    const auto from = p.party_player;
    auto *inviter = find_peer(s, from);
    const auto name = inviter && !inviter->member.name.empty() ? inviter->member.name : s.transport.name(from);
    if (p.party_action == PartyAction::invited) {
        std::erase_if(s.party_invites, [&](const auto &invite) { return invite.from == from; });
        if (s.party_invites.size() >= 8) s.party_invites.erase(s.party_invites.begin());
        s.party_invites.push_back({from, now});
        ++s.party_revision;
        // The game's own invite toast (Accept / Decline) when it can show one; chat either way.
        const bool toast = post_native_party_invite(from);
        add_chat(s, 0, "ReSkate", name + " invited you to their party. " +
                 (toast ? "Accept it from the notification, the Multiplayer menu or /party accept."
                        : "Accept it in the Multiplayer menu or type /party accept."));
    } else if (p.party_action == PartyAction::withdrawn) {
        const auto before = s.party_invites.size();
        std::erase_if(s.party_invites, [&](const auto &invite) { return invite.from == from; });
        if (s.party_invites.size() != before) ++s.party_revision;
    }
    publish(s);
}
void expire_party_invites(Session &s, std::uint64_t now) {
    const auto before = s.party_invites.size();
    std::erase_if(s.party_invites, [&](const auto &invite) {
        return now < invite.received || now - invite.received > invite_lifetime_us || !find_peer(s, invite.from);
    });
    if (s.party_invites.size() != before) ++s.party_revision;
}
std::string send_party_request(Session &s, PartyAction action, std::uint64_t player) {
    if (!dedicated_host(s)) return "Everyone in a lobby is in one party with the host.";
    if (!valid_party_request(action, player) || action == PartyAction::invited || action == PartyAction::withdrawn)
        return "That party request isn't valid.";
    if (player && !find_peer(s, player)) return "That player is not in the session.";
    const auto now = now_us();
    auto request = packet(s, PacketKind::party, now);
    request.party_action = action;
    request.party_player = player;
    if (!send_packet(s, s.host_id, request, true, false)) return "Could not reach the server.";
    return {};
}
} // namespace dingosdk::multiplayer::session_detail

#pragma once
#include <cstdint>
#include <map>
#include <vector>

namespace dingosdk::multiplayer {
// Parties on a dedicated server (a listen host puts everyone in one party instead). The
// server owns this book: players ask it to invite, accept, leave and so on (Packet::party),
// and every roster carries each player's party (Member::party / party_leader / party_open).
// Party numbers are the book's own, never 0 (0 = not in a party). A party always has at
// least two members: one left alone is dissolved.
class PartyBook {
  public:
    // The game's Party panel has eight member rows.
    static constexpr std::size_t default_limit = 8;
    static constexpr std::uint64_t invite_lifetime_us = 60000000; // an unanswered invite lapses after 60 s
    static constexpr std::size_t max_invites = 8;                 // pending invites one player can hold
    enum class Result {
        ok,
        self,         // aimed at oneself
        same_party,   // the target is already in the actor's party
        full,         // the party has no room
        not_leader,   // only the leader may do that
        not_member,   // the actor (or target) is not in a party / not in the actor's party
        no_invite,    // no pending invite from that player
        closed,       // join: the target's party is invite-only (the leader was asked instead)
        no_party,     // join: the target is not in a party
        busy,         // invite: the target holds too many invites
        renewed       // invite: already pending from this player; it lasts another lifetime, with no new notice
    };
    struct Party {
        std::uint64_t leader{};
        std::vector<std::uint64_t> members; // in join order, leader included
        bool open{};                        // anyone may join without an invite
    };
    struct Invite {
        std::uint64_t from{}, to{};
        std::uint64_t expires{};
    };
    explicit PartyBook(std::size_t limit = default_limit) : limit_(limit < 2 ? 2 : limit) {}

    std::uint32_t party_of(std::uint64_t player) const;
    const Party *party(std::uint32_t id) const;
    const std::map<std::uint32_t, Party> &parties() const { return parties_; }
    const std::vector<Invite> &invites() const { return invites_; }
    bool invited(std::uint64_t to, std::uint64_t from) const;
    std::size_t limit() const { return limit_; }
    void set_limit(std::size_t limit) { limit_ = limit < 2 ? 2 : limit; }
    // Bumped by every change to membership, leaders or openness (the roster must be resent).
    std::uint64_t revision() const { return revision_; }

    // `from` invites `to` into their party (a player not in a party invites into a new one).
    // Inviting again refreshes the invite.
    Result invite(std::uint64_t from, std::uint64_t to, std::uint64_t now);
    // `to` joins `from`'s party (leaving their own first), made now if `from` has none.
    Result accept(std::uint64_t to, std::uint64_t from, std::uint64_t now);
    Result decline(std::uint64_t to, std::uint64_t from);
    // `who` joins `target`'s party: allowed for an open party or a pending invite from it.
    Result join(std::uint64_t who, std::uint64_t target, std::uint64_t now);
    Result leave(std::uint64_t who);
    Result kick(std::uint64_t leader, std::uint64_t target);
    Result promote(std::uint64_t leader, std::uint64_t target);
    Result set_open(std::uint64_t leader, bool open);
    // A player left the server: out of their party, and every invite from or to them dropped.
    void remove(std::uint64_t player);
    // Drops lapsed invites; returns them so their holders can be told.
    std::vector<Invite> expire(std::uint64_t now);
    // The invites dropped since the last call because they can no longer be accepted
    // (the inviter left, their party filled up, the invitee joined it): their holders are told.
    std::vector<Invite> take_withdrawn();

  private:
    std::map<std::uint32_t, Party> parties_;
    std::map<std::uint64_t, std::uint32_t> member_of_;
    std::vector<Invite> invites_, withdrawn_;
    std::uint32_t next_{1};
    std::uint64_t revision_{1};
    std::size_t limit_;

    Party *find(std::uint32_t id);
    void add(std::uint32_t id, std::uint64_t player);
    // Takes a player out of their party, handing the lead on and dissolving a party left with one.
    void take_out(std::uint64_t player);
    void changed() { ++revision_; }
    // Drops the invites that can no longer be accepted, into withdrawn_.
    void prune_invites();
};
} // namespace dingosdk::multiplayer

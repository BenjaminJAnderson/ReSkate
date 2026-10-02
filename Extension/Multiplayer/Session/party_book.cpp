#include "party_book.h"
#include <algorithm>
#include <utility>

namespace dingosdk::multiplayer {
std::uint32_t PartyBook::party_of(std::uint64_t player) const {
    const auto found = member_of_.find(player);
    return found == member_of_.end() ? 0 : found->second;
}
const PartyBook::Party *PartyBook::party(std::uint32_t id) const {
    const auto found = parties_.find(id);
    return found == parties_.end() ? nullptr : &found->second;
}
PartyBook::Party *PartyBook::find(std::uint32_t id) {
    const auto found = parties_.find(id);
    return found == parties_.end() ? nullptr : &found->second;
}
bool PartyBook::invited(std::uint64_t to, std::uint64_t from) const {
    return std::any_of(invites_.begin(), invites_.end(), [&](const Invite &i) { return i.to == to && i.from == from; });
}
void PartyBook::add(std::uint32_t id, std::uint64_t player) {
    parties_[id].members.push_back(player);
    member_of_[player] = id;
}
void PartyBook::take_out(std::uint64_t player) {
    const auto id = party_of(player);
    auto *p = find(id);
    if (!p) return;
    member_of_.erase(player);
    std::erase(p->members, player);
    if (p->members.size() < 2) {
        for (const auto member : p->members) member_of_.erase(member);
        parties_.erase(id);
    } else if (p->leader == player) {
        p->leader = p->members.front(); // the longest-standing member leads next
    }
    changed();
}
void PartyBook::prune_invites() {
    const auto stale = [&](const Invite &i) {
        const auto party = party_of(i.from);
        const auto *p = this->party(party);
        // Accepting would do nothing (already together) or could not fit.
        return (party && party == party_of(i.to)) || (p && p->members.size() >= limit_);
    };
    for (const auto &i : invites_) if (stale(i)) withdrawn_.push_back(i);
    std::erase_if(invites_, stale);
}
PartyBook::Result PartyBook::invite(std::uint64_t from, std::uint64_t to, std::uint64_t now) {
    if (from == to) return Result::self;
    const auto party = party_of(from);
    if (party && party == party_of(to)) return Result::same_party;
    if (const auto *p = this->party(party); p && p->members.size() >= limit_) return Result::full;
    const auto found = std::find_if(invites_.begin(), invites_.end(), [&](const Invite &i) { return i.to == to && i.from == from; });
    if (found != invites_.end()) {
        found->expires = now + invite_lifetime_us;
        return Result::renewed;
    }
    if (std::count_if(invites_.begin(), invites_.end(), [&](const Invite &i) { return i.to == to; }) >=
        static_cast<std::ptrdiff_t>(max_invites))
        return Result::busy;
    invites_.push_back({from, to, now + invite_lifetime_us});
    return Result::ok;
}
PartyBook::Result PartyBook::accept(std::uint64_t to, std::uint64_t from, std::uint64_t now) {
    const auto found = std::find_if(invites_.begin(), invites_.end(), [&](const Invite &i) {
        return i.to == to && i.from == from && now < i.expires;
    });
    if (found == invites_.end()) return Result::no_invite;
    invites_.erase(found);
    auto party = party_of(from);
    if (party && party == party_of(to)) return Result::same_party;
    if (const auto *p = this->party(party); p && p->members.size() >= limit_) return Result::full;
    take_out(to);
    if (!party) {
        party = next_++;
        if (!next_) next_ = 1;
        parties_[party] = {from, {}, false};
        add(party, from);
    }
    add(party, to);
    changed();
    prune_invites();
    return Result::ok;
}
PartyBook::Result PartyBook::decline(std::uint64_t to, std::uint64_t from) {
    const auto before = invites_.size();
    std::erase_if(invites_, [&](const Invite &i) { return i.to == to && i.from == from; });
    return invites_.size() == before ? Result::no_invite : Result::ok;
}
PartyBook::Result PartyBook::join(std::uint64_t who, std::uint64_t target, std::uint64_t now) {
    if (who == target) return Result::self;
    const auto party = party_of(target);
    if (!party) return Result::no_party;
    if (party == party_of(who)) return Result::same_party;
    const auto *p = this->party(party);
    // An invite from anyone in that party lets the player in, as does an open party.
    const auto invite = std::find_if(invites_.begin(), invites_.end(), [&](const Invite &i) {
        return i.to == who && party_of(i.from) == party && now < i.expires;
    });
    if (invite != invites_.end()) return accept(who, invite->from, now);
    if (!p->open) return Result::closed;
    if (p->members.size() >= limit_) return Result::full;
    take_out(who);
    add(party, who);
    changed();
    prune_invites();
    return Result::ok;
}
PartyBook::Result PartyBook::leave(std::uint64_t who) {
    if (!party_of(who)) return Result::not_member;
    take_out(who);
    return Result::ok;
}
PartyBook::Result PartyBook::kick(std::uint64_t leader, std::uint64_t target) {
    const auto party = party_of(leader);
    const auto *p = this->party(party);
    if (!p) return Result::not_member;
    if (p->leader != leader) return Result::not_leader;
    if (target == leader) return Result::self;
    if (party_of(target) != party) return Result::not_member;
    take_out(target);
    return Result::ok;
}
PartyBook::Result PartyBook::promote(std::uint64_t leader, std::uint64_t target) {
    const auto party = party_of(leader);
    auto *p = find(party);
    if (!p) return Result::not_member;
    if (p->leader != leader) return Result::not_leader;
    if (target == leader) return Result::self;
    if (party_of(target) != party) return Result::not_member;
    p->leader = target;
    changed();
    return Result::ok;
}
PartyBook::Result PartyBook::set_open(std::uint64_t leader, bool open) {
    auto *p = find(party_of(leader));
    if (!p) return Result::not_member;
    if (p->leader != leader) return Result::not_leader;
    if (p->open != open) {
        p->open = open;
        changed();
    }
    return Result::ok;
}
void PartyBook::remove(std::uint64_t player) {
    take_out(player);
    std::erase_if(invites_, [&](const Invite &i) {
        if (i.to == player) return true;
        if (i.from != player) return false;
        withdrawn_.push_back(i);
        return true;
    });
}
std::vector<PartyBook::Invite> PartyBook::expire(std::uint64_t now) {
    std::vector<Invite> lapsed;
    for (const auto &i : invites_) if (now >= i.expires) lapsed.push_back(i);
    std::erase_if(invites_, [&](const Invite &i) { return now >= i.expires; });
    return lapsed;
}
std::vector<PartyBook::Invite> PartyBook::take_withdrawn() {
    prune_invites();
    return std::exchange(withdrawn_, {});
}
} // namespace dingosdk::multiplayer

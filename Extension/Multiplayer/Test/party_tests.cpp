#include "Extension/Multiplayer/Session/party_book.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace dingosdk::multiplayer;
namespace {
void check(bool ok, const std::string &message) {
    if (ok) return;
    std::cerr << message << '\n';
    std::exit(1);
}
using R = PartyBook::Result;
constexpr std::uint64_t a = 1, b = 2, c = 3, d = 4;

void invites() {
    PartyBook book;
    const auto start = book.revision();
    check(book.invite(a, a, 0) == R::self, "A player invited themself");
    check(book.invite(a, b, 0) == R::ok && book.invited(b, a), "An invite was not recorded");
    // Inviting again only renews it, so the server does not notify the player again.
    check(book.invite(a, b, 1) == R::renewed && book.invited(b, a), "A repeated invite was not a renewal");
    check(book.revision() == start && !book.party_of(a), "An invite alone made a party");
    check(book.accept(b, c, 0) == R::no_invite, "An invite nobody sent was accepted");
    check(book.accept(b, a, 1) == R::ok, "An invite could not be accepted");
    const auto party = book.party_of(a);
    check(party && book.party_of(b) == party && book.party(party)->leader == a && book.revision() != start,
          "Accepting did not make a party led by the inviter");
    check(!book.invited(b, a), "An accepted invite stayed pending");
    check(book.invite(a, b, 2) == R::same_party, "A member was invited into their own party");
    // Any member may invite; the invite lapses after its lifetime.
    check(book.invite(b, c, 10) == R::ok, "A member could not invite");
    const auto lapsed = book.expire(10 + PartyBook::invite_lifetime_us);
    check(lapsed.size() == 1 && lapsed[0].to == c && !book.invited(c, b), "An invite did not lapse");
    check(book.accept(c, b, 10 + PartyBook::invite_lifetime_us) == R::no_invite, "A lapsed invite was accepted");
    check(book.invite(b, c, 20) == R::ok && book.decline(c, b) == R::ok && !book.invited(c, b), "A declined invite stayed");
}

void leaving() {
    PartyBook book;
    book.invite(a, b, 0); book.accept(b, a, 0);
    book.invite(a, c, 0); book.accept(c, a, 0);
    const auto party = book.party_of(a);
    check(book.party(party)->members.size() == 3, "The party did not grow to three");
    check(book.kick(b, c) == R::not_leader, "A member who doesn't lead removed someone");
    check(book.promote(a, b) == R::ok && book.party(party)->leader == b, "The lead was not handed on");
    check(book.set_open(a, true) == R::not_leader && book.set_open(b, true) == R::ok && book.party(party)->open,
          "Only the leader may open the party");
    check(book.leave(b) == R::ok && book.party(party)->leader == a, "A leaving leader did not hand the lead on");
    check(book.party(party)->open, "The party closed when its leader left");
    check(book.kick(a, c) == R::ok && !book.party(party) && !book.party_of(a) && !book.party_of(c),
          "A party left with one player was not ended");
    check(book.leave(a) == R::not_member, "A player left a party they weren't in");
}

void joining() {
    PartyBook book(3);
    book.invite(a, b, 0); book.accept(b, a, 0);
    const auto party = book.party_of(a);
    check(book.join(c, d, 0) == R::no_party, "A player joined someone with no party");
    check(book.join(c, b, 0) == R::closed && !book.party_of(c), "A player joined an invite-only party");
    book.invite(b, c, 0);
    check(book.join(c, a, 1) == R::ok && book.party_of(c) == party, "An invite from a member did not let the player join");
    check(book.join(d, a, 1) == R::closed, "A player joined past invite-only");
    book.set_open(a, true);
    check(book.join(d, a, 1) == R::full, "A player joined a full party");
    check(book.invite(a, d, 1) == R::full, "A full party invited someone");
    // Switching parties: d starts their own with a stranger, then accepts an invite to a's.
    PartyBook other;
    other.invite(a, b, 0); other.accept(b, a, 0);
    other.invite(c, d, 0); other.accept(d, c, 0);
    other.invite(a, d, 0);
    check(other.accept(d, a, 0) == R::ok && other.party_of(d) == other.party_of(a) && !other.party_of(c),
          "Accepting did not move the player between parties");
    // An invite that can no longer fit is withdrawn so its holder can be told.
    PartyBook small(2);
    small.invite(a, b, 0);
    small.invite(a, c, 0);
    small.accept(b, a, 0);
    const auto withdrawn = small.take_withdrawn();
    check(withdrawn.size() == 1 && withdrawn[0].to == c && !small.invited(c, a), "A stale invite was not withdrawn");
}

void departures() {
    PartyBook book;
    book.invite(a, b, 0); book.accept(b, a, 0);
    book.invite(a, c, 0);
    book.remove(a);
    check(!book.party_of(b), "A departed leader's two-player party survived");
    const auto withdrawn = book.take_withdrawn();
    check(withdrawn.size() == 1 && withdrawn[0].to == c && withdrawn[0].from == a, "A departed inviter's invite stayed");
    for (std::uint64_t i = 10; i < 10 + PartyBook::max_invites; ++i) book.invite(i, d, 0);
    check(book.invite(99, d, 0) == R::busy, "A player held unlimited invites");
}
} // namespace

int main() {
    invites();
    leaving();
    joining();
    departures();
    std::cout << "Party book: invites, leaving, joining and departures passed.\n";
}

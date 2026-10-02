#include "Extension/Multiplayer/Steam/steam_friend_join.h"
#include <iostream>
#include <stdexcept>
using namespace dingosdk;
using namespace dingosdk::multiplayer;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        check(steam_join_target("+reskate_lobby 9001") == 9001, "ReSkate lobby target parses");
        for (const auto* value : {"", "+connect_lobby 9001", "+reskate_lobby ", "+reskate_lobby 0",
                "+reskate_lobby -1", "+reskate_lobby 9001 extra", "+reskate_lobby 9001\n",
                "+reskate_lobby 18446744073709551616", "9001"})
            check(!steam_join_target(value), "Unrelated or malformed Steam requests must be ignored");
        MultiplayerModel model;
        model.active = model.hosting = model.public_host = model.lobby_listed = model.local_ready = true;
        model.public_lobby = 9001; model.invite = "private-session-secret";
        const auto open = model;
        check(steam_join_presence(model) == "+reskate_lobby 9001", "Public host advertises a lobby ID without its session code");
        model.password_required = true;
        check(steam_join_presence(model).empty(), "Password-protected sessions have no one-click join");
        model = open; model.public_host = false;
        check(steam_join_presence(model).empty(), "Private hosts are never advertised");
        model = open; model.public_lobby = 0;
        check(steam_join_presence(model).empty(), "Direct private codes cannot leak into presence");
        model = open; model.players = model.capacity;
        check(steam_join_presence(model).empty(), "Full sessions stop advertising");
        model = open; model.local_ready = false;
        check(steam_join_presence(model).empty(), "Loading sessions stop advertising");
        model = open; model.lobby_listed = false;
        check(steam_join_presence(model).empty(), "Closed listings stop advertising");
        model = open; model.active = false;
        check(steam_join_presence(model).empty(), "Ended sessions stop advertising");
        model = open; model.hosting = model.public_host = model.lobby_listed = false; model.connected = true;
        check(steam_join_presence(model) == "+reskate_lobby 9001", "Connected guests can advertise their verified public lobby");
        model.connected = false;
        check(steam_join_presence(model).empty(), "Guests cannot advertise before admission");
        std::cout << "Steam friend join checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

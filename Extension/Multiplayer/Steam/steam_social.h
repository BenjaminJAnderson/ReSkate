#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dingosdk::multiplayer {
struct SteamSocialPlayer {
    std::uint64_t id{};
    std::string name;
    bool online{}, playing{};
    bool operator==(const SteamSocialPlayer &) const = default;
};
struct SteamSocialSnapshot {
    SteamSocialPlayer local;
    std::vector<SteamSocialPlayer> friends;
    std::uint64_t revision{};
};
// Read the game's existing Steam session. Never initializes Steam, changes
// presence, or sends friend/invite requests. Poll only on the client thread.
std::shared_ptr<const SteamSocialSnapshot> steam_social_snapshot();
}

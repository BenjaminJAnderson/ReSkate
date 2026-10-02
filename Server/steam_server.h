#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace dingosdk::server {
// What the server browser shows. Players behind a NAT often cannot query the
// server directly, so everything a browser row needs also rides in the game
// tags that Steam's master list returns (see server_tags).
struct Advertisement {
    std::string name, map;
    unsigned players{}, max_players{};
    bool password{}, listed{true};
    std::uint64_t secret{};
};
std::string server_tags(const Advertisement &);

// A Steam game server for Skate's app, signed in anonymously. Its SteamID is
// new each start, so invite codes last only while the server runs.
class SteamServer {
  public:
    ~SteamServer();
    // Loads steam_api64.dll from `folder`, next to the Steam client files it needs.
    bool start(const std::filesystem::path &folder, std::uint16_t port, std::uint16_t query_port, std::string &error);
    void run_callbacks();
    bool logged_on();
    std::uint64_t steam_id();
    std::string public_ip();
    void advertise(const Advertisement &);
    void stop();
    void *module() const { return module_; }

  private:
    void *module_{}, *server_{};
    bool started_{};
    std::string tags_;
};
} // namespace dingosdk::server

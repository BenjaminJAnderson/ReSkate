#pragma once
#include "Extension/Profile/local_profile.h"
#include <optional>
#include <string>
#include <string_view>

namespace dingosdk::news {
// Live Hub news: the SDK fetches news.json from the ReSkateCache repository at
// startup, so posts can change without a release. Offline, or if the fetch
// fails, the built-in feed (config/defaults/news.json) is used instead.
inline constexpr std::wstring_view live_news_url =
    L"https://raw.githubusercontent.com/Dingo-Shenanigans/ReSkateCache/main/config/defaults/news.json";
// Relative image paths in the live file resolve against this.
inline constexpr std::string_view live_news_images = "https://raw.githubusercontent.com/Dingo-Shenanigans/ReSkateCache/main/";
// The folder news.json lives in. A post without its own image uses the file
// named by its position there: 0.png for the first post, 1.png for the next.
inline constexpr std::string_view live_news_folder =
    "https://raw.githubusercontent.com/Dingo-Shenanigans/ReSkateCache/main/config/defaults/";
inline constexpr std::wstring_view live_news_folder_listing =
    L"https://api.github.com/repos/Dingo-Shenanigans/ReSkateCache/contents/config/defaults?ref=main";

// Called every tick: once the game's network stack is up, adds the Let's
// Encrypt roots to its TLS trust list so GitHub-hosted news images load.
void update_image_trust();
// Starts the background fetch once (skipped in offline mode).
void start_live_news();
// The live feed once it has arrived and passed validation.
std::optional<profile::NewsFeed> live_news_feed();
// True once the fetch has finished (either way) or waited long enough; until
// then the first Hub news delivery holds back so the live post shows at once.
bool live_news_settled();
// The feed the Hub shows: the live one, else the save's built-in one.
profile::NewsFeed current_news_feed(const profile::Snapshot& snapshot);
// An image field as the game loads it: cdn:/ links become the CDN's https URL,
// https URLs stay, and a plain path is taken from the ReSkateCache repository.
std::string news_image_url(std::string_view value);
} // namespace dingosdk::news

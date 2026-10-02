#include "live_news.h"
#include "Engine/Core/Json/json.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/launcher_support.h"
#include "Engine/Game/Build/20260929/protossl.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/World/location_travel.h"
#include "Engine/Vfs/https_download.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace dingosdk::news {
namespace {
constexpr std::uint64_t max_news_bytes = 256 * 1024, max_listing_bytes = 256 * 1024, settle_ms = 8000;
std::mutex feed_mutex;
std::optional<profile::NewsFeed> live_feed;
std::atomic<bool> started{}, finished{};
std::atomic<std::uint64_t> started_at{};

// The game's TLS (DirtySDK ProtoSSL) only trusts EA's own authorities and a
// few roots it adds itself (DigiCert, GlobalSign, Amazon), so it refuses
// images from GitHub, whose certificates come from Let's Encrypt. Adding the
// Let's Encrypt roots lets news posts use images from the ReSkateCache repo;
// certificate checks stay on. Written like the game's own: one line each.
constexpr std::string_view isrg_root_x1 =
    "-----BEGIN CERTIFICATE-----"
    "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"
    "-----END CERTIFICATE-----";
constexpr std::string_view isrg_root_x2 =
    "-----BEGIN CERTIFICATE-----"
    "MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw"
    "CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg"
    "R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00"
    "MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT"
    "ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw"
    "EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW"
    "+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9"
    "ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T"
    "AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI"
    "zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW"
    "tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1"
    "/q4AaOeMSQ+2b1tbFfLn"
    "-----END CERTIFICATE-----";

std::atomic<bool> images_trusted{};
} // namespace

void update_image_trust() {
    if (images_trusted) return;
    namespace ssl = addr::protossl;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!base || !*reinterpret_cast<const std::uintptr_t *>(base + ssl::dirty_allocator)) return;
    images_trusted = true;
    if (std::memcmp(reinterpret_cast<const void *>(base + ssl::set_ca_cert), ssl::set_ca_cert_prefix.data(),
                             ssl::set_ca_cert_prefix.size())) {
        logging::log(logging::Level::warning, logging::Channel::news,
                     "News images: this game build differs; GitHub images will show the placeholder.");
        return;
    }
    // verify=0: a new root cannot chain to the existing ones; connections
    // made with it are still fully verified.
    using SetCaCert = int (*)(const char *, int, unsigned char);
    const auto set_ca_cert = reinterpret_cast<SetCaCert>(base + ssl::set_ca_cert);
    int added{};
    for (const auto pem : {isrg_root_x1, isrg_root_x2}) {
        const auto result = set_ca_cert(pem.data(), static_cast<int>(pem.size()), 0);
        if (result > 0) added += result;
        else logging::log(logging::Level::warning, logging::Channel::news, "News images: root certificate rejected ({}).", result);
    }
    logging::log(logging::Level::info, logging::Channel::news, "News images: trusted {} Let's Encrypt root(s).", added);
}
namespace {

std::filesystem::path temporary_file(std::wstring_view name) {
    wchar_t folder[MAX_PATH + 1]{};
    const auto length = GetTempPathW(MAX_PATH, folder);
    auto path = std::filesystem::path(length ? std::wstring(folder, length) : L".") /
                (L"reskate-" + std::wstring(name) + L"-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    std::error_code error;
    std::filesystem::remove(path, error); // https::get needs a fresh destination
    return path;
}
// RESKATE_NEWS_FILE=<path> reads a local news file instead, to preview a
// change before publishing it.
std::filesystem::path preview_file() {
    std::wstring value(4096, L'\0');
    const auto length = GetEnvironmentVariableW(L"RESKATE_NEWS_FILE", value.data(), static_cast<DWORD>(value.size()));
    if (!length || length >= value.size()) return {};
    value.resize(length);
    return value;
}
std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
// The news folder's files by name, each with its git hash; empty when GitHub's
// API can't be reached (it allows 60 requests an hour per address).
std::optional<std::map<std::string, std::string>> news_folder_files() {
    const auto path = temporary_file(L"news-folder");
    std::optional<std::map<std::string, std::string>> files;
    try {
        const auto download = https::get(live_news_folder_listing, path, max_listing_bytes, 5, L"ReSkate-News/1");
        if (download.ok) {
            const auto listing = Json::parse(read_text(path));
            if (listing.is_array()) {
                files.emplace();
                for (const auto &entry : listing)
                    if (entry.is_object() && entry.contains("name") && entry.contains("sha") && entry.at("name").is_string() &&
                        entry.at("sha").is_string())
                        files->emplace(entry.at("name").get<std::string>(), entry.at("sha").get<std::string>());
            }
        } else {
            logging::log(logging::Level::info, logging::Channel::news,
                         "News image list unavailable (HTTP {}, error {}); assuming <post>.png.", download.http_status,
                         download.error);
        }
    } catch (const std::exception &e) {
        logging::log(logging::Level::info, logging::Channel::news, "News image list is unreadable ({}).", e.what());
        files.reset();
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    return files;
}
// Post N (from 0) without its own image uses N.png (or .jpg) from the news
// folder. The file's git hash goes on the URL so a replaced image isn't
// served from a cache as the old one.
void apply_numbered_images(profile::NewsFeed &feed) {
    if (std::none_of(feed.posts.begin(), feed.posts.end(),
                     [](const auto &post) { return post.small_image.empty() || post.large_image.empty(); }))
        return;
    const auto files = news_folder_files();
    for (std::size_t index = 0; index < feed.posts.size(); ++index) {
        auto &post = feed.posts[index];
        std::string url;
        if (!files) {
            url = std::string(live_news_folder) + std::to_string(index) + ".png";
        } else {
            for (const auto *extension : {".png", ".jpg", ".jpeg"}) {
                const auto name = std::to_string(index) + extension;
                if (const auto found = files->find(name); found != files->end()) {
                    url = std::string(live_news_folder) + name + "?v=" + found->second.substr(0, 12);
                    break;
                }
            }
        }
        if (!url.empty()) logging::log(logging::Level::info, logging::Channel::news, "News post {} image: {}", index + 1, url);
        if (post.small_image.empty()) post.small_image = url;
        if (post.large_image.empty()) post.large_image = url;
    }
}
void fetch() {
    const auto preview = preview_file();
    const auto path = preview.empty() ? temporary_file(L"news") : preview;
    try {
        const auto download = preview.empty() ? https::get(live_news_url, path, max_news_bytes, 10, L"ReSkate-News/1")
                                              : https::Download{std::filesystem::exists(path), 0, 0};
        if (!download.ok) {
            logging::log(logging::Level::info, logging::Channel::news,
                         "Live news unavailable (HTTP {}, error {}); showing the built-in news.", download.http_status,
                         download.error);
        } else {
            auto feed = profile::parse_news(Json::parse(read_text(path)));
            for (auto &post : feed.posts) {
                post.small_image = news_image_url(post.small_image);
                post.large_image = news_image_url(post.large_image);
            }
            apply_numbered_images(feed);
            logging::log(logging::Level::info, logging::Channel::news, "Live news: {} post{}.", feed.posts.size(),
                         feed.posts.size() == 1 ? "" : "s");
            std::lock_guard lock(feed_mutex);
            live_feed = std::move(feed);
        }
    } catch (const std::exception &e) {
        logging::log(logging::Level::warning, logging::Channel::news,
                     "Live news file is invalid ({}); showing the built-in news.", e.what());
    }
    std::error_code error;
    if (preview.empty()) std::filesystem::remove(path, error);
    finished = true;
}
} // namespace

std::string news_image_url(std::string_view value) {
    if (value.empty()) return {};
    if (value.starts_with("cdn:/")) return travel_artwork_url(value);
    if (value.starts_with("https://") || value.starts_with("http://")) return std::string(value);
    while (value.starts_with('/')) value.remove_prefix(1);
    return std::string(live_news_images) + std::string(value);
}
void start_live_news() {
    if (started.exchange(true)) return;
    started_at = GetTickCount64();
    if (launcher::offline_mode()) {
        finished = true;
        return;
    }
    std::thread(fetch).detach();
}
std::optional<profile::NewsFeed> live_news_feed() {
    std::lock_guard lock(feed_mutex);
    return live_feed;
}
bool live_news_settled() {
    return finished || !started || GetTickCount64() - started_at >= settle_ms;
}
// Offline, the news comes from the defaults built into this DLL
// (config/defaults/news.json) rather than the save: a save keeps the news
// section it was first given, so an edited offline post would never reach it.
profile::NewsFeed built_in_news(const profile::Snapshot &snapshot) {
    static const auto feed = []() -> std::optional<profile::NewsFeed> {
        try {
            const auto defaults = Json::parse(profile::embedded_defaults());
            if (defaults.contains("news")) return profile::parse_news(defaults.at("news"));
        } catch (const std::exception &e) {
            logging::log(logging::Level::warning, logging::Channel::news, "Built-in news is invalid ({}).", e.what());
        }
        return std::nullopt;
    }();
    return feed ? *feed : profile::news_feed(snapshot);
}
profile::NewsFeed current_news_feed(const profile::Snapshot &snapshot) {
    if (auto live = live_news_feed()) return std::move(*live);
    return built_in_news(snapshot);
}
} // namespace dingosdk::news

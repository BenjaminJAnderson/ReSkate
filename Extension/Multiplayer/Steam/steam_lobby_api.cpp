#include "steam_lobbies.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Core/Platform/launcher_support.h"
#include <Windows.h>
#include <filesystem>
#include <stdexcept>

namespace dingosdk::multiplayer {
namespace {
// Windows x64 Steamworks callback layouts (pack 8). No global callback pump or
// Steam initialization: the game owns both. Poll only our own API call handles.
struct Created {
    std::int32_t result;
    std::uint32_t padding;
    std::uint64_t lobby;
};
struct Entered {
    std::uint64_t lobby;
    std::uint32_t permissions;
    bool locked;
    std::uint8_t padding[3];
    std::uint32_t response;
};
static_assert(sizeof(Created) == 16 && offsetof(Created, lobby) == 8);
static_assert(sizeof(Entered) == 24 && offsetof(Entered, response) == 16);
template <class T> T symbol(HMODULE module, const char *name) {
    const auto result = GetProcAddress(module, name);
    if (!result)
        throw std::runtime_error(std::string("Missing Steam lobby export: ") + name);
#pragma warning(push)
#pragma warning(disable : 4191)
    return reinterpret_cast<T>(result);
#pragma warning(pop)
}
std::string text(const char *value, std::size_t limit) {
    if (!value)
        return {};
    std::size_t size{};
    while (size <= limit && value[size])
        ++size;
    return size <= limit ? std::string(value, size) : std::string{};
}
class NativeLobbyApi final : public LobbyApi {
    void *matchmaking_{}, *utils_{}, *friends_{};
    std::uint64_t (*create_)(void *, int, int){};
    std::uint64_t (*search_)(void *){};
    std::uint64_t (*join_)(void *, std::uint64_t){};
    void (*leave_)(void *, std::uint64_t){};
    bool (*completed_)(void *, std::uint64_t, bool *){};
    bool (*result_)(void *, std::uint64_t, void *, int, int, bool *){};
    std::uint64_t (*at_)(void *, int){};
    std::uint64_t (*owner_)(void *, std::uint64_t){};
    const char *(*get_)(void *, std::uint64_t, const char *){};
    bool (*set_)(void *, std::uint64_t, const char *, const char *){};
    bool (*joinable_)(void *, std::uint64_t, bool){};
    bool (*visibility_)(void *, std::uint64_t, int){};
    void (*filter_)(void *, const char *, const char *, int){};
    void (*distance_)(void *, int){};
    void (*limit_)(void *, int){};
    void (*slots_)(void *, int){};
    const char *(*name_)(void *){};

  public:
    void open() override {
        if (matchmaking_)
            return;
        if (launcher::offline_mode())
            throw std::runtime_error("Multiplayer is unavailable in offline mode. Start Steam and relaunch ReSkate.");
        const auto module = GetModuleHandleW(L"steam_api64.dll");
        if (!module)
            throw std::runtime_error("Steam is unavailable. Start ReSkate with Steam online.");
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size())
            throw std::runtime_error("Cannot verify the Steam DLL.");
        path.resize(length);
        launcher::validate_steam_api_file(std::filesystem::path(path));
        if (!symbol<int (*)()>(module, "SteamAPI_GetHSteamUser")())
            throw std::runtime_error("Steam is not initialized.");
        utils_ = symbol<void *(*)()>(module, "SteamAPI_SteamUtils_v010")();
        if (!utils_ ||
            symbol<std::uint32_t (*)(void *)>(module, "SteamAPI_ISteamUtils_GetAppID")(utils_) != 3354750)
            throw std::runtime_error("Steam app identity differs.");
#define BIND(member, suffix) member = symbol<decltype(member)>(module, "SteamAPI_ISteamMatchmaking_" suffix)
        BIND(create_, "CreateLobby");
        BIND(search_, "RequestLobbyList");
        BIND(join_, "JoinLobby");
        BIND(leave_, "LeaveLobby");
        BIND(at_, "GetLobbyByIndex");
        BIND(owner_, "GetLobbyOwner");
        BIND(get_, "GetLobbyData");
        BIND(set_, "SetLobbyData");
        BIND(joinable_, "SetLobbyJoinable");
        BIND(visibility_, "SetLobbyType");
        BIND(filter_, "AddRequestLobbyListStringFilter");
        BIND(distance_, "AddRequestLobbyListDistanceFilter");
        BIND(limit_, "AddRequestLobbyListResultCountFilter");
        BIND(slots_, "AddRequestLobbyListFilterSlotsAvailable");
#undef BIND
        completed_ = symbol<decltype(completed_)>(module, "SteamAPI_ISteamUtils_IsAPICallCompleted");
        result_ = symbol<decltype(result_)>(module, "SteamAPI_ISteamUtils_GetAPICallResult");
        friends_ = symbol<void *(*)()>(module, "SteamAPI_SteamFriends_v017")();
        name_ = symbol<decltype(name_)>(module, "SteamAPI_ISteamFriends_GetPersonaName");
        matchmaking_ = symbol<void *(*)()>(module, "SteamAPI_SteamMatchmaking_v009")();
        if (!matchmaking_)
            throw std::runtime_error("Steam matchmaking is unavailable.");
    }
    std::uint64_t create(unsigned capacity) override {
        return create_(matchmaking_, 0, static_cast<int>(capacity));
    } // Initially private.
    std::uint64_t search() override {
        filter_(matchmaking_, "rs_kind", "reskate-co-skate", 0);
        const auto protocol = std::to_string(protocol_version);
        filter_(matchmaking_, "rs_protocol", protocol.c_str(), 0);
        filter_(matchmaking_, "rs_build", supported_build::game_sha256.data(), 0);
        filter_(matchmaking_, "rs_open", "1", 0);
        distance_(matchmaking_, 3); // Worldwide; Steam still handles relay routing.
        slots_(matchmaking_, 1);
        limit_(matchmaking_, 50);
        return search_(matchmaking_);
    }
    std::uint64_t join(std::uint64_t lobby) override { return join_(matchmaking_, lobby); }
    std::optional<LobbyResult> poll(std::uint64_t call, LobbyCall kind) override {
        bool failed{};
        if (!completed_(utils_, call, &failed))
            return failed ? std::optional<LobbyResult>{LobbyResult{}} : std::nullopt;
        if (failed)
            return LobbyResult{};
        if (kind == LobbyCall::create) {
            Created result{};
            const bool ok = result_(utils_, call, &result, sizeof(result), 513, &failed);
            return LobbyResult{ok && !failed && result.result == 1, result.lobby, 0,
                               static_cast<std::uint32_t>(result.result)};
        }
        if (kind == LobbyCall::join) {
            Entered result{};
            const bool ok = result_(utils_, call, &result, sizeof(result), 504, &failed);
            return LobbyResult{ok && !failed && result.response == 1, result.lobby, 0, result.response};
        }
        std::uint32_t count{};
        const bool ok = result_(utils_, call, &count, sizeof(count), 510, &failed);
        return LobbyResult{ok && !failed, 0, count, 0};
    }
    void leave(std::uint64_t id) override { leave_(matchmaking_, id); }
    std::uint64_t at(int index) override { return at_(matchmaking_, index); }
    std::uint64_t owner(std::uint64_t id) override { return owner_(matchmaking_, id); }
    std::string data(std::uint64_t id, const char *key) override {
        return text(get_(matchmaking_, id, key), 1024);
    }
    bool data(std::uint64_t id, const char *key, const std::string &value) override {
        return set_(matchmaking_, id, key, value.c_str());
    }
    bool joinable(std::uint64_t id, bool value) override { return joinable_(matchmaking_, id, value); }
    bool visibility(std::uint64_t id, bool value) override {
        return visibility_(matchmaking_, id, value ? 2 : 0);
    }
    std::string name() override { return friends_ ? text(name_(friends_), 128) : std::string{}; }
};
} // namespace
std::unique_ptr<LobbyApi> make_steam_lobby_api() { return std::make_unique<NativeLobbyApi>(); }
} // namespace dingosdk::multiplayer

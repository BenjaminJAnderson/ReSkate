#include "offline_steam.h"

#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/launcher_support.h"

#include <array>
#include <atomic>
#include <cstring>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace dingosdk::offline_steam {
namespace {

// Interfaces Skate.exe requests by version string. Every method of every
// interface is a stub that returns 0 and logs its first call; the overrides
// below give the methods the game depends on real answers. Slot numbers come
// from the vtable offsets in the shipped steam_api64.dll flat exports.
enum Interface : int {
    user, friends, utils, matchmaking, matchmaking_servers, networking,
    networking_messages, networking_sockets, networking_utils, input,
    controller, parties, game_search, apps, user_stats, remote_storage, remote_play,
    unknown, interface_count
};
constexpr std::array<const char*, interface_count> interface_names{
    "SteamUser", "SteamFriends", "SteamUtils", "SteamMatchMaking", "SteamMatchMakingServers",
    "SteamNetworking", "SteamNetworkingMessages", "SteamNetworkingSockets",
    "SteamNetworkingUtils", "SteamInput", "SteamController", "SteamParties",
    "SteamMatchGameSearch", "SteamApps", "SteamUserStats", "SteamRemoteStorage",
    "SteamRemotePlay", "unknown"};
constexpr std::size_t slot_count = 128;
constexpr std::uint32_t app_id = 3354750;
constexpr char empty[] = "";

std::array<std::array<std::atomic<bool>, slot_count>, interface_count> called{};

void note(int interface, std::size_t slot) noexcept {
    if (called[interface][slot].exchange(true)) return;
    try {
        logging::log(logging::Level::info, logging::Channel::network,
            "Offline Steam: first call to {} slot {}", interface_names[interface], slot);
    } catch (...) {}
}

template<int I, std::size_t S>
std::uint64_t stub(void*) noexcept { note(I, S); return 0; }

template<int I, std::size_t... S>
std::array<void*, slot_count> make_table(std::index_sequence<S...>) {
#pragma warning(push)
#pragma warning(disable: 4191)
    return {reinterpret_cast<void*>(&stub<I, S>)...};
#pragma warning(pop)
}

struct Object { void** vtable; std::array<std::uint8_t, 248> padding{}; };

template<int I> std::array<void*, slot_count> table = make_table<I>(std::make_index_sequence<slot_count>{});
std::array<Object, interface_count> objects{};

// ---------------------------------------------------------------- callbacks

// Same declaration as Steamworks' CCallbackBase, so MSVC lays the vtable out
// exactly as the game's compiled callbacks expect.
class CallbackBase {
public:
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool io_failure, std::uint64_t call) = 0;
    virtual int GetCallbackSizeBytes() = 0;
    std::uint8_t flags;
    int id;
};

std::mutex callback_mutex;
std::vector<CallbackBase*> callbacks;
std::deque<std::pair<int, std::vector<std::uint8_t>>> pending;

template<class T> void post(int id, const T& payload) {
    std::vector<std::uint8_t> bytes(sizeof(T));
    std::memcpy(bytes.data(), &payload, sizeof(T));
    std::lock_guard lock(callback_mutex);
    pending.emplace_back(id, std::move(bytes));
}

void __cdecl register_callback(CallbackBase* callback, int id) {
    if (!callback) return;
    callback->flags |= 1;
    callback->id = id;
    std::lock_guard lock(callback_mutex);
    callbacks.push_back(callback);
}

void __cdecl unregister_callback(CallbackBase* callback) {
    if (!callback) return;
    callback->flags &= static_cast<std::uint8_t>(~1u);
    std::lock_guard lock(callback_mutex);
    std::erase(callbacks, callback);
}

void __cdecl register_call_result(void*, std::uint64_t) {}
void __cdecl unregister_call_result(void*, std::uint64_t) {}

void __cdecl run_callbacks() {
    for (;;) {
        std::pair<int, std::vector<std::uint8_t>> next;
        std::vector<CallbackBase*> targets;
        {
            std::lock_guard lock(callback_mutex);
            if (pending.empty()) return;
            next = std::move(pending.front());
            pending.pop_front();
            for (auto* callback : callbacks) if (callback->id == next.first) targets.push_back(callback);
        }
        for (auto* callback : targets) callback->Run(next.second.data());
    }
}

// ---------------------------------------------------------------- overrides

using Uint64Out = std::uint64_t*;

// Methods returning CSteamID use MSVC's hidden return pointer (after this).
Uint64Out zero_steam_id(void*, Uint64Out out) noexcept { if (out) *out = 0; return out; }
const char* empty_string(void*) noexcept { return empty; }
std::uint64_t one(void*) noexcept { return 1; }

// ISteamUser
Uint64Out user_steam_id(void*, Uint64Out out) noexcept { if (out) *out = steam_id(); return out; }

std::atomic<std::uint32_t> next_ticket{1};

#pragma pack(push, 8)
struct AuthSessionTicketResponse { std::uint32_t ticket; int result; };
struct WebApiTicketResponse { std::uint32_t ticket; int result; int size; std::uint8_t data[2560]; };
#pragma pack(pop)

void fill_ticket(std::uint8_t* data, std::uint32_t size) noexcept {
    const auto id = steam_id();
    for (std::uint32_t i = 0; i < size; ++i) data[i] = static_cast<std::uint8_t>(id >> (8 * (i % 8)));
}

std::uint32_t auth_session_ticket(void*, void* ticket, int capacity, std::uint32_t* size, const void*) noexcept {
    const auto handle = next_ticket.fetch_add(1);
    const auto length = static_cast<std::uint32_t>(capacity >= 64 ? 64 : (capacity > 0 ? capacity : 0));
    if (ticket) fill_ticket(static_cast<std::uint8_t*>(ticket), length);
    if (size) *size = length;
    try { post(163, AuthSessionTicketResponse{handle, 1}); } catch (...) {}
    return handle;
}

std::uint32_t web_api_ticket(void*, const char*) noexcept {
    const auto handle = next_ticket.fetch_add(1);
    try {
        auto response = std::make_unique<WebApiTicketResponse>();
        response->ticket = handle;
        response->result = 1;
        response->size = 64;
        fill_ticket(response->data, 64);
        post(168, *response);
    } catch (...) {}
    return handle;
}

// ISteamFriends
const char* persona_name(void*) noexcept { return launcher::offline_player_name; }

// ISteamUtils
std::uint64_t server_time(void*) noexcept { return static_cast<std::uint32_t>(std::time(nullptr)); }
const char* country(void*) noexcept { return "US"; }
const char* language(void*) noexcept { return "english"; }
const char* language_list(void*) noexcept { return "english"; }
std::uint64_t battery(void*) noexcept { return 255; }
std::uint64_t app(void*) noexcept { return app_id; }

// ISteamNetworkingUtils
std::uint64_t local_timestamp(void*) noexcept {
    LARGE_INTEGER counter{}, frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<std::uint64_t>(counter.QuadPart / frequency.QuadPart * 1000000 +
        counter.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart) + 1000000;
}

template<int I> void set(std::size_t slot, auto function) {
#pragma warning(push)
#pragma warning(disable: 4191)
    table<I>[slot] = reinterpret_cast<void*>(function);
#pragma warning(pop)
}

void build_objects() {
    set<user>(0, &one);                    // GetHSteamUser
    set<user>(1, &one);                    // BLoggedOn
    set<user>(2, &user_steam_id);          // GetSteamID
    set<user>(13, &auth_session_ticket);   // GetAuthSessionTicket
    set<user>(14, &web_api_ticket);        // GetAuthTicketForWebApi
    set<user>(24, &one);                   // GetPlayerSteamLevel

    set<friends>(0, &persona_name);        // GetPersonaName
    set<friends>(2, &one);                 // GetPersonaState: online
    for (const auto slot : {4, 19, 25, 39, 41, 51, 57}) set<friends>(slot, &zero_steam_id);
    for (const auto slot : {7, 9, 14, 20, 21, 45, 47, 78}) set<friends>(slot, &empty_string);
    set<friends>(43, &one);                // SetRichPresence

    set<utils>(2, &one);                   // GetConnectedUniverse: public
    set<utils>(3, &server_time);           // GetServerRealTime
    set<utils>(4, &country);               // GetIPCountry
    set<utils>(8, &battery);               // GetCurrentBatteryPower: AC
    set<utils>(9, &app);                   // GetAppID
    set<utils>(23, &language);             // GetSteamUILanguage

    for (const auto slot : {12, 18, 35}) set<matchmaking>(slot, &zero_steam_id);
    for (const auto slot : {19, 24}) set<matchmaking>(slot, &empty_string);

    set<apps>(0, &one);                    // BIsSubscribed
    set<apps>(4, &language);               // GetCurrentGameLanguage
    set<apps>(5, &language_list);          // GetAvailableGameLanguages
    set<apps>(6, &one);                    // BIsSubscribedApp
    set<apps>(19, &one);                   // BIsAppInstalled
    set<apps>(20, &user_steam_id);         // GetAppOwner
    set<apps>(21, &empty_string);          // GetLaunchQueryParam

    for (const auto slot : {12, 15, 24}) set<user_stats>(slot, &empty_string); // achievement/leaderboard names
    set<remote_storage>(19, &empty_string);                                  // GetFileNameAndSize
    set<remote_play>(2, &zero_steam_id);                                     // GetSessionSteamID
    set<remote_play>(3, &empty_string);                                      // GetSessionClientName

    set<networking_utils>(12, &local_timestamp); // GetLocalTimestamp
    set<networking_utils>(33, &one);             // SetConfigValue

    const auto bind = [](int interface, std::array<void*, slot_count>& vtable) {
        objects[interface].vtable = vtable.data();
    };
    bind(user, table<user>); bind(friends, table<friends>); bind(utils, table<utils>);
    bind(matchmaking, table<matchmaking>); bind(matchmaking_servers, table<matchmaking_servers>);
    bind(networking, table<networking>); bind(networking_messages, table<networking_messages>);
    bind(networking_sockets, table<networking_sockets>); bind(networking_utils, table<networking_utils>);
    bind(input, table<input>); bind(controller, table<controller>); bind(parties, table<parties>);
    bind(game_search, table<game_search>); bind(apps, table<apps>);
    bind(user_stats, table<user_stats>); bind(remote_storage, table<remote_storage>);
    bind(remote_play, table<remote_play>); bind(unknown, table<unknown>);
}

Interface classify(std::string_view version) noexcept {
    // Longest names first: prefixes overlap (SteamNetworking...).
    constexpr std::array<std::pair<std::string_view, Interface>, 17> names{{
        {"STEAMAPPS_INTERFACE_VERSION", apps}, {"STEAMUSERSTATS_INTERFACE_VERSION", user_stats},
        {"STEAMREMOTESTORAGE_INTERFACE_VERSION", remote_storage},
        {"STEAMREMOTEPLAY_INTERFACE_VERSION", remote_play},
        {"SteamMatchMakingServers", matchmaking_servers}, {"SteamMatchGameSearch", game_search},
        {"SteamMatchMaking", matchmaking}, {"SteamNetworkingMessages", networking_messages},
        {"SteamNetworkingSockets", networking_sockets}, {"SteamNetworkingUtils", networking_utils},
        {"SteamNetworking", networking}, {"SteamUser", user}, {"SteamFriends", friends},
        {"SteamUtils", utils}, {"SteamInput", input}, {"SteamController", controller},
        {"SteamParties", parties}}};
    for (const auto& [name, interface] : names) if (version.starts_with(name)) return interface;
    return unknown;
}

// ---------------------------------------------------------------- flat API

void* __cdecl find_or_create_user_interface(std::int32_t, const char* version) {
    const auto interface = classify(version ? version : "");
    if (interface == unknown) {
        try {
            logging::log(logging::Level::info, logging::Channel::network,
                "Offline Steam: generic stand-in for {}", version ? version : "(null)");
        } catch (...) {}
    }
    return &objects[interface];
}

void* __cdecl find_or_create_game_server_interface(std::int32_t, const char*) { return nullptr; }

// Steamworks' per-interface accessor cache: {init function, counter, pointer}.
void* __cdecl context_init(void* data) {
    auto* context = static_cast<void**>(data);
    if (!context) return nullptr;
    if (reinterpret_cast<std::uintptr_t>(context[1]) != 1) {
        reinterpret_cast<void (*)(void*)>(context[0])(&context[2]);
        context[1] = reinterpret_cast<void*>(std::uintptr_t{1});
    }
    return &context[2];
}

int __cdecl internal_init(const char*, char* error) {
    if (error) error[0] = '\0';
    return 0; // k_ESteamAPIInitResult_OK
}
bool __cdecl init_ok() { return true; }
void __cdecl shutdown() {}
std::int32_t __cdecl handle() { return 1; }
const char* __cdecl install_path() { return empty; }
void __cdecl release_thread_memory() {}

void detour(HMODULE module, const char* name, void* replacement) {
    const auto target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) return;
    const auto create = hook_prepare(target, replacement, nullptr);
    if (create != HookOk)
        throw std::runtime_error(std::string("Cannot hook ") + name + ": " + hook_status_string(create));
    const auto enable = hook_enable(target);
    if (enable != HookOk)
        throw std::runtime_error(std::string("Cannot enable ") + name + ": " + hook_status_string(enable));
}

} // namespace

std::uint64_t steam_id() noexcept { return launcher::offline_steam_id(); }

bool install(HMODULE steam, std::string& error) noexcept {
    try {
        build_objects();
#pragma warning(push)
#pragma warning(disable: 4191)
        const std::pair<const char*, void*> replacements[]{
            {"SteamInternal_SteamAPI_Init", reinterpret_cast<void*>(&internal_init)},
            {"SteamAPI_Init", reinterpret_cast<void*>(&init_ok)},
            {"SteamAPI_InitSafe", reinterpret_cast<void*>(&init_ok)},
            {"SteamAPI_IsSteamRunning", reinterpret_cast<void*>(&init_ok)},
            {"SteamAPI_Shutdown", reinterpret_cast<void*>(&shutdown)},
            {"SteamAPI_GetHSteamUser", reinterpret_cast<void*>(&handle)},
            {"SteamAPI_GetHSteamPipe", reinterpret_cast<void*>(&handle)},
            {"SteamAPI_GetSteamInstallPath", reinterpret_cast<void*>(&install_path)},
            {"SteamAPI_ReleaseCurrentThreadMemory", reinterpret_cast<void*>(&release_thread_memory)},
            {"SteamAPI_RunCallbacks", reinterpret_cast<void*>(&run_callbacks)},
            {"SteamAPI_RegisterCallback", reinterpret_cast<void*>(&register_callback)},
            {"SteamAPI_UnregisterCallback", reinterpret_cast<void*>(&unregister_callback)},
            {"SteamAPI_RegisterCallResult", reinterpret_cast<void*>(&register_call_result)},
            {"SteamAPI_UnregisterCallResult", reinterpret_cast<void*>(&unregister_call_result)},
            {"SteamInternal_ContextInit", reinterpret_cast<void*>(&context_init)},
            {"SteamInternal_FindOrCreateUserInterface", reinterpret_cast<void*>(&find_or_create_user_interface)},
            {"SteamInternal_FindOrCreateGameServerInterface", reinterpret_cast<void*>(&find_or_create_game_server_interface)},
        };
#pragma warning(pop)
        for (const auto& [name, replacement] : replacements) detour(steam, name, replacement);
        error = "offline, SteamID " + std::to_string(steam_id());
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace dingosdk::offline_steam

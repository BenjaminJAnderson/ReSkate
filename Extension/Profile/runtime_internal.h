#pragma once
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Abi/native_data.h"
#include "Engine/Game/Abi/native_string.h"
#include "Engine/Game/Build/fingerprint.h"
#include "local_profile_runtime.h"
#include "Engine/Game/World/location_travel.h"
#include "local_profile.h"
#include "Extension/Objects/object_placements.h"
#include <thread>
#include <condition_variable>
#include <chrono>
#include "Extension/Progression/fast_travel_unlock.h"
#include <charconv>
#include "Engine/Core/Platform/launcher_support.h"
#include "Extension/Progression/mission_progression_override.h"
#include "Extension/Boot/offline_boot.h"
#include <Windows.h>
#include "Engine/Core/Hooks/hooks.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
#include <memory>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <set>

#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/graphics_controls.h"
#include "Engine/Game/Build/20260929/location_travel.h"
#include "Engine/Game/Build/20260929/profile.h"
#include "Engine/Game/Build/20260929/world_controls.h"

namespace dingosdk::profile_runtime {
using game::build::Fingerprint;
// The profile runtime files share these contracts unqualified.
using namespace addr::graphics_controls;
using namespace addr::location_travel;
using namespace addr::profile;
using namespace addr::world_controls;
using LoadEvents = void (*)(std::uintptr_t);

using Ready = bool (*)(std::uintptr_t);

using SendEvent = void (*)(std::uintptr_t, const void*);

using GetBool = bool (*)(std::uint64_t, const void*, bool);

using SetBool = void (*)(std::uint64_t, const void*, bool);

using Cache = void (*)(std::uintptr_t, const void*);

using CompleteLoad = void (*)(const std::uintptr_t*, void*, void*);

using Success = void* (*)(void*);

using RefreshGraph = void (*)(std::uintptr_t);

using RegisterOnboarding = void (*)(std::uintptr_t, const void*, std::uint64_t);

using ApplyOnboarding = void (*)(std::uintptr_t, const void*, const void*, std::uint64_t, std::int32_t);

using DestroyString = void (*)(void*);

using HasEntitlement = bool (*)(std::uintptr_t, const void*);

using ExecuteExpression = void (*)(std::uintptr_t, std::uint32_t);

using ExecuteProfiledExpression = void (*)(std::uintptr_t, std::uint32_t, std::uintptr_t);

using TriggerReady = bool (*)();

using LoadCosmeticRecipe = bool (*)(const void*, void*);

using SaveCosmeticRecipe = void (*)(std::uintptr_t, void*, const char*, const char*, bool);

using CopyCosmeticRecipes = void (*)(void*, const void*, const void*);

using NativeString = game::NativeStringView;

struct NativeEvent {
    NativeString id, context;
    std::uint64_t timestamp;
    std::int32_t count;
    std::uint32_t padding{};
    explicit NativeEvent(const profile::PlayEvent& e)
        : id(e.id), context(e.context), timestamp(e.timestamp), count(e.count) {}
};

static_assert(sizeof(NativeString) == 0x18 && sizeof(NativeEvent) == 0x40);

static_assert(offsetof(NativeEvent, timestamp) == 0x30 && offsetof(NativeEvent, count) == 0x38);

struct LocalRuntime {
    std::uintptr_t base{};
    std::unique_ptr<profile::Store> store;
    std::atomic<bool> active{};
    bool attempted{};
    std::recursive_mutex native_mutex;
    LoadEvents load{};
    Ready ready{};
    SendEvent send{};
    GetBool get_bool{};
    SetBool set_bool{};
    Cache cache{};
    CompleteLoad complete_load{};
    Success success{};
    RefreshGraph refresh_graph{};
    RegisterOnboarding register_onboarding{};
    ApplyOnboarding apply_onboarding{};
    DestroyString destroy_string{};
    HasEntitlement has_entitlement{};
    Ready entitlements_ready{};
    ExecuteExpression execute_expression{};
    ExecuteProfiledExpression execute_profiled_expression{};
    TriggerReady trigger_online{}, trigger_session{};
    LoadCosmeticRecipe load_cosmetic{};
    SaveCosmeticRecipe save_cosmetic{};
    CopyCosmeticRecipes copy_cosmetic{};
    std::atomic<unsigned> logged_trigger_ready{};
    std::atomic<bool> logged_entitlements_ready{};
    std::map<std::string, bool, std::less<>> logged_entitlements;
    std::string mission_feedback;
    std::shared_ptr<const profile::MissionState> mission_source;
    LocalMissions mission_cache;
    std::string progression_feedback;
    std::uint64_t progression_revision{UINT64_MAX};
    ProgressionModel progression_cache;
    std::map<std::string, std::uint64_t, std::less<>> logged_onboarding;
};

LocalRuntime& local_runtime();

extern thread_local bool hydrating;

struct PreserveError { DWORD error{GetLastError()}; ~PreserveError() { SetLastError(error); } };

using memory::read_bytes;

using memory::read;

bool identifier(const void* wrapper, std::string& value);

bool entitlements_ready_hook(std::uintptr_t manager);

bool has_entitlement_hook(std::uintptr_t manager, const void* wrapper);

std::uintptr_t onboarding_manager(std::uintptr_t context);

void apply_onboarding_event(std::uintptr_t manager, const profile::PlayEvent& event);

void register_onboarding_hook(std::uintptr_t manager, const void* wrapper, std::uint64_t handle);

bool valid_manager(std::uintptr_t manager);

void cache_event(std::uintptr_t manager, const profile::PlayEvent& event);

void refresh_graphs(std::uintptr_t manager);

bool hydrate(std::uintptr_t manager);

void load_events_hook(std::uintptr_t manager);

bool ready_hook(std::uintptr_t manager);

void send_event_hook(std::uintptr_t manager, const void* wrapper);

bool get_bool_hook(std::uint64_t user, const void* key, bool fallback);

void set_bool_hook(std::uint64_t user, const void* key, bool value);

bool saved_quest(std::string_view id, std::int32_t& value) noexcept;

bool save_quest_completion(std::string_view id) noexcept;

bool matches(std::uintptr_t base, const Fingerprint& f);
extern std::string binding_feedback;

}

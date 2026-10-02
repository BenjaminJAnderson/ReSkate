#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
// ActivityCountData: four Int32 counters, two flags and native alignment padding.
struct alignas(4) ChallengeCounts { std::array<std::byte, 0x14> bytes{}; };
static_assert(sizeof(ChallengeCounts) == 0x14);
struct ChallengeFunctions {
    using Request = void* (*)(void*, const void*, const void*, const void*, const void*);
    Request begin{}, end{};
    void (*counts)(const void*, bool, ChallengeCounts*){};
    void* (*index_insert)(std::uintptr_t, void*, const void*){};
    void (*index_append)(std::uintptr_t, const std::uint64_t*){};
    bool (*available)(const void*){};
    bool (*select)(std::uintptr_t, const void*){};
    std::uint64_t (*lookup)(std::uintptr_t, const void*){};
    void (*invoke_end)(const void*, const void*, const void*){};
    void* (*error)(void*, std::uint32_t, const char*){};
    void* (*insert)(std::uintptr_t, void*, std::uint8_t, const void*, const void*){};
    void (*dispatch)(std::uintptr_t, std::uintptr_t, const void*, const void*, std::uint64_t){};
    void* (*create_request)(void*, std::uintptr_t){};
    void (*release_request)(void*){};
};

struct PendingChallenge {
    std::uintptr_t callback{};
    bool ending{}, failed{};
    std::string id, player;
    std::vector<std::string> players, goals;
    std::uint64_t attempt{};
    // A virtual coop participant (Extension/Throwdowns/virtual_player_names.h) left: the
    // leave handler's end request carries its entry and the local one, but the local attempt
    // goes on. Nothing is committed and the local player is not told the challenge ended.
    bool virtual_leave{};
};

struct ChallengeRuntime {
    ChallengeFunctions f;
    profile::ChallengePolicy policy;
    std::vector<PendingChallenge> pending;
    std::map<std::string, std::uint64_t, std::less<>> attempts;
    std::string selected;
    std::uint64_t next_catalog_poll{};
    std::uintptr_t last_owner{};
    std::uint64_t next_progress_poll{};
    unsigned forwarded_requests{};
    std::map<std::string, std::string, std::less<>> title_fallbacks;
    std::map<std::string, std::uint64_t, std::less<>> catalog_groups;
    std::map<std::string, std::uint64_t, std::less<>> catalog_activities;
    std::map<std::string, std::vector<std::pair<std::string, std::uint64_t>>, std::less<>> catalog_goals;
    std::set<std::string> changed_groups;
    std::string observed_id;
    std::vector<profile::ChallengeGoal> observed_goals;
    std::uint64_t map_progress_revision{UINT64_MAX};
};

ChallengeRuntime& challenge_runtime();

std::atomic_bool& hidden_challenges();

std::string_view challenge_title_fallback(std::string_view key);

extern thread_local bool select_authored_challenge;

struct ChallengeCompletionDelivery {
    bool delivered{};
    std::int32_t error_code{};
    const char* blocked_at{"callback_not_observed"};
};

extern thread_local ChallengeCompletionDelivery* challenge_completion_delivery;

struct ChallengeCompletionScope {
    ChallengeCompletionDelivery* previous{challenge_completion_delivery};
    explicit ChallengeCompletionScope(ChallengeCompletionDelivery* delivery) { challenge_completion_delivery = delivery; }
    ~ChallengeCompletionScope() { challenge_completion_delivery = previous; }
};

bool local_challenges_active();

const profile::ChallengeDefinition* local_challenge(std::string_view id);

bool challenge_native_array(const void* wrapper, std::uintptr_t& data, std::uint32_t& count, std::uint32_t maximum);

bool challenge_player_id(const void* wrapper, std::string& value);

bool challenge_native_strings(const void* wrapper, std::vector<std::string>& values, std::uint32_t maximum,
    bool players = false);

void* challenge_request(bool ending, void* out, const void* id_value, const void* player_value,
    const void* values, const void* callback);

void* challenge_begin_hook(void* out, const void* id, const void* player, const void* values, const void* callback);

void* challenge_end_hook(void* out, const void* id, const void* player, const void* values, const void* callback);

bool challenge_available_hook(const void* value);

std::uint64_t challenge_lookup_hook(std::uintptr_t owner, const void* value);

bool challenge_select_hook(std::uintptr_t owner, const void* value);

struct ChallengeEmptyArray {
    std::uint32_t capacity{}, count{};
    std::uint64_t element{};
};

template<class T, std::size_t N> struct ChallengeArray {
    std::uint32_t capacity{N}, count{};
    std::array<T, N> values{};
};

struct ChallengeCriterionValue {
    // The native copy constructor treats these three fields as
    // reference-counted ValueRefs, not inline arrays. Null means absent.
    std::uintptr_t rewards{};
    const char* id{};
    std::uintptr_t conditions{}, targets{}, additional_rewards{};
    const char* description{};
    const char* title{};
    const char* feed{};
    const char* short_description{};
    std::uint32_t category{};
    bool completed{}, optional{};
    std::array<std::byte, 2> padding{};
};

static_assert(sizeof(ChallengeCriterionValue) == 0x50 && offsetof(ChallengeCriterionValue, id) == 8 &&
    offsetof(ChallengeCriterionValue, completed) == 0x4c && offsetof(ChallengeCriterionValue, optional) == 0x4d);

std::uintptr_t challenge_owner();

std::uint64_t challenge_map_lookup(std::uintptr_t map, const std::string& id);

bool challenge_map_type_contract(std::uintptr_t base);
void challenge_counts_hook(const void* neighborhood, bool visible_only, ChallengeCounts* output);
bool hydrate_challenge_neighborhoods(std::uintptr_t owner);
void hydrate_challenge_catalog(std::uintptr_t owner);

void update_challenge_catalog();

void update_selected_challenge_progress(bool learn = true);

void deliver_local_challenge_completion(std::uintptr_t vm, std::uint32_t pc) noexcept;

void update_challenge_requests();

void initialize_challenge_functions(std::uintptr_t base);
}

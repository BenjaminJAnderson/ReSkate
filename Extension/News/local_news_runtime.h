#pragma once
#include "Extension/Profile/runtime_internal.h"
#include "Engine/Game/Abi/native_callback_queue.h"

namespace dingosdk::profile_runtime {
struct NativeNewsPost {
    std::array<std::uintptr_t, 12> strings{};
    std::int32_t priority{};
    std::uint32_t padding{};
};

static_assert(sizeof(NativeNewsPost) == 0x68);

static_assert(offsetof(NativeNewsPost, priority) == 0x60);

struct NewsFunctions {
    void* (*first)(void*, const void*){};
    void* (*list)(void*, const void*){};
    void* (*construct)(void*){};
    void (*destroy)(void*){};
    void* (*construct_array)(void*){};
    void (*destroy_array)(void*){};
    void* (*append)(void*, void*){};
    void* (*subscribe)(void*, const void*, const void*){};
};

struct PendingNews {
    std::uintptr_t callback{};
    std::string group;
};

struct NewsRuntime {
    NewsFunctions functions;
    std::set<std::string> observed_groups;
    game::NativeCallbackQueue<PendingNews> pending;
};

NewsRuntime& news_runtime();

void initialize_news_functions(std::uintptr_t base);

struct NewsPostGuard {
    NativeNewsPost value;
    bool owned{true};
    NewsPostGuard() { news_runtime().functions.construct(&value); }
    ~NewsPostGuard() { if (owned) news_runtime().functions.destroy(&value); }
};

struct NewsArrayGuard {
    std::uintptr_t value{};
    bool owned{true};
    NewsArrayGuard() { news_runtime().functions.construct_array(&value); }
    ~NewsArrayGuard() { if (owned) news_runtime().functions.destroy_array(&value); }
};


void fill_news_post(NativeNewsPost& native, const profile::NewsPost& post, std::int32_t priority);

void observe_news_group(const void* group, std::size_t count);

void fill_news_array(NewsArrayGuard& array, const profile::NewsFeed& feed);

void* news_subscribe_hook(void* destination, const void* group, const void* callback);

void update_news_subscriptions();

void* news_first_hook(void* destination, const void* group);

void* news_list_hook(void* destination, const void* group);
}

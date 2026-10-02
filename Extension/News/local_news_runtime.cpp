#include "Engine/Core/Log/logging.h"
#include "local_news_runtime.h"
#include "live_news.h"
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
// Native MotdData getters are the decoded boundary consumed by the NEWS UI.

NewsRuntime& news_runtime() { static auto* n = new NewsRuntime; return *n; }

void initialize_news_functions(std::uintptr_t base) {
    auto& f = news_runtime().functions;
    f.construct = reinterpret_cast<decltype(f.construct)>(base + news_ctor_contract.rva);
    f.destroy = reinterpret_cast<decltype(f.destroy)>(base + news_destroy_contract.rva);
    f.construct_array = reinterpret_cast<decltype(f.construct_array)>(base + news_array_ctor_contract.rva);
    f.destroy_array = reinterpret_cast<decltype(f.destroy_array)>(base + news_array_destroy_contract.rva);
    f.append = reinterpret_cast<decltype(f.append)>(base + news_append_contract.rva);
}

void fill_news_post(NativeNewsPost& native, const profile::NewsPost& post, std::int32_t priority) {
    const auto set = [&](unsigned index, const std::string& value) {
        if (!value.empty()) game::native_data().values.assign(&native.strings[index], value.c_str(), static_cast<std::uint32_t>(value.size()));
    };
    // Current native MotdMessage -> MotdData conversion (see the build table);
    // the twelve owned CString slots and priority at +60 are unchanged.
    set(6, post.id); set(8, post.title); set(3, post.description); set(5, post.body);
    set(10, post.small_image); set(4, post.large_image);
    set(11, post.fallback_small); set(9, post.fallback_large);
    native.priority = priority;
}

void observe_news_group(const void* group, std::size_t count) {
    std::string id;
    if (!identifier(group, id)) id = "";
    auto& seen = news_runtime().observed_groups;
    if (seen.size() < 32 && seen.insert(id).second)
        dingosdk::logging::event(dingosdk::logging::Channel::news, dingosdk::Json{{"event", "local_news_published"}, {"group", id}, {"posts", count}}.dump().c_str());
}

void fill_news_array(NewsArrayGuard& array, const profile::NewsFeed& feed) {
    auto priority = static_cast<std::int32_t>(feed.posts.size());
    for (const auto& entry : feed.posts) {
        NewsPostGuard post;
        fill_news_post(post.value, entry, priority--);
        news_runtime().functions.append(&array.value, &post.value);
    }
}

void* news_subscribe_hook(void* destination, const void* group, const void* callback) {
    auto& s = local_runtime(); auto& n = news_runtime(); auto& f = n.functions;
    if (!s.active.load(std::memory_order_acquire)) return f.subscribe(destination, group, callback);
    {
        PreserveError preserve;
        std::lock_guard lock(s.native_mutex);
        try {
            const auto feed = news::current_news_feed(*s.store->shared_snapshot());
            if (feed.enabled && n.pending.available()) {
                game::NativeDelegateGuard owned;
                game::native_data().values.copy_delegate(&owned.value, callback);
                if (owned.value) {
                    std::string id;
                    if (!identifier(group, id)) return f.subscribe(destination, group, callback);
                    n.pending.push({owned.value, std::move(id)});
                    owned.value = 0;
                }
                // Deliver after the NewsService constructor returns. The feed is
                // immutable during this session, so no remote subscription is needed.
                return game::native_data().values.null_reference(destination, 0);
            }
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::news, "{\"event\":\"local_news_failed\",\"operation\":\"subscribe\"}"); }
    }
    return f.subscribe(destination, group, callback);
}

void update_news_subscriptions() {
    news::update_image_trust();
    auto& n = news_runtime();
    // Hold the first delivery briefly while the live news downloads.
    if (!news::live_news_settled()) return;
    n.pending.deliver([&](const PendingNews& request, std::uintptr_t callback) {
        try {
            const auto feed = news::current_news_feed(*local_runtime().store->shared_snapshot());
            if (!feed.enabled) return;
            NewsArrayGuard array;
            fill_news_array(array, feed);
            game::native_data().values.invoke(&callback, &array.value);
            const char* id = request.group.c_str();
            observe_news_group(&id, feed.posts.size());
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::news, "{\"event\":\"local_news_failed\",\"operation\":\"callback\"}"); }
    });
}

void* news_first_hook(void* destination, const void* group) {
    auto& s = local_runtime(); auto& f = news_runtime().functions;
    if (!s.active.load(std::memory_order_acquire)) return f.first(destination, group);
    {
        PreserveError preserve;
        std::lock_guard lock(s.native_mutex);
        try {
            const auto feed = news::current_news_feed(*s.store->shared_snapshot());
            if (feed.enabled) {
                NewsPostGuard post;
                if (!feed.posts.empty()) fill_news_post(post.value, feed.posts.front(), static_cast<std::int32_t>(feed.posts.size()));
                observe_news_group(group, feed.posts.size());
                // Return storage is uninitialized. Transfer native-owned strings to the caller.
                std::memcpy(destination, &post.value, sizeof(post.value)); post.owned = false;
                return destination;
            }
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::news, "{\"event\":\"local_news_failed\",\"operation\":\"first\"}"); }
    }
    return f.first(destination, group);
}

void* news_list_hook(void* destination, const void* group) {
    auto& s = local_runtime(); auto& f = news_runtime().functions;
    if (!s.active.load(std::memory_order_acquire)) return f.list(destination, group);
    {
        PreserveError preserve;
        std::lock_guard lock(s.native_mutex);
        try {
            const auto feed = news::current_news_feed(*s.store->shared_snapshot());
            if (feed.enabled) {
                NewsArrayGuard array;
                fill_news_array(array, feed);
                observe_news_group(group, feed.posts.size());
                std::memcpy(destination, &array.value, sizeof(array.value)); array.owned = false;
                return destination;
            }
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::news, "{\"event\":\"local_news_failed\",\"operation\":\"list\"}"); }
    }
    return f.list(destination, group);
}
}

#include "Engine/Game/Abi/native_callback_queue.h"
#include <array>
#include <cstdio>

namespace {
struct Request { std::uintptr_t callback; };
std::array<unsigned, 16> destroyed{};
void destroy(void* wrapper) {
    const auto value = *static_cast<std::uintptr_t*>(wrapper);
    if (value < destroyed.size()) ++destroyed[value];
}
void check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
void regression() {
    using dingosdk::game::NativeCallbackQueue;
    dingosdk::game::native_data().values.destroy_delegate = destroy;
    NativeCallbackQueue<Request> queue;
    unsigned invoked{};
    bool assets_alive = true;
    const auto invoke = [&](const Request&, std::uintptr_t) {
        check(assets_alive, "A callback reached an unloaded script descriptor");
        ++invoked;
    };
    // A Build Kit request can wait for inventory across many client ticks.
    queue.push({1}); queue.push({2});
    queue.before_transition(14);
    assets_alive = false;
    queue.deliver(invoke);
    queue.before_transition(3);
    check(invoked == 0 && destroyed[1] == 1 && destroyed[2] == 1 && !queue.available(),
        "Native unload must cancel retained callbacks once, even with inventory still unavailable");
    // The splash screen subscribes during construction, before state 13.
    queue.before_transition(4); assets_alive = true;
    check(queue.available(), "Fresh loading-world subscriptions must be accepted");
    queue.push({3}); queue.deliver(invoke);
    check(invoked == 1 && destroyed[3] == 1, "Fresh subscriptions must still complete normally");
    // A callback may rebuild/unload the world, invalidating the rest of its batch.
    queue.push({4}); queue.push({5});
    queue.deliver([&](const Request&, std::uintptr_t callback) {
        check(callback == 4, "Reentrant unload left a stale callback in the active batch");
        ++invoked;
        queue.push({6});
        queue.before_transition(22);
        assets_alive = false;
    });
    check(invoked == 2 && destroyed[4] == 1 && destroyed[5] == 1 && destroyed[6] == 1,
        "Cancel pending and in-flight batches without invoking or leaking old callbacks");
    queue.before_transition(16); assets_alive = true;
    queue.push({7}); queue.push({8});
    try { queue.deliver([](const Request&, std::uintptr_t) { throw std::runtime_error("delivery failed"); }); }
    catch (const std::runtime_error&) {}
    check(destroyed[7] == 1 && destroyed[8] == 1, "Failed delivery must release the entire retained batch");
    queue.push({9}); queue.before_transition(24); queue.before_transition(4); queue.before_transition(13);
    queue.deliver(invoke);
    check(!queue.available() && destroyed[9] == 1 && invoked == 2, "Shutdown cannot resume callback delivery");
}
}
int main() {
    try { regression(); std::puts("Native callback lifetime regressions passed."); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}

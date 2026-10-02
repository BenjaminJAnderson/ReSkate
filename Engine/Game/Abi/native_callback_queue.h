#pragma once
#include "native_data.h"
#include <utility>
#include <vector>

namespace dingosdk::game {
// Copying a native delegate owns its optional binding, but not the script's
// asset-backed function descriptor. Cancel deferred work before that world
// unloads, including work already moved into a reentrant delivery batch.
template<class Request> class NativeCallbackQueue {
    std::vector<Request> pending_;
    std::uint64_t generation_{};
    bool blocked_{}, shutdown_{};
public:
    bool available() const { return !blocked_ && pending_.size() < 32; }
    bool empty() const { return pending_.empty(); }
    void push(Request request) { pending_.push_back(std::move(request)); }
    template<class Deliver> void deliver(Deliver&& invoke) {
        if (blocked_) return;
        const auto generation = generation_;
        struct Batch {
            std::vector<Request> requests;
            ~Batch() {
                for (auto& request : requests) {
                    NativeDelegateGuard owned;
                    owned.value = request.callback;
                }
            }
        } pending{std::exchange(pending_, {})};
        for (auto& request : pending.requests) {
            NativeDelegateGuard owned;
            owned.value = std::exchange(request.callback, 0);
            if (!blocked_ && generation == generation_) invoke(request, owned.value);
        }
    }
    void before_transition(unsigned next) {
        if (next == 3 || next == 14 || next == 22 || next == 24 || next == 25) {
            blocked_ = true;
            shutdown_ |= next == 24 || next == 25;
            ++generation_;
            auto pending = std::exchange(pending_, {});
            for (auto& request : pending) {
                NativeDelegateGuard owned;
                owned.value = request.callback;
            }
        } else if (!shutdown_ && (next == 4 || next == 8 || next == 13 || next == 16 || next == 21)) {
            // Constructors in the next load can subscribe before gameplay starts.
            blocked_ = false;
        }
    }
};
}

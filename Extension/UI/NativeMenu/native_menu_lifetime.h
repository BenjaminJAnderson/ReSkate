#pragma once
#include <algorithm>
#include <atomic>
#include <vector>

namespace dingosdk::multiplayer::menu_data {
// Native transitions also unload assets when no ReSkate load request exists.
// Suspend publication and queued actions before cleanup can dispatch callbacks.
// Only a new active world may resume them; shutdown remains permanent.
class MenuLifetime {
    enum class Phase { loading, active, shutdown };
    std::atomic<Phase> phase_{Phase::loading};
public:
    bool blocked() const noexcept { return phase_.load() != Phase::active; }
    template<class Cleanup> void before_transition(unsigned next, Cleanup&& cleanup) {
        if (next == 24) {
            if (phase_.exchange(Phase::shutdown) != Phase::shutdown) cleanup();
        } else if (next == 14 || next == 22 || next == 3) {
            auto expected = Phase::active;
            if (phase_.compare_exchange_strong(expected, Phase::loading)) cleanup();
        } else if (next == 13 || next == 21) {
            auto expected = Phase::loading;
            phase_.compare_exchange_strong(expected, Phase::active);
        }
    }
};

// Model creation returns an independently owned root. Dropping the C++ handle
// does not destroy it. Track partial builds and old pause-menu generations too.
template<class Model> class OwnedMenuModels {
    std::vector<Model> models_;
public:
    bool empty() const { return models_.empty(); }
    void track(Model value) {
        if (std::none_of(models_.begin(), models_.end(), [&](const auto& existing) {
                return existing.handle == value.handle && existing.type == value.type;
            })) models_.push_back(value);
    }
    template<class Detach, class Destroy> void release(Detach&& detach, Destroy&& destroy) {
        if (empty()) return;
        detach();
        while (!models_.empty()) {
            destroy(models_.back());
            models_.pop_back(); // Keep unfinished cleanup available on failure.
        }
    }
    // Use only after the native manager itself has been replaced.
    void manager_replaced() { models_.clear(); }
};
} // namespace dingosdk::multiplayer::menu_data

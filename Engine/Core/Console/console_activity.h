#pragma once

#include "console_core.h"
#include <array>

namespace dingosdk {
// Values sampled by the existing game-thread observers; no counters that change
// every frame belong in the console. Positions are logged only on entity arrival.
struct ConsoleActivity {
    unsigned state{26};
    bool native_loading_logging{}; // Detailed native stages replace coarse level messages when available.
    std::string state_name, level, sublevel, flow;
    bool sublevel_active{}, client_sublevel_present{}, players_available{};
    unsigned players{}, controllables{};
    std::uint64_t setup_attempts{};
    std::uintptr_t skater{};
    std::array<float, 3> position{};
};

class ConsoleActivityFeed {
public:
    using Emit = void (*)(ConsoleSource, std::string_view, ConsoleSeverity);
    void update(const ConsoleActivity& activity, Emit emit, std::uint64_t now_ms = console_timestamp_ms());
private:
    std::optional<ConsoleActivity> previous_;
    bool preparation_reported_{}, waiting_reported_{};
    std::optional<std::uint64_t> load_started_;
};
}

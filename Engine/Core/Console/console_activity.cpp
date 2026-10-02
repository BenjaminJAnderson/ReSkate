#include "console_activity.h"
#include <format>

namespace dingosdk {
namespace {
enum class Phase { assets, profile, disconnected, unloading, loading, active, connecting,
    sublevel_loading, sublevel_active, shutdown, idle };
Phase phase(unsigned state) {
    switch (state) {
    case 0: return Phase::assets;
    case 1: return Phase::profile;
    case 2: return Phase::disconnected;
    case 3: case 14: case 22: return Phase::unloading;
    case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: return Phase::loading;
    case 13: return Phase::active;
    case 15: case 23: return Phase::connecting;
    case 16: case 17: case 18: case 19: case 20: return Phase::sublevel_loading;
    case 21: return Phase::sublevel_active;
    case 24: case 25: return Phase::shutdown;
    default: return Phase::idle;
    }
}
}
void ConsoleActivityFeed::update(const ConsoleActivity& a, Emit sink, std::uint64_t now_ms) {
    const ConsoleActivity empty;
    const auto& before = previous_ ? *previous_ : empty;
    const auto emit = [&](ConsoleSource source, std::string text,
                          ConsoleSeverity severity = ConsoleSeverity::info) {
        if (sink) sink(source, text, severity);
    };
    const auto current = phase(a.state);
    const bool loading = current == Phase::loading || current == Phase::sublevel_loading;
    const bool sublevel = current == Phase::sublevel_loading || current == Phase::sublevel_active;
    const auto& destination = sublevel ? a.sublevel : a.level;
    const bool changed = !previous_ || current != phase(before.state) || a.level != before.level ||
        (sublevel && a.sublevel != before.sublevel);
    if (changed && !a.native_loading_logging) {
        const auto suffix = destination.empty() ? "." : ": " + destination;
        if (loading) {
            if (!load_started_ || current != phase(before.state)) load_started_ = now_ms;
            waiting_reported_ = false;
            preparation_reported_ = false;
            emit(ConsoleSource::level, std::string(sublevel ? "Loading sublevel" : "Loading world") + suffix);
        } else if (current == Phase::active || current == Phase::sublevel_active) {
            const auto duration = load_started_ ? std::format(" (load took {:.1f}s)",
                static_cast<double>(now_ms - *load_started_) / 1000.0) : "";
            emit(ConsoleSource::level, std::string(sublevel ? "Sublevel active" : "World active") + suffix + duration,
                ConsoleSeverity::success);
            load_started_.reset();
        } else {
            load_started_.reset();
            switch (current) {
            case Phase::assets: emit(ConsoleSource::assets, "Loading shared game assets."); break;
            case Phase::profile: emit(ConsoleSource::profile, "Applying profile options."); break;
            case Phase::disconnected: emit(ConsoleSource::network, "Game reported a lost connection.", ConsoleSeverity::warning); break;
            case Phase::unloading: emit(ConsoleSource::level, "Unloading world content" + suffix); break;
            case Phase::connecting: emit(ConsoleSource::level, "Connecting to local world" + suffix); break;
            case Phase::shutdown: emit(ConsoleSource::runtime, "Game is shutting down."); break;
            default: break;
            }
        }
    }
    if (!a.native_loading_logging && loading && load_started_ && !waiting_reported_ && now_ms - *load_started_ >= 30000) {
        emit(ConsoleSource::level, "Still loading after 30 seconds: " + a.state_name +
            (destination.empty() ? "." : " - " + destination), ConsoleSeverity::warning);
        waiting_reported_ = true;
    }
    if (!a.sublevel.empty() && a.sublevel_active && a.client_sublevel_present &&
        current != Phase::sublevel_active && (a.sublevel != before.sublevel ||
        !before.sublevel_active || !before.client_sublevel_present))
        emit(ConsoleSource::level, "Sublevel content ready: " + a.sublevel, ConsoleSeverity::success);
    if (a.players_available && (!before.players_available || a.players != before.players) &&
        (a.players || before.players)) {
        emit(ConsoleSource::player, a.players ? "Local player joined (" + std::to_string(a.players) + ")."
                                             : "Local player left.");
    }
    if (a.players_available && a.controllables != before.controllables) {
        emit(ConsoleSource::player, a.controllables ? "Controllable entity assigned to local player."
                                                  : "Local player released its controllable entity.");
    }
    if (a.setup_attempts > before.setup_attempts && !preparation_reported_) {
        emit(ConsoleSource::skater, "Skater initialization started.");
        preparation_reported_ = true;
    }
    if (a.skater != before.skater) {
        if (a.skater) emit(ConsoleSource::skater,
            std::format("Local skater entity available at ({:.1f}, {:.1f}, {:.1f}).",
                a.position[0], a.position[1], a.position[2]), ConsoleSeverity::success);
        else emit(ConsoleSource::skater, "Local skater entity no longer available.");
    }
    if (!a.flow.empty() && a.flow != before.flow)
        emit(ConsoleSource::flow, a.flow, a.flow == "InGame" ? ConsoleSeverity::success : ConsoleSeverity::info);
    previous_ = a;
}
}

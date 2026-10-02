#pragma once
#include <optional>

// How long the engine's idle job threads keep polling for work before they sleep
// (Engine/Game/Build/<build>/job_system.h; analysis/sim-performance.md). The stock 250 us had
// a dozen workers spinning most of the time: measured 2026-10-01 in idle free roam on 32
// threads, 250 us ~880% process CPU, 50 us ~490%, 10 us ~330%, 0 ~250% -- with the same client
// frame time down to 50 us (4.3 ms) and slightly longer ones below it (0: 4.5 ms, worst 8.6 ms).
namespace dingosdk::job_spin {
inline constexpr double default_microseconds = 10;
// The spin in microseconds, if this build's job system is the one in the address table.
std::optional<double> microseconds() noexcept;
double stock_microseconds() noexcept;
// The job system's timestamp-counter ticks per microsecond (its own calibration), if known.
std::optional<double> ticks_per_microsecond() noexcept;
bool set_microseconds(double value) noexcept;
// Once the job system is up: the default above. True once applied (or unavailable for good).
bool apply_default() noexcept;
}

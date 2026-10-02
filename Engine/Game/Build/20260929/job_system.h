#pragma once
#include <cstdint>

// The engine's job system (worker loop 0x3221db0, run-until-done 0x3222210, job pop 0x3226a30).
// See analysis/sim-performance.md.
namespace dingosdk::game::build::v20260929::job_system {
// How long an idle worker keeps polling its schedulers and queues before it sleeps, in timestamp
// counter ticks. Computed once at job-system start (0x3227660) from spin_milliseconds; both
// loops read it live on every pass, so a new value applies at once.
inline constexpr std::uintptr_t spin_ticks = 0x760a670;
// The configured spin time in milliseconds (float, 0.25 in this build; job config + 0x1a0).
inline constexpr std::uintptr_t spin_milliseconds = 0x760aa10;
// The number of job threads started (int).
inline constexpr std::uintptr_t thread_count = 0x7607724;
}

#pragma once
#include "profiler.h"
#include <Windows.h>

namespace dingosdk::profiler::detail {
// Threads the summary and the sampler name: the one running the client update, and the one
// presenting frames.
extern std::atomic<std::uint32_t> client_thread, present_thread;
// True while a sample runs; keeps the monitor (and so the zones) alive.
extern std::atomic<bool> sampling;
// Starts the once-a-second monitor if anything wants it.
void wake_monitor() noexcept;
// What a thread is, for reports: its description, or what its start address belongs to.
std::string thread_label(HANDLE thread, std::uint32_t id);
}

#pragma once
#include <cstdint>

namespace dingosdk {
// Caller validates the executable and initializes Detours hook service before starting.
// Keep this module and its trampoline loaded while any detour can run.
bool start_camera_observer(std::uintptr_t image_base) noexcept;
void camera_tick_enter(std::uintptr_t client) noexcept;
void camera_tick_native_return() noexcept;
void camera_tick_leave() noexcept;
// A bounded observation gate, not a native lifetime/concurrency lock. Requires
// the current outer post-tick phase and a completed same-thread/context sample
// since the last observed overlap, with its ownership chain revalidated now.
bool camera_probe_phase_observed(std::uintptr_t context) noexcept;
// Null means ready. A new isolated local callback can recover after overlap.
const char* camera_probe_unavailable_reason(std::uintptr_t context) noexcept;
}

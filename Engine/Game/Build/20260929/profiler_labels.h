#pragma once
#include "Engine/Game/Build/fingerprint.h"
#include <cstdint>

// Engine functions the profiler's stack sampler names in its report (Engine/Core/Profiling,
// labelled in Runtime/runtime_init.cpp). Anything else shows as its Ghidra address; name more in
// ReSkate.labels.tsv next to Skate.exe without rebuilding.
namespace dingosdk::game::build::v20260929::profiler_labels {
// The client game update ReSkate's tick hooks (runtime::client_tick).
inline constexpr std::uintptr_t client_update = 0x2e77190;
// The client frame job: names its end job "clientEndJob", runs the frame's update passes
// (0x4b3c750; ReSkate's tick is called from one of them) and returns when the frame is done.
// Runtime/frame_timing.cpp times it as the client frame. Once per client frame.
inline constexpr Fingerprint client_frame_job{0x2e77770, {
    0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,
    0x40,0xf3,0xff,0xff,0x48,0x81,0xec,0xc0,0x0d,0x00,0x00,0x48,0x8b,0x05,0x2e,0xcc}};
// The expression VM's entry points: scripts, UI and gameplay graphs (profile.h contracts).
inline constexpr std::uintptr_t expression_vm = 0x16d1190;
inline constexpr std::uintptr_t expression_vm_profiled = 0x16d2010;
// The main-menu expression evaluator (runtime::menu_expression).
inline constexpr std::uintptr_t menu_expression = 0x2d39900;
// Job system. The run loop pops and runs jobs until a condition holds: a worker's main loop, and
// a thread waiting on other jobs runs them meanwhile ("help while waiting"). Execute runs one
// job's function (job + 0x90); idle wait is where a worker with nothing to do blocks.
inline constexpr std::uintptr_t job_run_until_done = 0x3222210;
inline constexpr std::uintptr_t job_execute = 0x321ed00;
inline constexpr std::uintptr_t job_idle_wait = 0x3228220;
// The "GameSimulationLoop" thread's entry (started by 0x463f790).
inline constexpr std::uintptr_t simulation_loop_thread = 0x463c970;
// A job worker's main loop, and the two pops it polls with (scheduler list walk, queue pop):
// where idle workers spin (job_system.h has the spin budget).
inline constexpr std::uintptr_t job_worker_loop = 0x3221db0;
inline constexpr std::uintptr_t job_pop = 0x3226a30;
inline constexpr std::uintptr_t job_queue_pop = 0x321f690;
// Named jobs (by the name each is created with) that lead an all-threads profile in free roam.
inline constexpr std::uintptr_t expression_shader_model_build = 0x505ce50;
inline constexpr std::uintptr_t render_bundle_job = 0x3f1bc40;
inline constexpr std::uintptr_t mesh_cull = 0x4eecaa0;
inline constexpr std::uintptr_t render_dispatch_end = 0x3f053f0;
// Input: the per-frame dispatch, and the UI pointer update under it, which hit-tests the mouse
// through every input-enabled UI view's widget tree each client frame (0x4323f50 per view).
inline constexpr std::uintptr_t input_dispatch = 0x31ff690;
inline constexpr std::uintptr_t ui_pointer_update = 0x31fdbf0;
inline constexpr std::uintptr_t ui_view_pointer = 0x4323f50;
}

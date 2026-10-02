#pragma once
#include <cstdint>
#include <string>

// Engine work the developers left on the table, as `perf` toggles (Engine/Game/Build/<build>/
// engine_tweaks.h; analysis/sim-performance.md "Engine tweaks"). Each hook is only prepared at
// start and attached while its tweak is on, so off is the stock game. main_sleep and dirty_skip
// start on (measured savings, the user's choice 2026-10-02); the others start off. Not saved.
//  * gi: Enlighten GI's per-frame update throttled to a rate (it re-solves every rendered frame,
//    240 times a second at a 240 fps cap). Its multi-iteration path is left alone.
//  * mesh_tree: WorldRenderSettings.MeshCullTreeEnabled, the octree pre-cull the game ships off,
//    latched at the start of the cull scheduler so the scheduler and its jobs agree.
//  * job_wake: an idle job worker waits this long in plain sight (state 4, one CAS to wake)
//    before it sleeps, and pushers prefer such a worker to waking a sleeping one.
//  * main_sleep: the main loop's 10 ms pacing sleep on a high-resolution timer with a 0.25 ms
//    spin instead of a coarse timer and a 1 ms spin.
//  * dirty_skip: skip the per-section spinlock that only clears dirty bits when none are set.
namespace dingosdk::engine_tweaks {
enum class Tweak { gi, mesh_tree, job_wake, main_sleep, dirty_skip };
// Checks this build's functions, prepares the hooks and turns the defaults on; once.
bool install(std::uintptr_t base) noexcept;
bool available(Tweak tweak) noexcept;
// gi: updates a second (0 = every frame, stock); job_wake: microseconds (0 = stock); the
// others 1 on, 0 off.
double value(Tweak tweak) noexcept;
// What install() turns on: main_sleep and dirty_skip 1, the others 0.
double default_value(Tweak tweak) noexcept;
bool set(Tweak tweak, double value, std::string& error);
// One line on what the tweak is doing (counts since it was turned on).
std::string status(Tweak tweak);
}

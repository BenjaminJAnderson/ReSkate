#pragma once
#include "engine.h"
#include <cstdint>

namespace dingosdk::game::build::v20260929::graph_throttle {
// What a loaded graph's delayed re-post slots are bound to before the throttle
// takes them over.
inline constexpr std::uintptr_t post_seconds_fast = engine::post_event_delayed_seconds;
inline constexpr std::uintptr_t post_frames_fast = engine::post_event_delayed_frames;
}

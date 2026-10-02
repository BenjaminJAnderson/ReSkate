#pragma once
#include "local_world_controls.h"
namespace dingosdk::profile_runtime {
void collect_atmosphere_textures(const EnvironmentNode& n);
bool apply_atmosphere_controls(EnvironmentNode& n, const WorldControls& choices, bool selected);
}

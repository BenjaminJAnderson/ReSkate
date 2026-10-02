#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {
// Turns every enabled Mods/<name>/ folder that carries a layout.toc into its
// own native layout layer, between the Patch and Data layers. Install before
// native startup, while the launcher still holds entry: the engine builds its
// layers once. An empty or absent Mods folder installs nothing and succeeds.
bool start_mod_layers(std::uintptr_t base, std::string& error);

// The engine's layout manager and its layer set, as seen when the merged patch
// was mounted; null until then or when no merged patch is mounted.
struct LayoutObjects {
    void* manager{};
    void* layers{};
};
LayoutObjects layout_objects() noexcept;
}

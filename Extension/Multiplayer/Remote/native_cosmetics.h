#pragma once
#include "native_skater.h"
namespace dingosdk::multiplayer {
std::optional<Appearance> capture_cosmetics(std::uintptr_t base, const NativeFrame &, std::string &detail);
// Only called for the owned remote actor on the verified client update thread.
void apply_cosmetic_recipe(std::uintptr_t base, std::uintptr_t entity, std::uintptr_t local_entity,
                           const CosmeticRecipe &);
} // namespace dingosdk::multiplayer

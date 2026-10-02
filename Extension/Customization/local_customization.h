#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Engine/Core/Json/json.h"

namespace dingosdk::profile {
// Exact decoded recipe values. Float parameters retain their IEEE-754 bits;
// resource pointers, allocator state and transient UI flags are never saved.
struct CosmeticSlot {
    std::uint32_t slot{};
    std::string asset;
    std::vector<std::uint32_t> parameter_bits;
    bool operator==(const CosmeticSlot&) const = default;
};
struct CosmeticRecipe {
    std::uint32_t template_key{}, template_version{};
    std::vector<std::uint32_t> scalar_bits;
    std::vector<CosmeticSlot> items;
    bool operator==(const CosmeticRecipe&) const = default;
};
struct CosmeticLoadout {
    std::vector<CosmeticRecipe> recipes;
    bool operator==(const CosmeticLoadout&) const = default;
};
dingosdk::Json encode_loadout(const CosmeticLoadout&);
CosmeticLoadout decode_loadout(const dingosdk::Json&);
void validate_customization(const dingosdk::Json&);
void validate_player_card(const CosmeticLoadout&);
// Cached display metadata only. Caller still enumerates installed assets and
// applies local catalog overrides; this does not determine ownership.
dingosdk::Json item_display_metadata(const std::string& asset, const dingosdk::Json& overrides);
}

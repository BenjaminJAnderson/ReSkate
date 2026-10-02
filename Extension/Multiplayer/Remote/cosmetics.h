#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <string>
#include <vector>

namespace dingosdk::multiplayer {
inline constexpr std::uint32_t skater_recipe_key = 2759515148U, board_recipe_key = 1583459055U;
inline constexpr std::size_t max_cosmetic_slots = 64, max_cosmetic_scalars = 64,
                             max_cosmetic_parameters = 128, max_cosmetic_asset = 255;
struct CosmeticSlot {
    std::uint32_t slot{};
    std::string asset;
    std::vector<std::uint32_t> parameters;
    bool operator==(const CosmeticSlot &) const = default;
};
struct CosmeticRecipe {
    std::uint32_t key{}, version{};
    std::vector<std::uint32_t> scalars;
    std::vector<CosmeticSlot> items;
    bool operator==(const CosmeticRecipe &) const = default;
};
struct PlayerCard {
    std::uint32_t background{}, emblem{}, title{};
    bool operator==(const PlayerCard &) const = default;
};
struct Appearance {
    CosmeticRecipe skater, board;
    PlayerCard card;
    bool operator==(const Appearance &) const = default;
};
// Stand-ins for a peer's cosmetic that this PC does not have (a player's
// cosmetics mod): each replaces an item in a slot of its own category. Asset
// keys stay the same between seasons. Slots with none of these (outfits,
// costumes, hats, glasses, socks, stickers, tattoos...) are left empty.
inline constexpr std::array<std::string_view, 8> default_cosmetic_items{
    "Own_TopShirt_Gen_TshirtRelaxed_00007",         // Black I Heart Tee
    "Own_BottomPants_Gen_ChinoLoose_00001",         // Olive Chinos
    "Own_ShoeSneaker_Gen_SlipOnSkateboarding_00002", // Black Slip-ons
    "Own_DeckGraphic_Gen_Popsicle_00001",           // Natural Blank deck
    "Own_DeckGripCutout_Gen_Popsicle_00001",        // Plain grip pattern
    "Own_DeckGripColor_Gen_Popsicle_00001",         // Black grip
    "Own_Truck_Gen_Default_00001",                  // Basic trucks
    "Own_WheelColor_Gen_Classic_00009",             // White wheels
};
inline std::uint32_t cosmetic_asset_hash(std::string_view value) {
    if (value.empty())
        return 0;
    std::uint32_t hash = 5381;
    for (unsigned char c : value)
        hash = hash * 33 ^ c;
    return hash;
}
// Values are opaque native parameter bits; no addresses, model handles,
// inventory flags or saved-profile identifiers cross the connection.
bool valid_appearance(const Appearance &) noexcept;
} // namespace dingosdk::multiplayer

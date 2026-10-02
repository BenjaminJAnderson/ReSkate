#pragma once

// The game's physics material grid (MaterialGridData, the root level's
// materialgrid_win32) and the collision that addresses it.
//
// A collision part stores one packed material word: bits 0-5 flags, 6-18 the
// material slot, 19-31 the property slot. A slot indexes MaterialIndexMap, which
// names a row of MaterialProperties and of the square InteractionGrid. The engine
// reads its per-slot tables without a bounds check, so a slot the live grid does
// not have is read from whatever memory follows and the game crashes on contact.
//
// Only the root level's grid is ever live. A Studio map that authors its own
// surfaces ships a copy of that grid with rows and slots appended from the end of
// the base (slot 116 on), and bakes those slots into its collision. Two maps both
// claim slot 116, so their additions cannot simply be layered: the combiner
// appends every mod's additions to one grid, each at its own offset, and hands
// back the slot renumbering that mod's collision needs.

#include "ebx_document.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dingosdk::frostbite::material_grid {

inline constexpr std::uint32_t physicsResourceType = 0x41759364;

// How one mod's slot numbers land in the combined grid: `slots[s]` is where its
// slot s now lives. Packed words whose slots are out of range fall to slot 0.
struct SlotMap {
    std::vector<std::uint32_t> slots;

    [[nodiscard]] bool identity() const noexcept;
    [[nodiscard]] std::uint32_t remap(std::uint32_t packed) const noexcept;
    // Everything past `baseSlots` goes to the default surface: for a grid that
    // could not be combined, which beats reading past the live grid.
    [[nodiscard]] static SlotMap to_default(std::size_t baseSlots, std::size_t slots);
};

class Combiner final {
public:
    explicit Combiner(const ebx::Document& base);

    // Appends what `edit` adds to the base grid. Throws, leaving the combined
    // grid as it was, when the edit is not a copy of this base with additions.
    SlotMap add(const ebx::Document& edit);

    [[nodiscard]] std::size_t base_slots() const noexcept { return baseSlots_; }
    [[nodiscard]] std::size_t slots() const noexcept { return slots_; }
    [[nodiscard]] std::size_t added_slots() const noexcept { return slots_ - baseSlots_; }
    [[nodiscard]] std::size_t carried_instances() const noexcept { return carried_; }
    [[nodiscard]] std::size_t reused_instances() const noexcept { return reused_; }

    // The combined grid, identity (file and root guid, name) unchanged.
    [[nodiscard]] ebx::Document result() const;

private:
    ebx::Document combined_;
    std::size_t baseSlots_{}, baseRows_{}, baseNetwork_{};
    std::vector<std::uint64_t> baseIndexMap_, baseNetworkIds_;
    std::size_t slots_{}, rows_{};
    std::size_t carried_{}, reused_{};
};

// Rewrites every part's packed material in a PhysicsResource payload (version 0
// or 2) through `map`; returns how many parts changed.
std::size_t remap_physics(std::span<std::byte> payload, const SlotMap& map);

} // namespace dingosdk::frostbite::material_grid

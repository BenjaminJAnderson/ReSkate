#pragma once
#include "Engine/Game/Build/fingerprint.h"
#include <cstdint>

namespace dingosdk::game::build::v20260929::physics_tuning {
// Gameplay/SkatePhysicsTuning (SkatePhysicsTuningAsset, EBX partition
// ca6b5fd8-323d-4f5c-9eb0-e6709f4bbfc2 in win32/levels/game/bam_levelroot/bam_levelroot): one
// shared instance per process, read by every skater's physics. SkatePhysicsTuningEntity
// (client realm) publishes it in this global through its only writer, `mov [global], rcx; ret`.
inline constexpr std::uintptr_t asset_global = 0x7785dc0;
inline constexpr Fingerprint asset_setter_contract{0x47619c0, {
    0x48,0x89,0x0d,0xf9,0x43,0x02,0x03,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,
    0x48,0x8b,0xc4,0x57,0x48,0x83,0xec,0x60,0x80,0xb9,0xb8,0x0a,0x00,0x00,0x00,0x48}};
// The instance: +8 its type info, +0x18 Name (char*). Groups sit at their EBX dataOffsets
// (Footplant 0x20, Trucks 0x2a0, Jump 0x2e10, Mode 0x435c, Deck 0x43d8, Wheels 0x452c).
inline constexpr std::uintptr_t asset_type_info = 0x77ab1e0;
inline constexpr std::uintptr_t asset_type_offset = 0x8, asset_name_offset = 0x18, asset_fields_start = 0x20;
inline constexpr std::uint32_t asset_size = 0x4580;
// FloatCurve (0x28 bytes): Points (pointer to the first element; u32 count at -4, masked
// 0x7fffffff) at +0x18, MinX +0x20, MaxX +0x24; FloatCurvePoint is 0x1c bytes. Curve
// pointers in the asset may carry a tag in bit 2.
inline constexpr std::uintptr_t curve_points_offset = 0x18, curve_min_offset = 0x20, curve_max_offset = 0x24;
inline constexpr std::uint32_t curve_point_size = 0x1c;
// About 60 fields (Feet, Jump, Steering, Manual, Slide, Push, Friction.SlideVsNormal,
// Wheels.Softest*) are copied into a heap block (0xc98 bytes) of each skater's physics
// controller when it is created: the controller is the skater component's physics core
// (component +0x70, bail_core_vtable), and its +0x418 points at the block. void
// refresh(block) copies them again from the asset (it reads only the asset and static
// globals, and assigns the block's curve copies, so it may run again on a live block).
inline constexpr std::uintptr_t core_tuning_block = 0x418;
inline constexpr Fingerprint refresh_block_contract{0x4750f30, {
    0x40,0x53,0x48,0x83,0xec,0x20,0xc5,0xfa,0x10,0x05,0x92,0x12,0x05,0x03,0xc5,0xfa,
    0x11,0x81,0xd0,0x07,0x00,0x00,0xc5,0xfa,0x10,0x0d,0x7a,0x12,0x05,0x03,0xc5,0xfa}};
} // namespace dingosdk::game::build::v20260929::physics_tuning

#pragma once
#include <array>
#include <cstdint>

// AI skaters spawned from the player blueprint.
namespace dingosdk::game::build::v20260929::ai_skaters {
// Entity bus: create_bus(owner, parent, 0, 0, 0, 0) creates a child bus and acquire_bus adds
// a reference (released with skater_entities::release_reference).
inline constexpr std::uintptr_t create_bus = 0x2b7eb40;
inline constexpr std::array<unsigned char, 19> create_bus_prefix{
    0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x48,0x48,0x8b,0xe9,0x41,0x8b,0xd9,0x48,
    0x8b,0x49,0x48};
inline constexpr std::uintptr_t acquire_bus = 0x2b7e530;
inline constexpr std::array<unsigned char, 13> acquire_bus_prefix{
    0xb8,0x01,0x00,0x00,0x00,0xf0,0x0f,0xc1,0x41,0x40,0xff,0xc0,0xc3};
// Writes the source data+0xe1 flag into the entity's +0x578 property binding.
inline constexpr std::uintptr_t set_property_binding = 0x544020;
inline constexpr std::array<unsigned char, 15> set_property_binding_prefix{
    0x88,0x54,0x24,0x10,0x48,0x83,0xec,0x28,0x48,0x81,0xc1,0x78,0x05,0x00,0x00};
// Skater entity vtable slot +0x458. Reads the +0x578 binding unchecked once the skater's physics exists.
inline constexpr std::uintptr_t binding_reader = 0x53eba0;
// The +0x578 load inside binding_reader.
inline constexpr std::uintptr_t binding_read_site = 0x53ebbf;
inline constexpr std::array<unsigned char, 7> binding_read_site_prefix{
    0x48,0x8d,0x8b,0x78,0x05,0x00,0x00};
// Skater component physics setter fed from source data+0xe0 after placement.
inline constexpr std::uintptr_t physics_source_setter = 0xf9cf40;
inline constexpr std::array<unsigned char, 17> physics_source_setter_prefix{
    0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x81,0xa0,0x00,0x00,
    0x00};
// Expected code of skater_entities::physics_cb_setter.
inline constexpr std::array<unsigned char, 7> physics_cb_setter_prefix{
    0x88,0x91,0xcb,0x00,0x00,0x00,0xc3};
// Native named-asset lookup that searches a domain and its parents. Reference only.
inline constexpr std::uintptr_t asset_domain_lookup = 0x41b5780;

// EATBrainTree component (collection slot 12). brain_tree_factory creates the bt_default
// instance (+0x60) through holder +0x40 (brain_tree_holder_vtable); brain_tree_update (vtable
// +0x138) ticks the tree only while run flag +0x70 is set.
inline constexpr std::uintptr_t brain_tree_vtable = 0x6252b20;
inline constexpr std::uintptr_t brain_tree_update = 0x16f5560;
inline constexpr std::uintptr_t brain_tree_holder_vtable = 0x6252d60;
// Reference only.
inline constexpr std::uintptr_t brain_tree_factory = 0x16f23a0;

// SkaterAIComponent (collection slot 14, created by skater_ai_factory, 0x50 bytes). Its update
// (vtable +0x138) creates the 0x2c90-byte AI skater controller at +0x40 (ai_controller_create,
// bound to the skater's physics body) and runs it (ai_controller_run) only while +0x48 is set.
inline constexpr std::uintptr_t skater_ai_vtable = 0x6170180;
inline constexpr std::uintptr_t skater_ai_update = 0xf97630;
inline constexpr std::array<unsigned char, 18> skater_ai_update_prefix{
    0x40,0x53,0x57,0x48,0x83,0xec,0x38,0x80,0x79,0x48,0x00,0x48,0x8b,0xf9,0x48,0x8b,
    0x59,0x40};
// Reference only.
inline constexpr std::uintptr_t skater_ai_factory = 0xf881d0;
// Reference only.
inline constexpr std::uintptr_t ai_controller_create = 0x10373a0;
// Reference only.
inline constexpr std::uintptr_t ai_controller_run = 0x10461e0;
}

#pragma once
#include <array>
#include <cstdint>

namespace dingosdk::game::build::v20260929::native_player_ui {
// Hooked functions.
// Native player map tick (map display interface vtable +8); its map
// projection inputs are reused for remote players.
inline constexpr std::uintptr_t map_tick = 0x685e40;
inline constexpr std::array<unsigned char, 14> map_tick_prefix{
    0x40, 0x55, 0x53, 0x57, 0x41, 0x56, 0x48, 0x8d, 0xac, 0x24, 0x18, 0xfe, 0xff, 0xff};
// First-256-byte move helper folded across many UI types.
inline constexpr std::uintptr_t ui_copy = 0x667c90;
inline constexpr std::array<unsigned char, 13> ui_copy_prefix{
    0xc5, 0xfc, 0x10, 0x02, 0xc5, 0xfc, 0x11, 0x01, 0xc5, 0xfc, 0x10, 0x4a, 0x20};
// Map display destructor.
inline constexpr std::uintptr_t map_destroy = 0x66a730;
inline constexpr std::array<unsigned char, 13> map_destroy_prefix{
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9};
// Player-location manager shutdown.
inline constexpr std::uintptr_t location_shutdown = 0x672230;
inline constexpr std::array<unsigned char, 13> location_shutdown_prefix{
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x1d, 0x33, 0x17, 0xb8, 0x06};

// Player-location manager functions.
inline constexpr std::uintptr_t location_create = 0x676a30;
inline constexpr std::array<unsigned char, 13> location_create_prefix{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x58};
// Manager vtable +0x18.
inline constexpr std::uintptr_t location_update = 0x678440;
inline constexpr std::array<unsigned char, 14> location_update_prefix{
    0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x40, 0x0f, 0xb7, 0xda, 0x48, 0x8b, 0xf9};
inline constexpr std::uintptr_t location_release = 0x677350;
inline constexpr std::array<unsigned char, 13> location_release_prefix{
    0x40, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20, 0x44, 0x0f, 0xb7, 0xfa};
// Manager vtable +0.
inline constexpr std::uintptr_t location_retain = 0x6763e0;
inline constexpr std::array<unsigned char, 14> location_retain_prefix{
    0x48, 0x83, 0xec, 0x28, 0x0f, 0xb7, 0xd2, 0x48, 0x81, 0xc1, 0xc8, 0x00, 0x00, 0x00};
// Appends an entry to the render message's location list.
inline constexpr std::uintptr_t list_append = 0x67ca10;
inline constexpr std::array<unsigned char, 14> list_append_prefix{
    0x40, 0x55, 0x56, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xec, 0x28, 0x4c, 0x8b, 0x31};
// Projects a world position onto the map.
inline constexpr std::uintptr_t map_project = 0x678dd0;
inline constexpr std::array<unsigned char, 9> map_project_prefix{0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x78};
// The map tick's call into ui_copy, and its return address.
inline constexpr std::uintptr_t copy_call = 0x6860dc;
inline constexpr std::array<unsigned char, 9> copy_call_prefix{0xe8, 0xaf, 0x1b, 0xfe, 0xff, 0x48, 0x8b, 0x45, 0x40};
inline constexpr std::uintptr_t copy_return = 0x6860e1;

// Globals and vtables
// Pointer to the player-location manager.
inline constexpr std::uintptr_t location_manager = 0x71f3970;
inline constexpr std::uintptr_t location_manager_vtable = 0x60cf5e8;
// Map display object and its interface at +0x40.
inline constexpr std::uintptr_t map_display_vtable = 0x60cf750;
inline constexpr std::uintptr_t map_display_interface_vtable = 0x60cf848;
}

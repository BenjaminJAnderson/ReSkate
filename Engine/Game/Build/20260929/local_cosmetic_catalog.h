#pragma once
#include <cstdint>

// Retail OwnableData catalog bridge. Related native code: OwnableData decoder
// 141e2e350 (tag 0xba, field 23 -> rarityId +160); protobuf reader constructor
// 1420c8450; trivial CoreAllocator adapter 1401f4230; inventory manager
// constructor 14094dc10 with its normal subscriptions 1406bc560; 140695b00
// stores the borrowed OwnableData pointer at +0x1d0; default-owned
// subscription callback 1406a0c30 -> 1406b6870.
namespace dingosdk::game::build::v20260929::local_cosmetic_catalog {
// Shared control block vtable for a native OwnableData message.
inline constexpr std::uintptr_t message_control_vtable = 0x60736a8;
// OwnableData (cosmetic UI) manager.
inline constexpr std::uintptr_t ownable_manager_vtable = 0x60d2600;
// Ownable inventory manager.
inline constexpr std::uintptr_t inventory_manager_vtable = 0x60d20e8;
// Pointer to the OwnableData manager.
inline constexpr std::uintptr_t ownable_manager = 0x71f5ee0;
// Pointer to the ownable inventory manager.
inline constexpr std::uintptr_t inventory_manager = 0x71f4278;
}

#pragma once
#include <array>
#include <cstdint>

// Offline location travel catalog. The owner pointer is engine::location_owner;
// the native hooks are in location_travel.
namespace dingosdk::game::build::v20260929::local_location_travel {
// Location travel owner vtable.
inline constexpr std::uintptr_t owner_vtable = 0x60d1e48;
// Data model TypeInfos of a travel location and an access point.
inline constexpr std::uintptr_t location_type = 0x7251610;
inline constexpr std::uintptr_t access_point_type = 0x7251828;
// Event types dispatched once the catalogs are published.
inline constexpr std::array<std::uintptr_t, 2> notify_event_types{0x72519e8, 0x7251a40};
// Type records (uint32 hash at +0, uint16 size at +6) of the 0x28-byte access
// point, the 0xc0-byte location and two 1-byte types.
inline constexpr std::uintptr_t access_point_record = 0x7f67560;
inline constexpr std::uintptr_t location_record = 0x7f676b0;
inline constexpr std::array<std::uintptr_t, 2> byte_records{0x7f67330, 0x7f672c0};
}

#pragma once
#include <cstdint>

// Neighborhood (district) progression records. The native response conversion
// 140346f80 hashes ProgressionId, then 14065c280 copies that hash into the base
// record (+0x38).
namespace dingosdk::game::build::v20260929::local_neighborhood {
// Neighborhood record model TypeInfo.
inline constexpr std::uintptr_t neighborhood_type = 0x72517d0;
// Reflected neighborhood record type: uint32 name hash (0xdcf37866) and uint16 size.
inline constexpr std::uintptr_t neighborhood_record_hash = 0x7f675d0;
inline constexpr std::uintptr_t neighborhood_record_size = 0x7f675d6;
// Reflected field records: Bool at +0xbd (hash 0x124511ee) and the UInt32
// progression hash at +0x38 (hash 0x9444f3fc).
inline constexpr std::uintptr_t neighborhood_flag_field = 0x82f7df8;
inline constexpr std::uintptr_t neighborhood_progression_field = 0x82d0028;
}

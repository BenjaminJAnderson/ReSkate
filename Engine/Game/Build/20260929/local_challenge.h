#pragma once
#include <cstdint>

// Local challenge catalog and completion delivery. Related native code: the
// real activity catalog's anonymous model allocation 1417e18c0; progress
// hydration 140735900 (uses GetModelField); OnAmpActivityEndRecieved_Global
// sender 1406de170 (rejects an empty EID); 1415b4d90 places a graph's page
// table after its aligned frame size; the criterion value copy constructor
// 14591b5e0 treats its array fields as reference-counted ValueRefs.
namespace dingosdk::game::build::v20260929::local_challenge {
// Challenge (activity) owner system.
inline constexpr std::uintptr_t challenge_owner_vtable = 0x6089158;
// Pointer to the game context that owns the challenge system.
inline constexpr std::uintptr_t challenge_context = 0x73c7cb0;
// uint32 registered offset of the challenge owner in that context.
inline constexpr std::uintptr_t challenge_owner_offset = 0x7201838;
// uint16 reflected sizes of ChallengeCounts and ActivityData.
inline constexpr std::uintptr_t challenge_counts_size = 0x7f15396;
inline constexpr std::uintptr_t activity_data_size = 0x7f16196;
// Reflected ActivityData criterion-group field record (hash 0x571954a7, +0x30, UInt64).
inline constexpr std::uintptr_t activity_criteria_field = 0x82dd3c0;
// Model TypeInfos of the challenge catalog.
inline constexpr std::uintptr_t activity_data_type = 0x7223880;
inline constexpr std::uintptr_t activity_kind_type = 0x72238e8;
inline constexpr std::uintptr_t criterion_group_type = 0x7223940;
inline constexpr std::uintptr_t criterion_type = 0x72239f0;
// ActivityListUpdated event type.
inline constexpr std::uintptr_t activity_list_updated_type = 0x7223fe8;
}

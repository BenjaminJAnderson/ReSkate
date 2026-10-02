#pragma once
#include <cstdint>

namespace dingosdk::game::build::v20260929::buildkit_limits {
// DingoObjectPersistenceSettings vtable (Build Kit object budget and cursor radius).
inline constexpr std::uintptr_t object_persistence_settings_vtable = 0x61259a0;
// DingoObjectPersistenceSettings TypeInfo; also its settings-manager lookup key.
inline constexpr std::uintptr_t object_persistence_settings_type = 0x723a510;
}

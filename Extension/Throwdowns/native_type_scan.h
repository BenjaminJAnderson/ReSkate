#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace dingosdk::multiplayer {
// A data-defined (EBX) type wanted by its runtime hash (== EBX TypeNameHash for events) and
// type kind (record flags bits 5-9: 2 = class, 24 = script function).
struct NativeTypeQuery { std::uint32_t hash{}; std::uintptr_t object{}; std::uint8_t kind = 2; };
// Fills `object` for every query it finds on the heap and returns how many are still
// missing. ~0.15 s in the world; never call it during level load (it then finds nothing
// and takes minutes). background: for a worker thread only; the scan runs at background
// priority, yields between regions and skips pages outside the working set.
std::size_t find_native_types(std::span<NativeTypeQuery> wanted, bool background = false) noexcept;
}

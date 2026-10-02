#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {
inline constexpr std::uint64_t morph_render_heap_bytes = 256ull * 1024 * 1024;

// Install while the launcher still holds executable entry, after Detours hook service init.
// Raises only DingoMorph's two render heap capacities at construction; it
// does not resize live heaps, change other graphics pools, or bypass allocation
// bounds checks. Capacity is per backing heap, not a total resident-memory cap.
bool start_morph_memory_pool(std::uintptr_t base, std::string& error);
} // namespace dingosdk

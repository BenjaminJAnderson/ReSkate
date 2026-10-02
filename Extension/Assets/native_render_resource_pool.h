#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dingosdk {
// The native resource handle stores this index in 16 bits. 65,536 entries is
// therefore the largest compatible pool (valid indices are 0..65,535).
inline constexpr std::size_t native_render_resource_capacity = 65'536;
inline constexpr std::size_t native_render_resource_record_size = 0xf0;

// Installs before executable entry. The native fixed-vector constructor keeps
// constructing its records normally, but its backing storage and all parallel
// per-resource arrays are redirected to process-lifetime allocations.
bool start_native_render_resource_pool(std::uintptr_t base, std::string& error);
} // namespace dingosdk

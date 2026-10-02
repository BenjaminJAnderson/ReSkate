#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
// Reads the executing graph's resource and its key (resource+0x10). Used to
// recognize authored graphs by key (board wear, script error attribution).
bool challenge_trace_identity(std::uintptr_t vm, std::uintptr_t& resource, std::uint32_t& key) noexcept;
}

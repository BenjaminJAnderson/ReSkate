#include "Extension/Profile/runtime_internal.h"
#include "local_challenge_trace.h"

namespace dingosdk::profile_runtime {
bool challenge_trace_identity(std::uintptr_t vm, std::uintptr_t& resource, std::uint32_t& key) noexcept {
    // The interpreter owns both objects for this synchronous call. A guarded
    // direct read keeps the common non-challenge path free of process syscalls.
    __try {
        if (vm < 0x10000) return false;
        resource = *reinterpret_cast<const std::uintptr_t*>(vm + 0x38);
        if (resource < 0x10000) return false;
        key = *reinterpret_cast<const std::uint32_t*>(resource + 0x10);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
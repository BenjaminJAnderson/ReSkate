#pragma once
#include <Windows.h>
#include <cstddef>

namespace dingosdk {
// Win32 failures from Detours are preserved; negative values describe the
// SDK-owned lifecycle. No vendor status codes escape through feature code.
enum HookStatus : LONG {
    HookOk = NO_ERROR,
    HookNotInitialized = -1,
    HookAlreadyInitialized = -2,
    HookAlreadyPrepared = -3,
    HookDisabled = -4,
    HookNotFound = -5,
    HookNotExecutable = -6,
    HookOutOfMemory = ERROR_NOT_ENOUGH_MEMORY,
    HookProtectionFailed = ERROR_ACCESS_DENIED,
    HookUnsupportedFunction = ERROR_INVALID_BLOCK,
};

// A prepared original is a permanent x64 relay. Detours changes its pointer
// transactionally, so originals published into atomics or retained by native
// callbacks stay callable before attach and after detach. Only Detours performs
// instruction relocation and target patching. Preparation never patches target.
HookStatus WINAPI hook_initialize();
// original may be null when the replacement never forwards to the target.
HookStatus WINAPI hook_prepare(void* target, void* replacement, void** original = nullptr);
HookStatus WINAPI hook_enable(void* target);
HookStatus WINAPI hook_disable(void* target);
HookStatus WINAPI hook_remove(void* target);
HookStatus WINAPI hook_queue_enable(void* target);
HookStatus WINAPI hook_apply_queued();
HookStatus WINAPI hook_shutdown();
const char* WINAPI hook_status_string(HookStatus status);
// Captured on the calling thread; formatting/logging happens only after Detours
// has resumed enlisted threads. Read before issuing another hook operation.
struct HookFailure { const char* operation{"none"}; LONG error{}; DWORD thread_id{}; };
HookFailure hook_last_failure() noexcept;

struct HookStatistics { std::size_t prepared{}, attached{}, retired{}; };
HookStatistics hook_statistics();
}

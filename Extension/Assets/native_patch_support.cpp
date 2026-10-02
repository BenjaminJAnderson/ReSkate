#include "native_patch_support.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/native_patch_support.h"

#include <Windows.h>
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include <bcrypt.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace dingosdk {
namespace {
using EnvelopeVerifier = std::uint8_t* (*)(std::uint8_t*, std::uint64_t*);
using SignatureVerifier = decltype(&BCryptVerifySignature);
std::atomic<EnvelopeVerifier> original_verifier{};
std::atomic<SignatureVerifier> original_signature_verifier{};
std::atomic<std::uintptr_t> signature_return_address{};
std::uintptr_t installed_base{};
std::mutex install_mutex;
namespace patch = addr::native_patch_support;
constexpr std::uintptr_t verifier_rva = patch::verifier;
constexpr std::uintptr_t signature_import_rva = patch::signature_import;
constexpr std::uintptr_t signature_return_rva = patch::signature_return;
constexpr NTSTATUS invalid_signature = static_cast<NTSTATUS>(0xc000a000U);

struct DataVerification {
    const std::uint8_t* signature{};
    std::uint64_t payload_size{};
};
thread_local DataVerification active_verification;

class DataVerificationScope {
public:
    DataVerificationScope(const std::uint8_t* raw, std::uint64_t length) noexcept
        : previous_(active_verification) {
        active_verification = raw
            ? DataVerification{raw + 8, length - native_patch_envelope_size}
            : DataVerification{};
    }
    ~DataVerificationScope() { active_verification = previous_; }
    DataVerificationScope(const DataVerificationScope&) = delete;
    DataVerificationScope& operator=(const DataVerificationScope&) = delete;
private:
    DataVerification previous_;
};

// Independent implementation of edited-container signature acceptance, inspired
// by InitfsTools' BCrypt proxy. This hook retains the actual CNG result outside
// the inspected data verifier; no certificate-query or termination API changes.
__declspec(noinline) NTSTATUS WINAPI verify_data_signature(BCRYPT_KEY_HANDLE key, void* padding,
    PUCHAR hash, ULONG hash_size, PUCHAR signature, ULONG signature_size, ULONG flags) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto original = original_signature_verifier.load(std::memory_order_acquire);
    const auto result = original(key, padding, hash, hash_size, signature, signature_size, flags);
    if (result != invalid_signature ||
        caller != signature_return_address.load(std::memory_order_acquire) ||
        !active_verification.signature || signature != active_verification.signature ||
        !key || !padding || !hash || hash_size != 20 || signature_size != 256 ||
        flags != BCRYPT_PAD_PKCS1) return result;

    logging::log(logging::Level::info, logging::Channel::assets,
        "Edited InitFS/package data accepted after signature mismatch ({} payload bytes).",
        active_verification.payload_size);
    return 0;
}

bool read_memory(const void* source, void* destination, std::size_t size) noexcept {
    SIZE_T read{};
    return ReadProcessMemory(GetCurrentProcess(), source, destination, size, &read) && read == size;
}

bool validate_contract(std::uintptr_t base) noexcept {
    if (!base || base > std::numeric_limits<std::uintptr_t>::max() - supported_build::game_image_size)
        return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read_memory(reinterpret_cast<const void*>(base), &dos, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 0x1000 ||
        !read_memory(reinterpret_cast<const void*>(base + dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage != supported_build::game_image_size) return false;
    for (const auto& contract : patch::contracts) {
        std::array<std::uint8_t, patch::verifier_bytes.size()> actual{};
        if (!read_memory(reinterpret_cast<const void*>(base + contract.rva), actual.data(), contract.bytes.size()) ||
            std::memcmp(actual.data(), contract.bytes.data(), contract.bytes.size()) != 0) return false;
    }
    return true;
}

bool writable_length(std::uint64_t* length) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(length);
    if (!address || address % alignof(std::uint64_t) != 0) return false;
    MEMORY_BASIC_INFORMATION page{};
    if (!VirtualQuery(length, &page, sizeof(page)) || page.State != MEM_COMMIT ||
        (page.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const auto protection = page.Protect & 0xff;
    const auto offset = address - reinterpret_cast<std::uintptr_t>(page.BaseAddress);
    return offset <= page.RegionSize && sizeof(*length) <= page.RegionSize - offset &&
        (protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
         protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY);
}

// An aligned compare/exchange commits the single out-parameter only if it has
// not changed since classification. SEH handles a concurrent protection change;
// no partial write, no shortened-length fallback, and no source-buffer writes.
bool commit_length(std::uint64_t* length, std::uint64_t expected) noexcept {
    __try {
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(length),
            static_cast<LONG64>(expected - native_patch_envelope_size),
            static_cast<LONG64>(expected))) == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint8_t* verify_envelope(std::uint8_t* raw, std::uint64_t* length) {
    const auto incoming_error = GetLastError();
    std::uint64_t count{};
    std::array<std::uint8_t, native_patch_envelope_size> header{};
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto length_address = reinterpret_cast<std::uintptr_t>(length);
    const bool native_envelope = read_memory(length, &count, sizeof(count)) &&
        count > native_patch_envelope_size &&
        count <= std::numeric_limits<std::uintptr_t>::max() - address &&
        // Native callers use a separate stack local; never change envelope/body
        // bytes through an aliased length pointer, even for otherwise valid data.
        (length_address >= address + count ||
            (length_address < address && address - length_address >= sizeof(count))) &&
        read_memory(raw, header.data(), header.size()) &&
        is_native_data_envelope(header, count, address);
    if (native_envelope && is_unsigned_native_patch_envelope(header, count, address) &&
        writable_length(length) && commit_length(length, count)) {
        SetLastError(incoming_error);
        return reinterpret_cast<std::uint8_t*>(address + native_patch_envelope_size);
    }
    const auto original = original_verifier.load(std::memory_order_acquire);
    // Tie acceptance to this particular signature buffer and this thread, even
    // when the verifier re-enters or other engine work invokes CNG concurrently.
    const DataVerificationScope scope(native_envelope ? raw : nullptr, count);
    SetLastError(incoming_error);
    return original(raw, length);
}
} // namespace

bool start_native_patch_support(std::uintptr_t base, std::string& error) {
    std::lock_guard lock(install_mutex);
    error.clear();
    if (installed_base) {
        if (installed_base == base) return true;
        error = "Native patch support is already installed on another image";
        return false;
    }
    if (!validate_contract(base)) {
        error = "Native package-envelope contract does not match the supported game";
        return false;
    }
    // bcrypt.h does not mark this declaration dllimport in this build. Taking
    // &BCryptVerifySignature therefore produces our linker jump thunk, whereas
    // Skate's IAT contains the resolved Windows export. Compare resolved exports
    // so a normal loader binding does not fail this contract check.
    const auto bcrypt_module = GetModuleHandleW(L"bcrypt.dll");
    const auto signature_export = bcrypt_module ? reinterpret_cast<SignatureVerifier>(
        GetProcAddress(bcrypt_module, "BCryptVerifySignature")) : nullptr;
    if (!signature_export) {
        error = "Cannot resolve the loaded BCryptVerifySignature export";
        return false;
    }
    SignatureVerifier signature_import{};
    if (!read_memory(reinterpret_cast<const void*>(base + signature_import_rva),
            &signature_import, sizeof(signature_import)) ||
        signature_import != signature_export) {
        error = "Native data signature import does not match BCryptVerifySignature";
        return false;
    }
    auto* target = reinterpret_cast<void*>(base + verifier_rva);
    auto* signature_target = reinterpret_cast<void*>(signature_import);
    EnvelopeVerifier original{};
    const auto created = hook_prepare(target, reinterpret_cast<void*>(&verify_envelope),
        reinterpret_cast<void**>(&original));
    if (created != HookOk) {
        error = "Cannot create native patch support (Detours hook service status " + std::to_string(created) + ")";
        return false;
    }
    if (!original) {
        hook_remove(target);
        error = "Native patch verifier trampoline is unavailable";
        return false;
    }
    SignatureVerifier original_signature{};
    const auto signature_created = hook_prepare(signature_target,
        reinterpret_cast<void*>(&verify_data_signature), reinterpret_cast<void**>(&original_signature));
    if (signature_created != HookOk || !original_signature) {
        if (signature_created == HookOk) hook_remove(signature_target);
        hook_remove(target);
        error = "Cannot create native data signature support (Detours hook service status " +
            std::to_string(signature_created) + ")";
        return false;
    }
    original_verifier.store(original, std::memory_order_release);
    original_signature_verifier.store(original_signature, std::memory_order_release);
    signature_return_address.store(base + signature_return_rva, std::memory_order_release);

    // Both hooks become visible in one Detours transaction before game startup.
    auto enabled = hook_queue_enable(target);
    if (enabled == HookOk) enabled = hook_queue_enable(signature_target);
    if (enabled == HookOk) enabled = hook_apply_queued();
    if (enabled != HookOk) {
        hook_remove(signature_target);
        hook_remove(target);
        signature_return_address.store(0, std::memory_order_release);
        original_signature_verifier.store(nullptr, std::memory_order_release);
        original_verifier.store(nullptr, std::memory_order_release);
        error = "Cannot enable native InitFS/package support (Detours hook service status " +
            std::to_string(enabled) + ")";
        return false;
    }
    installed_base = base;
    return true;
}
} // namespace dingosdk

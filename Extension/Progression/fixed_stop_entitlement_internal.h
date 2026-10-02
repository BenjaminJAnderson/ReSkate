#pragma once
#include "fixed_stop_entitlement_provider.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/image_identity.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Provider state and the native result ABI shared by the fixed_stop_*.cpp files.
namespace dingosdk::fixed_stop_detail {
inline constexpr std::size_t maximum_identifier_bytes = 255;
inline constexpr std::size_t maximum_group_members = 64;
inline constexpr std::size_t result_element_size = 32;

struct NativeResultVector {
    void* data{};
    std::uint32_t metadata{};
    std::uint8_t state{};
    std::array<std::byte, 3> padding{};
};

struct NativeResultElement {
    void* successful_ids{};
    void* eid_string{};
    void* failed_ids{};
    std::uint8_t owned{};
    std::array<std::byte, 7> padding{};
};

struct NativeError {
    std::array<std::uint64_t, 5> words{};
};

static_assert(sizeof(NativeResultVector) == 16);
static_assert(sizeof(NativeResultElement) == result_element_size);
static_assert(offsetof(NativeResultElement, eid_string) == 8);
static_assert(offsetof(NativeResultElement, failed_ids) == 16);
static_assert(offsetof(NativeResultElement, owned) == 24);
static_assert(sizeof(NativeError) == 40);

using ResultVectorInit = void* (*)(NativeResultVector*);
using ArrayAllocate = void* (*)(void*, std::size_t, std::size_t, void*);
using ResultElementInit = void* (*)(NativeResultElement*);
using AssignString = void* (*)(void*, const char*, std::uint32_t);
using ConstructError = void* (*)(NativeError*, std::uint32_t, const char*);
using CompletePromise = void (*)(void*, NativeError*, NativeResultVector*);
using DestroyVector = void (*)(NativeResultVector*);
using DestroyString = void (*)(void*);
using AssignFuture = void* (*)(void*, void*);
using EidKey = const void* (*)(int);
using FindMemberEid = const char* (*)(void*, const void*);
using ImageVerifier = bool (*)(std::uintptr_t) noexcept;

struct NativeApi {
    ResultVectorInit result_vector_init{};
    ArrayAllocate array_allocate{};
    ResultElementInit result_element_init{};
    AssignString assign_string{};
    ConstructError construct_error{};
    CompletePromise complete_promise{};
    DestroyVector destroy_vector{};
    DestroyString destroy_string{};
    AssignFuture assign_future{};
    EidKey eid_key{};
    FindMemberEid find_member_eid{};
    std::uintptr_t client_game_manager_global{};
};

using NativeResolver = NativeApi (*)(std::uintptr_t) noexcept;

enum class ProviderGate : unsigned char {
    not_attempted,
    image_mismatch,
    entry_fingerprint_mismatch,
    group_contract_fingerprint_mismatch,
    result_vector_init_fingerprint_mismatch,
    array_allocate_fingerprint_mismatch,
    result_element_init_fingerprint_mismatch,
    assign_string_fingerprint_mismatch,
    construct_error_fingerprint_mismatch,
    complete_promise_fingerprint_mismatch,
    destroy_vector_fingerprint_mismatch,
    destroy_string_fingerprint_mismatch,
    assign_future_fingerprint_mismatch,
    eid_key_fingerprint_mismatch,
    find_member_eid_fingerprint_mismatch,
    native_resolution_failed,
    prepared,
};

NativeApi resolve_native_api(std::uintptr_t base) noexcept;

struct ProviderState {
    ProviderState() noexcept
        : verify_image(&supported_build::running_image_matches),
          resolve_native(&resolve_native_api) {}

    std::mutex initialization_mutex;
    std::atomic<bool> prepared{};
    std::atomic<bool> enabled{};
    std::atomic<FixedStopOwnership> ownership{};
    std::atomic<ProviderGate> gate{ProviderGate::not_attempted};
    std::atomic<std::uintptr_t> base{};
    ImageVerifier verify_image{};
    NativeResolver resolve_native{};
    NativeApi native{};
    bool attempted{};
    std::atomic<std::uint64_t> attempts{};
    std::atomic<std::uint64_t> completions{};
    std::atomic<std::uint64_t> mixed_completions{};
    std::atomic<std::uint64_t> forwarded_unprepared{};
    std::atomic<std::uint64_t> forwarded_disabled{};
    std::atomic<std::uint64_t> forwarded_malformed_request{};
    std::atomic<std::uint64_t> forwarded_no_fixed_stop{};
    std::atomic<std::uint64_t> forwarded_unsupported_batch{};
    std::atomic<std::uint64_t> forwarded_no_group{};
    std::atomic<std::uint64_t> forwarded_malformed_group{};
    std::atomic<std::uint64_t> forwarded_no_eid{};
    std::atomic<std::uint64_t> forwarded_allocation_failure{};
    std::atomic<std::uint64_t> forwarded_reentrant{};
    std::atomic<std::uint64_t> native_exceptions{};
    std::mutex latest_mutex;
    std::uint32_t latest_requested_count{};
    std::uint32_t latest_fixed_stop_count{};
    std::uint32_t latest_eid_count{};
    bool latest_mixed{};
    FixedStopEntitlementAttempt latest_attempt{
        FixedStopEntitlementAttempt::forwarded_unprepared};
};

ProviderState& fixed_stop_entitlement_provider_state();

struct PreserveLastError {
    DWORD value{GetLastError()};
    void capture_current() noexcept { value = GetLastError(); }
    ~PreserveLastError() { SetLastError(value); }
};

inline bool valid_range(std::uintptr_t address, std::size_t size) noexcept {
    return address >= 0x10000 && size && size <= memory::highest_user_address &&
        address <= memory::highest_user_address - size;
}

bool read_c_string(std::uintptr_t address,
    std::array<char, maximum_identifier_bytes + 1>& output,
    std::size_t& length) noexcept;

using EidBuffer = std::array<char, maximum_identifier_bytes + 1>;

// fixed_stop_native_result.cpp
bool gather_group_eids(const NativeApi& api,
    std::array<EidBuffer, maximum_group_members>& eids,
    std::uint32_t& eid_count,
    FixedStopEntitlementAttempt& failure);
bool complete_request(const NativeApi& api, void* future_out, void* promise,
    const std::array<EidBuffer, maximum_group_members>& eids,
    std::uint32_t eid_count, const std::vector<std::string>& owned_ids);
} // namespace dingosdk::fixed_stop_detail

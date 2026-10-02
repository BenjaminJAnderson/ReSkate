#include "entitlement_request_hook.h"
#include "entitlement_request_hook_internal.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/image_identity.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/entitlement_trace.h"
#include "fixed_stop_entitlement_provider.h"

#include <Windows.h>
#include "Engine/Core/Hooks/hooks.h"
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace dingosdk {
namespace {

using entitlement_request_trace::detail::RequestRecord;
using entitlement_request_trace::detail::TraceRoute;
using entitlement_request_trace::detail::read_pointer_array_request;
using entitlement_request_trace::detail::read_service_request;

using CheckUsersEntitlements = void* (*)(void*, const void*, void*);
using ProgressionRequestBuilder = void (*)(std::uintptr_t, const void*, const void*);
using EntitlementResultReducer = void (*)(void*, const void*, const void*);
using ImageVerifier = bool (*)(std::uintptr_t) noexcept;

constexpr std::uintptr_t trace_image_size = supported_build::game_image_size;
namespace trace = addr::entitlement_trace;
constexpr std::uintptr_t check_entitlements_rva = trace::check_entitlements;
constexpr std::uintptr_t request_builder_rva = trace::request_builder;
constexpr std::uintptr_t result_reducer_rva = trace::result_reducer;
constexpr std::uintptr_t continuation_callback_rva = trace::continuation_callback;
constexpr std::uintptr_t entry_promise_capture_rva = trace::entry_promise_capture;
constexpr std::uintptr_t entry_builder_callsite_rva = trace::entry_builder_call;
constexpr std::uintptr_t entry_builder_return_rva = trace::entry_builder_return;
constexpr std::uintptr_t callback_reducer_callsite_rva = trace::callback_reducer_call;
constexpr std::uintptr_t empty_error_string_rva = addr::engine::empty_cstring;
constexpr std::size_t request_record_capacity = 256;

constexpr auto check_entitlements_fingerprint = trace::check_entitlements_prefix;
constexpr auto request_builder_fingerprint = trace::request_builder_prefix;
constexpr auto result_reducer_fingerprint = trace::result_reducer_prefix;
constexpr auto continuation_callback_fingerprint = trace::continuation_callback_prefix;
constexpr auto entry_promise_capture_fingerprint = trace::entry_promise_capture_prefix;
constexpr auto entry_builder_callsite_fingerprint = trace::entry_builder_call_prefix;
constexpr auto callback_reducer_callsite_fingerprint = trace::callback_reducer_call_prefix;

static_assert(check_entitlements_rva + check_entitlements_fingerprint.size() <= trace_image_size);
static_assert(request_builder_rva + request_builder_fingerprint.size() <= trace_image_size);
static_assert(result_reducer_rva + result_reducer_fingerprint.size() <= trace_image_size);
static_assert(continuation_callback_rva + continuation_callback_fingerprint.size() <= trace_image_size);
static_assert(entry_promise_capture_rva + entry_promise_capture_fingerprint.size() <= trace_image_size);
static_assert(entry_builder_callsite_rva + entry_builder_callsite_fingerprint.size() ==
    entry_builder_return_rva);
static_assert(callback_reducer_callsite_rva + callback_reducer_callsite_fingerprint.size() ==
    trace::callback_reducer_return);

enum class TraceGate : unsigned char {
    not_attempted,
    image_mismatch,
    entry_fingerprint_mismatch,
    builder_fingerprint_mismatch,
    reducer_fingerprint_mismatch,
    continuation_fingerprint_mismatch,
    promise_capture_fingerprint_mismatch,
    builder_callsite_fingerprint_mismatch,
    reducer_callsite_fingerprint_mismatch,
    entry_create_failed,
    builder_create_failed,
    reducer_create_failed,
    trampoline_missing,
    entry_queue_failed,
    builder_queue_failed,
    reducer_queue_failed,
    apply_failed_forwarding,
    rollback_failed,
    prepared,
};

struct EntitlementTraceState {
    EntitlementTraceState() noexcept : verify_image(&supported_build::running_image_matches) {}

    std::mutex initialization_mutex;
    std::mutex trace_mutex;
    std::atomic<CheckUsersEntitlements> original_entry{};
    std::atomic<ProgressionRequestBuilder> original_builder{};
    std::atomic<EntitlementResultReducer> original_reducer{};
    std::atomic<std::uintptr_t> base{};
    std::atomic<bool> active{};
    ImageVerifier verify_image{};
    TraceGate gate{TraceGate::not_attempted};
    bool attempted{};
    std::uint64_t next_sequence{};
    std::uint64_t entry_calls{};
    std::uint64_t valid_request_lists{};
    std::uint64_t malformed_request_lists{};
    std::uint64_t all_fixed_stop_requests{};
    std::uint64_t mixed_requests{};
    std::uint64_t requests_without_fixed_stops{};
    std::uint64_t exact_builder_calls{};
    std::uint64_t service_list_matches{};
    std::uint64_t service_list_mismatches{};
    std::uint64_t synchronous_entry_errors{};
    std::uint64_t asynchronous_submissions{};
    std::uint64_t local_provider_completions{};
    std::uint64_t local_provider_exceptions{};
    std::uint64_t associated_completions{};
    std::uint64_t successful_completions{};
    std::uint64_t error_completions{};
    std::uint64_t unknown_completions{};
    std::uint64_t orphan_completions{};
    std::uint64_t dropped_requests{};
    std::uint64_t native_exceptions{};
    std::array<RequestRecord, request_record_capacity> records{};
    std::size_t next_record{};
};

struct ThreadTraceContext {
    std::uint64_t sequence{};
};

thread_local ThreadTraceContext thread_trace_context{};

EntitlementTraceState& entitlement_trace_state() {
    static auto* value = new EntitlementTraceState;
    return *value;
}

struct PreserveLastError {
    DWORD value{GetLastError()};
    ~PreserveLastError() { SetLastError(value); }
};

template<std::size_t N>
bool fingerprint_matches(
    std::uintptr_t address, const std::array<unsigned char, N>& expected) noexcept {
    std::array<unsigned char, N> actual{};
    return memory::read(address, actual) && actual == expected;
}

RequestRecord* find_sequence(
    EntitlementTraceState& state, std::uint64_t sequence) noexcept {
    if (!sequence) return nullptr;
    for (auto& record : state.records)
        if (record.used && record.sequence == sequence) return &record;
    return nullptr;
}

RequestRecord* find_pending_promise(
    EntitlementTraceState& state, std::uintptr_t promise) noexcept {
    RequestRecord* match{};
    for (auto& record : state.records) {
        if (!record.used || !record.pending || record.promise != promise) continue;
        if (!match || record.sequence > match->sequence) match = &record;
    }
    return match;
}

RequestRecord* allocate_record(EntitlementTraceState& state) noexcept {
    for (std::size_t offset = 0; offset != state.records.size(); ++offset) {
        const auto index = (state.next_record + offset) % state.records.size();
        if (state.records[index].used && state.records[index].pending) continue;
        state.next_record = (index + 1) % state.records.size();
        state.records[index] = {};
        state.records[index].used = true;
        state.records[index].pending = true;
        state.records[index].sequence = ++state.next_sequence;
        return &state.records[index];
    }
    ++state.dropped_requests;
    return nullptr;
}

void* check_entitlements_detour(
    void* result, const void* requested, void* promise) {
    auto& state = entitlement_trace_state();
    if (!state.active.load(std::memory_order_acquire)) {
        const auto original = state.original_entry.load(std::memory_order_acquire);
        return original ? original(result, requested, promise) : result;
    }
    const auto incoming_error = GetLastError();
    std::uint64_t sequence{};
    {
        std::scoped_lock lock(state.trace_mutex);
        ++state.entry_calls;
        if (auto* record = allocate_record(state)) {
            sequence = record->sequence;
            record->promise = reinterpret_cast<std::uintptr_t>(promise);
            record->request_list_valid = read_pointer_array_request(requested, *record);
            if (record->request_list_valid) {
                ++state.valid_request_lists;
                if (record->all_fixed_stop) ++state.all_fixed_stop_requests;
                else if (record->mixed) ++state.mixed_requests;
                else ++state.requests_without_fixed_stops;
            } else {
                ++state.malformed_request_lists;
            }
        }
    }

    const auto previous_context = thread_trace_context;
    thread_trace_context.sequence = sequence;
    SetLastError(incoming_error);
    FixedStopEntitlementAttempt local_attempt{};
    try {
        local_attempt = try_complete_fixed_stop_entitlement_request(
            result, requested, promise);
    } catch (...) {
        const auto provider_error = GetLastError();
        thread_trace_context = previous_context;
        {
            std::scoped_lock lock(state.trace_mutex);
            ++state.local_provider_exceptions;
            if (auto* record = find_sequence(state, sequence)) {
                record->pending = false;
                record->route = TraceRoute::local_provider_exception;
            }
        }
        SetLastError(provider_error);
        throw;
    }
    if (local_attempt == FixedStopEntitlementAttempt::completed) {
        thread_trace_context = previous_context;
        std::scoped_lock lock(state.trace_mutex);
        ++state.local_provider_completions;
        if (auto* record = find_sequence(state, sequence)) {
            if (!record->completion_observed) {
                record->completion_observed = true;
                record->completion_error_known = true;
                record->completion_error = false;
                ++state.associated_completions;
                ++state.successful_completions;
            }
            record->pending = false;
            record->route = TraceRoute::local_provider_success;
            std::uintptr_t future{};
            if (result && memory::read(reinterpret_cast<std::uintptr_t>(result), future))
                record->future = future;
        }
        SetLastError(incoming_error);
        return result;
    }
    SetLastError(incoming_error);
    void* native_result{};
    try {
        const auto original = state.original_entry.load(std::memory_order_acquire);
        native_result = original ? original(result, requested, promise) : result;
    } catch (...) {
        const auto native_error = GetLastError();
        thread_trace_context = previous_context;
        {
            std::scoped_lock lock(state.trace_mutex);
            ++state.native_exceptions;
            if (auto* record = find_sequence(state, sequence)) record->pending = false;
        }
        SetLastError(native_error);
        throw;
    }
    const auto native_error = GetLastError();
    thread_trace_context = previous_context;

    std::uintptr_t future{};
    const bool future_read = result &&
        memory::read(reinterpret_cast<std::uintptr_t>(result), future);
    {
        std::scoped_lock lock(state.trace_mutex);
        if (auto* record = find_sequence(state, sequence)) {
            if (future_read) record->future = future;
            if (!record->builder_observed) {
                record->pending = false;
                record->route = TraceRoute::synchronous_entry_error;
                ++state.synchronous_entry_errors;
            } else {
                ++state.asynchronous_submissions;
                if (!record->completion_observed)
                    record->route = TraceRoute::asynchronous_submitted;
            }
        }
    }
    SetLastError(native_error);
    return native_result;
}

void request_builder_detour(
    std::uintptr_t manager, const void* request, const void* continuation) {
    auto& state = entitlement_trace_state();
    if (!state.active.load(std::memory_order_acquire)) {
        const auto original = state.original_builder.load(std::memory_order_acquire);
        if (original) original(manager, request, continuation);
        return;
    }
    const auto incoming_error = GetLastError();
    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto base = state.base.load(std::memory_order_acquire);
    if (thread_trace_context.sequence &&
        return_address == base + entry_builder_return_rva) {
        std::scoped_lock lock(state.trace_mutex);
        if (auto* record = find_sequence(state, thread_trace_context.sequence)) {
            record->builder_observed = true;
            record->service_list_valid = read_service_request(request, *record);
            record->service_list_matches = record->request_list_valid &&
                record->service_list_valid &&
                record->requested_count == record->service_entitlement_count &&
                record->requested_hash == record->service_hash;
            ++state.exact_builder_calls;
            if (record->service_list_matches) ++state.service_list_matches;
            else ++state.service_list_mismatches;
        }
    }
    SetLastError(incoming_error);
    try {
        const auto original = state.original_builder.load(std::memory_order_acquire);
        if (original) original(manager, request, continuation);
    } catch (...) {
        const auto native_error = GetLastError();
        {
            std::scoped_lock lock(state.trace_mutex);
            ++state.native_exceptions;
        }
        SetLastError(native_error);
        throw;
    }
}

void result_reducer_detour(
    void* promise, const void* error, const void* result) {
    auto& state = entitlement_trace_state();
    if (!state.active.load(std::memory_order_acquire)) {
        const auto original = state.original_reducer.load(std::memory_order_acquire);
        if (original) original(promise, error, result);
        return;
    }
    const auto incoming_error = GetLastError();
    std::uintptr_t error_value{};
    const bool error_known = error &&
        memory::read(reinterpret_cast<std::uintptr_t>(error), error_value);
    const bool is_error = error_known &&
        error_value != state.base.load(std::memory_order_acquire) + empty_error_string_rva;
    {
        std::scoped_lock lock(state.trace_mutex);
        if (auto* record = find_pending_promise(
                state, reinterpret_cast<std::uintptr_t>(promise))) {
            record->pending = false;
            record->completion_observed = true;
            record->completion_error_known = error_known;
            record->completion_error = is_error;
            ++state.associated_completions;
            if (!error_known) {
                record->route = TraceRoute::asynchronous_unknown;
                ++state.unknown_completions;
            } else if (is_error) {
                record->route = TraceRoute::asynchronous_error;
                ++state.error_completions;
            } else {
                record->route = TraceRoute::asynchronous_success;
                ++state.successful_completions;
            }
        } else {
            ++state.orphan_completions;
        }
    }
    SetLastError(incoming_error);
    try {
        const auto original = state.original_reducer.load(std::memory_order_acquire);
        if (original) original(promise, error, result);
    } catch (...) {
        const auto native_error = GetLastError();
        {
            std::scoped_lock lock(state.trace_mutex);
            ++state.native_exceptions;
        }
        SetLastError(native_error);
        throw;
    }
}

const char* gate_name(TraceGate gate) noexcept {
    switch (gate) {
    case TraceGate::not_attempted: return "not_attempted";
    case TraceGate::image_mismatch: return "image_mismatch";
    case TraceGate::entry_fingerprint_mismatch: return "entry_fingerprint_mismatch";
    case TraceGate::builder_fingerprint_mismatch: return "builder_fingerprint_mismatch";
    case TraceGate::reducer_fingerprint_mismatch: return "reducer_fingerprint_mismatch";
    case TraceGate::continuation_fingerprint_mismatch: return "continuation_fingerprint_mismatch";
    case TraceGate::promise_capture_fingerprint_mismatch: return "promise_capture_fingerprint_mismatch";
    case TraceGate::builder_callsite_fingerprint_mismatch: return "builder_callsite_fingerprint_mismatch";
    case TraceGate::reducer_callsite_fingerprint_mismatch: return "reducer_callsite_fingerprint_mismatch";
    case TraceGate::entry_create_failed: return "entry_create_failed";
    case TraceGate::builder_create_failed: return "builder_create_failed";
    case TraceGate::reducer_create_failed: return "reducer_create_failed";
    case TraceGate::trampoline_missing: return "trampoline_missing";
    case TraceGate::entry_queue_failed: return "entry_queue_failed";
    case TraceGate::builder_queue_failed: return "builder_queue_failed";
    case TraceGate::reducer_queue_failed: return "reducer_queue_failed";
    case TraceGate::apply_failed_forwarding: return "apply_failed_forwarding";
    case TraceGate::rollback_failed: return "rollback_failed";
    case TraceGate::prepared: return "prepared";
    }
    return "unknown";
}

const char* route_name(TraceRoute route) noexcept {
    switch (route) {
    case TraceRoute::entered: return "entered";
    case TraceRoute::local_provider_success: return "local_provider_success";
    case TraceRoute::local_provider_exception: return "local_provider_exception";
    case TraceRoute::synchronous_entry_error: return "synchronous_entry_error";
    case TraceRoute::asynchronous_submitted: return "asynchronous_submitted";
    case TraceRoute::asynchronous_success: return "asynchronous_success";
    case TraceRoute::asynchronous_error: return "asynchronous_error";
    case TraceRoute::asynchronous_unknown: return "asynchronous_unknown";
    }
    return "unknown";
}

std::string escape_json(std::string_view value) {
    std::ostringstream output;
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
        switch (byte) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (byte < 0x20U) {
                output << "\\u00" << hex[byte >> 4] << hex[byte & 0x0fU];
            } else {
                output << static_cast<char>(byte);
            }
        }
    }
    return output.str();
}

void append_record_json(std::ostringstream& output, const RequestRecord* record) {
    if (!record) {
        output << "null";
        return;
    }
    output << "{\"sequence\":" << record->sequence
           << ",\"route\":\"" << route_name(record->route) << "\""
           << ",\"requested_count\":" << record->requested_count
           << ",\"fixed_stop_count\":" << record->requested_fixed_stop_count
           << ",\"all_fixed_stop\":" << (record->all_fixed_stop ? "true" : "false")
           << ",\"mixed\":" << (record->mixed ? "true" : "false")
           << ",\"request_list_valid\":" << (record->request_list_valid ? "true" : "false")
           << ",\"recorded_list_truncated\":" << (record->request_list_truncated ? "true" : "false")
           << ",\"builder_observed\":" << (record->builder_observed ? "true" : "false")
           << ",\"service_entitlement_count\":" << record->service_entitlement_count
           << ",\"service_fixed_stop_count\":" << record->service_fixed_stop_count
           << ",\"group_eid_count\":" << record->group_eid_count
           << ",\"service_list_valid\":" << (record->service_list_valid ? "true" : "false")
           << ",\"service_list_matches\":" << (record->service_list_matches ? "true" : "false")
           << ",\"future_nonnull\":" << (record->future ? "true" : "false")
           << ",\"completion_observed\":" << (record->completion_observed ? "true" : "false")
           << ",\"completion_error_known\":" << (record->completion_error_known ? "true" : "false")
           << ",\"completion_error\":" << (record->completion_error ? "true" : "false")
           << ",\"identifiers\":[";
    for (std::uint32_t index = 0; index != record->recorded_count; ++index) {
        if (index) output << ',';
        const auto& identifier = record->identifiers[index];
        output << "{\"value\":\""
               << escape_json(std::string_view(identifier.value.data(), identifier.length))
               << "\",\"fixed_stop\":" << (identifier.fixed_stop ? "true" : "false")
               << '}';
    }
    output << "]}";
}

} // namespace

bool prepare_entitlement_request_trace(std::uintptr_t image_base) noexcept {
    PreserveLastError preserve;
    auto& state = entitlement_trace_state();
    std::scoped_lock initialization_lock(state.initialization_mutex);
    if (state.attempted) return state.gate == TraceGate::prepared;
    state.attempted = true;
    state.base.store(image_base, std::memory_order_release);

    if (!state.verify_image || !state.verify_image(image_base)) {
        state.gate = TraceGate::image_mismatch;
        return false;
    }
    if (!fingerprint_matches(image_base + check_entitlements_rva,
            check_entitlements_fingerprint)) {
        state.gate = TraceGate::entry_fingerprint_mismatch;
        return false;
    }
    if (!fingerprint_matches(image_base + request_builder_rva,
            request_builder_fingerprint)) {
        state.gate = TraceGate::builder_fingerprint_mismatch;
        return false;
    }
    if (!fingerprint_matches(image_base + result_reducer_rva,
            result_reducer_fingerprint)) {
        state.gate = TraceGate::reducer_fingerprint_mismatch;
        return false;
    }
    if (!fingerprint_matches(image_base + continuation_callback_rva,
            continuation_callback_fingerprint)) {
        state.gate = TraceGate::continuation_fingerprint_mismatch;
        return false;
    }
    if (!fingerprint_matches(image_base + entry_promise_capture_rva,
            entry_promise_capture_fingerprint)) {
        state.gate = TraceGate::promise_capture_fingerprint_mismatch;
        return false;
    }
    if (!fingerprint_matches(image_base + entry_builder_callsite_rva,
            entry_builder_callsite_fingerprint)) {
        state.gate = TraceGate::builder_callsite_fingerprint_mismatch;
        return false;
    }
    if (!fingerprint_matches(image_base + callback_reducer_callsite_rva,
            callback_reducer_callsite_fingerprint)) {
        state.gate = TraceGate::reducer_callsite_fingerprint_mismatch;
        return false;
    }

    struct HookSet {
        void* entry{};
        void* builder{};
        void* reducer{};
        bool entry_created{};
        bool builder_created{};
        bool reducer_created{};
    } hooks;
    const auto rollback_unenabled = [&hooks]() noexcept {
        bool clean = true;
        const auto remove = [&clean](void* target) noexcept {
            if (hook_remove(target) != HookOk) clean = false;
        };
        if (hooks.reducer_created) remove(hooks.reducer);
        if (hooks.builder_created) remove(hooks.builder);
        if (hooks.entry_created) remove(hooks.entry);
        return clean;
    };
    const auto fail_unenabled = [&state, &rollback_unenabled](TraceGate reason) noexcept {
        state.gate = rollback_unenabled() ? reason : TraceGate::rollback_failed;
        return false;
    };

    void* entry_original{};
    void* builder_original{};
    void* reducer_original{};
    hooks.entry = reinterpret_cast<void*>(image_base + check_entitlements_rva);
    hooks.builder = reinterpret_cast<void*>(image_base + request_builder_rva);
    hooks.reducer = reinterpret_cast<void*>(image_base + result_reducer_rva);
    if (hook_prepare(hooks.entry,
            reinterpret_cast<void*>(&check_entitlements_detour), &entry_original) != HookOk) {
        state.gate = TraceGate::entry_create_failed;
        return false;
    }
    hooks.entry_created = true;
    if (hook_prepare(hooks.builder,
            reinterpret_cast<void*>(&request_builder_detour), &builder_original) != HookOk) {
        return fail_unenabled(TraceGate::builder_create_failed);
    }
    hooks.builder_created = true;
    if (hook_prepare(hooks.reducer,
            reinterpret_cast<void*>(&result_reducer_detour), &reducer_original) != HookOk) {
        return fail_unenabled(TraceGate::reducer_create_failed);
    }
    hooks.reducer_created = true;
    if (!entry_original || !builder_original || !reducer_original) {
        return fail_unenabled(TraceGate::trampoline_missing);
    }
#pragma warning(push)
#pragma warning(disable: 4191)
    state.original_entry.store(
        reinterpret_cast<CheckUsersEntitlements>(entry_original), std::memory_order_release);
    state.original_builder.store(
        reinterpret_cast<ProgressionRequestBuilder>(builder_original), std::memory_order_release);
    state.original_reducer.store(
        reinterpret_cast<EntitlementResultReducer>(reducer_original), std::memory_order_release);
#pragma warning(pop)

    if (hook_queue_enable(hooks.entry) != HookOk) {
        return fail_unenabled(TraceGate::entry_queue_failed);
    }
    if (hook_queue_enable(hooks.builder) != HookOk) {
        return fail_unenabled(TraceGate::builder_queue_failed);
    }
    if (hook_queue_enable(hooks.reducer) != HookOk) {
        return fail_unenabled(TraceGate::reducer_queue_failed);
    }
    if (hook_apply_queued() != HookOk) {
        // ApplyQueued may have enabled a prefix before returning an error. Keep every
        // trampoline registered for process lifetime so a detour already in flight
        // can always forward safely. The inactive gate prevents tracing or local
        // completion on this permanently failed route.
        state.gate = TraceGate::apply_failed_forwarding;
        return false;
    }
    state.active.store(true, std::memory_order_release);
    state.gate = TraceGate::prepared;
    return true;
}

EntitlementRequestTraceObservation entitlement_request_trace_observation() {
    PreserveLastError preserve;
    auto& state = entitlement_trace_state();
    std::scoped_lock lock(state.initialization_mutex, state.trace_mutex);
    EntitlementRequestTraceObservation observation;
    observation.prepared = state.gate == TraceGate::prepared;
    observation.entry_calls = state.entry_calls;
    observation.valid_request_lists = state.valid_request_lists;
    observation.malformed_request_lists = state.malformed_request_lists;
    observation.all_fixed_stop_requests = state.all_fixed_stop_requests;
    observation.mixed_requests = state.mixed_requests;
    observation.requests_without_fixed_stops = state.requests_without_fixed_stops;
    observation.exact_builder_calls = state.exact_builder_calls;
    observation.service_list_matches = state.service_list_matches;
    observation.service_list_mismatches = state.service_list_mismatches;
    observation.synchronous_entry_errors = state.synchronous_entry_errors;
    observation.asynchronous_submissions = state.asynchronous_submissions;
    observation.local_provider_completions = state.local_provider_completions;
    observation.local_provider_exceptions = state.local_provider_exceptions;
    observation.associated_completions = state.associated_completions;
    observation.successful_completions = state.successful_completions;
    observation.error_completions = state.error_completions;
    observation.unknown_completions = state.unknown_completions;
    observation.orphan_completions = state.orphan_completions;
    observation.dropped_requests = state.dropped_requests;
    observation.native_exceptions = state.native_exceptions;
    const RequestRecord* latest{};
    const RequestRecord* latest_fixed{};
    for (const auto& record : state.records) {
        if (!record.used) continue;
        if (record.pending) ++observation.active_requests;
        if (!latest || record.sequence > latest->sequence) latest = &record;
        if (record.request_list_valid && record.requested_fixed_stop_count &&
            (!latest_fixed || record.sequence > latest_fixed->sequence))
            latest_fixed = &record;
    }
    if (latest) {
        observation.latest_sequence = latest->sequence;
        observation.latest_requested_count = latest->requested_count;
        observation.latest_fixed_stop_count = latest->requested_fixed_stop_count;
        observation.latest_service_entitlement_count = latest->service_entitlement_count;
        observation.latest_group_eid_count = latest->group_eid_count;
        observation.latest_all_fixed_stop = latest->all_fixed_stop;
        observation.latest_mixed = latest->mixed;
        observation.latest_service_list_matches = latest->service_list_matches;
        observation.latest_completion_observed = latest->completion_observed;
        observation.latest_completion_error = latest->completion_error;
    }
    std::ostringstream json;
    json << "{\"event\":\"entitlement_request_trace\",\"gate\":\""
         << gate_name(state.gate) << "\",\"prepared\":"
         << (observation.prepared ? "true" : "false")
         << ",\"entry_calls\":" << observation.entry_calls
         << ",\"valid_request_lists\":" << observation.valid_request_lists
         << ",\"malformed_request_lists\":" << observation.malformed_request_lists
         << ",\"all_fixed_stop_requests\":" << observation.all_fixed_stop_requests
         << ",\"mixed_requests\":" << observation.mixed_requests
         << ",\"requests_without_fixed_stops\":" << observation.requests_without_fixed_stops
         << ",\"exact_builder_calls\":" << observation.exact_builder_calls
         << ",\"service_list_matches\":" << observation.service_list_matches
         << ",\"service_list_mismatches\":" << observation.service_list_mismatches
         << ",\"synchronous_entry_errors\":" << observation.synchronous_entry_errors
         << ",\"asynchronous_submissions\":" << observation.asynchronous_submissions
         << ",\"local_provider_completions\":" << observation.local_provider_completions
         << ",\"local_provider_exceptions\":" << observation.local_provider_exceptions
         << ",\"associated_completions\":" << observation.associated_completions
         << ",\"successful_completions\":" << observation.successful_completions
         << ",\"error_completions\":" << observation.error_completions
         << ",\"unknown_completions\":" << observation.unknown_completions
         << ",\"orphan_completions\":" << observation.orphan_completions
         << ",\"active_requests\":" << observation.active_requests
         << ",\"dropped_requests\":" << observation.dropped_requests
         << ",\"native_exceptions\":" << observation.native_exceptions
         << ",\"latest_request\":";
    append_record_json(json, latest);
    json << ",\"latest_fixed_stop_request\":";
    append_record_json(json, latest_fixed);
    json << '}';
    observation.json = json.str();
    return observation;
}

} // namespace dingosdk

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Per-request records kept by the entitlement trace detours; the request and
// service lists are parsed in entitlement_request_parse.cpp.
namespace dingosdk::entitlement_request_trace::detail {

inline constexpr std::size_t maximum_recorded_identifiers = 128;
inline constexpr std::size_t recorded_identifier_capacity = 96;
inline constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;

enum class TraceRoute : unsigned char {
    entered,
    local_provider_success,
    local_provider_exception,
    synchronous_entry_error,
    asynchronous_submitted,
    asynchronous_success,
    asynchronous_error,
    asynchronous_unknown,
};

struct CapturedIdentifier {
    std::array<char, recorded_identifier_capacity> value{};
    std::uint16_t length{};
    bool fixed_stop{};
};

struct RequestRecord {
    bool used{};
    bool pending{};
    std::uint64_t sequence{};
    std::uintptr_t promise{};
    std::uintptr_t future{};
    std::uint32_t requested_count{};
    std::uint32_t requested_fixed_stop_count{};
    std::uint32_t recorded_count{};
    std::uint32_t service_entitlement_count{};
    std::uint32_t service_fixed_stop_count{};
    std::uint32_t group_eid_count{};
    std::uint64_t requested_hash{fnv_offset};
    std::uint64_t service_hash{fnv_offset};
    bool request_list_valid{};
    bool request_list_truncated{};
    bool all_fixed_stop{};
    bool mixed{};
    bool builder_observed{};
    bool service_list_valid{};
    bool service_list_matches{};
    bool completion_observed{};
    bool completion_error_known{};
    bool completion_error{};
    TraceRoute route{TraceRoute::entered};
    std::array<CapturedIdentifier, maximum_recorded_identifiers> identifiers{};
};

bool read_pointer_array_request(const void* wrapper, RequestRecord& record) noexcept;
bool read_service_request(const void* request, RequestRecord& record) noexcept;

} // namespace dingosdk::entitlement_request_trace::detail

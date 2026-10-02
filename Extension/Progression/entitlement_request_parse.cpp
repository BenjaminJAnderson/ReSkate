#include "entitlement_request_hook_internal.h"
#include "Engine/Core/Platform/memory.h"
#include "fast_travel_unlock.h"

#include <array>
#include <cstring>
#include <string_view>

namespace dingosdk::entitlement_request_trace::detail {
namespace {

constexpr std::size_t maximum_scanned_identifiers = 512;
constexpr std::size_t maximum_identifier_length = 255;
constexpr std::size_t frostbite_string_size = 24;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

bool valid_range(std::uintptr_t address, std::size_t size) noexcept {
    return address >= 0x10000 && size && size <= memory::highest_user_address &&
        address <= memory::highest_user_address - size;
}

std::uint64_t hash_identifier(
    std::uint64_t value, std::string_view identifier) noexcept {
    for (const unsigned char byte : identifier) {
        value ^= byte;
        value *= fnv_prime;
    }
    value ^= 0xffU;
    value *= fnv_prime;
    const auto length = static_cast<std::uint32_t>(identifier.size());
    for (unsigned shift = 0; shift != 32; shift += 8) {
        value ^= static_cast<unsigned char>(length >> shift);
        value *= fnv_prime;
    }
    return value;
}

bool read_c_string(std::uintptr_t address,
    std::array<char, maximum_identifier_length + 1>& output,
    std::size_t& length) noexcept {
    length = 0;
    if (!valid_range(address, 1)) return false;
    for (; length <= maximum_identifier_length; ++length) {
        char value{};
        if (!memory::read(address + length, value)) return false;
        output[length] = value;
        if (!value) return true;
    }
    return false;
}

bool read_frostbite_string(std::uintptr_t address,
    std::array<char, maximum_identifier_length + 1>& output,
    std::size_t& length) noexcept {
    unsigned char marker{};
    if (!memory::read(address + 0x0f, marker)) return false;
    std::uintptr_t data = address;
    if (marker & 0x80U) {
        std::uint32_t stored_length{};
        if (!memory::read(address, data) || !memory::read(address + 8, stored_length) ||
            stored_length > maximum_identifier_length)
            return false;
        length = stored_length;
    } else {
        if (marker > 15U) return false;
        length = 15U - marker;
    }
    if (!length) {
        output[0] = '\0';
        return true;
    }
    if (!memory::read_bytes(data, output.data(), length)) return false;
    output[length] = '\0';
    return true;
}

void record_identifier(RequestRecord& record, std::string_view identifier,
    bool fixed_stop) noexcept {
    if (record.recorded_count >= maximum_recorded_identifiers) {
        record.request_list_truncated = true;
        return;
    }
    auto& captured = record.identifiers[record.recorded_count++];
    const auto length = (identifier.size() < recorded_identifier_capacity - 1)
        ? identifier.size() : recorded_identifier_capacity - 1;
    if (length) std::memcpy(captured.value.data(), identifier.data(), length);
    captured.value[length] = '\0';
    captured.length = static_cast<std::uint16_t>(length);
    captured.fixed_stop = fixed_stop;
    if (length != identifier.size()) record.request_list_truncated = true;
}

bool vector_count(std::uintptr_t begin, std::uintptr_t end,
    std::size_t stride, std::uint32_t& count) noexcept {
    count = 0;
    if (begin == end) return begin == 0 || valid_range(begin, 1);
    if (!begin || end < begin || !stride) return false;
    const auto bytes = end - begin;
    if (bytes % stride) return false;
    const auto elements = bytes / stride;
    if (elements > maximum_scanned_identifiers ||
        !valid_range(begin, static_cast<std::size_t>(bytes)))
        return false;
    count = static_cast<std::uint32_t>(elements);
    return true;
}

} // namespace

bool read_pointer_array_request(const void* wrapper, RequestRecord& record) noexcept {
    RequestRecord parsed = record;
    std::uintptr_t data{};
    if (!wrapper || !memory::read(reinterpret_cast<std::uintptr_t>(wrapper), data) ||
        !valid_range(data - 4, 4))
        return false;
    std::uint32_t encoded_count{};
    if (!memory::read(data - 4, encoded_count)) return false;
    const auto count = encoded_count & 0x7fffffffU;
    parsed.requested_count = count;
    if (count > maximum_scanned_identifiers ||
        (count && !valid_range(data, static_cast<std::size_t>(count) * sizeof(std::uintptr_t))))
        return false;
    std::array<char, maximum_identifier_length + 1> text{};
    for (std::uint32_t index = 0; index != count; ++index) {
        std::uintptr_t identifier_address{};
        if (!memory::read(data + static_cast<std::uintptr_t>(index) * sizeof(identifier_address),
                identifier_address))
            return false;
        std::size_t length{};
        if (!read_c_string(identifier_address, text, length)) return false;
        const std::string_view identifier{text.data(), length};
        const bool fixed_stop = static_cast<bool>(
            classify_fixed_bus_stop_entitlement(identifier));
        if (fixed_stop) ++parsed.requested_fixed_stop_count;
        parsed.requested_hash = hash_identifier(parsed.requested_hash, identifier);
        record_identifier(parsed, identifier, fixed_stop);
    }
    parsed.all_fixed_stop = count != 0 &&
        parsed.requested_fixed_stop_count == count;
    parsed.mixed = parsed.requested_fixed_stop_count != 0 &&
        parsed.requested_fixed_stop_count != count;
    record = parsed;
    return true;
}

bool read_service_request(const void* request, RequestRecord& record) noexcept {
    if (!request) return false;
    std::array<std::uintptr_t, 7> fields{};
    if (!memory::read(reinterpret_cast<std::uintptr_t>(request), fields)) return false;
    if (!vector_count(fields[0], fields[1], frostbite_string_size,
            record.group_eid_count) ||
        !vector_count(fields[4], fields[5], frostbite_string_size,
            record.service_entitlement_count))
        return false;
    std::array<char, maximum_identifier_length + 1> text{};
    for (std::uint32_t index = 0; index != record.service_entitlement_count; ++index) {
        std::size_t length{};
        if (!read_frostbite_string(fields[4] +
                static_cast<std::uintptr_t>(index) * frostbite_string_size,
                text, length))
            return false;
        const std::string_view identifier{text.data(), length};
        if (classify_fixed_bus_stop_entitlement(identifier))
            ++record.service_fixed_stop_count;
        record.service_hash = hash_identifier(record.service_hash, identifier);
    }
    return true;
}

} // namespace dingosdk::entitlement_request_trace::detail

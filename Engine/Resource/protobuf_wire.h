#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Schema-less protobuf wire-format reading: enough to walk the game's cached
// catalogue records by field number without generated message types.
namespace dingosdk::protobuf {
enum class Wire : std::uint8_t { varint = 0, fixed64 = 1, bytes = 2, fixed32 = 5 };

struct Field {
    std::uint32_t number{};
    Wire wire{};
    std::uint64_t integer{};              // varint and fixed values
    std::span<const unsigned char> bytes; // length-delimited payloads
};

inline std::optional<std::uint64_t> read_varint(std::span<const unsigned char> data, std::size_t& at) noexcept {
    std::uint64_t result{};
    for (unsigned shift = 0; shift < 64 && at < data.size(); shift += 7) {
        const auto byte = data[at++];
        result |= std::uint64_t{byte & 0x7fu} << shift;
        if (byte < 0x80) return result;
    }
    return std::nullopt;
}

// Every field of one message, or nothing when the bytes are not a well-formed message.
inline std::optional<std::vector<Field>> parse(std::span<const unsigned char> data) {
    std::vector<Field> fields;
    for (std::size_t at = 0; at < data.size();) {
        const auto tag = read_varint(data, at);
        if (!tag || !(*tag >> 3) || (*tag >> 3) > 0x1fffffff) return std::nullopt;
        Field field{static_cast<std::uint32_t>(*tag >> 3), static_cast<Wire>(*tag & 7)};
        switch (field.wire) {
        case Wire::varint: {
            const auto value = read_varint(data, at);
            if (!value) return std::nullopt;
            field.integer = *value;
            break;
        }
        case Wire::fixed64:
        case Wire::fixed32: {
            const std::size_t size = field.wire == Wire::fixed64 ? 8 : 4;
            if (data.size() - at < size) return std::nullopt;
            for (std::size_t i = 0; i < size; ++i) field.integer |= std::uint64_t{data[at + i]} << (8 * i);
            at += size;
            break;
        }
        case Wire::bytes: {
            const auto size = read_varint(data, at);
            if (!size || *size > data.size() - at) return std::nullopt;
            field.bytes = data.subspan(at, static_cast<std::size_t>(*size));
            at += static_cast<std::size_t>(*size);
            break;
        }
        default:
            return std::nullopt;
        }
        fields.push_back(field);
    }
    return fields;
}

// A stream of varint-length-prefixed messages, or nothing when the framing breaks.
inline std::optional<std::vector<std::span<const unsigned char>>> frames(std::span<const unsigned char> data) {
    std::vector<std::span<const unsigned char>> result;
    for (std::size_t at = 0; at < data.size();) {
        const auto size = read_varint(data, at);
        if (!size || !*size || *size > data.size() - at) return std::nullopt;
        result.push_back(data.subspan(at, static_cast<std::size_t>(*size)));
        at += static_cast<std::size_t>(*size);
    }
    return result;
}

inline std::string_view text(std::span<const unsigned char> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

inline const Field* first(const std::vector<Field>& fields, std::uint32_t number, Wire wire) noexcept {
    for (const auto& field : fields)
        if (field.number == number && field.wire == wire) return &field;
    return nullptr;
}
}

#pragma once
#include "protocol.h"
#include <algorithm>
#include <stdexcept>

namespace dingosdk::multiplayer::pose_delta {
// Operate on exact packed bytes: decoding/re-quantizing a reference quaternion
// can change its final bit. Field boundaries also survive narrow/wide changes.
struct Cursor {
    std::span<const std::uint8_t> bytes;
    std::size_t at{};
    std::span<const std::uint8_t> take(std::size_t n) {
        if (at > bytes.size() || n > bytes.size() - at)
            throw std::runtime_error("Truncated pose difference");
        const auto out = bytes.subspan(at, n);
        at += n;
        return out;
    }
};
using Fields = std::array<std::span<const std::uint8_t>, 4>;
inline Fields fields(Cursor &r) {
    const auto flags = r.take(1);
    if (flags[0] & ~15U) throw std::runtime_error("Invalid pose flags");
    return {flags, r.take((flags[0] & 1) ? 12 : 6), r.take(6), r.take((flags[0] & 2) ? 12 : 0)};
}
inline void append(std::vector<std::uint8_t> &out, std::span<const std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}
inline unsigned count(std::span<const std::uint8_t> raw) {
    constexpr auto start = packet_header_size;
    if (raw.size() < start + 6 || raw[6] != 8 || raw[7]) return 0;
    const unsigned skater = raw[start] | (unsigned{raw[start + 1]} << 8);
    const unsigned board = raw[start + 2] | (unsigned{raw[start + 3]} << 8);
    if (skater > max_skater_bones || board > max_board_bones) return 0;
    return 1 + skater + board;
}
inline std::vector<std::uint8_t> encode(std::span<const std::uint8_t> raw,
                                        std::span<const std::uint8_t> base) {
    constexpr auto prefix = packet_header_size + 6;
    const auto n = count(raw);
    if (!n || n != count(base) || !std::equal(raw.begin() + packet_header_size,
        raw.begin() + packet_header_size + 4, base.begin() + packet_header_size)) return {};
    std::vector<std::uint8_t> out;
    // At most every field changes: the whole pose plus one mask nibble per transform.
    out.reserve(raw.size() + (n + 1) / 2);
    out.assign(raw.begin(), raw.begin() + prefix);
    // Header is fixed width and has frequently repeated session/world fields.
    for (std::size_t i = 0; i < prefix; ++i) out[i] ^= base[i];
    const auto masks = out.size();
    out.resize(masks + (n + 1) / 2);
    Cursor a{raw, prefix}, b{base, prefix};
    for (unsigned i = 0; i < n; ++i) {
        const auto current = fields(a), reference = fields(b);
        unsigned mask{};
        for (unsigned f = 0; f < 4; ++f) {
            if (std::equal(current[f].begin(), current[f].end(), reference[f].begin(), reference[f].end()))
                continue;
            mask |= 1U << f;
            // XOR only matching widths; otherwise transmit the new field.
            for (std::size_t j = 0; j < current[f].size(); ++j)
                out.push_back(current[f][j] ^ (current[f].size() == reference[f].size() ? reference[f][j] : 0));
        }
        out[masks + i / 2] |= static_cast<std::uint8_t>(mask << ((i % 2) * 4));
    }
    if (a.at != raw.size() || b.at != base.size()) return {};
    return out;
}
inline std::vector<std::uint8_t> decode(std::span<const std::uint8_t> patch,
                                        std::span<const std::uint8_t> base, std::size_t size) {
    constexpr auto prefix = packet_header_size + 6;
    const auto n = count(base);
    if (!n || size > max_packet) throw std::runtime_error("Invalid pose reference");
    Cursor delta{patch}, reference{base, prefix};
    const auto header = delta.take(prefix);
    std::vector<std::uint8_t> out;
    out.reserve(size); // the exact result size; checked below
    out.assign(header.begin(), header.end());
    for (std::size_t i = 0; i < prefix; ++i) out[i] ^= base[i];
    if (count(out) != n || !std::equal(out.begin() + packet_header_size,
        out.begin() + packet_header_size + 4, base.begin() + packet_header_size))
        throw std::runtime_error("Pose reference layout changed");
    const auto masks = delta.take((n + 1) / 2);
    if ((n & 1) && (masks.back() & 0xf0)) throw std::runtime_error("Invalid pose mask padding");
    for (unsigned i = 0; i < n; ++i) {
        const auto previous = fields(reference);
        const unsigned mask = (masks[i / 2] >> ((i % 2) * 4)) & 15;
        std::uint8_t flags = previous[0][0];
        if (mask & 1) flags ^= delta.take(1)[0];
        if (flags & ~15U) throw std::runtime_error("Invalid changed pose flags");
        out.push_back(flags);
        const std::array<std::size_t, 4> widths{1, (flags & 1) ? 12U : 6U, 6, (flags & 2) ? 12U : 0U};
        for (unsigned f = 1; f < 4; ++f) {
            if (!(mask & (1U << f))) {
                if (widths[f] != previous[f].size()) throw std::runtime_error("Missing changed pose field");
                append(out, previous[f]);
            } else {
                const auto value = delta.take(widths[f]);
                for (std::size_t j = 0; j < value.size(); ++j)
                    out.push_back(value[j] ^ (value.size() == previous[f].size() ? previous[f][j] : 0));
            }
        }
        if (out.size() > size) throw std::runtime_error("Pose difference exceeds output bound");
    }
    if (out.size() != size || delta.at != patch.size() || reference.at != base.size())
        throw std::runtime_error("Pose difference size mismatch");
    return out;
}
} // namespace dingosdk::multiplayer::pose_delta

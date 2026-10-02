#pragma once
#include "Engine/Game/Multiplayer/voice_settings.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dingosdk::multiplayer {
inline constexpr std::size_t max_voice_bytes = 16384;
struct VoiceData {
    float distance = 35.f;
    float gain = 1.f;
    std::uint32_t policy_revision = 1;
    std::vector<std::uint8_t> bytes;
};
struct VoicePolicy {
    bool allowed = true;
    std::uint32_t revision = 1;
    bool operator==(const VoicePolicy &) const = default;
    bool accepts(const VoiceData &voice) const noexcept {
        return allowed && revision && voice.policy_revision == revision;
    }
};
inline bool valid_voice(const VoiceData &voice) noexcept {
    return !voice.bytes.empty() && voice.bytes.size() <= max_voice_bytes &&
        voice.policy_revision && valid_voice_volume(voice.gain) &&
        std::isfinite(voice.distance) && (voice.distance == 0.f ||
        (voice.distance >= min_hearing_distance && voice.distance <= max_hearing_distance));
}
struct VoiceBudget {
    std::uint64_t since{}, bytes{}, packets{};
    bool accept(std::uint64_t now, std::size_t size) noexcept {
        if (now < since || now - since >= 1000000) { since = now; bytes = packets = 0; }
        if (!size || size > max_voice_bytes || packets >= 80 || bytes + size > 96 * 1024) return false;
        ++packets; bytes += size;
        return true;
    }
};
inline std::array<float, 2> voice_stereo(const std::array<float, 3> &listener,
    const std::array<float, 3> &right, const std::array<float, 3> &speaker) noexcept {
    float dot{}, length{}, axis{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto d = speaker[i] - listener[i];
        dot += d * right[i]; length += d * d; axis += right[i] * right[i];
    }
    const auto pan = std::isfinite(dot) && std::isfinite(length) && std::isfinite(axis) && length > .01f && axis > .01f ?
        std::clamp(dot / (std::sqrt(length) * std::sqrt(axis)), -1.f, 1.f) : 0.f;
    return {std::sqrt((1.f - pan) * .5f), std::sqrt((1.f + pan) * .5f)};
}
// How loud a speaker is at a distance, for a listener who hears out to `radius`
// (0: everywhere, full volume). Full volume up close, then a natural inverse-
// distance rolloff (about -6 dB per doubling), faded smoothly to silence over
// the last quarter of the radius, so a voice trails off instead of cutting out.
inline float voice_gain(const std::array<float, 3> &listener, const std::array<float, 3> &speaker,
                        float radius) noexcept {
    float squared{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto d = listener[i] - speaker[i];
        squared += d * d;
    }
    if (!std::isfinite(squared) || !std::isfinite(radius) || radius < 0.f) return 0.f;
    if (radius == 0.f) return 1.f;
    const auto distance = std::sqrt(squared);
    if (distance >= radius) return 0.f;
    // (Not "near": Windows headers define it as a macro.)
    const auto full_volume = std::max(3.f, radius * .15f);
    if (distance <= full_volume) return 1.f;
    auto gain = full_volume / distance;
    const auto fade_start = radius * .75f;
    if (distance > fade_start) {
        const auto t = (distance - fade_start) / (radius - fade_start);
        gain *= 1.f - t * t * (3.f - 2.f * t);
    }
    return gain;
}
}

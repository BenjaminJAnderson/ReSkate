#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <cmath>
#include <bit>
namespace dingosdk::multiplayer {
inline constexpr std::size_t audio_float_count = 58, audio_selector_count = 26, audio_flag_count = 43;
inline constexpr std::size_t max_audio_samples = 32;
struct AudioState {
    std::array<float, audio_float_count> values{};
    std::array<std::uint32_t, audio_selector_count> selectors{};
    std::array<std::uint8_t, audio_flag_count> flags{};
    bool operator==(const AudioState &) const = default;
};
struct AudioSample {
    std::uint32_t age_us{};
    AudioState state;
    bool event{}; // Discrete flag/selector edge; preserve even between network ticks.
};
inline bool audio_event_changed(const AudioState &a, const AudioState &b) noexcept {
    if (a.flags != b.flags || a.selectors != b.selectors) return true;
    // Native input builder: three vectors, speed at +e0,
    // heading at +144, and smoothed speed at +1ac are continuous.
    // Preserve every change to other scalars: they can contain short impulses.
    for (std::size_t i = 9; i < audio_float_count; ++i)
        if (i != 17 && i != 34 && i != 47 &&
            std::bit_cast<std::uint32_t>(a.values[i]) != std::bit_cast<std::uint32_t>(b.values[i])) return true;
    return false;
}
inline bool valid_audio(const AudioState &s) noexcept {
    for (const auto v : s.values)
        if (!std::isfinite(v) || std::abs(v) > 1000000.f)
            return false;
    for (const auto v : s.flags)
        if (v > 1)
            return false;
    return s.flags[0] == 0; // Remote audio must never use the local-player mix.
}
inline bool valid_audio_batch(const std::vector<AudioSample> &samples) noexcept {
    if (samples.empty() || samples.size() > max_audio_samples)
        return false;
    std::uint32_t previous = 1000000;
    for (const auto &s : samples) {
        if (s.age_us > previous || !valid_audio(s.state))
            return false;
        previous = s.age_us;
    }
    return true;
}
} // namespace dingosdk::multiplayer

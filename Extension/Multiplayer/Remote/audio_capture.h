#pragma once
#include "audio_state.h"
#include <deque>
#include <optional>

namespace dingosdk::multiplayer {
// Keep the original input values at contact/action edges and the newest
// continuous sample between them. No amplitude or selector quantization.
class AudioCaptureBuffer {
    struct Captured { AudioState state; std::uint64_t time; bool event; };
    std::deque<Captured> queue_;
    std::optional<AudioState> previous_, sent_;
    std::uint64_t last_send_{};
  public:
    void push(const AudioState &value, std::uint64_t now) {
        const bool event = !previous_ || audio_event_changed(*previous_, value);
        if (!event && !queue_.empty() && !queue_.back().event)
            queue_.back() = {value, now, false};
        else
            queue_.push_back({value, now, event});
        previous_ = value;
        while (queue_.size() > 256) queue_.pop_front();
    }
    std::vector<AudioSample> drain(std::uint64_t now) {
        while (!queue_.empty() && now > queue_.front().time && now - queue_.front().time > 250000)
            queue_.pop_front();
        while (queue_.size() > max_audio_samples) queue_.pop_front();
        std::vector<AudioSample> out;
        for (const auto &value : queue_)
            out.push_back({static_cast<std::uint32_t>(now > value.time ? now - value.time : 0), value.state, value.event});
        queue_.clear();
        // Idle audio still refreshes before the receiver's one-second stop timeout.
        if (out.size() == 1 && !out[0].event && sent_ && out[0].state == *sent_ &&
            now >= last_send_ && now - last_send_ < 250000) return {};
        if (!out.empty()) { sent_ = out.back().state; last_send_ = now; }
        return out;
    }
    void clear() { queue_.clear(); previous_.reset(); sent_.reset(); last_send_ = 0; }
};
} // namespace dingosdk::multiplayer

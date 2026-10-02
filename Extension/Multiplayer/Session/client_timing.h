#pragma once
#include "Engine/Game/Multiplayer/session_model.h"
#include <algorithm>

namespace dingosdk::multiplayer {
struct ClientTiming {
    enum Stage : unsigned { capture, network, send, render, publish };
    std::uint64_t since{}, last_entry{}, entries{}, completed{}, gap_max{}, work{}, work_max{};
    std::array<std::uint64_t, 5> sum{}, peak{};
    void begin(std::uint64_t now) {
        if (!since) since = now;
        if (last_entry && now >= last_entry) gap_max = std::max(gap_max, now - last_entry);
        last_entry = now;
        ++entries;
    }
    void record(Stage stage, std::uint64_t start, std::uint64_t end) {
        const auto elapsed = end >= start ? end - start : 0;
        sum[stage] += elapsed;
        peak[stage] = std::max(peak[stage], elapsed);
    }
    void finish(std::uint64_t start, std::uint64_t end) {
        const auto elapsed = end >= start ? end - start : 0;
        work += elapsed;
        work_max = std::max(work_max, elapsed);
        ++completed;
    }
    MultiplayerClientTiming snapshot(std::uint64_t now) const {
        MultiplayerClientTiming result;
        if (since && now > since)
            result.callback_hz = static_cast<float>(entries > 0 ? entries - 1 : 0) * 1000000.f /
                static_cast<float>(now - since);
        result.gap_max_ms = static_cast<float>(gap_max) / 1000.f;
        result.work_max_ms = static_cast<float>(work_max) / 1000.f;
        const auto divisor = 1000.f * static_cast<float>(std::max<std::uint64_t>(completed, 1));
        result.work_ms = static_cast<float>(work) / divisor;
        for (unsigned i = 0; i < sum.size(); ++i) {
            result.mean_ms[i] = static_cast<float>(sum[i]) / divisor;
            result.peak_ms[i] = static_cast<float>(peak[i]) / 1000.f;
        }
        return result;
    }
};
} // namespace dingosdk::multiplayer

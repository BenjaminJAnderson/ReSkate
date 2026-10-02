#pragma once
#include "Engine/Game/Multiplayer/session_model.h"
#include <charconv>
#include <optional>
#include <string_view>

namespace dingosdk::multiplayer {
inline constexpr std::string_view steam_join_prefix = "+reskate_lobby ";
inline std::optional<std::uint64_t> steam_join_target(std::string_view text) {
    if (!text.starts_with(steam_join_prefix)) return {};
    text.remove_prefix(steam_join_prefix.size());
    std::uint64_t id{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !id) return {};
    return id;
}
inline std::string steam_join_presence(const MultiplayerModel& model) {
    if (!model.active || model.echo || !model.public_lobby || !model.local_ready || model.password_required ||
        model.players >= model.capacity || (model.hosting ? !model.public_host || !model.lobby_listed : !model.connected)) return {};
    return std::string(steam_join_prefix) + std::to_string(model.public_lobby);
}
// Optional Steam integration, ticked on the client thread. Steam callbacks only
// enqueue requests; normal lobby verification runs before opening any P2P link.
void tick_steam_friend_join() noexcept;
}

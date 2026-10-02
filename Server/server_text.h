#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Parsing helpers for console lines, admin requests and chat commands.
namespace dingosdk::server {
inline std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
// The first word and the rest, both trimmed.
inline std::pair<std::string_view, std::string_view> split(std::string_view text) {
    text = trim(text);
    const auto space = text.find(' ');
    if (space == std::string_view::npos) return {text, {}};
    return {text.substr(0, space), trim(text.substr(space + 1))};
}
inline std::optional<std::uint64_t> number(std::string_view text) {
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return {};
    return value;
}
inline std::optional<bool> on_off(std::string_view text) {
    if (text == "on" || text == "true" || text == "yes") return true;
    if (text == "off" || text == "false" || text == "no") return false;
    return {};
}
inline std::string lower(std::string_view text) {
    std::string result(text);
    for (auto &c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return result;
}
} // namespace dingosdk::server

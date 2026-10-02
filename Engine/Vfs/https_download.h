#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace dingosdk::https {
struct Download {
    bool ok{};
    unsigned long http_status{}, error{};
};
// Streams an HTTPS GET into `destination`, which must not exist yet. Follows
// redirects that stay on HTTPS; refuses bodies over `max_bytes` or transfers
// that take longer than `timeout_seconds` in total.
Download get(std::wstring_view url, const std::filesystem::path& destination,
    std::uint64_t max_bytes, std::uint32_t timeout_seconds, const wchar_t* agent);
}

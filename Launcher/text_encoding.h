#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace dingosdk::launcher_text {
// UTF-16 to UTF-8; empty when the text cannot be converted.
inline std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string output(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), length, nullptr, nullptr);
    return output;
}
// UTF-8 to UTF-16; empty when the text cannot be converted.
inline std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring output(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length);
    return output;
}
} // namespace dingosdk::launcher_text

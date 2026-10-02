#pragma once

#include "console_entry.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk {

inline constexpr std::size_t console_max_command_bytes = 4096;
inline constexpr std::size_t console_max_arguments = 64;
inline constexpr std::size_t console_max_autocomplete_matches = 256;

enum class CommandParseError {
    none,
    input_too_long,
    invalid_character,
    too_many_arguments,
    unterminated_quote,
    dangling_escape,
};

struct CommandParseResult {
    std::vector<std::string> arguments;
    CommandParseError error{CommandParseError::none};
    std::size_t error_offset{};

    explicit operator bool() const noexcept { return error == CommandParseError::none; }
};

// Parses printable ASCII command text. Space and horizontal tab separate
// arguments. Single and double quotes preserve separators, and a backslash
// escapes the following printable character both inside and outside quotes.
CommandParseResult parse_console_command(std::string_view input);

bool ascii_case_insensitive_starts_with(std::string_view value,
                                        std::string_view prefix) noexcept;

struct ConsoleLogLimits {
    std::size_t max_lines{2048};
    std::size_t max_bytes{1024 * 1024};
    std::size_t max_line_bytes{4096};
};

class ConsoleLogBuffer {
public:
    explicit ConsoleLogBuffer(ConsoleLogLimits limits = {});
    ConsoleLogBuffer(const ConsoleLogBuffer&) = delete;
    ConsoleLogBuffer& operator=(const ConsoleLogBuffer&) = delete;

    // Splits LF and CRLF input into logical lines. A terminal newline does not
    // create an extra line, while explicit adjacent newlines preserve blanks.
    // Returns the latest sequence after the append, or the prior sequence for
    // empty input.
    std::uint64_t append(std::string_view text, ConsoleSeverity severity,
        ConsoleSource source, logging::Context context);

    // Returns currently retained lines whose sequence is strictly greater than
    // after_sequence.
    std::vector<ConsoleLogLine> snapshot_after(std::uint64_t after_sequence) const;

    std::uint64_t latest_sequence() const noexcept;

private:
    void append_line_locked(std::string_view text, std::uint64_t elapsed_ms,
        ConsoleSeverity severity, ConsoleSource source, logging::Context context);
    void enforce_limits_locked();

    const ConsoleLogLimits limits_;
    mutable std::mutex mutex_;
    std::deque<ConsoleLogLine> lines_;
    std::size_t stored_bytes_{};
    std::uint64_t latest_sequence_{};
};

} // namespace dingosdk

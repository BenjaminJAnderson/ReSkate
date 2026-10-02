#pragma once
#include "local_profile.h"

// Shared persistence primitives for feature codecs; not part of the public SDK.
namespace dingosdk::profile::detail {
using Json = dingosdk::Json;
inline constexpr std::size_t max_bytes = 4 * 1024 * 1024;
inline constexpr std::size_t max_records = 16384;
std::uint32_t checksum(std::string_view text);
void require(bool ok, const char* error);
bool valid_text(std::string_view text, bool empty = false);
std::string read_file(const std::filesystem::path& path);
Json parse_json(std::string_view text);
std::uint64_t unsigned_value(const Json& value, std::uint64_t maximum);
const Json& object_field(const Json& object, const char* name);
void validate_challenges(const Snapshot& snapshot);
void trim_challenge_changes(Snapshot& snapshot);
void validate_settings(const Snapshot& snapshot);
}

namespace dingosdk::profile {
Snapshot decode_legacy(std::string_view text);
}

#include "profile_internal.h"
#include <iomanip>
#include <sstream>
#include <set>

namespace dingosdk::profile {
using namespace detail;
// Read-only migration support for the original checksummed profile.rsp format.
std::string encode_legacy(const Snapshot& s) {
    require(s.onboarding_seed <= 4, "Unsupported onboarding seed version");
    require(s.bool_options.size() + s.play_events.size() + s.quests.size() + s.entitlements.size() +
        s.neighborhood_ranks.size() <= max_records,
        "Too many local profile records");
    std::ostringstream out;
    out << "RESKATE_LOCAL_PROFILE 1\nrevision " << s.revision << "\nonboarding_seed " << s.onboarding_seed << '\n';
    for (const auto& [key, v] : s.bool_options) {
        require(valid_text(key), "Invalid profile option key");
        out << "bool " << std::quoted(key) << ' ' << (v ? 1 : 0) << '\n';
    }
    for (const auto& [key, e] : s.play_events) {
        require(key == e.id && valid_text(key) && valid_text(e.context, true) && e.count >= 0,
            "Invalid play event");
        out << "event " << std::quoted(key) << ' ' << std::quoted(e.context) << ' '
            << e.timestamp << ' ' << e.count << '\n';
    }
    for (const auto& [key, v] : s.quests) {
        require(valid_text(key) && v >= 0 && v <= 6, "Invalid quest state");
        out << "quest " << std::quoted(key) << ' ' << v << '\n';
    }
    for (const auto& [key, v] : s.entitlements) {
        require(valid_text(key), "Invalid entitlement key");
        out << "entitlement " << std::quoted(key) << ' ' << (v ? 1 : 0) << '\n';
    }
    for (const auto& [key, v] : s.neighborhood_ranks) {
        require(std::find(neighborhood_ids.begin(), neighborhood_ids.end(), key) != neighborhood_ids.end() &&
            v <= 10000, "Invalid neighborhood rank");
        out << "rank " << std::quoted(key) << ' ' << v << '\n';
    }
    const auto body = out.str();
    out << "checksum " << std::hex << std::setw(8) << std::setfill('0') << checksum(body) << '\n';
    auto result = out.str();
    require(result.size() <= max_bytes, "Local profile exceeds size limit");
    return result;
}

Snapshot decode_legacy(std::string_view text) {
    require(text.size() <= max_bytes, "Local profile exceeds size limit");
    const auto at = text.rfind("checksum ");
    require(at != std::string_view::npos && at > 0 && text[at - 1] == '\n', "Incomplete local profile");
    std::istringstream check(std::string{text.substr(at + 9)});
    std::uint32_t crc{};
    require(bool(check >> std::hex >> crc), "Invalid profile checksum");
    check >> std::ws;
    require(check.eof() && checksum(text.substr(0, at)) == crc, "Local profile checksum mismatch");
    std::istringstream in(std::string{text.substr(0, at)});
    std::string tag;
    unsigned version{};
    Snapshot s;
    require(bool(in >> tag >> version) && tag == "RESKATE_LOCAL_PROFILE" && version == 1,
        "Unsupported local profile version");
    require(bool(in >> tag >> s.revision) && tag == "revision", "Missing profile revision");
    require(bool(in >> tag >> s.onboarding_seed) && tag == "onboarding_seed", "Missing seed version");
    std::size_t count{};
    while (in >> tag) {
        require(++count <= max_records, "Too many local profile records");
        std::string key;
        require(bool(in >> std::quoted(key)), "Missing profile record key");
        if (tag == "bool") {
            int value{};
            require(bool(in >> value) && (value == 0 || value == 1) &&
                s.bool_options.emplace(key, value != 0).second, "Invalid/duplicate boolean option");
        } else if (tag == "entitlement") {
            int value{};
            require(bool(in >> value) && (value == 0 || value == 1) &&
                s.entitlements.emplace(key, value != 0).second, "Invalid/duplicate entitlement");
        } else if (tag == "rank") {
            std::uint32_t value{};
            require(bool(in >> value) && s.neighborhood_ranks.emplace(key, value).second,
                "Invalid/duplicate neighborhood rank");
        } else if (tag == "event") {
            PlayEvent e; e.id = key;
            require(bool(in >> std::quoted(e.context) >> e.timestamp >> e.count) &&
                s.play_events.emplace(key, e).second, "Invalid/duplicate play event");
        } else if (tag == "quest") {
            std::int32_t value{};
            require(bool(in >> value) && s.quests.emplace(key, value).second, "Invalid/duplicate quest");
        } else throw std::runtime_error("Unknown local profile record");
    }
    // Canonical encoding also rejects invalid ranges, signed unsigned values,
    // extra tokens and ambiguous spellings. Never overwrite unrecognized saves.
    require(encode_legacy(s) == text, "Noncanonical or invalid local profile");
    return s;
}

}

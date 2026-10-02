#include "Extension/Profile/profile_internal.h"
#include <cmath>
#include <set>
#include <type_traits>

namespace dingosdk::profile {
using namespace detail;
std::optional<RipScore> rip_score(const Snapshot& s) {
    if (!s.extensions.contains("progress") || !s.extensions.at("progress").contains("rip_score")) return {};
    const auto& score = object_field(s.extensions.at("progress"), "rip_score");
    const auto value = unsigned_value(score.at("value"), 1000000000);
    const auto cap = unsigned_value(score.at("cap"), 1000000000);
    const auto level = unsigned_value(score.at("level"), 10000);
    require(cap > 0 && value <= cap && level > 0, "Invalid local RIP Score progression");
    return RipScore{static_cast<std::int64_t>(value), static_cast<std::int64_t>(cap), static_cast<std::int32_t>(level)};
}

}

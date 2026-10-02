#include <algorithm>
#include "Extension/Profile/profile_internal.h"
#include "Extension/Profile/profile_update.h"
#include "Engine/Vfs/content_catalogs.h"
#include <cmath>
#include <set>
#include <type_traits>

namespace dingosdk::profile {
using namespace detail;
namespace {
std::vector<ChallengeGoal> parse_goals(const Json& goals) {
    require(goals.is_array() && goals.size() <= 256, "Invalid challenge goal catalog");
    std::vector<ChallengeGoal> result;
    std::set<std::string> unique;
    for (const auto& goal : goals) {
        require(goal.is_object() && goal.at("id").is_string() && goal.at("optional").is_boolean(),
            "Invalid challenge goal metadata");
        const auto goal_id = goal.at("id").get<std::string>();
        require(valid_text(goal_id) && unique.insert(goal_id).second, "Invalid or duplicate challenge goal");
        ChallengeGoal parsed{goal_id, goal.at("optional").get<bool>()};
        const auto key = [&](const char* field, std::string& out) {
            if (!goal.contains(field)) return;
            require(goal.at(field).is_string(), "Invalid challenge goal text key");
            out = goal.at(field).get<std::string>();
            require(valid_text(out), "Invalid challenge goal text key");
        };
        key("title_key", parsed.title_key); key("feed_key", parsed.feed_key);
        key("short_description_key", parsed.short_description_key); key("description_key", parsed.description_key);
        result.push_back(std::move(parsed));
    }
    return result;
}
ChallengeDefinition parse_definition(const std::string& id, const Json& entry) {
    require(valid_text(id) && entry.is_object(), "Invalid challenge definition");
    require(entry.at("available").is_boolean(), "Challenge availability must be boolean");
    require(entry.at("type").is_string() && entry.at("asset").is_string(), "Invalid challenge metadata");
    const auto type = entry.at("type").get<std::string>(), asset = entry.at("asset").get<std::string>();
    static const std::set<std::string> types{"OTS", "Slam", "Line", "Session", "Stunt", "OTL", "Speedline"};
    require(types.contains(type) && valid_text(asset), "Unsupported challenge type or asset");
    ChallengeDefinition definition{id, type, asset, entry.at("available").get<bool>(), {}, {}, {}};
    if (entry.contains("neighborhood")) {
        require(entry.at("neighborhood").is_string(), "Invalid challenge neighborhood");
        definition.neighborhood = entry.at("neighborhood").get<std::string>();
        require(definition.neighborhood.empty() || valid_text(definition.neighborhood), "Invalid challenge neighborhood");
    }
    if (entry.contains("title_key")) {
        require(entry.at("title_key").is_string(), "Invalid challenge title key");
        definition.title_key = entry.at("title_key").get<std::string>();
        require(valid_text(definition.title_key), "Invalid challenge title key");
    }
    if (entry.contains("description_key") && entry.at("description_key").is_string()) {
        auto key = entry.at("description_key").get<std::string>();
        if (valid_text(key)) definition.description_key = std::move(key);
    }
    if (entry.contains("goals")) definition.goals = parse_goals(entry.at("goals"));
    return definition;
}
// The authored catalogue, read once from the installed content cache.
const std::map<std::string, ChallengeDefinition, std::less<>>& cached_challenges() {
    static const auto catalog = [] {
        std::map<std::string, ChallengeDefinition, std::less<>> result;
        for (const auto& [id, entry] : content_cache::catalogs().challenges.items()) {
            try { result.emplace(id, parse_definition(id, entry)); }
            catch (...) {} // A type this provider cannot run stays out of the catalogue.
        }
        return result;
    }();
    return catalog;
}
}
// Local decoded challenge service. The catalogue comes from the content cache;
// the save holds only this player's changes to it: "available" when a challenge
// is switched off, and "goals" when the running game reported different ones.
// Without an installed cache, definitions stored by older versions still work.
ChallengePolicy challenge_policy(const Snapshot& s) {
    ChallengePolicy result;
    if (!s.extensions.contains("challenges")) return result;
    const auto& section = object_field(s.extensions, "challenges");
    require(section.at("enabled").is_boolean(), "Challenge enabled must be boolean");
    result.enabled = section.at("enabled").get<bool>();
    const auto saved = section.contains("catalog") ? object_field(section, "catalog") : Json::object();
    require(saved.size() <= 2048, "Too many challenge definitions");
    if (!content_cache::catalogs().available) {
        for (const auto& [id, entry] : saved.items()) result.catalog.emplace(id, parse_definition(id, entry));
        return result;
    }
    result.catalog = cached_challenges();
    for (const auto& [id, change] : saved.items()) {
        const auto found = result.catalog.find(id);
        if (found == result.catalog.end() || !change.is_object()) continue;
        if (change.contains("available")) {
            require(change.at("available").is_boolean(), "Challenge availability must be boolean");
            found->second.available = change.at("available").get<bool>();
        }
        if (change.contains("goals")) found->second.goals = parse_goals(change.at("goals"));
    }
    return result;
}
namespace detail {
// Saves from before the content cache stored every definition. Keep only this
// player's changes; without a cache there is nothing to compare against.
void trim_challenge_changes(Snapshot& s) {
    if (!content_cache::catalogs().available || !s.extensions.contains("challenges")) return;
    auto& section = s.extensions.at("challenges");
    if (!section.contains("catalog")) return;
    const auto& base = cached_challenges();
    Json kept = Json::object();
    for (const auto& [id, change] : section.at("catalog").items()) {
        const auto found = base.find(id);
        if (found == base.end() || !change.is_object()) continue;
        Json row = Json::object();
        if (change.contains("available") && change.at("available").is_boolean() && !change.at("available").get<bool>())
            row["available"] = false;
        if (change.contains("goals")) {
            try {
                if (parse_goals(change.at("goals")) != found->second.goals) row["goals"] = change.at("goals");
            } catch (...) {}
        }
        if (row.size()) kept[id] = std::move(row);
    }
    section["catalog"] = std::move(kept);
}
std::vector<std::string> challenge_criteria_array(const Json& values) {
    require(values.is_array() && values.size() <= 256, "Invalid completed challenge goals");
    std::vector<std::string> result;
    std::set<std::string> unique;
    for (const auto& value : values) {
        require(value.is_string(), "Challenge goal ID must be text");
        const auto id = value.get<std::string>();
        require(valid_text(id) && unique.insert(id).second, "Invalid or duplicate challenge goal ID");
        result.push_back(id);
    }
    return result;
}
void validate_challenges(const Snapshot& s) {
    (void)challenge_policy(s);
    if (!s.extensions.contains("challenges")) return;
    const auto& section = s.extensions.at("challenges");
    if (!section.contains("progress")) return;
    const auto& progress = object_field(section, "progress");
    require(progress.size() <= 2048, "Too many challenge progress records");
    for (const auto& [id, row] : progress.items()) {
        require(valid_text(id) && row.is_object(), "Invalid challenge progress");
        const auto attempt = unsigned_value(row.at("attempt"), UINT32_MAX);
        require(attempt > 0, "Invalid challenge attempt");
        (void)challenge_criteria_array(row.at("completed_criteria"));
        if (row.contains("receipt")) {
            const auto& receipt = object_field(row, "receipt");
            require(unsigned_value(receipt.at("attempt"), UINT32_MAX) <= attempt &&
                receipt.at("attempt") > 0 && receipt.at("status") == "committed", "Invalid challenge receipt");
            (void)challenge_criteria_array(receipt.at("completed_criteria"));
            // No official economy table is present in this build. Empty grants
            // are explicit, rather than inventing currencies or paid items.
            require(receipt.at("grants").is_array() && receipt.at("grants").empty(),
                "Challenge reward grants are not configured in this provider");
        }
    }
}
}
std::vector<std::string> challenge_completed_criteria(const Snapshot& s, std::string_view id) {
    if (!s.extensions.contains("challenges")) return {};
    const auto& section = s.extensions.at("challenges");
    if (!section.contains("progress") || !section.at("progress").contains(std::string(id))) return {};
    return challenge_criteria_array(section.at("progress").at(std::string(id)).at("completed_criteria"));
}
std::uint64_t Store::begin_challenge(std::string_view id) {
    std::lock_guard lock(mutex_);
    const auto policy = challenge_policy(value_);
    const auto definition = policy.catalog.find(id);
    require(policy.enabled && definition != policy.catalog.end() && definition->second.available,
        "Challenge is not locally available");
    const auto hidden = value_.bool_options.find(hide_challenges_option);
    require(hidden == value_.bool_options.end() || !hidden->second, "Challenges are hidden");
    Update update(*this);
    auto& row = update.json(value_.extensions, {"challenges", "progress", id});
    if (row.is_null()) row = {{"attempt", std::uint64_t{0}}, {"completed_criteria", Json::array()}};
    const auto attempt = unsigned_value(row.at("attempt"), UINT32_MAX - 1) + 1;
    row["attempt"] = attempt;
    update.commit();
    return attempt;
}
bool Store::remember_challenge_goals(std::string_view id, const std::vector<ChallengeGoal>& goals) {
    std::lock_guard lock(mutex_);
    const auto policy = challenge_policy(value_);
    const auto found = policy.catalog.find(id);
    require(policy.enabled && found != policy.catalog.end(), "Unknown authored challenge");
    if (goals.empty() || found->second.goals == goals) return false;
    Update update(*this);
    auto& stored = update.json(value_.extensions, {"challenges", "catalog", id})["goals"];
    stored = Json::array();
    for (const auto& goal : goals) {
        Json row{{"id", goal.id}, {"optional", goal.optional}};
        if (!goal.title_key.empty()) row["title_key"] = goal.title_key;
        if (!goal.feed_key.empty()) row["feed_key"] = goal.feed_key;
        if (!goal.short_description_key.empty()) row["short_description_key"] = goal.short_description_key;
        if (!goal.description_key.empty()) row["description_key"] = goal.description_key;
        stored.push_back(std::move(row));
    }
    // Commit validates the discovered identities; earned results remain intact.
    update.commit();
    return true;
}
std::uint64_t Store::finish_challenge(std::string_view id, std::uint64_t attempt,
    const std::vector<std::string>& completed) {
    std::lock_guard lock(mutex_);
    require(valid_text(id) && attempt > 0, "Invalid challenge completion");
    const auto goals = challenge_criteria_array(Json(completed));
    const auto policy = challenge_policy(value_);
    const auto definition = policy.catalog.find(id);
    require(policy.enabled && definition != policy.catalog.end() && definition->second.available,
        "Challenge is not locally available");
    Update update(*this);
    const auto& section = value_.extensions.at("challenges");
    require(section.contains("progress") && section.at("progress").is_object() &&
        section.at("progress").contains(std::string(id)), "Challenge has not begun");
    auto& row = update.json(value_.extensions, {"challenges", "progress", id});
    require(row.at("attempt") == attempt, "Stale challenge completion");
    std::set<std::string> submitted(goals.begin(), goals.end());
    if (row.contains("receipt") && row.at("receipt").at("attempt") == attempt) {
        const auto old = challenge_criteria_array(row.at("receipt").at("completed_criteria"));
        require(std::set<std::string>(old.begin(), old.end()) == submitted, "Conflicting challenge completion");
        return attempt;
    }
    const auto old = challenge_criteria_array(row.at("completed_criteria"));
    std::set<std::string> merged(old.begin(), old.end());
    merged.insert(submitted.begin(), submitted.end());
    row["completed_criteria"] = merged;
    row["receipt"] = {{"attempt", attempt}, {"status", "committed"},
        {"completed_criteria", submitted}, {"grants", Json::array()}};
    update.commit();
    return attempt;
}

}

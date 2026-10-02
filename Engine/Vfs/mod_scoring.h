#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::mods {

struct Catalog;

// How the enabled mods change trick scoring or skater physics: the points each
// trick is worth (gameplay/scorables, one asset per trick), the scoring
// multipliers, the throwdown logic that turns tricks into a score, and the
// skater's core physics. Multiplayer sends the fingerprint, and a dedicated
// server or lobby host keeps players whose tricks score or handle differently
// out of throwdowns and coop challenges.
struct ScoringCheck {
    // 0 when no enabled mod changes scoring; otherwise a hash of every change.
    std::uint64_t fingerprint{};
    std::vector<std::string> mods;   // the mods that change it, highest priority first
    std::vector<std::string> assets; // what they change (lower-case asset names), sorted
};

// The assets that decide how many points tricks score or how the skater handles (lower-case name).
[[nodiscard]] bool scoring_asset(std::string_view name) noexcept;

// Compares every enabled mod's copies of the scoring assets with the game's
// own. A copy the game does not ship under that name is new, not a change.
// Never throws: a bundle that cannot be read is left out (the merge leaves its
// mod out of the game too) and noted in `notes`.
[[nodiscard]] ScoringCheck check_scoring(const Catalog& catalog, std::vector<std::string>* notes = nullptr) noexcept;

// The scoring the running game uses, as multiplayer reports it.
struct ScoringState {
    bool known{};                    // the launch's check has finished
    std::uint64_t fingerprint{};
    std::vector<std::string> mods;
};
// Records a check: the launch's, then each live apply's. A live apply can add
// changes but never take them back: a scoring asset the game has already
// loaded keeps its points until the game restarts. Any thread.
void publish_scoring(const ScoringCheck& check) noexcept;
[[nodiscard]] ScoringState scoring_state() noexcept;

} // namespace dingosdk::mods

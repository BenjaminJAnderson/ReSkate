// Opens a COPY of a real save and reports what loading it changes. Only runs
// when given a scratch folder holding reskate.sqlite3 (never point it at the
// live profile folder: loading commits the migrated document).
//   dingosdk_profile_migration_tests <scratch folder>
#include "Extension/Profile/local_profile.h"
#include <iostream>

int main(int argc, char** argv) {
    using namespace dingosdk;
    if (argc < 2) {
        std::cout << "No scratch save given; skipped.\n";
        return 0;
    }
    const auto save = std::filesystem::absolute(argv[1]) / "reskate.sqlite3";
    profile::Store store(save);
    const auto snapshot = store.snapshot();
    const auto policy = profile::challenge_policy(snapshot);
    const auto& section = snapshot.extensions.at("challenges");
    std::size_t available{};
    for (const auto& [id, definition] : policy.catalog) available += definition.available;
    std::cout << "challenges: " << policy.catalog.size() << " in catalogue (" << available << " available), "
              << section.at("catalog").size() << " stored changes, " << section.at("progress").size()
              << " with progress\n"
              << "entitlements: " << snapshot.entitlements.size() << "\n";
    return policy.catalog.empty() ? 1 : 0;
}

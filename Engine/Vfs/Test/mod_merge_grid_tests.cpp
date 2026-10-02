// Runs the whole mod merge on a data root (first argument: a folder holding the
// game's Data/ and a Mods/ folder) and prints what it reported, so a merge of
// maps with custom surfaces can be checked without starting the game. Skips
// when no folder is given.
#include "Engine/Vfs/mod_catalog.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2 || !std::filesystem::exists(std::filesystem::path(argv[1]) / "Data")) {
        std::cout << "No data root given; skipped.\n";
        return 0;
    }
    const auto catalog = dingosdk::mods::load_catalog(argv[1]);
    for (const auto& note : catalog.notes) std::cout << "note: " << note << '\n';
    for (const auto& warning : catalog.warnings) std::cout << "WARNING: " << warning << '\n';
    if (!catalog.issue.empty()) std::cout << "ISSUE: " << catalog.issue << '\n';
    std::cout << (catalog.merged ? "merged\n" : "not merged\n");
    return catalog.issue.empty() && catalog.merged ? 0 : 1;
}

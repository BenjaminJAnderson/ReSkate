#include "Engine/Game/UI/loading_screen_selection.h"
#include <cstdio>
#include <stdexcept>
using namespace dingosdk::loading_screen;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        check(same_map("Levels/Game/BAM_LevelRoot/BAM_LevelRoot", "levels\\game\\BAM_LevelRoot\\BAM_LevelRoot"), "Map matching ignores asset path case and slash direction");
        check(!same_map("Levels/Game/BAM", "Levels/Game/BAM_Extra"), "Map matching requires the full destination");
        Pending p{123, 1000, "grom"};
        check(p.take(123, true, 500).empty() && p.destination == "grom", "Boot screen cannot consume a level screen request");
        check(p.take(456, false, 500).empty() && p.destination == "grom", "An unrelated controller cannot consume the request");
        check(p.take(123, false, 500) == "grom" && p.take(123, false, 501).empty(), "Destination applies once before the native description updates");
        p = {123, 1000, "bam"};
        check(p.take(123, false, 1000).empty(), "Expired requests cannot affect later loads");
        p = {123, 1000, "bam"}; p.clear();
        check(p.take(123, false, 500).empty(), "Cancelled requests cannot affect later loads");
        std::array<std::uint8_t, 15> conditions; conditions.fill(1);
        check(unconditional(conditions, 0), "Stock all-session all-platform artwork is eligible");
        check(!unconditional(conditions, 1), "Custom inclusion criteria remain under native control");
        conditions[11] = 0;
        check(!unconditional(conditions, 0), "Session-specific artwork remains under native control");
        std::puts("Loading-screen selection checks passed."); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

#pragma once
#include <cstdint>

// Authored graphs that re-post themselves every frame and get nothing done. Every graph run
// costs the game thread, so these are quietened, each through the one call that re-posts it:
//
// - three debug-only loops re-post first and then find their debug switch off (a retail
//   setting stub, or an instance flag that is always clear): the re-post becomes a no-op, so
//   they run once and stop. MPActivityCoordinator.OnDebugDrawUpdate (0xe300874d),
//   InputVisualization_Dev.OnUpdate (0xe399e493, two instances) and
//   ActivityLeaderboardManager.OnDebugPrintLoop (0xf5a89f23);
// - four loops wait for something offline play never gives them and ask again every frame:
//   NarrativeSet's failed Play Event retry (0xa4becd75, which also formats a log line each
//   time), QuickDropPersistenceManager.OnWaitForEntitlements (0xf64d93b7),
//   MilestoneManager.OnInitLoop (0x7fab7dbd) and QuestManager.OnDelayedInit (0xa24d0261).
//   Their waiting re-post now asks again after 1 s (60 frames), so they still finish if
//   what they wait for ever arrives.
//
// Only the waiting branch's call is changed; the graphs' other calls stay native.
namespace dingosdk::multiplayer {
// Call after every graph run, any realm (the expression pump). Other graphs return after one
// hash compare, lock free; each loaded copy of the seven above is checked and patched once.
void throttle_graph_ran(std::uintptr_t base, std::uintptr_t vm) noexcept;
} // namespace dingosdk::multiplayer

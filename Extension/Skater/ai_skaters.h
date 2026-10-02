#pragma once
#include <cstddef>
#include <cstdint>

// Experimental client-side AI skaters. The player skater blueprint carries
// Skate's AI stack (bt_default brain tree, client-only EATBrainTree, goals and
// navigator, SkaterAIComponent). These actors are created on the local client
// exactly like a skater source creates its skater, without a player
// association. The shipped CharacterBlueprint_Physics_Skater_AI is stale and
// cannot be spawned (see ai_skaters.cpp).
namespace dingosdk::ai_skaters {
// Console adapters. Requests run on the next verified client tick and report
// their results to the Skater log channel.
void request_spawn(unsigned count, bool physics, bool brain);
// Starts or pauses every AI skater's bt_default brain tree and native AI
// skater controller.
void request_brain(bool enabled);
void request_clear();
void request_report();
// Client thread only, after the native client tick.
void tick(std::uintptr_t base, std::uintptr_t client, bool ready) noexcept;
std::size_t active_count() noexcept;
} // namespace dingosdk::ai_skaters

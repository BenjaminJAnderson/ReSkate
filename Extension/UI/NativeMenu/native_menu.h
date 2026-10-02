#pragma once
#include <cstdint>
#include <string>

namespace dingosdk::overlay { struct CallbacksV3; }

namespace dingosdk::multiplayer {
// Call after the session tick, including while disconnected. All UI model
// operations execute on the client thread under the native model lock.
void tick_native_menu(std::uintptr_t base, bool loading) noexcept;
// Run on the client thread BEFORE submitting a map load, while widget assets
// and dynamic schemas still exist. Returns false if cleanup could not finish.
bool prepare_native_menu_level_load(std::uintptr_t base) noexcept;
// Register with the validated client-transition hook, before the native handler
// releases assets. Suspends updates through native loads, permanently on exit.
void native_menu_before_level_transition(std::uintptr_t base, unsigned next) noexcept;
// Uses the same validated runtime queues as the overlay. The reader must be a
// snapshot reader; it must not consume the overlay's console log cursor.
void set_native_menu_callbacks(const overlay::CallbacksV3& callbacks);
} // namespace dingosdk::multiplayer

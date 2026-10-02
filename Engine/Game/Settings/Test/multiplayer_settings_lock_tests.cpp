// Which engine settings are locked during a multiplayer session.
#include "Engine/Game/Settings/multiplayer_settings_lock.h"
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED: " << what << "\n"; ++failures; }
}
} // namespace

int main() {
    using dingosdk::locked_in_multiplayer;
    // The console's names and the debug settings catalog's ("...Settings") alike.
    check(locked_in_multiplayer("SimulationTime.TimeScale"), "slow motion (console)");
    check(locked_in_multiplayer("SimulationTimeSettings.TimeScale"), "slow motion (catalog)");
    check(locked_in_multiplayer("simulationtime.forcedeltatime"), "case is ignored");
    check(locked_in_multiplayer("FBCorePhysics.EnableDebugJumps"), "debug jumps");
    check(locked_in_multiplayer("DingoGameplaySettings.UpdatedShuvSpeedBoostRatio"), "shuv speed");
    check(locked_in_multiplayer("DelMarGame.DelMarEnableAutoTrickFeature"), "auto tricks");
    check(locked_in_multiplayer("DingoActivity.ChallengeDefaultMaxTimer"), "challenge timers");
    check(locked_in_multiplayer("DingoThrowdowns.WinCondition"), "throwdown settings");
    check(locked_in_multiplayer("Physics.DefaultClientWorldCapacity.BodyCount"), "nested physics fields");
    check(locked_in_multiplayer("AutoPlayerSettings.RunScript") && locked_in_multiplayer("AutoPlayers.RunScript"), "auto players");
    check(!locked_in_multiplayer("EmitterSystem.TimeScale"), "particle time scale is looks only");
    check(!locked_in_multiplayer("WorldRender.ShadowsEnabled"), "graphics stay free");
    check(!locked_in_multiplayer("Input.LeftStickDeadZoneCenter"), "input preferences stay free");
    check(!locked_in_multiplayer("PhysicsDebug.DrawShapes") && !locked_in_multiplayer("PhysicsRender.Enabled"),
          "physics debug drawing is not physics");
    check(!locked_in_multiplayer("SimulationTime") && !locked_in_multiplayer(""), "a group alone is not a setting");
    check(!locked_in_multiplayer("Settings.TimeScale"), "a bare suffix is not a group");

    check(!dingosdk::multiplayer_settings_locked(), "unlocked outside a session");
    dingosdk::set_multiplayer_session_active(true);
    check(dingosdk::multiplayer_settings_locked(), "locked in a session");
    dingosdk::set_multiplayer_session_active(false);
    if (!failures) std::cout << "Multiplayer settings lock: ok\n";
    return failures ? 1 : 0;
}

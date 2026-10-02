#pragma once
#include "Engine/Game/Input/controller_bindings.h"
#include "Engine/Game/Input/playstation_report.h"

namespace dingosdk::overlay::detail {
struct PlayStationSample {
    bool present{};   // A Sony pad is open, even if another source supplies input.
    bool available{}; // It sent a report recently; `pad` is current.
    PlayStationGamepad pad;
    ControllerStyle style{};
    unsigned generation{}; // Changes whenever the set of open pads changes.
};
// Reads DualShock 4 / DualSense pads straight from HID. Thread-safe; cheap to
// call every frame. Pads hidden by HidHide or opened exclusively by another
// tool are skipped, and that tool's virtual XInput pad is read instead.
PlayStationSample read_playstation_pads();
}

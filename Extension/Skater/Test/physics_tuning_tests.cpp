// The physics tuning differences a host sends its guests: round trips, refusals, and (given
// the game folder) the game's own tuning read from its data.
//   dingosdk_physics_tuning_tests [Skate folder]
#include "Extension/Skater/physics_tuning_model.h"
#include "Engine/Game/Build/20260929/physics_tuning.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace dingosdk::physics_tuning;
int failures{};
void check(bool ok, const char *what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}
void set_float(std::vector<std::uint8_t> &image, std::size_t offset, float value) { std::memcpy(image.data() + offset, &value, 4); }
float get_float(const std::vector<std::uint8_t> &image, std::size_t offset) {
    float value;
    std::memcpy(&value, image.data() + offset, 4);
    return value;
}
Curve curve(float y) {
    Curve c{0, 1, std::vector<std::uint8_t>(2 * 0x1c)};
    for (std::size_t p = 0; p < 2; ++p) {
        const float x = static_cast<float>(p);
        std::memcpy(c.points.data() + p * 0x1c + 0xc, &x, 4);
        std::memcpy(c.points.data() + p * 0x1c + 0x14, &y, 4);
    }
    return c;
}
Model synthetic() {
    Model m;
    m.image.assign(0x100, 0);
    // 0x20, 0x24, 0x28: reals; 0x2c: a flag; 0x30: a pointer slot; 0x40: an int.
    m.fields = {{0x20, 4, true, false}, {0x24, 4, true, false}, {0x28, 4, true, false}, {0x2c, 1, false, true}, {0x40, 4, false, false}};
    set_float(m.image, 0x20, 1.5f);
    set_float(m.image, 0x24, 2.5f);
    set_float(m.image, 0x28, 3.5f);
    m.curve_slots = {0x30, 0x38};
    m.curves = {curve(1), curve(2)};
    return m;
}
void codec() {
    const auto m = synthetic();
    Values live{m.image, m.curves};
    check(encode_differences(m, live, 16384).bytes.empty(), "The game's own tuning has differences");
    const auto same = apply_differences(m, {});
    check(same && same->image == m.image && same->curves == m.curves, "No differences is not the game's tuning");

    set_float(live.image, 0x24, 9.f);
    set_float(live.image, 0x28, 10.f);
    live.image[0x2c] = 1;
    live.image[0x31] = 0x7f; // a pointer: never sent
    live.curves[1] = curve(5);
    const auto encoded = encode_differences(m, live, 16384);
    check(encoded.runs == 1 && encoded.curves == 1, "Adjacent changes are not one run, or the curve was left out");
    const auto applied = apply_differences(m, encoded.bytes);
    check(applied && get_float(applied->image, 0x24) == 9.f && get_float(applied->image, 0x28) == 10.f &&
              applied->image[0x2c] == 1 && get_float(applied->image, 0x20) == 1.5f,
          "Changed values lost");
    check(applied && applied->image[0x31] == 0, "A pointer byte was sent");
    check(applied && applied->curves[1] == curve(5) && applied->curves[0] == m.curves[0], "Curves lost");

    // Untouched curves are the game's even when the guest's own differ: the target is the host's.
    check(applied && applied->curves.size() == m.curve_slots.size(), "Curve slots lost");

    // Only curves that fit are sent.
    const auto tight = encode_differences(m, live, encoded.bytes.size() - 1);
    check(tight.left_out == 1 && tight.curves == 0, "An oversized curve was not left out");

    // Refusals: a write outside the fields, a bad slot, trailing bytes, a truncation.
    auto outside = encoded.bytes;
    outside[4] = 0x2d; // run offset 0x2d: inside no field start
    check(!apply_differences(m, outside), "A run outside the fields applied");
    for (std::size_t i = 1; i < encoded.bytes.size(); ++i)
        check(!apply_differences(m, std::span(encoded.bytes.data(), i)), "Truncated differences applied");
    auto longer = encoded.bytes;
    longer.push_back(0);
    check(!apply_differences(m, longer), "Differences with trailing bytes applied");
    auto slot = encoded.bytes;
    const auto curve_at = encoded.bytes.size() - (12 + 2 * 0x1c);
    slot[curve_at] = 0x34;
    check(!apply_differences(m, slot), "A curve for an unknown slot applied");

    // Values that would break the game keep the game's.
    set_float(live.image, 0x24, std::numeric_limits<float>::infinity());
    live.image[0x2c] = 7;
    const auto bad = apply_differences(m, encode_differences(m, live, 16384).bytes);
    check(bad && get_float(bad->image, 0x24) == 2.5f && bad->image[0x2c] == 0, "A non-finite real or bad flag applied");
    auto nan_curve = live;
    nan_curve.curves[1]->min = std::numeric_limits<float>::quiet_NaN();
    check(!apply_differences(m, encode_differences(m, nan_curve, 16384).bytes), "A curve with a NaN applied");
}
void game(const char *folder) {
    const auto started = std::chrono::steady_clock::now();
    Model m;
    try {
        m = read_game_tuning(folder);
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        ++failures;
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    std::size_t curves{};
    for (const auto &c : m.curves) curves += c.has_value();
    std::cout << m.fields.size() << " values, " << m.curve_slots.size() << " curve slots (" << curves << " curves), "
              << elapsed.count() << " ms\n";
    namespace addr = dingosdk::game::build::v20260929::physics_tuning;
    check(m.image.size() == addr::asset_size && m.fields.size() > 500 && curves > 40, "The game's tuning looks incomplete");
    // Only real FloatCurves are synced: asset +0x3090 (Lip +0x280, copied to the skater block's
    // +0x7e0) is a keyed table, and writing a curve's range over it crashed a guest on a coping.
    check(curves == m.curve_slots.size(), "A pointer that is not a FloatCurve is synced as one");
    check(std::ranges::find(m.curve_slots, std::uint16_t{0x3090}) == m.curve_slots.end(),
          "The keyed table at +0x3090 is synced as a curve");
    // Trucks.TruckYPos is at 0x2a0 + 52 (the offsets the game's own code reads).
    const auto truck_y = std::ranges::find(m.fields, std::uint16_t{0x2a0 + 52}, &Field::offset);
    check(truck_y != m.fields.end() && truck_y->real && std::isfinite(get_float(m.image, truck_y->offset)), "TruckYPos missing");
    // A host who moved the trucks: one run, applied back exactly.
    Values live{m.image, m.curves};
    set_float(live.image, 0x2a0 + 52, get_float(m.image, 0x2a0 + 52) + 0.01f);
    const auto encoded = encode_differences(m, live, 16384);
    const auto applied = apply_differences(m, encoded.bytes);
    check(encoded.runs == 1 && encoded.bytes.size() < 32 && applied && applied->image == live.image, "TruckYPos round trip failed");
}
} // namespace

int main(int argc, char **argv) {
    codec();
    if (argc > 1 && *argv[1]) game(argv[1]);
    else std::cout << "No game folder given; the game's own tuning was not read.\n";
    if (failures) return 1;
    std::cout << "physics tuning: ok\n";
    return 0;
}

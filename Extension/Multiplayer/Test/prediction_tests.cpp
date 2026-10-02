#include "Extension/Multiplayer/Net/protocol.h"
#include "Extension/Multiplayer/Session/client_timing.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace dingosdk::multiplayer;
namespace {
void check(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
bool near(float a, float b) { return std::abs(a - b) < .0001f; }
Packet frame(unsigned sequence, std::uint64_t time, float x, unsigned interval = 50000) {
    Packet p;
    p.kind = PacketKind::pose;
    p.sequence = sequence; p.time_us = time; p.epoch = 1;
    p.pose_interval_us = interval;
    p.pose.root.position = {x, 1, 3};
    p.pose.skater.resize(395); p.pose.board.resize(17);
    p.pose.skater[1].position = {x + .1f, 1.1f, 3};
    p.pose.board[0].position = {x + .2f, .1f, 3.1f};
    p.pose.board[2].position = {x + .2f, .09f, 3.1f};
    p.pose.skater[10].rotation = {0, 0, 1, 0};
    p.pose.board[5].rotation = {1, 0, 0, 0};
    p.pose.board[4].position = {.01f, .02f, .03f};
    return p;
}
Packet seed(PoseBuffer &buffer, unsigned interval = 50000, float speed = 10) {
    Packet last;
    for (unsigned i = 0; i < 3; ++i) {
        last = frame(i + 1, 1000000 + i * std::uint64_t{interval},
            speed * static_cast<float>(i * interval) / 1000000.f, interval);
        check(buffer.push(last, last.time_us + 7000000), "Fixture rejected");
    }
    return last;
}
void gaps_and_alignment() {
    for (const unsigned interval : {8333U, 16666U, 33333U, 50000U, 100000U, 200000U}) {
        PoseBuffer buffer;
        const auto last = seed(buffer, interval);
        const auto at_latest = last.time_us + 7000000 + std::max(100000U, interval + 50000);
        const auto frozen = buffer.sample(at_latest + 25000);
        const auto predicted = buffer.sample_remote(at_latest + 25000);
        check(frozen && predicted && near(frozen->root.position[0], last.pose.root.position[0]) &&
              near(predicted->root.position[0], last.pose.root.position[0] + .25f), "Short gap did not preserve velocity");
        check(buffer.playback().mode == PosePlaybackMode::predicted && buffer.playback().prediction_us == 25000,
              "Prediction telemetry differs");
        auto expected = last.pose;
        offset_pose(expected, {.25f, 0, 0});
        const auto aligned = [](const Transform& a, const Transform& b) {
            return near(a.position[0], b.position[0]) && near(a.position[1], b.position[1]) &&
                near(a.position[2], b.position[2]) && a.rotation == b.rotation && a.scale == b.scale;
        };
        check(aligned(predicted->root, expected.root) &&
              std::equal(predicted->skater.begin(), predicted->skater.end(), expected.skater.begin(), expected.skater.end(), aligned) &&
              std::equal(predicted->board.begin(), predicted->board.end(), expected.board.begin(), expected.board.end(), aligned),
              "Prediction split board/skater anchors or changed recorded trick bones");
        const auto cap = buffer.sample_remote(at_latest + 100000);
        const auto held = buffer.sample_remote(at_latest + 400000);
        check(cap && held && near(cap->root.position[0], last.pose.root.position[0] + 1.f) &&
              held->root == cap->root && buffer.playback().mode == PosePlaybackMode::held,
              "Prediction runs indefinitely or snaps back after horizon");
        check(!buffer.sample_remote(last.time_us + 8000001), "Disconnected player never expires");
    }
    PoseBuffer fast;
    const auto last = seed(fast, 50000, 45);
    const auto pose = fast.sample_remote(last.time_us + 7200000);
    check(pose && near(pose->root.position[0] - last.pose.root.position[0], 2), "Prediction exceeds two metres");
}
void correction_and_reset() {
    PoseBuffer buffer;
    seed(buffer);
    constexpr std::uint64_t recovery = 8225000;
    const auto predicted = buffer.sample_remote(recovery);
    check(predicted && near(predicted->root.position[0], 1.25f), "Recovery fixture did not predict");
    auto stopped = frame(4, 1200000, 1);
    check(buffer.push(stopped, recovery), "Stopped pose rejected");
    const auto first = buffer.sample_remote(recovery);
    check(first && near(first->root.position[0], predicted->root.position[0]) && buffer.playback().correcting,
              "Small correction popped on arrival");
    const auto middle = buffer.sample_remote(recovery + 50000);
    check(middle && middle->root.position[0] > 1 && middle->root.position[0] < first->root.position[0],
              "Correction did not converge");
    stopped.sequence = 5; stopped.time_us += 50000;
    check(buffer.push(stopped, recovery + 50000), "Second stopped pose rejected");
    const auto corrected = buffer.sample_remote(recovery + 100000);
    check(corrected && near(corrected->root.position[0], 1) && !buffer.playback().correcting,
              "Successive packets extended correction into slow motion");
    const auto aligned = corrected->board[0].position[0] - corrected->root.position[0];
    check(near(aligned, .2f), "Correction changed board attachment");
    auto teleport = frame(6, 1300000, 100);
    check(buffer.push(teleport, recovery + 150000), "Teleport rejected");
    check(buffer.sample_remote(recovery + 150000)->root.position[0] == 100 &&
          !buffer.playback().correcting, "Teleport blended through old location");
    ++teleport.epoch; ++teleport.sequence; teleport.time_us += 50000;
    teleport.pose.root.position[0] = 200;
    check(buffer.push(teleport, recovery + 200000) && buffer.size() == 1 &&
          buffer.sample_remote(recovery + 200000)->root.position[0] == 200, "Reconnect retained prediction history");
    buffer.clear();
    check(!buffer.sample_remote(recovery + 250000) && buffer.playback().mode == PosePlaybackMode::unavailable,
              "Clear retained prediction or diagnostic state");
}
void bad_estimates_and_stalls() {
    PoseBuffer fast;
    auto last = seed(fast, 50000, 80);
    const auto fast_pose = fast.sample_remote(last.time_us + 7125000);
    check(fast_pose && fast_pose->root == last.pose.root && fast.playback().mode == PosePlaybackMode::held,
              "Implausible velocity extrapolated");
    PoseBuffer landing;
    seed(landing);
    check(landing.push(frame(4, 1150000, 1), 8150000), "Landing fixture rejected");
    check(landing.sample_remote(8275000)->root.position[0] == 1 && landing.playback().mode == PosePlaybackMode::held,
              "Abrupt landing/stop extrapolated");
    PoseBuffer buffer;
    seed(buffer);
    const auto before = buffer.sample_remote(8225000);
    check(before && buffer.push(frame(4, 1200000, 1), 8225000), "Stall fixture rejected");
    buffer.sample_remote(8225000);
    check(buffer.sample_remote(8525000) && !buffer.playback().correcting, "Client stall replayed correction backlog");
    auto older = frame(3, 1100000, 1);
    check(!buffer.push(older, 8530000), "Reordered packet changed prediction history");
    auto layout = frame(5, 1250000, 1);
    layout.pose.board.clear();
    check(buffer.push(layout, 8535000) && buffer.sample_remote(8535000)->board.empty() &&
          !buffer.playback().correcting, "Board removal retained predicted attachment");
    PoseBuffer burst;
    for (unsigned i = 0; i < 30; ++i)
        check(burst.push(frame(i + 1, 1000000 + i * 50000ULL, i * .5f), 9000000), "Burst rejected");
    const auto pose = burst.sample_remote(9100000);
    check(pose && near(pose->root.position[0], 14.5f), "Queued updates replayed as slow motion");
}
void reusable_playback_buffers() {
    PoseBuffer buffer;
    const auto last = seed(buffer);
    Pose out;
    check(buffer.sample(last.time_us + 7100000, out), "Reusable sample was unavailable");
    const auto *skater = out.skater.data();
    const auto *board = out.board.data();
    check(buffer.sample_remote(last.time_us + 7125000, out), "Reusable remote sample was unavailable");
    check(out.skater.data() == skater && out.board.data() == board,
          "Playback replaced same-sized transform storage instead of reusing it");
    check(out.skater.size() == 395 && out.board.size() == 17,
          "Reusable playback changed the native pose layout");
}
void client_timing() {
    ClientTiming timing;
    for (unsigned frame_index = 0; frame_index < 61; ++frame_index) {
        const auto now = 1000000 + frame_index * 16667ULL;
        timing.begin(now);
        timing.record(ClientTiming::network, now, now + 2000);
        timing.record(ClientTiming::render, now + 2000, now + 3000);
        timing.finish(now, now + 4000);
    }
    auto result = timing.snapshot(2004000);
    check(result.callback_hz > 59 && result.callback_hz < 61 && near(result.work_ms, 4) &&
          near(result.mean_ms[1], 2) && near(result.mean_ms[3], 1), "Client timings do not separate receive/render work");
    timing.begin(2300000);
    timing.record(ClientTiming::capture, 2300000, 2330000);
    timing.finish(2300000, 2331000);
    result = timing.snapshot(2331000);
    check(result.gap_max_ms > 299 && result.work_max_ms == 31 && result.peak_ms[0] == 30,
          "Client gap or capture hitch disappeared from diagnostics");
}
}
int main() {
    try {
        gaps_and_alignment(); correction_and_reset(); bad_estimates_and_stalls(); reusable_playback_buffers();
        client_timing();
        std::cout << "Prediction: 20/10/5 TPS gaps, 100ms/2m bounds, exact board/trick alignment, correction convergence, "
                     "stops, teleports, reconnects, stale data, bursts and client timing checks passed.\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

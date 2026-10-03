#include "graph_throttle.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/graph_throttle.h"
#include "Engine/Core/Platform/memory.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <format>
#include <stdexcept>

namespace dingosdk::multiplayer {
namespace {
using Address = std::uintptr_t;

// The two re-post natives as the loader binds them in a call slot: the functions of their
// {fn, hash} table rows. Each takes one pointer per operand, five here, and writes none back.
//   PostEventDelayedSeconds 0x5e3aee8e: self, event, port offset, payload, float seconds.
//   PostEventDelayedFrames  0x9967fc07: self, payload, port offset, payload type, int frames.
constexpr std::uint32_t post_seconds_hash = 0x5e3aee8e, post_frames_hash = 0x9967fc07;
constexpr Address post_seconds_fast = addr::graph_throttle::post_seconds_fast,
                  post_frames_fast = addr::graph_throttle::post_frames_fast;
using PostSeconds = void (*)(const void*, const void*, const void*, const void*, const float*);
using PostFrames = void (*)(const void*, const void*, const void*, const void*, const std::int32_t*);

// How long a waiting loop now waits before it asks again.
constexpr float wait_seconds = 1.0f;
constexpr std::int32_t wait_frames = 60;
std::atomic<Address> image{}; // Skate.exe's base, set before the first slot is pointed here

void post_seconds_later(const void* self, const void* event, const void* port, const void* payload, const float*) {
    reinterpret_cast<PostSeconds>(image.load(std::memory_order_acquire) + post_seconds_fast)(self, event, port, payload,
                                                                                               &wait_seconds);
}
void post_frames_later(const void* self, const void* payload, const void* port, const void* type, const std::int32_t*) {
    reinterpret_cast<PostFrames>(image.load(std::memory_order_acquire) + post_frames_fast)(self, payload, port, type,
                                                                                            &wait_frames);
}
void skip_post(const void*, const void*, const void*, const void*, const void*) {}

enum class Change : std::uint8_t { stop, wait };
// Each graph as shipped (res_all dump, build 20260908): header +0x20 frame bytes, +0x24
// constant bytes, +0x2c code words; code at code_offset with its first word; the native
// call table of {hash, pc} pairs. The graph calls `native` `sites` times; the one patched is
// at `pc` (word 0 = next << 8 | 4: four operands and the delay), with S0 as its first operand
// and the zero delay constant K`delay` as its fifth.
struct Graph {
    std::uint32_t hash;
    const char* name;
    std::uint32_t frame, constants, code_words, code_offset, first_word, table_offset, table_count;
    std::uint32_t native, sites, pc, next, delay;
    Change change;
};
constexpr std::array graphs{
    // Re-posts at pc 0x38, then GetBoolSetting("CommunityEvent.DebugDrawQueues") (a retail stub:
    // its default, false) sends it to RETURN.
    Graph{0xe300874d, "MPActivityCoordinator.OnDebugDrawUpdate", 0x90, 0x1c0, 0x309, 0x360, 0xc27, 0xfb4, 48,
          post_seconds_hash, 1, 0x38, 0x6c, 0xec, Change::stop},
    // Re-posts at pc 0x38, then returns while field +0x50 is clear (every traced run).
    Graph{0xe399e493, "InputVisualization_Dev.OnUpdate", 0x40, 0x158, 0x2ce, 0x248, 0xc27, 0xd90, 63,
          post_seconds_hash, 1, 0x38, 0x6c, 0xcc, Change::stop},
    // Re-posts at pc 0x38; its per-entry print loop needs field +0x118 and the retail-stubbed
    // "DingoActivity.DrawActivityDebug".
    Graph{0xf5a89f23, "ActivityLeaderboardManager.OnDebugPrintLoop", 0x70, 0x1d8, 0x2b7, 0x3a8, 0xc27, 0xebc, 49,
          post_seconds_hash, 1, 0x38, 0x6c, 0x110, Change::stop},
    // Play Event failed (pc 0x20 -> 0x2454): logs "…retrying for {0}" and re-posts the
    // activation at pc 0x24ec. Its other re-post (pc 0x608, success path) stays.
    Graph{0xa4becd75, "NarrativeSet Play Event retry", 0xb0, 0x888, 0x949, 0xa58, 0x102e, 0x3044, 167,
          post_seconds_hash, 2, 0x24ec, 0x2520, 0x35c, Change::wait},
    // Not IsOnline && IsSessionReady (pc 0x84 -> 0xadc): asks again at pc 0xadc.
    Graph{0xf64d93b7, "QuickDropPersistenceManager.OnWaitForEntitlements", 0x60, 0x210, 0x2c5, 0x3b0, 0xc27, 0xee4, 52,
          post_seconds_hash, 1, 0xadc, 0xb10, 0x124, Change::wait},
    // Not ready (pc 0x198 -> 0x4f8): asks again at pc 0x4f8. Its ready re-post (pc 0x4c4) stays.
    Graph{0x7fab7dbd, "MilestoneManager.OnInitLoop", 0x40, 0x100, 0x14c, 0x210, 0xc27, 0x750, 26, post_seconds_hash, 2,
          0x4f8, 0x52c, 0xa8, Change::wait},
    // Not IsOnline && quests present (pc 0x84 -> 0xc8): asks again next frame at pc 0xc8. Its
    // ready branch (pc 0x94, two frames) stays.
    Graph{0xa24d0261, "QuestManager.OnDelayedInit", 0x40, 0x28, 0x40, 0xb8, 0xc27, 0x1b8, 5, post_frames_hash, 2, 0xc8,
          0xfc, 0x1c, Change::wait},
};

// The last copy of each graph that did not match, so it is not checked again every run.
std::array<std::atomic<Address>, graphs.size()> refused{};

template <class T> T read(Address at) {
    T value{};
    if (!memory::peek(at, value)) throw std::runtime_error("memory unavailable");
    return value;
}

Address fast_binding(const Graph& graph) {
    return graph.native == post_frames_hash ? post_frames_fast : post_seconds_fast;
}
void* replacement(const Graph& graph) {
    if (graph.change == Change::stop) return reinterpret_cast<void*>(&skip_post);
    return graph.native == post_frames_hash ? reinterpret_cast<void*>(&post_frames_later)
                                            : reinterpret_cast<void*>(&post_seconds_later);
}

// This copy's re-post slot, if the copy is exactly the shipped graph.
Address slot_of(Address resource, const Graph& graph) {
    if (read<std::uint32_t>(resource + 0x20) != graph.frame || read<std::uint32_t>(resource + 0x24) != graph.constants ||
        read<std::uint32_t>(resource + 0x2c) != graph.code_words)
        throw std::runtime_error("header differs");
    const auto code = resource + graph.code_offset;
    if (read<std::uint32_t>(code) != graph.first_word) throw std::runtime_error("code differs");
    return code + graph.pc + 4;
}

// Points this copy's re-post at the replacement, after checking the call table and the call.
void patch(Address base, Address resource, const Graph& graph, Address slot) {
    const auto code = resource + graph.code_offset;
    unsigned sites{};
    bool found{};
    for (std::uint32_t i = 0; i < graph.table_count; ++i) {
        const auto entry = read<std::array<std::uint32_t, 2>>(resource + graph.table_offset + 8 * std::size_t{i});
        if (entry[1] >= graph.code_words * 4) throw std::runtime_error("call table differs");
        if (entry[0] != graph.native) continue;
        ++sites;
        found = found || entry[1] == graph.pc;
    }
    if (sites != graph.sites || !found) throw std::runtime_error("re-post calls differ");
    // Four operands and the delay: self (S0), ..., the zero delay constant.
    if (read<std::uint32_t>(code + graph.pc) != ((graph.next << 8) | 0x04) ||
        read<std::array<std::uint32_t, 2>>(code + graph.pc + 12) != std::array<std::uint32_t, 2>{3, 0} ||
        read<std::array<std::uint32_t, 2>>(code + graph.pc + 12 + 8 * 4) != std::array<std::uint32_t, 2>{0, graph.delay})
        throw std::runtime_error(std::format("re-post at pc {:#x} differs", graph.pc));
    const auto bound = read<Address>(slot);
    if (bound != base + fast_binding(graph))
        throw std::runtime_error(std::format("re-post at pc {:#x} is bound to {:#x}, expected {:#x}", graph.pc,
                                             bound - base, fast_binding(graph)));
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(slot), &info, sizeof(info)) ||
        !(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
        throw std::runtime_error("graph memory is not writable");
    image.store(base, std::memory_order_release);
    // These graphs run only in the client realm, on the thread running this pump.
    *reinterpret_cast<void**>(slot) = replacement(graph);
}

void settle(Address base, Address resource, std::size_t index) noexcept {
    const auto& graph = graphs[index];
    if (refused[index].load(std::memory_order_relaxed) == resource) return;
    try {
        const auto slot = slot_of(resource, graph);
        if (read<Address>(slot) == reinterpret_cast<Address>(replacement(graph))) return; // this copy is done
        patch(base, resource, graph, slot);
        if (graph.change == Change::stop)
            logging::log(logging::Level::info, logging::Channel::progression,
                         "Graph throttle: {} ({:#x}) stopped; its runs only drew debug output.", graph.name, graph.hash);
        else
            logging::log(logging::Level::info, logging::Channel::progression,
                         "Graph throttle: {} ({:#x}) now waits 1 s between attempts.", graph.name, graph.hash);
    } catch (const std::exception& e) {
        refused[index].store(resource, std::memory_order_relaxed);
        logging::log(logging::Level::warning, logging::Channel::progression,
                     "Graph throttle: {} ({:#x}) left native: {}.", graph.name, graph.hash, e.what());
    } catch (...) {
        refused[index].store(resource, std::memory_order_relaxed);
    }
}
} // namespace

bool throttled_graph(std::uint32_t hash) noexcept {
    for (const auto& graph : graphs)
        if (graph.hash == hash) return true;
    return false;
}

void throttle_graph_ran(Address base, Address vm) noexcept {
    // The pump has just read these for the same run: the expression and its resource are alive.
    if (!vm) return;
    const auto resource = *reinterpret_cast<const Address*>(vm + 0x38);
    if (!resource) return;
    const auto hash = *reinterpret_cast<const std::uint32_t*>(resource + 0x10);
    for (std::size_t i = 0; i < graphs.size(); ++i)
        if (graphs[i].hash == hash) {
            settle(base, resource, i);
            return;
        }
}
} // namespace dingosdk::multiplayer

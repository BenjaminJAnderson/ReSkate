#include "native_route_lookahead.h"
#include "route_cycle_detector.h"
#include "route_endpoint_stub.h"
#include "route_predecessor_stub.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/route_lookahead.h"

#include <atomic>
#include <intrin.h>
#include <mutex>
#include <span>
#include <utility>

namespace dingosdk {
namespace {
using Lookahead = bool (*)(std::uintptr_t, float);
using ExitCount = unsigned (*)(std::uintptr_t);
using namespace addr::route_lookahead;

Lookahead original_lookahead{};
ExitCount original_exit_count{};
std::uintptr_t exit_count_return{}, installed_base{};
std::mutex install_mutex;
std::atomic<bool> reported_cycle{};
thread_local RouteCycleDetector* active_walk{};
struct EndpointGuard {
    const RouteLookaheadContract& contract;
    RouteEndpointScan scan;
    void* code{};
};
std::array endpoint_guards{
    EndpointGuard{route_endpoint_contract, RouteEndpointScan::forward},
    EndpointGuard{route_reverse_endpoint_contract, RouteEndpointScan::reverse},
    EndpointGuard{route_conflict_approach_contract, RouteEndpointScan::conflictApproach},
    EndpointGuard{route_conflict_departure_contract, RouteEndpointScan::conflictDeparture},
};
struct ProjectionGuard {
    const RouteLookaheadContract& contract;
    void* code{};
};
std::array projection_guards{
    ProjectionGuard{route_array_projection_contract},
    ProjectionGuard{route_vector_projection_contract},
};

bool prepare_guard_code(void*& destination, std::span<const unsigned char> code) {
    if (destination) return true;
    auto* allocation = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!allocation) return false;
    std::memcpy(allocation, code.data(), code.size());
    DWORD previous{};
    if (!VirtualProtect(allocation, code.size(), PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), allocation, code.size())) {
        VirtualFree(allocation, 0, MEM_RELEASE);
        return false;
    }
    destination = allocation;
    return true;
}

struct WalkScope {
    RouteCycleDetector detector;
    RouteCycleDetector* previous{std::exchange(active_walk, &detector)};
    ~WalkScope() { active_walk = previous; }
};

bool lookahead(std::uintptr_t follower, float elapsed) {
    WalkScope scope;
    return original_lookahead(follower, elapsed);
}

unsigned exit_count(std::uintptr_t node) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto count = original_exit_count(node);
    if (caller != exit_count_return || !active_walk || !active_walk->observe(node, count)) return count;
    if (!reported_cycle.exchange(true, std::memory_order_relaxed))
        logging::write(logging::Level::info, logging::Channel::world,
                       "NPC route cycle detected; keeping the native minimum lookahead.");
    // Only this caller's == 1 continuation test changes; graph degree and path selection stay native.
    return 2;
}
}

bool start_native_route_lookahead(std::uintptr_t base, std::string& error) {
    std::lock_guard lock(install_mutex);
    if (installed_base) return installed_base == base;
    for (const auto* contract : {&route_lookahead_contract, &route_endpoint_contract,
                                &route_reverse_endpoint_contract, &route_conflict_approach_contract,
                                &route_conflict_departure_contract, &route_array_projection_contract,
                                &route_vector_projection_contract}) {
        for (const auto& site : contract->sites) {
            std::array<unsigned char, 32> actual{};
            if (!base || !memory::read(base + site.rva, actual) || actual != site.bytes) {
                error = "NPC route guard fingerprint mismatch";
                return false;
            }
        }
    }
    for (auto& guard : endpoint_guards) {
        const auto code = route_endpoint_stub(base + guard.contract.sites[1].rva,
                                             base + guard.contract.sites[2].rva, guard.scan);
        if (!prepare_guard_code(guard.code, code)) {
            error = "Cannot prepare NPC endpoint guard";
            return false;
        }
    }
    for (auto& guard : projection_guards) {
        const auto code = route_predecessor_stub(base + guard.contract.sites[1].rva,
                                                base + guard.contract.sites[2].rva);
        if (!prepare_guard_code(guard.code, code)) {
            error = "Cannot prepare NPC projection guard";
            return false;
        }
    }
    auto* walk = reinterpret_cast<void*>(base + route_lookahead_contract.sites[0].rva);
    auto* count = reinterpret_cast<void*>(base + route_lookahead_contract.sites[1].rva);
    constexpr auto projection_offset = 2 + endpoint_guards.size();
    std::array<void*, projection_offset + projection_guards.size()> targets{walk, count};
    for (std::size_t i = 0; i < endpoint_guards.size(); ++i)
        targets[i + 2] = reinterpret_cast<void*>(base + endpoint_guards[i].contract.sites[0].rva);
    for (std::size_t i = 0; i < projection_guards.size(); ++i)
        targets[i + projection_offset] = reinterpret_cast<void*>(base + projection_guards[i].contract.sites[0].rva);
    exit_count_return = base + route_exit_count_return;
    const auto fail = [&](const char* operation) {
        error = operation;
        for (auto* target : targets) hook_disable(target);
        for (auto* target : targets) hook_remove(target);
        return false;
    };
    if (hook_prepare(walk, reinterpret_cast<void*>(&lookahead), reinterpret_cast<void**>(&original_lookahead)) != HookOk ||
        hook_prepare(count, reinterpret_cast<void*>(&exit_count), reinterpret_cast<void**>(&original_exit_count)) != HookOk)
        return fail("Cannot prepare NPC route lookahead hooks");
    for (std::size_t i = 0; i < endpoint_guards.size(); ++i)
        if (hook_prepare(targets[i + 2], endpoint_guards[i].code) != HookOk)
            return fail("Cannot prepare NPC route endpoint hooks");
    for (std::size_t i = 0; i < projection_guards.size(); ++i)
        if (hook_prepare(targets[i + projection_offset], projection_guards[i].code) != HookOk)
            return fail("Cannot prepare NPC route projection hooks");
    for (auto* target : targets)
        if (hook_queue_enable(target) != HookOk)
            return fail("Cannot queue NPC route lookahead hooks");
    if (hook_apply_queued() != HookOk)
        return fail("Cannot enable NPC route lookahead hooks");
    installed_base = base;
    return true;
}
}

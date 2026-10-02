#include "runtime_internal.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Profiling/profiler.h"
#include "Engine/Game/UI/menu_entry.h"
#include <set>
#include <utility>

namespace dingosdk::runtime::detail {
namespace {
// This void OnBegin graph builds ID_SESSION_JOIN_FAIL. Intercepting its exact
// serialized identity leaves input handling and unrelated error dialogs native.
using MenuExpression = void (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
MenuExpression original_menu_expression{};
void menu_expression(std::uintptr_t context, std::uintptr_t inputs,
                     std::uintptr_t outputs, std::uintptr_t observer) {
    const auto incoming_error = GetLastError();
    bool handled = false;
    try {
        DINGO_PROFILE_ZONE("hooks/menu expression");
        // Runs for every menu expression: the evaluator's live objects are peeked.
        std::uintptr_t graph{};
        if (runtime().menu_splash_ready.load(std::memory_order_relaxed) &&
            memory::peek(context + 8, graph) && dingosdk::is_menu_session_failure(graph,
            [](auto address, auto& value) { return memory::peek(address, value); }))
            handled = queue_menu_bam();
    } catch (...) {} // Queue failures preserve native behavior.
    SetLastError(incoming_error);
    if (!handled) original_menu_expression(context, inputs, outputs, observer);
}
}
void observe_player_card_expression(std::uintptr_t graph, std::uintptr_t runtime_graph, void** inputs) {
    // Bound this diagnostic to the authored card and object expressions. It observes
    // the normal invocation without replacing arguments or changing UI data.
    std::uintptr_t bytecode{}, value_pointer{}, value{}, instance_pointer{}, instance{};
    std::uint32_t hash{};
    if (!memory::peek(graph + 0x38, bytecode) || !memory::peek(bytecode + 0x10, hash)) return;
    const bool object_graph = hash == 0x7f3a3f0b || hash == 0x39fb3c0d;
    if (!object_graph && hash != 0xf751a1e4 && hash != 0xacfe6dce) return;
    const auto arguments = reinterpret_cast<std::uintptr_t>(inputs);
    if (!object_graph && (!read(arguments + 8, value_pointer) || !read(value_pointer, value))) return;
    (void)(read(arguments, instance_pointer) && read(instance_pointer, instance));
    if (object_graph) value = instance;
    static std::mutex mutex;
    static std::set<std::pair<std::uint32_t, std::uintptr_t>> seen;
    std::lock_guard lock(mutex);
    if (seen.size() < 64 && seen.emplace(hash, value).second)
        record(std::string("{\"event\":\"") + (object_graph ? "local_object_expression" : "local_card_expression") +
            "\",\"graph\":" + std::to_string(hash) +
            ",\"context\":" + std::to_string(value) + ",\"instance\":" + std::to_string(instance) +
            ",\"runtime_graph\":" + std::to_string(runtime_graph) + "}");
}
void start_menu_entry() {
    auto* target = reinterpret_cast<void*>(runtime().base + rt::menu_expression);
    bool enabled = false;
    if (matches(reinterpret_cast<std::uintptr_t>(target), rt::menu_expression_prefix)) {
        void* original{};
        if (dingosdk::hook_prepare(target, reinterpret_cast<void*>(&menu_expression), &original) == dingosdk::HookOk) {
            original_menu_expression = reinterpret_cast<MenuExpression>(original);
            enabled = dingosdk::hook_enable(target) == dingosdk::HookOk;
            if (!enabled) (void)dingosdk::hook_remove(target);
        }
    }
    record(std::string("{\"event\":\"menu_entry_initialized\",\"active\":") + (enabled ? "true}" : "false}"));
}
}

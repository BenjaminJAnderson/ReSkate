#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct EntitlementTriggerGraph {
    std::uint32_t key;
    std::array<std::uint32_t, 10> layout;
};

inline constexpr std::array entitlement_trigger_graphs{
    EntitlementTriggerGraph{0xe988e00a, {176, 2136, 718, 2441, 12, 0, 25, 1638575, 4063243, 16845056}},
    EntitlementTriggerGraph{0xb8fdb421, {176, 2136, 718, 2440, 12, 0, 25, 1966254, 4063254, 16845056}},
};

extern thread_local std::uintptr_t executing_expression;

struct ExpressionExecutionScope;

extern thread_local const ExpressionExecutionScope* expression_scope;

struct ExpressionExecutionScope {
    std::uintptr_t previous{executing_expression};
    const ExpressionExecutionScope* parent{expression_scope};
    std::uintptr_t vm;
    explicit ExpressionExecutionScope(std::uintptr_t current) : vm(current) {
        executing_expression = current; expression_scope = this;
    }
    ~ExpressionExecutionScope() { executing_expression = previous; expression_scope = parent; }
};

void execute_expression_hook(std::uintptr_t vm, std::uint32_t pc);

void execute_profiled_expression_hook(std::uintptr_t vm, std::uint32_t pc, std::uintptr_t profiler);

unsigned local_entitlement_trigger_graph();

void append_entitlement_trigger_context(std::ostringstream& event);

bool local_trigger_ready(unsigned predicate);

bool trigger_online_hook();

bool trigger_session_hook();
}

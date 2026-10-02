#pragma once
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct RipScoreRuntime {
    std::uint64_t (*get_context)(const void*){};
    std::uint64_t next_poll{};
    bool published{};
};

RipScoreRuntime& rip_score_runtime();

bool rip_score_type_contract(std::uintptr_t base);

void update_rip_score();
}

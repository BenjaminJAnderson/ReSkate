#pragma once
#include <cstdint>

namespace dingosdk::runtime {
// Call after image validation, before executable entry, outside the loader lock.
// Failure leaves initialization incomplete; the launcher must abort startup.
bool initialize_bootstrap(std::uintptr_t base);
}

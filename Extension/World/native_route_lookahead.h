#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {
bool start_native_route_lookahead(std::uintptr_t base, std::string& error);
}

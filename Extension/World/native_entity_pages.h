#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {
// Install before executable entry, before entity pages or spatial bounds storage
// are allocated. Both retain native allocation, snapshot and teardown ownership.
bool start_native_entity_pages(std::uintptr_t base, std::string& error);
}

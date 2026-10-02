#pragma once
#include <cstdint>
#include <string>
namespace dingosdk {
bool start_custom_script_loader(std::uintptr_t base, std::string& error);
}

#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace dingosdk {

// Flow_Splash/BC04A07_AG_CommandActionData_0_OnBegin: the void action
// presenting ID_SESSION_JOIN_FAIL. These scalar bytecode fields survive fixup.
inline constexpr std::array<unsigned char, 40> menu_session_failure_header{
    0x47,0x88,0xa0,0x59,0xff,0xff,0xff,0xff,0x08,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
    0x30,0x00,0x00,0x00,0x40,0x03,0x00,0x00,0xc9,0x05,0x00,0x00,0xd9,0x02,0x00,0x00,
    0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

template<class Read>
bool is_menu_session_failure(std::uintptr_t graph, Read&& read) {
    constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
    if (!graph || graph > maximum - 0x40) return false;
    std::uintptr_t bytecode{};
    std::array<unsigned char, menu_session_failure_header.size()> header{};
    // The evaluator passes a runtime graph wrapper, not the serialized RES.
    // Its +0x38 field owns the compiled bytecode; +0x10 in the wrapper is a vtable.
    return read(graph + 0x38, bytecode) && bytecode && bytecode <= maximum - 0x38 &&
        read(bytecode + 0x10, header) && header == menu_session_failure_header;
}

} // namespace dingosdk

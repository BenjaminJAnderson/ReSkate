#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace dingosdk {

inline std::array<unsigned char, 86> route_predecessor_stub(std::uintptr_t advance,
                                                         std::uintptr_t clamp) noexcept {
    std::array<unsigned char, 86> code{
        0x4c,0x8b,0x4b,0x28,       // r9 = current link's NodeFrom
        0x49,0x83,0xe1,0xfb,
        0x4d,0x8b,0x51,0x18,       // r10 = adjacency array
        0x45,0x8b,0x42,0xfc,
        0x41,0x81,0xe0,0xff,0xff,0xff,0x7f,
        0x33,0xc9,
        0x41,0x3b,0xc8,            // loop: index < count
        0x73,0x2a,                 // exhausted: use native endpoint projection
        0x49,0x8b,0x14,0xca,
        0x48,0x83,0xe2,0xfb,
        0x48,0x8b,0x42,0x28,
        0x48,0x83,0xe0,0xfb,
        0x49,0x3b,0xc1,
        0x75,0x04,                 // preserve native incoming-link selection
        0xff,0xc1,
        0xeb,0xe2,
        0x48,0x8b,0xda,            // rbx = predecessor
        0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
        0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
    };
    std::memcpy(code.data() + 64, &advance, sizeof(advance));
    std::memcpy(code.data() + 78, &clamp, sizeof(clamp));
    return code;
}

}

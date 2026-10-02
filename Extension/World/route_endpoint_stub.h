#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace dingosdk {

enum class RouteEndpointScan { forward, reverse, conflictApproach, conflictDeparture };

inline std::array<unsigned char, 47> route_endpoint_stub(std::uintptr_t advance, std::uintptr_t stop,
                                                       RouteEndpointScan scan) noexcept {
    std::array<unsigned char, 47> code{
        0x48,0x8b,0x47,0x40,       // mov rax,[rdi+40h]: current link's destination
        0x48,0x83,0xe0,0xfb,       // and rax,-5: remove the native pointer tag
        0x4c,0x39,0xc0,            // cmp rax,r8: destination before this iteration
        0x74,0x14,                // je stop: no progress, including a self-link
        0xc5,0x78,0x2f,0xff,       // vcomiss xmm15,xmm7: original distance comparison
        0x76,0x0e,                // jbe stop
        0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
        0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
    };
    if (scan != RouteEndpointScan::forward) {
        code[0] = 0x49;
        code[2] = 0x43; // mov rax,[r11+offset]
        code[3] = scan == RouteEndpointScan::reverse ? 0x28 : 0x40;
        code[14] = scan == RouteEndpointScan::reverse ? 0xf8 : 0x78;
        code[16] = scan == RouteEndpointScan::reverse ? 0xee :
                   scan == RouteEndpointScan::conflictApproach ? 0xfe : 0xfd;
    }
    std::memcpy(code.data() + 25, &advance, sizeof(advance));
    std::memcpy(code.data() + 39, &stop, sizeof(stop));
    return code;
}

}

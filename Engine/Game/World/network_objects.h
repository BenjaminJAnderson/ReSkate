#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dingosdk {
struct NetworkObject {
    std::uint64_t id{}; // Scoped to its authenticated owner and session epoch.
    std::string item;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    float scale{1};
    bool operator==(const NetworkObject &) const = default;
};
struct NetworkObjectOwner {
    std::uint64_t owner{}, epoch{};
    std::vector<NetworkObject> objects;
};
struct NetworkObjectSnapshot {
    std::string map;
    std::vector<NetworkObject> objects;
};
} // namespace dingosdk

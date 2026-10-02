#pragma once

#include <cstdint>
#include <limits>

namespace dingosdk {

class RouteCycleDetector {
public:
    bool observe(std::uintptr_t node, unsigned exits) noexcept {
        if (exits != 1) {
            *this = {};
            return false;
        }
        if (cycle_) return true;
        if (!checkpoint_) {
            checkpoint_ = node;
            return false;
        }
        ++length_;
        if (node == checkpoint_) return cycle_ = true;
        if (length_ == power_) {
            checkpoint_ = node;
            length_ = 0;
            if (power_ <= std::numeric_limits<std::uint64_t>::max() / 2) power_ *= 2;
        }
        return false;
    }

private:
    std::uintptr_t checkpoint_{};
    std::uint64_t power_{1}, length_{};
    bool cycle_{};
};

}

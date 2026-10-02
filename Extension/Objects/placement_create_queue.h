#pragma once
#include "object_placements.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>

namespace dingosdk::profile_runtime {
// Private correlation IDs must never enter a native message. Its source enum
// is encoded in three bits and accepts only 0..4 in the native serializer.
struct PendingPlacementCreate {
    profile::PlacedObject object;
    std::uint32_t item{}, token{}, generation{};
};

class PlacementCreateQueue {
    std::optional<PendingPlacementCreate> pending_;
public:
    bool push(const profile::PlacedObject& object, std::uint32_t item,
              std::uint32_t token, std::uint32_t generation) {
        if (pending_ || !token || !generation || !profile::valid_placed_object(object)) return false;
        pending_ = PendingPlacementCreate{object, item, token, generation};
        return true;
    }
    void clear() { pending_.reset(); }
    bool empty() const { return !pending_; }
    std::optional<PendingPlacementCreate> take(std::uint32_t generation) {
        auto result = std::exchange(pending_, {});
        if (result && result->generation != generation) result.reset();
        return result;
    }
};

// Same recipe produced by the native create-message handler.
// The native server create routine copies it synchronously, including callbacks.
struct alignas(16) PlacementCreateRecipe {
    std::array<std::uint32_t, 4> header{0, 0, 1, 1};
    std::array<std::uint32_t, 20> words{};
    // This is the native simulation owner, not the ReSkate/Steam owner. The
    // latter lives in network_objects_detail::Key and must not enter this ABI.
    PlacementCreateRecipe(const PendingPlacementCreate& request, std::uint64_t native_owner) {
        words[0] = request.item;
        // The native free-roam drop uses a global value of 127 for normal drops.
        // This is message +0x55 / BuildKitComponent +0x3a, not the separate
        // request-entry byte at +0x48. 255 selects a different park context.
        words[2] = 0x7f;
        std::memcpy(words.data() + 4, &native_owner, sizeof(native_owner));
        std::memcpy(words.data() + 8, request.object.position.data(), sizeof(float) * 3);
        words[11] = 0x3f800000; // homogeneous position w = 1
        std::memcpy(words.data() + 12, request.object.rotation.data(), sizeof(float) * 4);
        words[16] = 1; // confirmed object
        words[17] = 3; // retail scripted-create source
    }
};
static_assert(offsetof(PlacementCreateRecipe, words) == 16 && sizeof(PlacementCreateRecipe) == 96);
}

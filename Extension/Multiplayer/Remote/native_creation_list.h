#pragma once
#include "native_pose_layout.h"
#include <algorithm>
#include <vector>

namespace dingosdk::multiplayer {
// BlueprintCreationParams+0x158 points at this temporary engine-owned page list.
// Pages hold 60 entity pointers. The final page stores a count instead of a next
// pointer at +8; bit 0 of its first word distinguishes those two layouts.
struct NativeCreationList {
    std::uintptr_t allocator{}, head{}, tail{}, spare{};
    std::uint32_t pages{}, spare_pages{};
};
static_assert(sizeof(NativeCreationList) == 0x28);
struct NativeCreatedEntities {
    std::vector<std::uintptr_t> pages, entities;
};
template <class Read>
NativeCreatedEntities read_native_creation_list(Read &&read, const NativeCreationList &list) {
    using namespace native_pose_detail;
    require(!list.spare && !list.spare_pages && list.pages <= 64, "Blueprint creation page count differs.");
    NativeCreatedEntities result;
    auto page = list.head;
    if (!page) {
        require(!list.tail && !list.pages, "Empty blueprint creation list differs.");
        return result;
    }
    while (page) {
        require(result.pages.size() < list.pages &&
                    std::find(result.pages.begin(), result.pages.end(), page) == result.pages.end(),
                "Blueprint creation page chain differs.");
        result.pages.push_back(page);
        const bool last = (value<std::uintptr_t>(read, page, "creation page flags") & 1) != 0;
        require(last == (page == list.tail), "Blueprint creation tail differs.");
        const auto count = last ? value<std::uint32_t>(read, add(page, 8), "creation count") : 60;
        // The native initializer assumes every page contains at least one item.
        require(count && count <= 60, "Blueprint creation item count exceeds bounds.");
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto entity = value<std::uintptr_t>(read, add(page, 0x10 + i * 8), "created entity");
            require(entity >= 0x10000 && entity <= highest &&
                        std::find(result.entities.begin(), result.entities.end(), entity) ==
                            result.entities.end(),
                    "Blueprint creation entity differs.");
            result.entities.push_back(entity);
        }
        page = last ? 0 : value<std::uintptr_t>(read, add(page, 8), "creation next page");
        require(last || page, "Blueprint creation page chain ended early.");
    }
    require(result.pages.size() == list.pages, "Blueprint creation page total differs.");
    return result;
}
} // namespace dingosdk::multiplayer

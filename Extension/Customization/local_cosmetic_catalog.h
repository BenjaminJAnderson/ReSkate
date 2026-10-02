#pragma once
#include "local_customization_runtime.h"
#include "Extension/Profile/runtime_internal.h"

namespace dingosdk::profile_runtime {
struct CosmeticShared { void* body{}; void* control{}; };

void cosmetic_release(CosmeticShared& value) noexcept;

struct CosmeticSharedGuard {
    CosmeticShared value;
    ~CosmeticSharedGuard() { cosmetic_release(value); }
};

struct CosmeticCatalogFunctions {
    void* (*allocate)(std::uintptr_t*, std::size_t, unsigned){};
    void* (*construct)(void*, const std::uintptr_t*){};
    bool (*decode)(void*, void*){};
    void (*create)(std::uintptr_t, std::uintptr_t){};
    void (*complete)(CosmeticShared*, const std::uintptr_t*){};
    void (*populate_inventory)(std::uintptr_t, const std::uintptr_t*){};
    bool (*ui_ready)(){};
    // Exact trivial CoreAllocator adapter of the native allocator: vtable, arena, alignment.
    // Protobuf allocation contexts borrow this adapter for their entire lifetime.
    std::array<std::uintptr_t, 3> allocator_adapter{};
    std::uintptr_t message_control_vtable{};
};

CosmeticCatalogFunctions& cosmetic_catalog_functions();

void initialize_cosmetic_catalog_functions(std::uintptr_t base);

void cosmetic_varint(std::string& out, std::uint64_t value);

void cosmetic_wire_number(std::string& out, unsigned field, std::uint64_t value);

void cosmetic_wire_string(std::string& out, unsigned field, std::string_view value);

std::string cosmetic_ownable_message(const std::string& key, const CosmeticItemInfo& info, bool owned,
    const dingosdk::Json& metadata);

CosmeticShared make_cosmetic_message(const std::string& wire);

struct CosmeticCatalogNode {
    std::uint32_t hash{}, padding{};
    CosmeticShared item;
    CosmeticCatalogNode* next{};
};

struct CosmeticCatalogMap {
    CosmeticCatalogNode** buckets{};
    std::uint32_t bucket_count{}, count{};
};

static_assert(sizeof(CosmeticCatalogNode) == 0x20 && sizeof(CosmeticCatalogMap) == 0x10);

struct CosmeticCatalogOwner {
    const std::uintptr_t* vtable{};
    volatile LONG strong{1}, weak{1};
    CosmeticCatalogMap map;
    std::vector<CosmeticCatalogNode> nodes;
    std::vector<CosmeticCatalogNode*> buckets;
    CosmeticCatalogNode sentinel;
    void clear() noexcept {
        for (auto& node : nodes) cosmetic_release(node.item);
        nodes.clear(); buckets.clear(); map = {};
    }
    ~CosmeticCatalogOwner() { clear(); }
};

static_assert(offsetof(CosmeticCatalogOwner, strong) == 8 && offsetof(CosmeticCatalogOwner, weak) == 12);

void* cosmetic_map_destructor(CosmeticCatalogOwner* owner, unsigned flags) noexcept;

void cosmetic_map_destroy(CosmeticCatalogOwner* owner) noexcept;

void cosmetic_map_free(CosmeticCatalogOwner* owner) noexcept;

void* cosmetic_map_deleter(void*, void*) noexcept;

CosmeticShared make_cosmetic_catalog(const profile::Snapshot& snapshot);

void publish_cosmetic_catalog();

std::vector<std::uint32_t> cosmetic_owned_ids(const profile::Snapshot& snapshot);

void publish_cosmetic_inventory();
}

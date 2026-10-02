#include "fixed_stop_entitlement_provider.h"
#include "fixed_stop_entitlement_internal.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/fixed_stop_entitlement.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dingosdk::fixed_stop_detail {
namespace {
namespace provider = addr::fixed_stop_entitlement;
constexpr std::size_t result_allocation_header_size = 8;
} // namespace

bool read_c_string(std::uintptr_t address,
    std::array<char, maximum_identifier_bytes + 1>& output,
    std::size_t& length) noexcept {
    length = 0;
    if (!valid_range(address, 1)) return false;
    for (; length <= maximum_identifier_bytes; ++length) {
        char value{};
        if (!memory::read(address + length, value)) return false;
        output[length] = value;
        if (!value) return true;
    }
    return false;
}

NativeApi resolve_native_api(std::uintptr_t base) noexcept {
    NativeApi api;
#pragma warning(push)
#pragma warning(disable: 4191)
    api.result_vector_init = reinterpret_cast<ResultVectorInit>(
        base + provider::result_vector_init);
    api.array_allocate = reinterpret_cast<ArrayAllocate>(base + provider::array_allocate);
    api.result_element_init = reinterpret_cast<ResultElementInit>(
        base + provider::result_element_init);
    api.assign_string = reinterpret_cast<AssignString>(base + addr::engine::string_assign);
    api.construct_error = reinterpret_cast<ConstructError>(base + provider::construct_error);
    api.complete_promise = reinterpret_cast<CompletePromise>(base + provider::complete_promise);
    api.destroy_vector = reinterpret_cast<DestroyVector>(base + provider::destroy_vector);
    api.destroy_string = reinterpret_cast<DestroyString>(base + addr::engine::native_text_release);
    api.assign_future = reinterpret_cast<AssignFuture>(base + provider::assign_future);
    api.eid_key = reinterpret_cast<EidKey>(base + provider::eid_key);
    api.find_member_eid = reinterpret_cast<FindMemberEid>(base + provider::find_member_eid);
#pragma warning(pop)
    api.client_game_manager_global = base + provider::client_game_manager;
    return api;
}

bool gather_group_eids(const NativeApi& api,
    std::array<EidBuffer, maximum_group_members>& eids,
    std::uint32_t& eid_count,
    FixedStopEntitlementAttempt& failure) {
    eid_count = 0;
    std::uintptr_t manager{};
    if (!memory::read(api.client_game_manager_global, manager) || !manager) {
        failure = FixedStopEntitlementAttempt::forwarded_no_group;
        return false;
    }
    if (!valid_range(manager, 0x78)) {
        failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
        return false;
    }
    std::uintptr_t group{};
    if (!memory::read(manager + 0x70, group) || !group) {
        failure = FixedStopEntitlementAttempt::forwarded_no_group;
        return false;
    }
    if (!valid_range(group, 0x98)) {
        failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
        return false;
    }
    std::uintptr_t begin{};
    std::uintptr_t end{};
    if (!memory::read(group + 0x88, begin) || !memory::read(group + 0x90, end) ||
        end < begin) {
        failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
        return false;
    }
    const auto member_bytes = end - begin;
    if (member_bytes % 0x10 != 0 ||
        member_bytes / 0x10 > maximum_group_members ||
        (member_bytes && !valid_range(begin,
            static_cast<std::size_t>(member_bytes)))) {
        failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
        return false;
    }
    const void* key{};
    for (auto cursor = begin; cursor != end; cursor += 0x10) {
        std::uintptr_t member{};
        if (!memory::read(cursor + 8, member)) {
            failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
            return false;
        }
        if (!member) continue;
        if (!key) {
            key = api.eid_key(0);
            if (!key) {
                failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
                return false;
            }
        }
        const auto eid = api.find_member_eid(reinterpret_cast<void*>(member), key);
        if (!eid) continue;
        std::size_t length{};
        EidBuffer value{};
        if (!read_c_string(reinterpret_cast<std::uintptr_t>(eid), value, length) ||
            !length) {
            failure = FixedStopEntitlementAttempt::forwarded_malformed_group;
            return false;
        }
        bool duplicate{};
        for (std::uint32_t index = 0; index != eid_count; ++index) {
            if (std::strcmp(eids[index].data(), value.data()) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) eids[eid_count++] = value;
    }
    if (!eid_count) {
        failure = FixedStopEntitlementAttempt::forwarded_no_eid;
        return false;
    }
    std::sort(eids.begin(), eids.begin() + eid_count,
        [](const EidBuffer& left, const EidBuffer& right) {
            return std::strcmp(left.data(), right.data()) < 0;
        });
    return true;
}

bool complete_request(const NativeApi& api, void* future_out, void* promise,
    const std::array<EidBuffer, maximum_group_members>& eids,
    std::uint32_t eid_count, const std::vector<std::string>& owned_ids) {
    NativeResultVector result{};
    api.result_vector_init(&result);
    struct VectorScope {
        const NativeApi& api;
        NativeResultVector& value;
        bool active{true};
        ~VectorScope() noexcept {
            if (!active) return;
            const auto incoming_error = GetLastError();
            try { api.destroy_vector(&value); } catch (...) {}
            SetLastError(incoming_error);
        }
        void destroy() {
            if (!active) return;
            active = false;
            api.destroy_vector(&value);
        }
    } vector_scope{api, result};
    const auto allocation_bytes = result_allocation_header_size +
        static_cast<std::size_t>(eid_count) * result_element_size;
    auto* allocation = static_cast<unsigned char*>(
        api.array_allocate(&result, allocation_bytes, alignof(void*), nullptr));
    if (!allocation) return false;
    auto* elements = reinterpret_cast<NativeResultElement*>(
        allocation + result_allocation_header_size);
    const auto capacity = eid_count;
    std::uint32_t initialized_count{};
    std::memcpy(allocation, &capacity, sizeof(capacity));
    std::memcpy(allocation + sizeof(capacity), &initialized_count,
        sizeof(initialized_count));
    result.data = elements;
    for (std::uint32_t index = 0; index != eid_count; ++index) {
        api.result_element_init(elements + index);
        initialized_count = index + 1;
        std::memcpy(allocation + sizeof(capacity), &initialized_count,
            sizeof(initialized_count));
    }
    for (std::uint32_t index = 0; index != eid_count; ++index) {
        auto& element = elements[index];
        // Native row +0 is the successful CString array, +0x10 the failed
        // array. Its constructor has already initialized both empty sentinels.
        // Publish only initialized string slots so native cleanup is valid
        // after an allocation/assignment failure at any point.
        const auto empty_string = element.eid_string;
        const auto id_count = static_cast<std::uint32_t>(owned_ids.size());
        auto* id_allocation = static_cast<unsigned char*>(api.array_allocate(
            &element.successful_ids, result_allocation_header_size +
                owned_ids.size() * sizeof(void*), alignof(void*), nullptr));
        if (!id_allocation) return false;
        std::uint32_t initialized_ids{};
        std::memcpy(id_allocation, &id_count, sizeof(id_count));
        std::memcpy(id_allocation + 4, &initialized_ids, sizeof(initialized_ids));
        auto** ids = reinterpret_cast<void**>(id_allocation + result_allocation_header_size);
        element.successful_ids = ids;
        for (std::uint32_t id_index = 0; id_index != id_count; ++id_index) {
            ids[id_index] = empty_string;
            initialized_ids = id_index + 1;
            std::memcpy(id_allocation + 4, &initialized_ids, sizeof(initialized_ids));
            api.assign_string(ids + id_index, owned_ids[id_index].data(),
                static_cast<std::uint32_t>(owned_ids[id_index].size()));
        }
        const auto length = std::strlen(eids[index].data());
        api.assign_string(&element.eid_string, eids[index].data(),
            static_cast<std::uint32_t>(length));
        element.owned = 1;
    }
    // The reducer stores the number of true rows at +0x08 and the logical AND
    // of every row at +0x0c before passing this 16-byte result to the promise.
    result.metadata = eid_count;
    result.state = 1;

    NativeError error{};
    api.construct_error(&error, 0, "");
    struct ErrorScope {
        const NativeApi& api;
        NativeError& value;
        bool active{true};
        ~ErrorScope() noexcept {
            if (!active) return;
            const auto incoming_error = GetLastError();
            try { api.destroy_string(&value); } catch (...) {}
            SetLastError(incoming_error);
        }
        void destroy() {
            if (!active) return;
            active = false;
            api.destroy_string(&value);
        }
    } error_scope{api, error};
    api.complete_promise(promise, &error, &result);
    error_scope.destroy();
    api.assign_future(future_out, nullptr);
    vector_scope.destroy();
    return true;
}
} // namespace dingosdk::fixed_stop_detail

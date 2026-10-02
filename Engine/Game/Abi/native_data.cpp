#include "native_data.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/profile.h"

namespace dingosdk::game {
bool initialize_native_data(std::uintptr_t base) {
    using namespace addr::profile;
    if (!base) return false;
    for (const auto& contract : {model_manager_contract, model_create_contract, model_destroy_contract, model_value_contract,
        model_find_contract, card_model_field_contract, neighborhood_publish_contract, model_lock_contract, model_unlock_contract,
        news_assign_contract, news_delegate_copy_contract, news_delegate_destroy_contract,
        news_invoke_contract, news_reference_contract, asset_find_contract}) {
        std::array<unsigned char, 32> bytes{};
        if (base > UINTPTR_MAX - contract.rva ||
            !memory::read(base + contract.rva, bytes) || bytes != contract.bytes) return false;
    }
    NativeData next;
    next.find_asset = reinterpret_cast<decltype(next.find_asset)>(base + asset_find_contract.rva);
    auto& m = next.models;
    auto& v = next.values;
    m.get_model = reinterpret_cast<decltype(m.get_model)>(base + model_manager_contract.rva);
    m.create = reinterpret_cast<decltype(m.create)>(base + model_create_contract.rva);
    m.destroy = reinterpret_cast<decltype(m.destroy)>(base + model_destroy_contract.rva);
    m.value = reinterpret_cast<decltype(m.value)>(base + model_value_contract.rva);
    m.find = reinterpret_cast<decltype(m.find)>(base + model_find_contract.rva);
    m.field = reinterpret_cast<decltype(m.field)>(base + card_model_field_contract.rva);
    m.publish = reinterpret_cast<decltype(m.publish)>(base + neighborhood_publish_contract.rva);
    m.lock = reinterpret_cast<decltype(m.lock)>(base + model_lock_contract.rva);
    m.unlock = reinterpret_cast<decltype(m.unlock)>(base + model_unlock_contract.rva);
    v.assign = reinterpret_cast<decltype(v.assign)>(base + news_assign_contract.rva);
    v.copy_delegate = reinterpret_cast<decltype(v.copy_delegate)>(base + news_delegate_copy_contract.rva);
    v.destroy_delegate = reinterpret_cast<decltype(v.destroy_delegate)>(base + news_delegate_destroy_contract.rva);
    v.invoke = reinterpret_cast<decltype(v.invoke)>(base + news_invoke_contract.rva);
    v.null_reference = reinterpret_cast<decltype(v.null_reference)>(base + news_reference_contract.rva);
    native_data() = next;
    return true;
}
}

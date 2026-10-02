#include "skater_observer.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/skater_entities.h"
#include "Engine/Game/Build/20260929/skater_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dingosdk {
namespace {
constexpr std::uintptr_t highest = memory::highest_user_address;
template<class T> bool read(HANDLE process, std::uintptr_t address, T& value) noexcept {
    if (address < 0x10000 || address > highest - sizeof(value)) return false;
    SIZE_T count{};
    return ReadProcessMemory(process, reinterpret_cast<const void*>(address), &value,
                             sizeof(value), &count) && count == sizeof(value);
}
template<class T> T field(const std::array<unsigned char, 0xd8>& data, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}
}
SkaterComponentSample read_skater_component(HANDLE process, std::uintptr_t base,
                                           std::uintptr_t component) noexcept {
    SkaterComponentSample result;
    if (base < 0x10000 || base > highest - dingosdk::supported_build::game_image_size) return result;
    std::array<unsigned char, 0xd8> data{};
    if (!read(process, component, data) || field<std::uintptr_t>(data, 0) != base + addr::engine::skater_component_vtable ||
        field<std::uintptr_t>(data, 0x38) != base + addr::skater_entities::skater_component_base_vtables[0] ||
        field<std::uintptr_t>(data, 0x40) != base + addr::skater_entities::skater_component_base_vtables[1]) return result;
    constexpr std::size_t offsets[]{0x58, 0x60, 0x70, 0xa0, 0xa8, 0xb0, 0xb8};
    for (std::size_t i = 0; i < std::size(offsets); ++i)
        result.present[i] = field<std::uintptr_t>(data, offsets[i]) != 0;
    constexpr auto globals = addr::skater_model::component_globals;
    for (std::size_t i = 0; i < std::size(globals); ++i) {
        std::uintptr_t value{};
        result.globals_known[i] = read(process, base + globals[i], value);
        result.globals_present[i] = result.globals_known[i] && value != 0;
    }
    std::copy_n(data.data() + 0xc3, result.flags.size(), result.flags.begin());
    const auto optional = field<std::uintptr_t>(data, 0x60);
    std::uintptr_t target{};
    if (optional >= 0x10000 && optional <= highest - 0x48) {
        result.optional_target_known = read(process, optional + 0x40, target);
        result.optional_target_present = result.optional_target_known && target != 0;
    }
    result.wait_seconds = field<float>(data, 0xcc);
    result.wait_finite = std::isfinite(result.wait_seconds);
    if (!result.wait_finite) result.wait_seconds = 0;
    std::uintptr_t check{};
    result.available = read(process, component, check) && check == base + addr::engine::skater_component_vtable;
    return result.available ? result : SkaterComponentSample{};
}
}

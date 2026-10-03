#pragma once
#include "Extension/Multiplayer/developer_identity.h"
#include "Extension/Multiplayer/Remote/native_cosmetics_layout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dingosdk {
namespace developer_hoodie_detail {
inline constexpr std::string_view asset = "Own_TopShirt_Gen_HoodieRelaxed_00001";
// The compiled appearance preset names controllers by their assigned CAS slot.
// "TopItem" is a Lua construction name, not the live controller's name.
inline constexpr std::string_view top_slot = "Characters/MainCharacters/Generic/CAS/Common/Slots/CAS_Top_Slot";
// Serialized ParamDbKey identities from this hoodie's AppearanceShaderExpressionPreset.
// Only the six fabric color regions: textures, stitches and other clothing stay intact.
inline constexpr std::array<std::uint64_t, 6> color_keys{
    0x8c46cd354b18a9e2ULL, 0x1567b5742f4ec0b2ULL, 0x1564a942cb33c53bULL,
    0xf526305270d94cb3ULL, 0x99879bf202d1960aULL, 0x998450425dbd9044ULL};
using Color = std::array<float, 3>;
struct Binding {
    std::uintptr_t material{}, node{}, type{};
    std::uint64_t key{};
    Color color{};
    bool operator==(const Binding &) const = default;
};
struct SavedColor {
    Binding binding;
    Color original{}, last{};
};
inline constexpr std::size_t max_bindings = color_keys.size() * 4;
struct Materials {
    std::uintptr_t component{}, appearance{}, item{};
    std::array<Binding, max_bindings> bindings{};
    std::size_t count{};
    bool eligible{};
};
inline std::uint32_t material_hash(std::string_view name) noexcept {
    std::uint32_t hash = 5381;
    for (auto c : name) hash = hash * 33 ^ static_cast<unsigned char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return hash;
}
inline Color rainbow(std::uint64_t milliseconds) noexcept {
    // One continuous hue cycle every eight seconds, in the material's existing gamut.
    const auto hue = static_cast<float>(milliseconds % 8000) * (6.f / 8000.f);
    const auto sector = static_cast<unsigned>(hue);
    constexpr float high = .8f, low = .025f;
    const auto rising = low + (high - low) * (hue - static_cast<float>(sector));
    const auto falling = high + low - rising;
    switch (sector) {
    case 0: return {high, rising, low};
    case 1: return {falling, high, low};
    case 2: return {low, high, rising};
    case 3: return {low, falling, high};
    case 4: return {rising, low, high};
    default: return {high, low, falling};
    }
}
// All discovery is bounded and follows the actor's own component/controller/material
// backlinks. Never edit the shared AppearanceShaderExpressionPreset or recipe.
template<class Read> Materials materials(Read &read, std::uintptr_t base, std::uintptr_t entity) {
    using namespace multiplayer;
    CosmeticMemory<Read &> m{read, base};
    Materials out;
    m.check(m.ptr(entity) == base + addr::engine::skater_entity_vtable, "Hoodie actor type differs.");
    out.component = m.component(entity);
    const bool pending = m.template get<std::uint8_t>(out.component, 0x10a) || m.template get<std::uint8_t>(out.component, 0x10d);
    const auto recipe = m.ptr(out.component, 0x158);
    const auto items = m.count(recipe, sizeof(NativeCosmeticItem), max_cosmetic_slots);
    const auto wanted = cosmetic_asset_hash(asset);
    for (std::size_t i = 0; i < items; ++i) {
        const auto item = m.template get<NativeCosmeticItem>(recipe, i * sizeof(NativeCosmeticItem));
        if (item.hash == wanted) {
            // Native recipes may store only the hash (an empty CString).
            const auto name = m.text(reinterpret_cast<std::uintptr_t>(item.asset));
            out.eligible = !pending && (name.empty() || name == asset);
        }
    }
    out.appearance = read_native_component(read, entity, base + addr::engine::skater_appearance_vtable);
    const auto begin = m.ptr(out.appearance, 0x90), end = m.ptr(out.appearance, 0x98), capacity = m.ptr(out.appearance, 0xa0);
    m.check(begin <= end && end <= capacity && (end - begin) % 8 == 0 && (capacity - begin) % 8 == 0 &&
                (capacity - begin) / 8 <= 64 && (begin || !capacity), "Hoodie controller array exceeds bounds.");
    for (auto entry = begin; entry < end; entry += 8) {
        const auto item = m.ptr(entry);
        if (!item || m.ptr(item, 0x48) != out.appearance + 0x40 || m.text(m.ptr(item, 0x30)) != top_slot) continue;
        m.check(!out.item, "Hoodie has multiple top controllers.");
        out.item = item;
        if (!(m.template get<std::uint32_t>(item, 0xb0) & 2)) { out.eligible = false; continue; }
        const auto buckets = m.ptr(item, 0x1f0);
        const auto size = m.template get<std::uint32_t>(item, 0x1f8), total = m.template get<std::uint32_t>(item, 0x1fc);
        if (!total) continue;
        m.check(size && size <= 256 && total <= 128, "Hoodie material map exceeds bounds.");
        const auto sentinel = m.ptr(buckets, size * 8ULL);
        const auto hash = material_hash("Top_mat");
        auto node = m.ptr(buckets, (hash % size) * 8ULL);
        for (std::uint32_t visited = 0; node && node != sentinel && visited < total; ++visited, node = m.ptr(node, 0x28)) {
            if (m.template get<std::uint32_t>(node) != hash) continue;
            std::array<std::uintptr_t, 4> seen{};
            for (std::size_t variant = 0; variant < seen.size(); ++variant) {
                const auto material = m.ptr(node, 8 + variant * 8);
                if (!material || std::find(seen.begin(), seen.begin() + variant, material) != seen.begin() + variant) continue;
                seen[variant] = material;
                const auto params = m.ptr(material, 0x268);
                const auto n = m.template get<std::uint32_t>(material, 0x270), count = m.template get<std::uint32_t>(material, 0x274);
                if (!count) continue;
                m.check(n && n <= 1024 && count <= 512, "Hoodie parameter map exceeds bounds.");
                const auto stop = m.ptr(params, n * 8ULL);
                for (const auto key : color_keys) {
                    auto parameter = m.ptr(params, (key % n) * 8ULL);
                    for (std::uint32_t v = 0; parameter && parameter != stop && v < count; ++v, parameter = m.ptr(parameter, 0x40)) {
                        if (m.template get<std::uint64_t>(parameter, 8) != key) continue;
                        // The EBX boxed ColorRgb has a serialized gamut word; the
                        // loaded native ColorRgb is three floats (12 bytes). Validate
                        // the actual type descriptor, not the serialized asset size.
                        const auto type = m.ptr(parameter, 0x20);
                        m.check(type >= base && type < base + 0x09144000 &&
                                    m.template get<std::uint16_t>(parameter, 2) == sizeof(Color) &&
                                    m.template get<std::uint32_t>(m.ptr(type)) == 0x885eff59U &&
                                    m.template get<std::uint16_t>(m.ptr(type), 6) == sizeof(Color) && m.template get<std::uint16_t>(parameter, 6) == 1 &&
                                    (m.template get<std::uint32_t>(parameter, 0x28) & 1) &&
                                    m.template get<std::uint16_t>(type, 8) == m.template get<std::uint16_t>(parameter, 4),
                                "Hoodie color parameter type differs.");
                        const auto color = m.template get<Color>(parameter, 0x10);
                        m.check(std::all_of(color.begin(), color.end(), [](float c) { return std::isfinite(c); }) &&
                                    out.count < out.bindings.size(), "Hoodie color exceeds bounds.");
                        out.bindings[out.count++] = {material, parameter, type, key, color};
                        break;
                    }
                }
            }
            break;
        }
    }
    return out;
}
inline bool same_binding(const Binding &a, const Binding &b) noexcept {
    return a.material == b.material && a.node == b.node && a.type == b.type && a.key == b.key;
}
} // namespace developer_hoodie_detail
struct DeveloperHoodieState {
    std::uintptr_t entity{}, component{}, appearance{}, item{};
    std::uint64_t generation{};
    std::array<developer_hoodie_detail::SavedColor, developer_hoodie_detail::max_bindings> colors{};
    std::size_t count{};
};
namespace developer_hoodie_detail {
// Writes are only to freshly rediscovered bindings. Restoration additionally
// requires the current color to still be ours; a new native preset takes precedence.
template<class Write, class Publish>
void animate(DeveloperHoodieState &state, const Materials &live, std::uintptr_t entity, std::uint64_t generation,
             bool developer, std::uint64_t milliseconds, Write &write, Publish &publish) {
    if (state.entity != entity || state.generation != generation || state.component != live.component ||
        state.appearance != live.appearance || state.item != live.item) state = {};
    const bool enabled = developer && live.eligible;
    const auto color = rainbow(milliseconds);
    DeveloperHoodieState next;
    if (enabled) {
        next.entity = entity; next.generation = generation; next.component = live.component;
        next.appearance = live.appearance; next.item = live.item;
    }
    bool changed{};
    for (std::size_t i = 0; i < live.count; ++i) {
        const auto &binding = live.bindings[i];
        const SavedColor *saved{};
        for (std::size_t j = 0; j < state.count; ++j)
            if (same_binding(binding, state.colors[j].binding)) { saved = &state.colors[j]; break; }
        if (enabled) {
            const auto original = saved && binding.color == saved->last ? saved->original : binding.color;
            if (binding.color == color || write(binding, color)) {
                next.colors[next.count++] = {binding, original, color};
                changed = changed || binding.color != color;
            }
        } else if (saved && binding.color == saved->last && binding.color != saved->original) {
            changed = write(binding, saved->original) || changed;
        }
    }
    state = next;
    if (changed) publish(live.item);
}
} // namespace developer_hoodie_detail
} // namespace dingosdk

#include "Engine/Core/Log/logging.h"
#include "graphics_labels.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Platform/launcher_support.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/graphics_labels.h"

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk {
namespace {
using Rebuild = void (*)(std::uintptr_t);
using Assign = void* (*)(std::uintptr_t, const char*, std::uint32_t);
namespace labels = addr::graphics_labels;
// Everything read here is the live tier manager's own data or the image: peeked, without a
// system call per read (profiled 2026-10-01; the rebuild hook runs often).
template<class T> bool read(std::uintptr_t address, T& value) noexcept { return memory::peek(address, value); }

struct Definition {
    std::string_view key, prefix, label, description;
};
// Keys/prefixes come from the installed Common/TierManager/tier-manager asset.
// English fallback copy is deliberately separate from native/server localization.
constexpr Definition definitions[]{
    {"Ambient Occlusion", "ambient_occlusion_", "Ambient occlusion", "Adjust contact shadows around nearby surfaces."},
    {"Antialiasing", "antialiasing_", "Anti-aliasing", "Smooth jagged edges."},
    {"Depth of Field", "depth_of_field_", "Depth of field", "Adjust out-of-focus background blur."},
    {"Effects Quality", "effects_quality_", "Effects quality", "Adjust the detail of visual effects."},
    {"Frame Synthesis Mode", "frame_synthesis_mode_", "Upscaling method", "Choose the image reconstruction method."},
    {"Frame Synthesis Quality DLSS", "frame_synthesis_quality_dlss_", "DLSS quality", "Balance image quality and performance for DLSS."},
    {"Frame Synthesis Quality FSR", "frame_synthesis_quality_fsr3_", "FSR quality", "Balance image quality and performance for FSR."},
    {"Frame Synthesis Quality XeSS", "frame_synthesis_quality_xe_ss_", "XeSS quality", "Balance image quality and performance for XeSS."},
    {"GI Grid", "gi_grid_", "Global illumination detail", "Adjust the detail of indirect lighting."},
    {"GIBS", "gibs_", "Global illumination", "Choose the indirect lighting mode."},
    {"Global Graphics Quality", "global_graphics_quality_", "Graphics preset", "Apply a preset across the graphics settings."},
    {"Lighting", "lighting_", "Lighting quality", "Adjust lighting quality."},
    {"Mesh Quality", "mesh_quality_", "Mesh quality", "Adjust the detail of scene geometry."},
    {"Motion Blur", "motion_blur_", "Motion blur", "Adjust the blur applied to movement."},
    {"Post Process", "post_process_", "Post-processing", "Adjust post-processing effects."},
    {"Resolution", "resolution_scale_", "Render resolution scale", "Choose the internal rendering scale range."},
    {"Resolution Scale Enable", "resolution_scaling_", "Resolution scaling", "Enable or disable render resolution scaling."},
    {"Scalablearchitecture", "scalable_architecture_", "World detail", "Adjust scalable world geometry detail."},
    {"Shadows", "shadows_", "Shadow quality", "Adjust shadow quality."},
    {"Target Framerate", "target_", "Target frame rate", "Choose the target rendering frame rate."},
    {"Target Simulation", "target_", "Simulation rate", "Choose the target simulation rate."},
    {"Texture Filtering", "texture_filtering_", "Texture filtering", "Adjust texture filtering quality."},
    {"Texture Quality", "texture_quality_", "Texture quality", "Adjust texture detail."},
    {"VSync", "v_sync_", "VSync", "Synchronize presentation with the display refresh rate."},
};

const Definition* definition(std::string_view key) {
    for (const auto& value : definitions) if (value.key == key) return &value;
    return nullptr;
}

std::string choice_label(const Definition& def, std::string_view key) {
    if (!key.starts_with(def.prefix)) return {};
    auto value = key.substr(def.prefix.size());
    struct Replacement { std::string_view key, label; };
    constexpr Replacement replacements[]{
        {"xe_ss", "XeSS"}, {"dlss", "DLSS"}, {"fsr", "FSR"}, {"pssr", "PSSR"},
        {"dlaa", "DLAA"}, {"none", "Off"}, {"native", "Native"},
        {"30_present", "30 FPS"}, {"30_present_tltr", "30 FPS"},
        {"60_present", "60 FPS"}, {"60_present_tltr", "60 FPS"},
        {"uncapped_present_tltr", "Uncapped"}, {"30_sim", "30 Hz"}, {"60_sim", "60 Hz"},
        {"1_2", "Half refresh rate"}, {"1_3", "One-third refresh rate"}, {"1_4", "Quarter refresh rate"},
        {"0_5_to_0_66_30_fps", "50-66% (30 FPS)"}, {"0_5_to_0_8", "50-80%"},
        {"0_5_to_1", "50-100%"}, {"0_66_to_1", "66-100%"},
        {"0_66_to_1_30_fps", "66-100% (30 FPS)"}, {"0_6_to_1", "60-100%"},
        {"0_7_to_1", "70-100%"}, {"0_8_to_1", "80-100%"}, {"0_9_to_1", "90-100%"},
        {"1", "100%"}, {"max_100", "Up to 100%"},
        {"switch2_30", "Switch 2 / 30 FPS"}, {"switch2_60", "Switch 2 / 60 FPS"},
        {"gtao_cas", "GTAO CAS"}, {"gtao_full", "GTAO Full"}, {"gtao_half", "GTAO Half"},
        {"gtao_half_no_checkerboard", "GTAO Half (no checkerboard)"}, {"ssao", "SSAO"},
        {"ssao_no_ao_fields_capsule_ao", "SSAO (no fields/capsules)"},
        {"fxaa_medium", "FXAA Medium"}, {"temporal", "Temporal"},
        {"on_enlighten_off", "On (Enlighten off)"}, {"on_ps5", "On (PS5)"}, {"on_ps5_pro", "On (PS5 Pro)"},
    };
    for (const auto& r : replacements) if (r.key == value) return std::string(r.label);
    if (value.empty() || value.size() > 96) return {};
    // Preserve compound preset distinctions (e.g. High / Ultra), without guessing
    // numerical quality settings from their ordering.
    std::string text;
    bool capital = true;
    for (const char c : value) {
        if (c == '_') { text += ' '; capital = true; }
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            text += capital && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
            capital = false;
        } else return {};
    }
    return text;
}

// Native reflected cache: arrays have a 32-bit size immediately before data.
struct Choice { std::uintptr_t key, label; std::uint32_t hash, padding; };
struct Setting {
    Choice current, default_value;
    std::uintptr_t choices, key, description, label;
    std::uint32_t hash, order;
    std::uint8_t enabled;
    std::array<std::uint8_t, 7> padding;
};
struct Category {
    std::uintptr_t settings, label, key;
    std::uint32_t hash, order;
};
static_assert(sizeof(Choice) == 0x18 && sizeof(Setting) == 0x60 && sizeof(Category) == 0x20);
static_assert(offsetof(Setting, key) == 0x38 && offsetof(Setting, label) == 0x48);
struct Patch { std::uintptr_t slot; std::string text; };
struct State {
    std::uintptr_t base{};
    Rebuild original{};
    Assign assign{};
    bool active{};
} state;

bool read_string(std::uintptr_t address, std::string& value) {
    value.clear();
    char text[512];
    const auto length = memory::peek_cstring(address, text, sizeof(text));
    if (length < 0) return false;
    value.assign(text, static_cast<std::size_t>(length));
    return true;
}
bool writable_slot(std::uintptr_t address) {
    // The slots sit in a few heap arrays: remember the writable regions already asked about
    // rather than calling VirtualQuery for every label on every rebuild.
    static std::array<std::pair<std::uintptr_t, std::uintptr_t>, 8> known{};
    static std::size_t next{};
    for (const auto& [low, high] : known)
        if (address >= low && address + sizeof(std::uintptr_t) <= high) return true;
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT || memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto protection = memory.Protect & 0xff;
    const bool writable = address >= start && address - start <= memory.RegionSize &&
        sizeof(std::uintptr_t) <= memory.RegionSize - (address - start) &&
        (protection == PAGE_READWRITE || protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_WRITECOPY);
    if (writable) known[next++ % known.size()] = {start, start + memory.RegionSize};
    return writable;
}
bool add_missing(std::vector<Patch>& patches, std::uintptr_t slot,
    std::uintptr_t text, std::string_view fallback) {
    char first{};
    if (!read(text, first)) return false;
    if (first || fallback.empty()) return true;
    if (!writable_slot(slot)) return false;
    patches.push_back({slot, std::string(fallback)});
    return true;
}
bool count_array(std::uintptr_t address, std::uint32_t& count) {
    if (address < 0x10004 || !read(address - 4, count)) return false;
    count &= 0x7fffffff;
    return count <= 64;
}
bool add_choice(std::vector<Patch>& patches, std::uintptr_t slot, const Choice& choice,
    const Definition& def) {
    std::string key;
    if (!read_string(choice.key, key)) return false;
    return add_missing(patches, slot + offsetof(Choice, label), choice.label, choice_label(def, key));
}
bool plan_labels(std::uintptr_t manager, std::vector<Patch>& patches) {
    std::uintptr_t vtable{}, begin{}, end{};
    if (!read(manager, vtable) || vtable != state.base + labels::tier_manager_vtable ||
        !read(manager + 0x1c8, begin) || !read(manager + 0x1d0, end) || end < begin ||
        (end - begin) % sizeof(Category) || (end - begin) / sizeof(Category) > 64) return false;
    for (auto address = begin; address != end; address += sizeof(Category)) {
        Category category{};
        std::string key;
        std::uint32_t count{};
        if (!read(address, category) || !read_string(category.key, key) ||
            !count_array(category.settings, count)) return false;
        if (key != "Frame Rate" && key != "Graphics" && key != "Graphics Quality" &&
            key != "Resolution Scaling" && key != "Upscaling") continue;
        if (!add_missing(patches, address + offsetof(Category, label), category.label, key)) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto slot = category.settings + i * sizeof(Setting);
            Setting setting{};
            if (!read(slot, setting) || !read_string(setting.key, key)) return false;
            const auto* def = definition(key);
            if (!def) continue;
            if (!add_missing(patches, slot + offsetof(Setting, label), setting.label, def->label) ||
                !add_missing(patches, slot + offsetof(Setting, description), setting.description, def->description) ||
                !add_choice(patches, slot, setting.current, *def) ||
                !add_choice(patches, slot + offsetof(Setting, default_value), setting.default_value, *def)) return false;
            std::uint32_t choices{};
            if (!count_array(setting.choices, choices)) return false;
            for (std::uint32_t j = 0; j < choices; ++j) {
                const auto choice_slot = setting.choices + j * sizeof(Choice);
                Choice choice{};
                if (!read(choice_slot, choice) || !add_choice(patches, choice_slot, choice, *def)) return false;
            }
        }
    }
    return true;
}

void rebuild(std::uintptr_t manager) {
    state.original(manager);
    const auto last_error = GetLastError();
    try {
        std::vector<Patch> patches;
        if (plan_labels(manager, patches)) {
            // This executes on the native rebuild caller before it notifies the UI.
            // Native string assignment owns allocation/freeing; no pointers survive
            // this invocation and native refresh/destruction remains responsible.
            for (const auto& patch : patches)
                state.assign(patch.slot, patch.text.c_str(), static_cast<std::uint32_t>(patch.text.size()));
            if (!patches.empty()) {
                const auto event = "{\"event\":\"graphics_labels_fallback\",\"strings\":" +
                    std::to_string(patches.size()) + "}";
                dingosdk::logging::event(dingosdk::logging::Channel::graphics, event.c_str());
            }
        } else dingosdk::logging::event(dingosdk::logging::Channel::graphics, "{\"event\":\"graphics_labels_fallback\",\"guard_rejected\":true}");
    } catch (...) {
        dingosdk::logging::event(dingosdk::logging::Channel::graphics, "{\"event\":\"graphics_labels_fallback\",\"error\":true}");
    }
    SetLastError(last_error);
}
}

bool initialize_graphics_labels(std::uintptr_t base, bool authored_offline) {
    if (!authored_offline) return false;
    if (state.active) return state.base == base;
    try {
        if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != base) return false;
        std::array<wchar_t, 32768> path{};
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size >= path.size()) return false;
        launcher::validate_game_file(path.data());
        std::array<unsigned char, labels::tier_rebuild_prefix.size()> actual_rebuild{};
        std::array<unsigned char, labels::string_assign_prefix.size()> actual_assign{};
        if (!read(base + labels::tier_rebuild, actual_rebuild) || actual_rebuild != labels::tier_rebuild_prefix ||
            !read(base + addr::engine::string_assign, actual_assign) || actual_assign != labels::string_assign_prefix) return false;
        state.base = base;
        state.assign = reinterpret_cast<Assign>(base + addr::engine::string_assign);
        auto* target = reinterpret_cast<void*>(base + labels::tier_rebuild);
        void* original{};
        if (hook_prepare(target, reinterpret_cast<void*>(&rebuild), &original) != HookOk) return false;
        state.original = reinterpret_cast<Rebuild>(original);
        if (hook_enable(target) != HookOk) { hook_remove(target); return false; }
        state.active = true;
        return true;
    } catch (...) { return false; }
}
}

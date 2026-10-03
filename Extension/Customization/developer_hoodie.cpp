#include "developer_hoodie.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Core/Log/logging.h"
#include "Extension/Multiplayer/Steam/steam_social.h"
#include "Extension/Multiplayer/Remote/native_skater.h"
#include <Windows.h>
#include <cstring>

namespace dingosdk {
namespace {
// Report a changed failure/activation at most once per actor per five seconds.
// Cosmetic streaming is transient; no per-frame log output or cached writes.
void report_hoodie(std::uintptr_t entity, std::uint64_t steam_id, std::string_view message,
                    logging::Level level) noexcept {
    struct Report { std::uintptr_t entity{}; std::uint64_t at{}; std::uint32_t message{}; };
    static std::array<Report, 33> reports{};
    const auto now = GetTickCount64();
    const auto hash = developer_hoodie_detail::material_hash(message);
    auto *entry = &reports.front();
    for (auto &candidate : reports) {
        if (candidate.entity == entity) { entry = &candidate; break; }
        if (candidate.at < entry->at) entry = &candidate;
    }
    if (entry->entity == entity && (entry->message == hash || now - entry->at < 5000)) return;
    *entry = {entity, now, hash};
    logging::log(level, logging::Channel::customization, "Developer hoodie (Steam {}, actor {:#x}): {}",
                 steam_id, entity, message);
}
// NativeValue is owned by this material. Preserve its padding, type, priority,
// flags and key, and set the same dirty byte as the native addEsVector setter.
bool write_color(const developer_hoodie_detail::Binding &binding, const developer_hoodie_detail::Color &color) noexcept {
    __try {
        std::memcpy(reinterpret_cast<void *>(binding.node + 0x10), color.data(), sizeof(color));
        *reinterpret_cast<std::uint8_t *>(binding.material + 0x2f6) = 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool publish_materials(std::uintptr_t base, std::uintptr_t item) noexcept {
    __try {
        reinterpret_cast<void (*)(std::uintptr_t, int)>(base + addr::native_cosmetics::publish_materials)(item, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace
void update_developer_hoodie(std::uintptr_t base, std::uintptr_t entity, std::uint64_t steam_id,
                             std::uint64_t generation, DeveloperHoodieState &state) noexcept {
    const bool developer = multiplayer::reskate_developer(steam_id);
    if (!entity) { state = {}; return; }
    if (!developer && !state.count) return;
    try {
        auto read = [](std::uintptr_t address, void *out, std::size_t size) { return memory::peek_bytes(address, out, size); };
        std::array<unsigned char, addr::native_cosmetics::publish_materials_prefix.size()> prefix{};
        if (!read(base + addr::native_cosmetics::publish_materials, prefix.data(), prefix.size()) ||
            prefix != addr::native_cosmetics::publish_materials_prefix) {
            report_hoodie(entity, steam_id, "material publisher differs from the supported build", logging::Level::warning);
            state = {}; return;
        }
        const auto live = developer_hoodie_detail::materials(read, base, entity);
        bool published = true;
        auto publish = [base, &published](std::uintptr_t item) { published = publish_materials(base, item); };
        developer_hoodie_detail::animate(state, live, entity, generation, developer, GetTickCount64(), write_color, publish);
        if (!published) report_hoodie(entity, steam_id, "native material publication failed", logging::Level::warning);
        else if (state.count) report_hoodie(entity, steam_id, "RGB active on the owned hoodie material", logging::Level::info);
        else if (developer && live.eligible)
            report_hoodie(entity, steam_id, "waiting for the assigned top slot's color parameters", logging::Level::warning);
    } catch (const std::exception &error) {
        report_hoodie(entity, steam_id, error.what(), logging::Level::warning);
    } catch (...) {
        // Streaming/replacement may invalidate a controller. Never recover by
        // writing cached addresses, or let an optional cosmetic stop the session.
    }
}
void tick_local_developer_hoodie(std::uintptr_t base, std::uintptr_t client, bool ready) noexcept {
    static DeveloperHoodieState state;
    // Pausing/loading can temporarily make the local actor unavailable without
    // destroying it. Keep originals until a fresh ownership walk succeeds.
    if (!ready) return;
    try {
        const auto social = multiplayer::steam_social_snapshot();
        const auto id = social ? social->local.id : 0;
        if (!multiplayer::reskate_developer(id) && !state.count) return;
        const auto local = multiplayer::capture_local(base, client, false);
        if (local.ready) update_developer_hoodie(base, local.entity, id, local.context, state);
        else report_hoodie(client, id, local.detail, logging::Level::warning);
    } catch (...) {}
}
} // namespace dingosdk

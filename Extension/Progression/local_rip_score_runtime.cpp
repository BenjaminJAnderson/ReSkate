#include "Engine/Core/Log/logging.h"
#include "Extension/Profile/runtime_internal.h"
#include "local_neighborhood_runtime.h"
#include "local_rip_score_runtime.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/local_rip_score.h"

namespace dingosdk::profile_runtime {
RipScoreRuntime& rip_score_runtime() { static auto* r = new RipScoreRuntime; return *r; }

bool rip_score_type_contract(std::uintptr_t base) {
    struct Field { std::uint64_t hash, offset; std::uintptr_t type; } field{};
    std::uint32_t hash{}; std::uint16_t size{};
    return read(base + addr::local_rip_score::rip_score_record_hash, hash) && hash == 0x3b7ce311 &&
        read(base + addr::local_rip_score::rip_score_record_size, size) && size == 0x70 &&
        read(base + addr::local_rip_score::rip_score_total_field, field) && field.hash == 0x2891685a && field.offset == 0x10 &&
        field.type == base + addr::engine::int64_type;
}

void update_rip_score() {
    auto& s = local_runtime(); auto& r = rip_score_runtime(); auto& n = game::native_data().models;
    if (!r.get_context || GetTickCount64() < r.next_poll) return;
    r.next_poll = GetTickCount64() + 1000;
    const auto saved = s.store->saved_rip_score();
    if (!saved) return;
    const char* id = "collectionscore"; // Authored CollectionScoreManager constructor.
    const auto handle = r.get_context(&id);
    const auto model = n.get_model(0xbf0f9789);
    if (!handle || !model) return;
    game::ModelWriteLock model_lock(model);
    alignas(8) std::array<std::byte, 0x70> record{};
    if (!read_bytes(n.value(model, handle, 0, 0), record.data(), record.size())) return;
    const auto set = [&](std::size_t offset, auto value) { std::memcpy(record.data() + offset, &value, sizeof(value)); };
    // Native progression model: current and previous totals/within-level XP,
    // thresholds, levels, progress fractions, and server-update notification flag.
    // Seeded local values are already durable before this UI publication.
    for (const auto offset : {0x10, 0x38, 0x40, 0x28}) set(offset, saved->value);
    for (const auto offset : {0x30, 0x00, 0x48, 0x08}) set(offset, saved->cap);
    set(0x64, saved->level); set(0x5c, saved->level);
    const auto fraction = static_cast<float>(static_cast<double>(saved->value) / static_cast<double>(saved->cap));
    set(0x60, fraction); set(0x58, fraction);
    set(0x68, std::uint8_t{0}); // Hydration must not queue level-up notifications.
    set(0x18, id);
    n.publish(model, handle, s.base + addr::local_rip_score::rip_score_type, record.data());
    std::int64_t actual{};
    const auto result = n.value(model, handle, 0, 0);
    if (read(result + 0x10, actual) && actual == saved->value && !r.published) {
        r.published = true;
        dingosdk::logging::event(dingosdk::logging::Channel::progression, dingosdk::Json{{"event", "local_rip_score_published"}, {"value", saved->value},
            {"cap", saved->cap}, {"level", saved->level}}.dump().c_str());
    }
}
}
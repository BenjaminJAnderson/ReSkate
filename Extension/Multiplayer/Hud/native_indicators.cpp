#include "native_indicators.h"
#include "Extension/Multiplayer/Session/peer_slots.h"
#include "native_indicator_slots.h"
#include "native_nametag_context.h"
#include "native_party.h"
#include "Engine/Game/Abi/native_data.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/native_indicators.h"
#include "Engine/Game/Build/20260929/native_menu.h"
#include "Engine/Game/Build/20260929/native_party.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

namespace dingosdk::multiplayer {
namespace {
using Address = std::uintptr_t;
namespace engine = addr::engine;
namespace indicators = addr::native_indicators;
namespace party = addr::native_party;
// Presenter, registry, widget and texture records of the live HUD: guarded copies,
// not a system call per read (the presenter tick runs every frame).
template <class T> T read(Address address) {
    T value{};
    if (!memory::peek(address, value))
        throw std::runtime_error("Native indicator memory unavailable.");
    return value;
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool read_bytes(Address address, void *value, std::size_t size) {
    return memory::peek_bytes(address, value, size);
}
struct Provider;
struct ProviderMethods {
    void (*destroy)(Provider *, unsigned);
    bool (*transform)(Provider *, void *);
    Address (*blueprint)(Provider *);
    Address (*parent)(Provider *);
    Address (*owner)(Provider *);
    Address (*data)(Provider *);
    bool (*enabled)(Provider *);
    void (*unregister)(Provider *);
    int (*sort)(Provider *);
};
// ID and channel vector are read directly by DingoIndicatorPresenter.
// The remaining fields are private to our position/data adapter. The game
// creates, draws, projects and destroys the actual Rime widgets.
struct Provider {
    const ProviderMethods *methods{};
    std::uint32_t id{}, padding{};
    const std::uint32_t *begin{}, *end{}, *capacity{};
    Address allocator{};
    std::array<std::uint32_t, 8> channels{};
    Address source{}, blueprint{}, parent{}, owner{}, data{};
    std::array<float, 3> offset{};
    std::array<float, 16> world{};
    bool active{}, bound{}, failed{}, compass{};
    Address widget_parent{};
    std::size_t slot{};
};
static_assert(offsetof(Provider, id) == 8 && offsetof(Provider, begin) == 0x10 &&
              offsetof(Provider, allocator) == 0x28 && sizeof(ProviderMethods) == 0x48);
struct Callback {
    void *owner{};
    void (*invoke)(void *, Address, Address){};
};
// The party compass icon found in the texture registry, under native_mutex.
// Reused while the registry still lists it in the same bucket (so it is still
// loaded); the whole registry is walked again only when it is not.
struct CompassTexture {
    Address manager{}, table{}, asset{};
    std::uint32_t buckets{}, bucket{};
    // After a walk that found neither icon: no new walk for this registry
    // size until retry_at, however many peers retry their compass.
    std::uint32_t missing_count{};
    ULONGLONG retry_at{};
};
struct Shared {
    std::recursive_mutex native_mutex;
    Address base{};
    std::atomic<bool> installed{};
    std::atomic<bool> nametags{true}, compass{true};
    void (*tick)(Address){};
    void (*remove)(Address, Provider *){};
    Address (*widget)(Address, Address, int, Address, Address, const Callback *, const Callback *){};
    bool attempted{};
    CompassTexture compass_texture;
};
Shared &shared() {
    static auto *s = new Shared;
    return *s;
}
struct State {
    std::mutex snapshot_mutex;
    // Bumped by each client snapshot. A slot whose snapshot refresh() has already
    // handled as absent (idle_generation) has nothing to do on presenter ticks.
    std::atomic<std::uint64_t> generation{};
    std::uint64_t idle_generation{}; // presenter tick only
    bool visible{};
    ULONGLONG sampled{}, next_discovery{}, next_refresh{};
    std::array<float, 16> world{};
    std::string name, published_name;
    Address model_manager{}, context_type{};
    std::uint64_t player_info{}, nametag_context{};
    std::string status = "Native nametag/compass: waiting for a peer.";
    Provider nametag, compass;
};
State &state() {
    // DLL is pinned until process exit. Providers must outlive deferred native
    // removals, including shutdown paths that no longer tick their presenter.
    static auto *s = new PeerStorage<State>;
    return s->current();
}
thread_local Address current_presenter{};
void status(std::string value) {
    auto &s = state();
    std::lock_guard lock(s.snapshot_mutex);
    if (s.status != value) {
        s.status = std::move(value);
        logging::write(logging::Level::info, logging::Channel::ui, s.status);
    }
}
void stop(Provider &provider);
void destroy_provider(Provider *p, unsigned) { stop(*p); }
bool provider_transform(Provider *p, void *out) {
    if (!p->active || p->failed)
        return false;
    std::memcpy(out, p->world.data(), sizeof(p->world));
    return true;
}
Address provider_blueprint(Provider *p) { return p->blueprint; }
Address provider_parent(Provider *p) { return p->parent; }
Address provider_owner(Provider *p) { return p->owner; }
Address provider_data(Provider *) {
    // A source entity would connect the new widget to its existing bindings.
    // Null creates independent widget properties, populated by our pre-init
    // callback. Never write the session marker's/player's shared context.
    return 0;
}
bool provider_enabled(Provider *p) { return p->active && !p->failed; }
void provider_unregister(Provider *p) { stop(*p); }
int provider_sort(Provider *) { return 0; }
const ProviderMethods methods{destroy_provider, provider_transform,  provider_blueprint,
                              provider_parent,  provider_owner,      provider_data,
                              provider_enabled, provider_unregister, provider_sort};
Address registry() { return shared().base + indicators::presenter_registry; }
std::vector<Address> presenters() {
    const auto r = registry();
    const auto table = read<Address>(r + 0x20);
    const auto buckets = read<std::uint32_t>(r + 0x28);
    const auto count = read<std::uint32_t>(r + 0x2c);
    require(buckets <= 4096 && count <= 32, "Native indicator presenter table exceeds bound.");
    std::vector<Address> result;
    for (unsigned i = 0; i < buckets && result.size() < count; ++i) {
        for (auto node = read<Address>(table + i * 8); node;) {
            require(result.size() < count, "Native indicator presenter chain is invalid.");
            const auto presenter = read<Address>(node);
            require(read<Address>(presenter) == shared().base + indicators::hud_presenter_vtable,
                    "Native indicator presenter type differs.");
            result.push_back(presenter);
            node = read<Address>(node + 8);
        }
    }
    require(result.size() == count, "Native indicator presenter count differs.");
    return result;
}
void stop(Provider &p) {
    if (!p.active)
        return;
    p.active = false;
    shared().remove(registry(), &p);
    // Release widgets before their borrowed template parent/assets can unload.
    // This is the game's pending-removal pass, on the native UI lifecycle path.
    for (const auto presenter : presenters())
        reinterpret_cast<void (*)(Address)>(shared().base + indicators::presenter_flush)(presenter);
    p.source = p.blueprint = p.parent = p.owner = p.data = p.widget_parent = 0;
    p.bound = p.failed = false;
}
void remove_hook(Address r, Provider *provider) {
    std::lock_guard lock(shared().native_mutex);
    // Idle slots (see tick_hook) have no registered provider.
    if (shared().installed.load(std::memory_order_acquire) && r == registry()) each_peer([&] {
        auto &s = state();
        if (s.generation.load(std::memory_order_acquire) != s.idle_generation) {
            for (auto *p : {&s.nametag, &s.compass}) {
                if (p->active && p->source == reinterpret_cast<Address>(provider)) {
                    try {
                        stop(*p);
                        s.next_discovery = 0;
                    } catch (const std::exception &e) {
                        p->failed = true;
                        status(e.what());
                    }
                }
            }
        }
    });
    shared().remove(r, provider);
}
Address property(Address parent, Address data, std::uint32_t hash, bool output) {
    return indicator_property(read_bytes, parent, data, hash, output);
}
void set_property(Provider &p, Address parent, std::uint32_t hash, Address type, const void *value) {
    const auto interface_data = read<Address>(p.blueprint + 0x30);
    const auto slot = property(parent, interface_data, hash, true);
    require(slot && parent != p.parent, "Native indicator widget has no independent output property.");
    const auto source_slot = property(p.parent, p.data, hash, false);
    require(slot != source_slot, "Native indicator property aliases the template source.");
    const auto actual_type = read<Address>(slot + 8);
    require(!actual_type || actual_type == type, "Native indicator property type differs.");
    Address initialized{};
    reinterpret_cast<void (*)(Address, Address *, Address, std::uint32_t, Address, const void *)>(
        shared().base + indicators::property_bind)(parent, &initialized, interface_data, hash, type, nullptr);
    require(initialized == slot, "Native indicator output binding differs.");
    reinterpret_cast<void (*)(const Address *, const void *, std::uint8_t)>(shared().base + engine::set_customization_flag)(
        &initialized, value, 1);
}
void copy_compass_property(Provider &p, Address parent, std::uint32_t hash, Address type) {
    const auto slot = property(p.parent, p.data, hash, false);
    const auto argument = indicator_property_argument(read_bytes, shared().base, slot, type);
    // The template parent remains live until remove_hook synchronously releases
    // this widget; assets and models are borrowed read-only for that lifetime.
    // The native setter retains asset references and copies ordinary values.
    // Passing &asset here would retain a stack address as an engine asset.
    set_property(p, parent, hash, type, reinterpret_cast<const void *>(argument));
}
void bind_party_compass_icon(Provider &p, Address parent) {
    // The texture registry owns these assets. Hold its native lock until the
    // widget setter has retained the selected asset; never cache a borrowed
    // texture across map/asset unloading.
    const auto manager = read<Address>(shared().base + addr::native_menu::texture_registry);
    require(manager, "Native player compass texture registry unavailable.");
    auto *section = reinterpret_cast<CRITICAL_SECTION *>(manager + 0x188);
    EnterCriticalSection(section);
    struct Unlock { CRITICAL_SECTION *value; ~Unlock() { LeaveCriticalSection(value); } } unlock{section};
    require(read<Address>(shared().base + addr::native_menu::texture_registry) == manager,
            "Native texture registry changed during compass binding.");
    const auto buckets = read<std::uint32_t>(manager + 0xa0);
    const auto count = read<std::uint32_t>(manager + 0xa4);
    const auto table = read<Address>(manager + 0x98);
    require(buckets && buckets <= 65536 && count <= 32768 && table,
            "Native texture registry exceeds bound.");
    const auto image = [](Address asset) {
        return asset && read<Address>(asset) == shared().base + addr::native_menu::image_asset_vtable &&
               read<Address>(asset + 8) == shared().base + addr::native_menu::image_asset_type;
    };
    constexpr const char *party_icon = "UI/Textures/Icons/Decorators/img_Decorator_PartyMember_64";
    const auto named = [](Address asset, const char *expected) {
        std::array<char, 128> value{};
        const auto length = std::strlen(expected) + 1;
        return length <= value.size() && memory::peek_bytes(read<Address>(asset + 0x18), value.data(), length) &&
               std::memcmp(value.data(), expected, length) == 0;
    };
    // The icon found for an earlier widget, if the registry still lists it in the
    // same bucket: its own chain proves the texture is still loaded.
    auto &cached = shared().compass_texture;
    if (cached.asset && cached.manager == manager && cached.table == table && cached.buckets == buckets &&
        cached.bucket < buckets) {
        unsigned visited{};
        for (auto node = read<Address>(table + cached.bucket * 8ULL); node; node = read<Address>(node + 16)) {
            require(++visited <= count, "Native texture registry chain differs.");
            if (read<Address>(node + 8) == cached.asset && image(cached.asset) && named(cached.asset, party_icon)) {
                set_property(p, parent, 0xbdd7e0da, shared().base + indicators::compass_icon_type,
                             reinterpret_cast<const void *>(cached.asset));
                return;
            }
        }
    }
    // After a walk that found neither icon, the next is due once the registry has
    // grown or a second has passed, however many peers retry their compass.
    const auto now = GetTickCount64();
    require(cached.asset || cached.manager != manager || cached.table != table || cached.buckets != buckets ||
                cached.missing_count != count || now >= cached.retry_at,
            "Waiting for the native party/player compass texture.");
    Address player{}, party{};
    unsigned visited{}, party_bucket{};
    for (unsigned i = 0; i < buckets && !party; ++i) {
        for (auto node = read<Address>(table + i * 8ULL); node; node = read<Address>(node + 16)) {
            require(++visited <= count, "Native texture registry chain differs.");
            const auto asset = read<Address>(node + 8);
            if (!image(asset)) continue;
            if (named(asset, party_icon)) {
                party = asset;
                party_bucket = i;
            }
            if (named(asset, "UI/Textures/Icons/Map/img_Icon_Map_Player_128")) player = asset;
        }
    }
    // Only the preferred icon is kept: with just the player icon, a later walk
    // may still find the party one.
    if (party)
        cached = {manager, table, party, buckets, party_bucket};
    else if (!player)
        cached = {manager, table, 0, buckets, 0, count, now + 1000};
    else
        cached = {};
    require(party || player, "Waiting for the native party/player compass texture.");
    set_property(p, parent, 0xbdd7e0da, shared().base + indicators::compass_icon_type,
                 reinterpret_cast<const void *>(party ? party : player));
}
void bind_widget(void *context, Address, Address widget) {
    auto &p = *static_cast<Provider *>(context);
    const PeerScope scope(p.slot);
    try {
        auto &s = state();
        const auto parent = read<Address>(widget + 0x30);
        require(parent && read<Address>(parent) == shared().base + indicators::widget_parent_vtable,
                "Native indicator widget parent differs.");
        if (p.compass) {
            copy_compass_property(p, parent, 0x02043b74, shared().base + engine::uint64_type);
            bind_party_compass_icon(p, parent);
            copy_compass_property(p, parent, 0x0ca8c5f8, shared().base + indicators::vec3_type);
        } else {
            require(s.nametag_context && s.nametag_context != s.player_info,
                    "Native nametag context unavailable.");
            set_property(p, parent, 0x161286ea, shared().base + engine::uint64_type, &s.nametag_context);
        }
        p.widget_parent = parent;
        p.bound = true;
        logging::printf(logging::Level::debug, logging::Channel::ui,
                        "Native peer %s widget created: id=%u widget=%p parent=%p",
                        p.compass ? "compass" : "nametag", p.id, reinterpret_cast<void *>(widget),
                        reinterpret_cast<void *>(parent));
    } catch (const std::exception &e) {
        p.failed = true;
        status(e.what());
    }
}
Address widget_hook(Address cache, Address result, int id, Address list, Address info, const Callback *before,
                    const Callback *after) {
    if (!current_presenter || cache != current_presenter + 0x268 || !info || !before || !after)
        return shared().widget(cache, result, id, list, info, before, after);
    std::lock_guard lock(shared().native_mutex);
    if (shared().installed.load(std::memory_order_acquire)) {
        for (std::size_t i = 0; i < max_remote_players; ++i) {
            const PeerScope scope(i);
            auto &s = state();
            if (s.generation.load(std::memory_order_acquire) == s.idle_generation)
                continue; // idle slots (see tick_hook) have no registered provider
            for (auto *p : {&s.nametag, &s.compass}) {
                if (!p->active || p->id != static_cast<unsigned>(id) ||
                    read<Address>(info + 0x30) != p->blueprint)
                    continue;
                // Only the native indicator call uses empty callbacks. Preserve
                // every other widget/cache request untouched.
                if ((!before->owner && !before->invoke) && (!after->owner && !after->invoke)) {
                    const Callback binding{p, bind_widget};
                    return shared().widget(cache, result, id, list, info, &binding, after);
                }
            }
        }
    }
    return shared().widget(cache, result, id, list, info, before, after);
}
// Native model type lookup, called only under the model manager's write lock.
Address model_type(Address manager, std::uint64_t handle) {
    const auto base = shared().base;
    const auto record = reinterpret_cast<Address (*)(Address, std::uint64_t, std::uint8_t)>(base + party::model_record)(
        manager, handle, 0);
    require(record, "Native nametag model record unavailable.");
    return reinterpret_cast<Address (*)(Address, std::uint64_t, Address, std::uint8_t)>(base + party::model_type)(
        manager, handle, record, 0);
}
void prepare_nametag_context(Provider &p);
void update_name(const std::string &name) {
    auto &s = state();
    auto &n = game::native_data().models;
    require(n.create && n.value && n.field && n.publish, "Native player model API unavailable.");
    const auto ui = read<Address>(shared().base + engine::ui_manager);
    const auto manager = ui ? read<Address>(ui + 0x140) : 0;
    require(manager, "Native player model manager unavailable.");
    game::ModelWriteLock lock(manager);
    if (s.model_manager != manager) {
        // Old handles belong to the old manager and must never be dereferenced.
        s.model_manager = manager;
        s.player_info = s.nametag_context = s.context_type = 0;
        s.published_name.clear();
    }
    if (const auto party_info = native_party_player_info(manager, peer_slot)) {
        const bool changed = s.player_info != party_info;
        s.player_info = party_info;
        if (changed && s.nametag.active) prepare_nametag_context(s.nametag);
        return;
    }
    if (!s.player_info || !n.value(manager, s.player_info, 0, 0)) {
        s.published_name.clear();
        s.player_info = n.create(
            manager, shared().base + engine::player_info_type, 0,
            game::native_name_hash(("ReSkate.Multiplayer.PlayerInfo." + std::to_string(peer_slot)).c_str()),
            false, 2);
        require(s.player_info, "Cannot allocate native peer name record.");
        alignas(8) std::array<std::byte, 0x108> info{};
        reinterpret_cast<void (*)(void *)>(shared().base + engine::card_info_construct)(info.data());
        // publish also returns false when the initialized value is unchanged.
        n.publish(manager, s.player_info, shared().base + engine::player_info_type, info.data());
        require(n.value(manager, s.player_info, 0, 0), "Cannot initialize native peer name record.");
    }
    const std::string text = name.empty() ? "Remote skater" : name;
    if (s.published_name == text)
        return;
    require(model_type(manager, s.player_info) == shared().base + engine::player_info_type,
            "Native peer record type differs.");
    const auto user = n.field(manager, s.player_info, 1, UINT32_MAX, false);
    require(user && model_type(manager, user) == shared().base + indicators::user_info_type,
            "Native peer user-info field differs.");
    struct NativeString {
        Address base, value{};
        ~NativeString() {
            if (value)
                reinterpret_cast<void (*)(Address *)>(base + engine::native_text_release)(&value);
        }
    } native{shared().base};
    game::native_data().values.assign(&native.value, text.c_str(), static_cast<std::uint32_t>(text.size()));
    for (const unsigned index : {0U, 4U}) {
        const auto field = n.field(manager, user, index, UINT32_MAX, false);
        require(field && model_type(manager, field) == shared().base + engine::string_type,
                "Native peer display-name field differs.");
        n.publish(manager, field, shared().base + engine::string_type, &native.value);
        const auto stored = n.value(manager, field, 0, 0);
        require(stored && read<Address>(stored), "Native peer display name is null.");
        std::vector<char> published(text.size() + 1);
        require(read_bytes(read<Address>(stored), published.data(), published.size()) &&
                    std::memcmp(published.data(), text.c_str(), published.size()) == 0,
                "Native peer display name publication differs.");
    }
    s.published_name = text;
}
void prepare_nametag_context(Provider &p) {
    auto &s = state();
    auto &n = game::native_data().models;
    game::ModelWriteLock lock(s.model_manager);
    const auto slot = property(p.parent, p.data, 0x161286ea, false);
    const auto argument =
        indicator_property_argument(read_bytes, shared().base, slot, shared().base + engine::uint64_type);
    const auto source = read<std::uint64_t>(argument);
    const auto type = model_type(s.model_manager, source);
    const auto value = n.value(s.model_manager, source, 0, 0);
    alignas(8) const auto context =
        nametag_context_copy(read_bytes, shared().base, type, value, s.player_info);
    // Prove that the template's inherited field really is a player handle.
    const auto original_player = read<std::uint64_t>(value);
    require(model_type(s.model_manager, original_player) == shared().base + engine::player_info_type,
            "Native nametag template does not reference player info.");
    if (s.context_type != type || !s.nametag_context || !n.value(s.model_manager, s.nametag_context, 0, 0)) {
        s.context_type = type;
        s.nametag_context =
            n.create(s.model_manager, type, 0,
                     game::native_name_hash(
                         ("ReSkate.Multiplayer.NametagContext." + std::to_string(peer_slot)).c_str()),
                     false, 2);
    }
    require(s.nametag_context && s.nametag_context != source && s.nametag_context != s.player_info,
            "Native nametag context must be independently owned.");
    n.publish(s.model_manager, s.nametag_context, type, context.data());
    const auto stored = n.value(s.model_manager, s.nametag_context, 0, 0);
    require(model_type(s.model_manager, s.nametag_context) == type && stored &&
                read<std::uint64_t>(stored) == s.player_info,
            "Native nametag player-context binding differs.");
    logging::printf(logging::Level::debug, logging::Channel::ui,
                    "Native nametag context: owned=%llx source=%llx player=%llx type=%p bytes=%zu",
                    s.nametag_context, source, s.player_info, reinterpret_cast<void *>(type), context.size());
}
bool find_template(Provider &target, const std::vector<Address> &sources, const char *name) {
    for (const auto source : sources) {
        if (!source || read<Address>(source) != shared().base + indicators::indicator_source_vtable)
            continue;
        const auto data = read<Address>(source - 8);
        const auto blueprint = read<Address>(data + 0x70) & ~Address{4};
        if (!blueprint)
            continue;
        std::array<char, 160> actual{};
        if (!read_bytes(read<Address>(blueprint + 0x18), actual.data(), std::strlen(name) + 1) ||
            std::strcmp(actual.data(), name) != 0)
            continue;
        const auto parent = read<Address>(source - 0x10);
        require(parent && read<Address>(parent) == shared().base + indicators::widget_parent_vtable,
                "Native indicator source parent differs.");
        const auto begin = read<Address>(source + 0x10), end = read<Address>(source + 0x18);
        require(end >= begin && (end - begin) % 4 == 0 && (end - begin) / 4 <= target.channels.size(),
                "Native indicator channel list exceeds bound.");
        target.methods = &methods;
        target.slot = peer_slot;
        target.source = source;
        target.blueprint = blueprint;
        target.parent = parent;
        target.owner = read<Address>(parent + 8);
        target.data = data;
        target.offset = read<std::array<float, 3>>(data + 0xe0);
        for (const auto f : target.offset)
            require(std::isfinite(f) && std::abs(f) < 20, "Native indicator anchor offset differs.");
        require(target.owner, "Native indicator template owner unavailable.");
        const auto count = (end - begin) / 4;
        if (count)
            require(read_bytes(begin, target.channels.data(), end - begin),
                    "Native indicator channels unavailable.");
        target.begin = target.channels.data();
        target.end = target.capacity = target.begin + count;
        target.bound = target.failed = false;
        return true;
    }
    return false;
}
void refresh() {
    auto &s = state();
    const auto now = GetTickCount64();
    if (now < s.next_refresh)
        return;
    s.next_refresh = now + 50;
    std::array<float, 16> world;
    std::string name;
    bool visible;
    std::uint64_t generation{};
    {
        std::lock_guard lock(s.snapshot_mutex);
        visible = s.visible && now - s.sampled < 1000;
        world = s.world;
        name = s.name;
        generation = s.generation.load(std::memory_order_relaxed);
    }
    if (!visible) {
        stop(s.nametag);
        stop(s.compass);
        s.next_discovery = 0;
        status("Native nametag/compass: waiting for a peer.");
        // Nothing more to do here until the client publishes this slot again.
        s.idle_generation = generation;
        return;
    }
    const bool nametags = shared().nametags.load(std::memory_order_relaxed);
    const bool compass = shared().compass.load(std::memory_order_relaxed);
    if (!nametags && (s.nametag.active || s.nametag.failed))
        stop(s.nametag);
    if (!compass && (s.compass.active || s.compass.failed))
        stop(s.compass);
    if (s.nametag.active)
        update_name(name);
    if (now >= s.next_discovery) {
        s.next_discovery = now + 1000;
        // Native icon textures may stream in after the widget blueprint.
        // Retry failed providers through normal unregister/register ownership.
        if (s.nametag.failed) stop(s.nametag);
        if (s.compass.failed) stop(s.compass);
        const auto begin = read<Address>(registry()), end = read<Address>(registry() + 8);
        require(end >= begin && (end - begin) % 8 == 0 && (end - begin) / 8 <= 8192,
                "Native indicator registry exceeds bound.");
        std::vector<Address> sources((end - begin) / 8);
        if (!sources.empty())
            require(read_bytes(begin, sources.data(), end - begin),
                    "Native indicator registry unavailable.");
        for (auto *p : {&s.nametag, &s.compass}) {
            if (p->active || (p == &s.nametag && !nametags) || (p == &s.compass && !compass))
                continue;
            const char *asset =
                p == &s.nametag
                    ? "UI/Features/Social/InspectPlayer/Indicators/Widgets/PlayerNametag_Indicator_Widget"
                    : "UI/Features/HUD/Compass/CompassTagIndicator";
            if (!find_template(*p, sources, asset))
                continue;
            p->compass = p == &s.compass;
            if (!p->compass) {
                update_name(name);
                prepare_nametag_context(*p);
            }
            p->active = true;
            reinterpret_cast<void (*)(Address, Provider *)>(shared().base + indicators::indicator_register)(registry(), p);
            require(p->id, "Native indicator registration did not assign an ID.");
        }
    }
    for (auto *p : {&s.nametag, &s.compass}) {
        p->world = world;
        for (unsigned i = 0; i < 3; ++i)
            p->world[12 + i] += p->offset[i];
        // Extra headroom for the peer's native nametag, above its authored
        // anchor. This affects only the owned label provider's world position.
        if (p == &s.nametag)
            p->world[13] += 0.30f;
    }
    if (!s.nametag.failed && !s.compass.failed) {
        const auto description = [](const Provider &p) {
            return p.bound ? "bound" : p.active ? "registered" : "waiting for template";
        };
        status(std::string("Native nametag: ") + (nametags ? description(s.nametag) : "off") +
               "; compass: " + (compass ? description(s.compass) : "off") + ".");
    }
}
void tick_hook(Address presenter) {
    std::lock_guard lock(shared().native_mutex);
    if (shared().installed.load(std::memory_order_acquire)) {
        // The presenter's type is the same for every slot: checked once per tick.
        Address type{};
        const bool presenter_ok =
            memory::peek(presenter, type) && type == shared().base + indicators::hud_presenter_vtable;
        each_peer([&] {
            auto &s = state();
            // Slots never published, or already returned to idle, are skipped.
            if (s.generation.load(std::memory_order_acquire) == s.idle_generation)
                return;
            try {
                require(presenter_ok, "Native HUD presenter type differs.");
                refresh();
            } catch (const std::exception &e) {
                s.nametag.failed = s.compass.failed = true;
                status(e.what());
            }
        });
    }
    const auto previous = current_presenter;
    current_presenter = presenter;
    shared().tick(presenter);
    current_presenter = previous;
}
void install(Address base) {
    shared().attempted = true;
    shared().base = base;
    auto prefix = [base](Address rva, const auto &bytes) {
        std::array<std::uint8_t, 32> actual{};
        require(bytes.size() <= actual.size() &&
                    memory::read_bytes(base + rva, actual.data(), bytes.size()) &&
                    std::equal(bytes.begin(), bytes.end(), actual.begin()),
                "Native indicator function fingerprint differs.");
    };
    prefix(indicators::hud_tick, indicators::hud_tick_prefix);
    prefix(indicators::provider_remove, indicators::provider_remove_prefix);
    prefix(indicators::indicator_register, indicators::indicator_register_prefix);
    prefix(indicators::presenter_flush, indicators::presenter_flush_prefix);
    prefix(indicators::widget_create, indicators::widget_create_prefix);
    prefix(indicators::property_bind, indicators::property_bind_prefix);
    prefix(engine::set_customization_flag, indicators::set_customization_flag_prefix);
    prefix(indicators::class_reference_getter, indicators::class_reference_getter_prefix);
    prefix(party::model_record, indicators::model_record_prefix);
    prefix(party::model_type, indicators::model_type_prefix);
    prefix(engine::card_info_construct, indicators::card_info_construct_prefix);
    prefix(engine::native_text_release, indicators::native_text_release_prefix);
    struct Hook {
        Address rva;
        void *replacement;
        void **original;
    };
    const std::array hooks{
        Hook{indicators::hud_tick, reinterpret_cast<void *>(tick_hook), reinterpret_cast<void **>(&shared().tick)},
        Hook{indicators::provider_remove, reinterpret_cast<void *>(remove_hook), reinterpret_cast<void **>(&shared().remove)},
        Hook{indicators::widget_create, reinterpret_cast<void *>(widget_hook), reinterpret_cast<void **>(&shared().widget)}};
    unsigned prepared{};
    for (const auto &hook : hooks) {
        if (hook_prepare(reinterpret_cast<void *>(base + hook.rva), hook.replacement, hook.original) !=
            HookOk) {
            while (prepared)
                hook_remove(reinterpret_cast<void *>(base + hooks[--prepared].rva));
            throw std::runtime_error("Cannot prepare native indicator hooks.");
        }
        ++prepared;
    }
    // One transaction (one suspension of every game thread) for all of them.
    for (const auto &hook : hooks)
        require(hook_queue_enable(reinterpret_cast<void *>(base + hook.rva)) == HookOk,
                "Cannot enable native indicator hooks; restart ReSkate.");
    require(hook_apply_queued() == HookOk, "Cannot enable native indicator hooks; restart ReSkate.");
    shared().installed.store(true, std::memory_order_release);
}
} // namespace
void update_native_indicators(Address base, const Pose *pose, const std::string &name) noexcept {
    auto &s = state();
    try {
        {
            std::lock_guard lock(s.snapshot_mutex);
            // The indicator consumes only the root. PoseBuffer already
            // validated the complete packet at admission.
            s.visible = pose && valid_transform(pose->root);
            s.sampled = GetTickCount64();
            s.name = name;
            if (s.visible)
                s.world = to_matrix(pose->root);
            s.generation.fetch_add(1, std::memory_order_release);
        }
        if (pose && !shared().attempted)
            install(base);
    } catch (const std::exception &e) {
        status(e.what());
    }
}
void set_native_nametags_enabled(bool enabled) noexcept {
    shared().nametags.store(enabled, std::memory_order_relaxed);
}
void prepare_native_indicators(Address base) noexcept {
    try {
        if (!shared().attempted) install(base);
    } catch (const std::exception &e) {
        status(e.what());
    }
}
void set_native_compass_enabled(bool enabled) noexcept {
    shared().compass.store(enabled, std::memory_order_relaxed);
}
std::string native_indicators_status() {
    std::lock_guard lock(state().snapshot_mutex);
    return state().status;
}
} // namespace dingosdk::multiplayer

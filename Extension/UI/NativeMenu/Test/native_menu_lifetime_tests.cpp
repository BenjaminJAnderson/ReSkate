#include "Extension/UI/NativeMenu/native_menu_lifetime.h"
#include <cstdio>
#include <array>
#include <map>
#include <stdexcept>

using dingosdk::multiplayer::menu_data::OwnedMenuModels;
using dingosdk::multiplayer::menu_data::MenuLifetime;
struct Model { unsigned handle, type; };
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void map_transition() {
    OwnedMenuModels<Model> owned;
    std::map<unsigned, unsigned> live{{1, 100}, {2, 200}, {3, 300}};
    owned.track({2, 200}); // First pause-menu instance.
    owned.track({3, 300}); // Partially built replacement instance.
    owned.track({3, 300}); // Engine create can return an existing owned root.
    bool attached = true, assets_loaded = true;
    std::vector<unsigned> destroyed;
    const auto destroy = [&](Model value) {
        check(!attached, "Detach tabs before destroying their data");
        check(assets_loaded, "Release list templates while their assets are still loaded");
        check(live.at(value.handle) == value.type, "Destroy only matching owned roots");
        live.erase(value.handle);
        destroyed.push_back(value.handle);
    };
    owned.release([&] { attached = false; }, destroy);
    assets_loaded = false; // The common loader may now submit either menu's request.
    check(owned.empty() && live.size() == 1 && live.contains(1), "Native base-game roots remain owned by the game");
    check(destroyed == std::vector<unsigned>{3, 2}, "Partial builds and older menu models are released once");
    owned.release([&] { throw std::runtime_error("Repeated teardown must be inert"); }, destroy);
    assets_loaded = true;
    live.emplace(4, 400);
    owned.track({4, 400}); // Reopening after loading creates a fresh menu.
    owned.release([] {}, destroy);
    check(destroyed.back() == 4 && owned.empty(), "The next world gets an independent menu lifetime");
}
void interrupted_cleanup() {
    OwnedMenuModels<Model> owned;
    owned.track({10, 100}); owned.track({20, 200});
    unsigned calls{};
    bool failed{};
    try {
        owned.release([] { throw std::runtime_error("Detach failed"); }, [&](Model) { ++calls; });
    } catch (const std::runtime_error&) { failed = true; }
    check(failed && calls == 0 && !owned.empty(), "Failed detach must preserve models and prevent unload");
    std::vector<unsigned> destroyed;
    try {
        owned.release([] {}, [&](Model value) {
            if (value.handle == 10) throw std::runtime_error("Destroy failed");
            destroyed.push_back(value.handle);
        });
    } catch (const std::runtime_error&) { }
    owned.release([] {}, [&](Model value) { destroyed.push_back(value.handle); });
    check(destroyed == std::vector<unsigned>{20, 10}, "Retry must finish cleanup without double destruction");
    owned.track({30, 300});
    owned.manager_replaced();
    owned.release([] {}, [&](Model) { throw std::runtime_error("Old manager handle reused"); });
    check(owned.empty(), "Replacing the native manager must discard its expired handles");
}
void window_close() {
    MenuLifetime lifetime;
    lifetime.before_transition(13, [] {});
    std::array<OwnedMenuModels<Model>, 2> pages;
    pages[0].track({10, 100}); pages[1].track({20, 200});
    std::array<bool, 2> attached{true, true};
    bool assets_loaded = true;
    unsigned destroyed{}, cleanups{};
    const auto cleanup = [&] {
        ++cleanups;
        check(lifetime.blocked(), "Stop updates before cleanup can invoke UI callbacks");
        for (unsigned slot = 0; slot < pages.size(); ++slot) {
            pages[slot].release([&] { attached[slot] = false; }, [&](Model) {
                check(!attached[slot] && assets_loaded,
                    "Close-window cleanup must release widget references before asset unload");
                ++destroyed;
            });
        }
    };
    for (unsigned state : {13U, 13U, 21U}) lifetime.before_transition(state, cleanup);
    check(cleanups == 0 && !lifetime.blocked(), "Active-state notifications must not destroy menu models");
    lifetime.before_transition(24, cleanup); // Before native state-24 handler.
    assets_loaded = false; // The native handler unloads the widget asset arena.
    for (unsigned state : {24U, 25U, 25U, 13U, 21U, 14U}) {
        lifetime.before_transition(state, cleanup);
        if (!lifetime.blocked()) pages[0].track({30, 300}); // A remaining client tick.
    }
    check(cleanups == 1 && destroyed == 2 && pages[0].empty() && pages[1].empty(),
        "Both menu tabs must stay released through all remaining shutdown ticks");
    for (auto& page : pages)
        page.release([] {}, [&](Model) { throw std::runtime_error("Late widget release reproduces the shutdown crash"); });

    MenuLifetime failed;
    failed.before_transition(24, [] {}); // Cleanup can report failure without clearing the latch.
    check(failed.blocked(), "Even an incomplete cleanup must prevent new models during shutdown");
}
void native_transition_without_load_request() {
    // The multiplayer crash used the game's transition path. No ReSkate load
    // was queued, so the scheduler's explicit cleanup never ran. Reopening the
    // pause menu and later ticks must not retain/recreate the old asset refs.
    for (unsigned leaving : {14U, 22U, 3U}) {
        MenuLifetime lifetime;
        std::array<OwnedMenuModels<Model>, 2> pages;
        check(lifetime.blocked(), "Do not build menus before the first active world");
        lifetime.before_transition(13, [] { throw std::runtime_error("Unexpected initial cleanup"); });
        pages[0].track({10, 100}); pages[1].track({20, 200});
        bool assets_loaded = true;
        unsigned cleanups{}, destroyed{}, published{}, actions{};
        const auto cleanup = [&] {
            ++cleanups;
            check(lifetime.blocked(), "Block reentrant callbacks before native transition cleanup");
            for (auto& page : pages) page.release([] {}, [&](Model) {
                check(assets_loaded, "Destroy menu asset references before the engine frees assets");
                ++destroyed;
                if (!lifetime.blocked()) ++actions;
            });
        };
        lifetime.before_transition(leaving, cleanup);
        assets_loaded = false;
        for (unsigned state : {leaving, 3U, 4U, 8U, 9U, 12U}) {
            lifetime.before_transition(state, cleanup);
            if (!lifetime.blocked()) { ++published; ++actions; }
        }
        check(cleanups == 1 && destroyed == 2 && published == 0 && actions == 0,
            "Native unload must clean both pages once and suppress updates/actions throughout loading");
        for (auto& page : pages) page.release([] {}, [&](Model) {
            throw std::runtime_error("Late model destruction reproduces the multiplayer crash");
        });
        assets_loaded = true;
        lifetime.before_transition(leaving == 22 ? 21 : 13, cleanup);
        check(!lifetime.blocked(), "The next active level/sublevel must allow reopening the menu");
        pages[0].track({30, 300}); pages[1].track({40, 400});
        lifetime.before_transition(14, cleanup);
        check(cleanups == 2 && destroyed == 4, "Clean subsequent worlds, not just the first transition");
        lifetime.before_transition(24, cleanup);
        check(lifetime.blocked() && destroyed == 4, "Shutdown during loading must not release roots twice");
    }
}
int main() {
    try {
        map_transition(); interrupted_cleanup(); window_close(); native_transition_without_load_request();
        std::puts("Native menu lifetime: scheduled/native transitions, blocked callbacks, reload, shutdown, partial builds and retries passed.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what()); return 1;
    }
}

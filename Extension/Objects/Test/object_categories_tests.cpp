// Checks the Object Browser layout built from category records, first on a small
// hand-made catalogue, then on the installed content cache when there is one.
#include "Extension/Objects/object_categories.h"
#include "Engine/Vfs/content_cache.h"
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
const dingosdk::objects::BrowserCategory* find(const std::vector<dingosdk::objects::BrowserCategory>& layout,
    std::string_view id) {
    for (const auto& category : layout)
        if (category.id == id) return &category;
    return nullptr;
}
}

int main() {
    using namespace dingosdk;
    content_cache::Catalogs catalogs;
    catalogs.available = true;
    catalogs.items["own_bkramps_aframe_long_00001"] = Json{{"title", "A Frame, Long"}, {"group", "qdparent_aframe"}};
    catalogs.items["own_bkramps_aframe_short_00001"] = Json{{"title", "A Frame, Short"}, {"group", "qdparent_aframe"}};
    catalogs.items["own_bkrails_flat_00001"] = Json{{"group", "qdparent_flatrail"}};
    catalogs.items["own_bkvertbasesmall_00001"] = Json{{"group", "bkvertbasesmall"}};
    catalogs.object_categories = {
        {"category_bk", "Build Kit", "cdn:/1", 1, false, {{"bkvertbasesmall", "Bowl Small", 0}}},
        {"category_rail", "Rail", "cdn:/2", 2, true, {{"qdparent_flatrail", "Flat Rail", 0}}},
        {"category_ramp", "Ramp", "cdn:/3", 1, true, {{"qdparent_aframe", "A Frame", 0}}},
    };
    const std::vector<objects::InstalledObject> owned{
        {"own_bkramps_aframe_long_00001", "bkramps", "A Frame, Long"},
        {"own_bkramps_aframe_short_00001", "bkramps", "A Frame, Short"},
        {"own_bkrails_flat_00001", "bkrails", "Flat"},
        {"own_bkvertbasesmall_00001", "bkvertbasesmall", "Bowl"},
        {"own_bkprops_unknown_00001", "bkprops", "Unknown prop"},
    };
    const auto layout = objects::browser_layout(owned, catalogs);
    check(layout.size() == 4, "four categories");
    check(layout.size() == 4 && layout[0].id == "category_ramp" && layout[1].id == "category_rail" &&
        layout[2].id == "category_bk" && layout[3].id == "bkprops",
        "browser categories by priority, then other build-kit ones, then unplaced families");
    if (const auto* ramp = find(layout, "category_ramp")) {
        check(ramp->title == "Ramp" && ramp->tiles.size() == 1, "variants share one tile");
        check(ramp->tiles[0].id == "qdparent_aframe" && ramp->tiles[0].title == "A Frame" &&
            ramp->tiles[0].items.size() == 2, "tile named after its group");
    }
    if (const auto* other = find(layout, "bkprops"))
        check(other->tiles.size() == 1 && other->tiles[0].id == "own_bkprops_unknown_00001" &&
            other->tiles[0].title == "Unknown prop", "unplaced object gets its own tile");
    const auto groups = objects::object_groups(catalogs);
    check(objects::browser_tile("OWN_BKRAMPS_AFRAME_LONG_00001", catalogs, groups) == "qdparent_aframe",
        "tile lookup ignores case");
    check(objects::browser_tile("own_bkprops_unknown_00001", catalogs, groups) == "own_bkprops_unknown_00001",
        "unplaced tile is the object itself");

    if (content_cache::installed()) {
        const auto& cache = content_cache::catalogs();
        std::vector<std::string> browser;
        for (const auto& category : cache.object_categories)
            if (category.quick_drop) browser.push_back(category.title);
        check(browser.size() == 5, "five Object Browser categories in the cache");
        const auto cached = objects::object_groups(cache);
        const auto placement = objects::object_placement("own_bkramps_generic_aframelong_00001", cache, cached);
        check(placement.category && placement.category->title == "Ramp" && placement.group &&
            placement.group->title == "A Frame", "cached object placed under Ramp / A Frame");
        check(cache.items.at("own_bkramps_generic_aframelong_00001").value("object_type", "") == "QuickDrop",
            "cached object type");
        std::cout << browser.size() << " browser categories, " << cached.size() << " object groups in the cache.\n";
    } else {
        std::cout << "No content cache installed; cache checks skipped.\n";
    }
    return failures ? 1 : 0;
}

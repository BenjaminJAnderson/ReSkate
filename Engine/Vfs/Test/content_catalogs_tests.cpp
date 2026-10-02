// Reads the installed content cache (or the folder given as the first argument)
// and checks catalogue entries that are stable for build 24855063. Skips when no
// pack is available. A second argument writes the catalogues as JSON.
#include "Engine/Vfs/content_cache.h"
#include "Engine/Vfs/content_catalogs.h"
#include "Engine/Game/World/location_travel.h"
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main(int argc, char** argv) {
    using namespace dingosdk;
    const std::filesystem::path folder = argc > 1 ? std::filesystem::path(argv[1]) : content_cache::directory();
    if (!std::filesystem::exists(folder)) {
        std::cout << "No content cache at " << folder.string() << "; skipped.\n";
        return 0;
    }
    const auto catalogs = content_cache::read_catalogs(folder);
    check(catalogs.available, "catalogues read");
    check(catalogs.entitlements.size() == 381, "381 entitlements");
    check(catalogs.items.size() >= 3000, "owned items");
    check(catalogs.items.at("own_bkbanks_generic_volcanolarge_00001").value("title", "") == "Volcano, L Wood",
        "item keyed by owned asset id");
    check(catalogs.items.at("own_costume_gen_dembones_00001").value("rarity_id", "") == "legendary", "item rarity");
    const auto& line = catalogs.challenges.at("Base_CH02_M02_Line01");
    check(line.value("type", "") == "Line" && line.value("neighborhood", "") == "neighbourhood_rank_historic",
        "challenge type and neighbourhood");
    check(line.at("asset").string() == "activities/challenges/prefabs/line/base_ch02_m02_line01_ecsprefab",
        "challenge asset path");
    check(line.at("goals").size() == 4 && line.at("goals").at(0).at("id").string() == "Temp_CompleteLine" &&
        line.at("goals").at(1).at("feed_key").string() == "ID_CRITERIA_MANUALDIST_FEED", "challenge goals in order");
    check(!catalogs.challenges.contains("location_bam"), "records without goals are not challenges");
    const content_cache::ObjectCategory* ramp{};
    std::size_t browser{};
    for (const auto& category : catalogs.object_categories) {
        browser += category.quick_drop;
        if (category.id == "category_ramp") ramp = &category;
    }
    check(browser == 5, "five Object Browser categories");
    check(ramp && ramp->title == "Ramp" && ramp->quick_drop && !ramp->groups.empty() &&
        ramp->groups.front().id == "qdparent_aframe" && ramp->groups.front().title == "A Frame",
        "Ramp category with its object groups");
    check(catalogs.items.at("own_bkramps_generic_aframelong_00001").value("group", "") == "qdparent_aframe",
        "object names its group");
    const auto travel = location_travel_policy(Json::object(), catalogs);
    check(travel.enabled && travel.destinations.size() == 4, "four travel destinations");
    if (const auto* bam = find_travel_destination(travel, "location_bam"))
        check(bam->map == "bam" && bam->name == "San Van Downtown" && bam->medium == 1 &&
            bam->white_icon == "https://dingo-dev-assets.akamaized.net/cdn/production/c3257fa9304b1a916f4f580ef13de468",
            "San Van destination");
    else check(false, "San Van destination");
    if (const auto* mpr = find_travel_destination(travel, "location_mpr"))
        check(mpr->name == "SUMR Camp" && mpr->large_image.ends_with("-windows-default"), "SUMR Camp destination");
    else check(false, "SUMR Camp destination");
    const auto stadium = travel.access_points.find("accesspoint_bam_stadium");
    check(stadium != travel.access_points.end() && stadium->second == std::vector<std::string>{"location_stadium"},
        "stadium entrance offers only the first stadium");
    check(travel.access_points.size() == 5, "five access points");
    if (argc > 2) {
        Json dump{{"items", catalogs.items}, {"challenges", catalogs.challenges}, {"entitlements", Json::array()}};
        for (const auto& id : catalogs.entitlements) dump["entitlements"].push_back(id);
        std::ofstream(argv[2], std::ios::binary) << dump.dump(1);
    }
    std::cout << catalogs.items.size() << " items, " << catalogs.challenges.size() << " challenges, "
              << catalogs.entitlements.size() << " entitlements.\n";
    return failures ? 1 : 0;
}

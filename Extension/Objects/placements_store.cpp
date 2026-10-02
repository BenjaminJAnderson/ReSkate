#include "object_placements.h"
#include "Extension/Profile/profile_internal.h"
#include "Extension/Profile/database_codec.h"
#include <cmath>
#include <set>
#include <type_traits>

namespace dingosdk::profile {
using namespace detail;
// JSON is the interchange format; SQLite owns durable per-object records.
bool valid_placed_object(const PlacedObject& object) noexcept {
    if (!object.id || !object.item.starts_with("own_bk") || !valid_text(object.item)) return false;
    for (const float x : object.position) if (!std::isfinite(x) || std::abs(x) > 100000) return false;
    if (!std::isfinite(object.scale) || object.scale < .01f || object.scale > 100.0f) return false;
    float norm{};
    for (const float x : object.rotation) {
        if (!std::isfinite(x)) return false;
        norm += x * x;
    }
    return std::isfinite(norm) && norm > 0.98f && norm < 1.02f;
}
static Json placement_json(const PlacementSnapshot& s) {
    auto root = s.extensions;
    require(root.is_object() && s.maps.size() <= 32, "Invalid object layouts");
    // Scale is stored in an extension so existing strict objects.sqlite3 files
    // remain readable without a destructive SQL schema migration.
    root.erase("object_scales");
    Json scales = Json::object();
    root["schema_version"] = 1;
    root["revision"] = s.revision;
    root["maps"] = Json::object();
    for (const auto& [map, objects] : s.maps) {
        require(valid_text(map) && objects.size() <= 1024, "Object layout exceeds its limit");
        std::set<std::uint64_t> ids;
        auto& entries = root["maps"][map]; entries = Json::array();
        for (const auto& object : objects) {
            require(valid_placed_object(object) && ids.insert(object.id).second, "Invalid or duplicate placed object");
            entries.push_back({{"id", object.id}, {"item", object.item},
                {"position", object.position}, {"rotation", object.rotation}});
            if (object.scale != 1.0f)
                scales[map][std::to_string(object.id)] = object.scale;
        }
    }
    if (!scales.empty()) root["object_scales"] = std::move(scales);
    return root;
}
std::string encode_placements(const PlacementSnapshot& s) {
    auto text = placement_json(s).dump(2) + '\n';
    require(text.size() <= max_bytes, "Object layouts exceed size limit");
    return text;
}
static PlacementSnapshot decode_document(const Json& root) {
    require(root.is_object() && unsigned_value(root.at("schema_version"), 1) == 1,
        "Unsupported object layout schema");
    PlacementSnapshot s;
    const auto scales = root.value("object_scales", Json::object());
    require(scales.is_object(), "Invalid object scale extension");
    s.extensions = root; s.extensions.erase("schema_version"); s.extensions.erase("revision");
    s.extensions.erase("maps"); s.extensions.erase("object_scales");
    s.revision = unsigned_value(root.at("revision"), UINT64_MAX);
    for (const auto& [map, entries] : object_field(root, "maps").items()) {
        require(entries.is_array(), "Object layout must be an array");
        auto& objects = s.maps[map];
        for (const auto& entry : entries) {
            require(entry.is_object() && entry.size() == 4 && entry.at("item").is_string() &&
                entry.at("position").is_array() && entry.at("position").size() == 3 &&
                entry.at("rotation").is_array() && entry.at("rotation").size() == 4, "Invalid placed object fields");
            PlacedObject object;
            object.id = unsigned_value(entry.at("id"), UINT64_MAX);
            object.item = entry.at("item").get<std::string>();
            for (unsigned i = 0; i < 3; ++i) {
                require(entry["position"][i].is_number(), "Invalid object position");
                object.position[i] = entry["position"][i].get<float>();
            }
            for (unsigned i = 0; i < 4; ++i) {
                require(entry["rotation"][i].is_number(), "Invalid object rotation");
                object.rotation[i] = entry["rotation"][i].get<float>();
            }
            objects.push_back(std::move(object));
        }
    }
    std::size_t scale_count{};
    for (const auto& [map, entries] : scales.items()) {
        const auto found_map = s.maps.find(map);
        require(found_map != s.maps.end() && entries.is_object(), "Invalid object scale map");
        for (auto& object : found_map->second) {
            const auto key = std::to_string(object.id);
            if (!entries.contains(key)) continue;
            require(entries.at(key).is_number(), "Invalid object scale");
            object.scale = entries.at(key).get<float>();
            ++scale_count;
        }
        require(scale_count <= 1024 * 32, "Object scale extension exceeds its limit");
    }
    std::size_t encoded_scale_count{};
    for (const auto& [map, entries] : scales.items()) {
        (void)map;
        encoded_scale_count += entries.size();
    }
    require(scale_count == encoded_scale_count, "Object scale refers to an unknown object");
    (void)placement_json(s);
    return s;
}
PlacementSnapshot decode_placements(std::string_view text) { return decode_document(parse_json(text)); }
PlacementStore::PlacementStore(std::filesystem::path path) : path_(storage::database_path(std::move(path))),
    database_(std::make_unique<storage::SaveDatabase>(path_, database::placement_schema())) {
    if (database_->exists()) {
        value_ = decode_document(database::placement_document(database_->document())); database_->backup();
    } else {
        const auto json = storage::json_source_path(path_);
        if (std::filesystem::exists(json)) value_ = decode_placements(read_file(json));
        else require(!std::filesystem::exists(json.wstring() + L".bak"), "Object layout backup requires recovery");
        database_->initialize(database::placement_tables(placement_json(value_)));
    }
}
PlacementStore::~PlacementStore() = default;
PlacementSnapshot PlacementStore::snapshot() const { std::lock_guard lock(mutex_); return value_; }
std::string PlacementStore::export_json() const { std::lock_guard lock(mutex_); return encode_placements(value_); }
void PlacementStore::import_json(std::string_view text) {
    auto next = decode_placements(text);
    std::lock_guard lock(mutex_); next.revision = value_.revision;
    if (next == value_) return;
    require(next.revision != UINT64_MAX, "Object layout revision overflow"); ++next.revision;
    database_->commit(database::placement_tables(placement_json(next))); value_ = std::move(next);
}
void PlacementStore::replace_map(std::string_view map, const ObjectLayout& layout) {
    std::lock_guard lock(mutex_);
    const auto existing = value_.maps.find(map);
    if (existing != value_.maps.end() && existing->second == layout) return;
    require(valid_text(map) && value_.revision != UINT64_MAX, "Invalid object layout revision or map");
    auto next = value_; next.maps[std::string(map)] = layout; ++next.revision;
    database_->commit(database::placement_tables(placement_json(next)));
    value_ = std::move(next);
}

}

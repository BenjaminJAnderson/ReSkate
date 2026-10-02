#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "Engine/Core/Json/json.h"

namespace dingosdk::storage { class SaveDatabase; }

namespace dingosdk::profile {
struct PlacedObject {
    std::uint64_t id{}; // ReSkate layout identity; never an ECS handle.
    std::string item;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0, 0, 0, 1}; // x,y,z,w
    float scale{1}; // Uniform: the native Build Kit physics pose has one scale lane.
    bool operator==(const PlacedObject&) const = default;
};
using ObjectLayout = std::vector<PlacedObject>;
struct PlacementSnapshot {
    std::uint64_t revision{};
    std::map<std::string, ObjectLayout, std::less<>> maps;
    dingosdk::Json extensions = dingosdk::Json::object();
    bool operator==(const PlacementSnapshot&) const = default;
};
bool valid_placed_object(const PlacedObject&) noexcept;
PlacementSnapshot decode_placements(std::string_view);
std::string encode_placements(const PlacementSnapshot&);
class PlacementStore {
public:
    explicit PlacementStore(std::filesystem::path);
    ~PlacementStore();
    PlacementStore(const PlacementStore&) = delete;
    PlacementStore& operator=(const PlacementStore&) = delete;
    PlacementSnapshot snapshot() const;
    std::string export_json() const;
    void import_json(std::string_view);
    const std::filesystem::path& path() const { return path_; }
    void replace_map(std::string_view, const ObjectLayout&);
private:
    std::filesystem::path path_;
    std::unique_ptr<storage::SaveDatabase> database_;
    mutable std::mutex mutex_;
    PlacementSnapshot value_;
};
}

#include "material_grid.h"

#include "ebx_carry.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace dingosdk::frostbite::material_grid {
namespace {
using ebx::Document;
using ebx::Object;
using ebx::Value;

constexpr std::uint32_t flagMask = 0x3F;
constexpr std::uint32_t slotMask = 0x1FFF;
constexpr std::uint32_t materialShift = 6;
constexpr std::uint32_t propertyShift = 19;

Object& root_object(Document& document) {
    if (document.instances.empty() || !document.instances.front().object)
        throw std::runtime_error("material grid has no root object");
    return *document.instances.front().object;
}

const Object& root_object(const Document& document) {
    const auto* root = document.root();
    if (!root || !root->object) throw std::runtime_error("material grid has no root object");
    return *root->object;
}

Value::Array& array(Object& object, std::string_view name) {
    for (auto& field : object.fields)
        if (field.name == name)
            if (auto* list = std::get_if<Value::Array>(&field.value.data)) return *list;
    throw std::runtime_error("material grid has no " + std::string(name) + " list");
}

const Value::Array& array(const Object& object, std::string_view name) {
    if (const auto* field = object.find(name))
        if (const auto* list = std::get_if<Value::Array>(&field->value.data)) return *list;
    throw std::runtime_error("material grid has no " + std::string(name) + " list");
}

std::uint64_t number(const Value& value) {
    if (const auto* unsignedValue = std::get_if<std::uint64_t>(&value.data)) return *unsignedValue;
    if (const auto* signedValue = std::get_if<std::int64_t>(&value.data))
        return static_cast<std::uint64_t>(*signedValue);
    throw std::runtime_error("material grid entry is not a number");
}

// Keeps the reader's choice of signed or unsigned storage.
void set_number(Value& value, std::uint64_t number) {
    if (std::holds_alternative<std::int64_t>(value.data)) value.data = static_cast<std::int64_t>(number);
    else value.data = number;
}

// A grid row: an InteractionGrid entry is a struct holding its cells.
Object& row_object(Value& value) {
    auto* object = std::get_if<std::shared_ptr<Object>>(&value.data);
    if (!object || !*object) throw std::runtime_error("material grid row is not a struct");
    return **object;
}

const Value::Array& cells(const Value& row) {
    const auto* object = std::get_if<std::shared_ptr<Object>>(&row.data);
    if (!object || !*object) throw std::runtime_error("material grid row is not a struct");
    return array(**object, "Items");
}

// What an instance holds, pointers resolved to what they point at, so a copy a
// Studio build re-guided can be matched to the base instance it was cloned from.
// Carrying such copies instead would duplicate the grid's relations per mod.
class Fingerprints final {
public:
    explicit Fingerprints(const Document& document)
        : document_(document), memo_(document.instances.size()), state_(document.instances.size()) {}

    const std::string& of(std::size_t index) {
        if (state_[index] == 2) return memo_[index];
        state_[index] = 1;
        const auto& instance = document_.instances[index];
        std::string text = instance.exported ? "E" : "I";
        // Each pointer followed recurses: a mod's long pointer chain must fail, not overflow the stack.
        if (depth_ >= 512) throw std::runtime_error("material grid pointers chain too deeply");
        ++depth_;
        struct Leave { unsigned& depth; ~Leave() { --depth; } } leave{depth_};
        if (instance.object) object(*instance.object, text);
        memo_[index] = std::move(text);
        state_[index] = 2;
        return memo_[index];
    }

private:
    void object(const Object& value, std::string& out) {
        out += '{';
        if (value.descriptor >= 0 && static_cast<std::size_t>(value.descriptor) < document_.types.size())
            out += document_.types[static_cast<std::size_t>(value.descriptor)].name;
        for (const auto& field : value.fields) {
            out += ';';
            out += field.name;
            out += '=';
            this->value(field.value, out);
        }
        out += '}';
    }

    void value(const Value& value, std::string& out) {
        std::visit([&](const auto& held) {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::monostate>) {
                out += '~';
            } else if constexpr (std::is_same_v<Held, bool>) {
                out += held ? "b1" : "b0";
            } else if constexpr (std::is_same_v<Held, std::int64_t>) {
                out += 'i' + std::to_string(held);
            } else if constexpr (std::is_same_v<Held, std::uint64_t>) {
                out += 'u' + std::to_string(held);
            } else if constexpr (std::is_same_v<Held, double>) {
                out += 'd' + std::to_string(std::bit_cast<std::uint64_t>(held));
            } else if constexpr (std::is_same_v<Held, std::string>) {
                out += 's' + std::to_string(held.size()) + ':' + held;
            } else if constexpr (std::is_same_v<Held, Guid>) {
                out += 'g' + held.string();
            } else if constexpr (std::is_same_v<Held, Sha1>) {
                out += 'h';
                for (const auto byte : held.bytes) out += std::to_string(std::to_integer<int>(byte)) + '.';
            } else if constexpr (std::is_same_v<Held, ebx::ResourceReference>) {
                out += 'r' + std::to_string(held.id);
            } else if constexpr (std::is_same_v<Held, ebx::PointerReference>) {
                if (held.kind == ebx::PointerKind::null || held.index < 0) {
                    out += "p0";
                } else if (held.kind == ebx::PointerKind::external) {
                    const auto& import = document_.imports.at(static_cast<std::size_t>(held.index));
                    out += "px" + import.fileGuid.string() + '/' + import.classGuid.string();
                } else {
                    const auto target = static_cast<std::size_t>(held.index);
                    if (target >= document_.instances.size()) throw std::runtime_error("EBX pointer is out of range");
                    out += state_[target] == 1 ? std::string("p@") : "p#" + of(target);
                }
            } else if constexpr (std::is_same_v<Held, ebx::TypeReference>) {
                if (held.primitive) out += "tp" + std::to_string(static_cast<int>(held.primitiveType));
                else if (held.descriptor >= 0 && static_cast<std::size_t>(held.descriptor) < document_.types.size())
                    out += "td" + document_.types[static_cast<std::size_t>(held.descriptor)].name;
                else out += "t?";
            } else if constexpr (std::is_same_v<Held, ebx::BoxedReference>) {
                out += "x" + std::to_string(held.encodedType) + ':' + std::to_string(held.dataOffset);
            } else if constexpr (std::is_same_v<Held, std::shared_ptr<Object>>) {
                if (held) object(*held, out);
                else out += "o0";
            } else if constexpr (std::is_same_v<Held, Value::Array>) {
                out += '[';
                for (const auto& element : held) {
                    this->value(element, out);
                    out += ',';
                }
                out += ']';
            }
        }, value.data);
    }

    const Document& document_;
    std::vector<std::string> memo_;
    std::vector<std::uint8_t> state_;
    unsigned depth_{};
};

struct Header {
    std::size_t parts{};
    std::size_t partTable{};
};

constexpr std::uint32_t physicsMagic = 0x69AF7015;
constexpr std::size_t partStride = 72;
constexpr std::size_t partMaterial = 0x40;
constexpr std::size_t partTableField = 0x24 + 3 * 4;

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at) {
    if (at + 4 > bytes.size()) throw std::runtime_error("PhysicsResource is truncated");
    std::uint32_t value{};
    std::memcpy(&value, bytes.data() + at, 4);
    return value;
}

Header physics_header(std::span<const std::byte> payload) {
    const auto version = read_u32(payload, 0);
    if (read_u32(payload, 4) != physicsMagic || (version != 0 && version != 2))
        throw std::runtime_error("not a native PhysicsResource");
    Header header;
    header.parts = read_u32(payload, 0x0C);
    header.partTable = partTableField + read_u32(payload, partTableField);
    if (header.partTable + partStride * header.parts > payload.size())
        throw std::runtime_error("PhysicsResource part table runs past its end");
    return header;
}

} // namespace

bool SlotMap::identity() const noexcept {
    for (std::size_t slot = 0; slot < slots.size(); ++slot)
        if (slots[slot] != slot) return false;
    return true;
}

std::uint32_t SlotMap::remap(std::uint32_t packed) const noexcept {
    const auto move = [&](std::uint32_t slot) {
        return slot < slots.size() ? slots[slot] : 0U;
    };
    const auto material = move((packed >> materialShift) & slotMask);
    const auto property = move((packed >> propertyShift) & slotMask);
    return (packed & flagMask) | (material << materialShift) | (property << propertyShift);
}

SlotMap SlotMap::to_default(std::size_t baseSlots, std::size_t slots) {
    SlotMap map;
    map.slots.resize(slots);
    for (std::size_t slot = 0; slot < slots; ++slot)
        map.slots[slot] = slot < baseSlots ? static_cast<std::uint32_t>(slot) : 0U;
    return map;
}

Combiner::Combiner(const Document& base) : combined_(ebx::detail::clone_document(base)) {
    const auto& root = root_object(combined_);
    const auto& indexMap = array(root, "MaterialIndexMap");
    const auto& properties = array(root, "MaterialProperties");
    const auto& grid = array(root, "InteractionGrid");
    const auto& network = array(root, "NetworkIdToPackedMaterial");
    if (grid.size() != properties.size()) throw std::runtime_error("base material grid is not square");
    for (const auto& row : grid)
        if (cells(row).size() != properties.size()) throw std::runtime_error("base material grid is not square");
    baseSlots_ = slots_ = indexMap.size();
    baseRows_ = rows_ = properties.size();
    baseNetwork_ = network.size();
    for (const auto& value : indexMap) baseIndexMap_.push_back(number(value));
    for (const auto& value : network) baseNetworkIds_.push_back(number(value));
}

SlotMap Combiner::add(const Document& edit) {
    const auto& editRoot = root_object(edit);
    const auto& editIndex = array(editRoot, "MaterialIndexMap");
    const auto& editProperties = array(editRoot, "MaterialProperties");
    const auto& editGrid = array(editRoot, "InteractionGrid");
    const auto& editNetwork = array(editRoot, "NetworkIdToPackedMaterial");

    // The edit has to be this game's grid with things appended: the same slots,
    // rows and network ids at the front, and a square matrix over all its rows.
    if (editIndex.size() < baseSlots_ || editProperties.size() < baseRows_ || editNetwork.size() < baseNetwork_)
        throw std::runtime_error("it is smaller than the game's material grid");
    for (std::size_t slot = 0; slot < baseSlots_; ++slot)
        if (number(editIndex[slot]) != baseIndexMap_[slot])
            throw std::runtime_error("its slot table differs from the game's (slot " + std::to_string(slot) + ")");
    for (std::size_t id = 0; id < baseNetwork_; ++id)
        if (number(editNetwork[id]) != baseNetworkIds_[id])
            throw std::runtime_error("its network ids differ from the game's (id " + std::to_string(id) + ")");
    if (editGrid.size() != editProperties.size()) throw std::runtime_error("its interaction grid is not square");
    for (const auto& row : editGrid)
        if (cells(row).size() != editProperties.size()) throw std::runtime_error("its interaction grid is not square");
    for (std::size_t slot = baseSlots_; slot < editIndex.size(); ++slot)
        if (number(editIndex[slot]) >= editProperties.size())
            throw std::runtime_error("slot " + std::to_string(slot) + " names a missing row");

    const auto newSlots = editIndex.size() - baseSlots_;
    const auto newRows = editProperties.size() - baseRows_;
    if (slots_ + newSlots > slotMask + 1) throw std::runtime_error("the combined material grid has no slots left");

    SlotMap map;
    map.slots.resize(editIndex.size());
    for (std::size_t slot = 0; slot < editIndex.size(); ++slot)
        map.slots[slot] = static_cast<std::uint32_t>(slot < baseSlots_ ? slot : slots_ + (slot - baseSlots_));
    if (!newSlots && !newRows && editNetwork.size() == baseNetwork_) return map;

    // Work on a copy, so a failure part way leaves the combined grid intact.
    auto next = ebx::detail::clone_document(combined_);
    auto& root = root_object(next);
    auto& indexMap = array(root, "MaterialIndexMap");
    auto& properties = array(root, "MaterialProperties");
    auto& grid = array(root, "InteractionGrid");
    auto& network = array(root, "NetworkIdToPackedMaterial");

    ebx::detail::Carrier carrier(edit, next);
    {
        Fingerprints existing(next);
        std::map<std::string, std::size_t, std::less<>> byContent;
        for (std::size_t index = 1; index < next.instances.size(); ++index)
            byContent.emplace(existing.of(index), index);
        Fingerprints incoming(edit);
        std::size_t matched{};
        for (std::size_t index = 1; index < edit.instances.size(); ++index)
            if (const auto found = byContent.find(incoming.of(index)); found != byContent.end()) {
                carrier.alias(index, found->second);
                ++matched;
            }
        reused_ += matched;
    }

    // Combined row -> the edit's row whose relations it takes: base rows are
    // shared, the edit's own rows follow the rows already combined, and rows
    // other mods added (never on the same map) stand in with the default row.
    const auto rowOf = [&](std::uint64_t editRow) {
        return editRow < baseRows_ ? editRow : rows_ + (editRow - baseRows_);
    };
    const auto sourceOf = [&](std::size_t combinedRow) -> std::size_t {
        if (combinedRow < baseRows_) return combinedRow;
        if (combinedRow < rows_) return 0;
        return baseRows_ + (combinedRow - rows_);
    };
    const auto totalRows = rows_ + newRows;

    for (std::size_t row = baseRows_; row < editProperties.size(); ++row)
        properties.push_back(carrier.value(editProperties[row]));
    for (std::size_t row = 0; row < rows_; ++row) {
        auto& items = array(row_object(grid[row]), "Items");
        const auto& source = cells(editGrid[sourceOf(row)]);
        for (std::size_t added = 0; added < newRows; ++added)
            items.push_back(carrier.value(source[baseRows_ + added]));
    }
    for (std::size_t added = 0; added < newRows; ++added) {
        const auto& sourceRow = editGrid[baseRows_ + added];
        // The row's own fields come across, its cells are rebuilt for the
        // combined column order.
        auto stripped = *std::get<std::shared_ptr<Object>>(sourceRow.data);
        for (auto& field : stripped.fields)
            if (field.name == "Items") field.value.data = Value::Array{};
        Value rowValue;
        rowValue.data = carrier.object(stripped);
        auto& items = array(row_object(rowValue), "Items");
        const auto& source = cells(sourceRow);
        items.reserve(totalRows);
        for (std::size_t column = 0; column < totalRows; ++column)
            items.push_back(carrier.value(source[sourceOf(column)]));
        grid.push_back(std::move(rowValue));
    }
    for (std::size_t slot = baseSlots_; slot < editIndex.size(); ++slot) {
        auto value = editIndex[slot];
        set_number(value, rowOf(number(value)));
        indexMap.push_back(std::move(value));
    }
    for (std::size_t id = baseNetwork_; id < editNetwork.size(); ++id) {
        auto value = editNetwork[id];
        set_number(value, map.remap(static_cast<std::uint32_t>(number(value))));
        network.push_back(std::move(value));
    }

    if (properties.size() != totalRows || grid.size() != totalRows)
        throw std::runtime_error("the combined material grid lost its shape");
    for (const auto& row : grid)
        if (cells(row).size() != totalRows) throw std::runtime_error("the combined material grid is not square");

    combined_ = std::move(next);
    slots_ += newSlots;
    rows_ = totalRows;
    carried_ += carrier.instances;
    return map;
}

Document Combiner::result() const {
    auto result = ebx::detail::clone_document(combined_);
    ebx::detail::sort_instances(result);
    return result;
}

std::size_t remap_physics(std::span<std::byte> payload, const SlotMap& map) {
    const auto header = physics_header(payload);
    std::size_t changed{};
    for (std::size_t part = 0; part < header.parts; ++part) {
        const auto at = header.partTable + partStride * part + partMaterial;
        const auto packed = read_u32(payload, at);
        const auto moved = map.remap(packed);
        if (moved == packed) continue;
        std::memcpy(payload.data() + at, &moved, 4);
        ++changed;
    }
    return changed;
}

} // namespace dingosdk::frostbite::material_grid

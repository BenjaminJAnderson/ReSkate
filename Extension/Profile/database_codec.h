#pragma once
#include "Engine/Core/Storage/save_database.h"
#include "local_profile.h"
#include <charconv>
#include <stdexcept>

namespace dingosdk::profile::database {
using storage::SaveSchema;
using storage::SaveTable;
SaveSchema profile_schema();
SaveSchema placement_schema();
Json profile_tables(Json document, const Json* previous_tables = nullptr, const Snapshot* previous = nullptr);
Json profile_document(const Json& tables);
Json placement_tables(Json document);
Json placement_document(const Json& tables);
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline Json empty_tables(SaveSchema schema) {
    Json tables = Json::object(); for (const auto& table : schema.tables) tables[table.name] = Json::object(); return tables;
}
inline void add(Json& tables, std::string_view table, Json row, unsigned keys = 1) {
    const auto key = storage::row_key(row, keys);
    require(!tables.at(table).contains(key), "Duplicate database row"); tables[table][key] = std::move(row);
}
inline const Json& singleton(const Json& tables, std::string_view name) {
    const auto& rows = tables.at(name); require(rows.size() == 1, "Missing database metadata"); return rows.begin().operator*();
}
inline Json parsed(const Json& text) { require(text.is_string(), "Expected JSON SQL column"); return Json::parse(text.string()); }
inline std::uint64_t uint_text(const Json& text) {
    require(text.is_string(), "Expected unsigned integer text");
    std::uint64_t result{}; const auto& s = text.string();
    const auto parsed_value = std::from_chars(s.data(), s.data()+s.size(), result);
    require(parsed_value.ec == std::errc{} && parsed_value.ptr == s.data()+s.size() && std::to_string(result) == s,
        "Invalid unsigned integer in save database"); return result;
}
inline Json* field(Json& root, std::initializer_list<std::string_view> path) {
    auto* value = &root;
    for (auto key : path) { if (!value->contains(key)) return nullptr; value = &value->at(key); }
    return value;
}
inline const Json* field(const Json& root, std::initializer_list<std::string_view> path) {
    const auto* value = &root;
    for (auto key : path) { if (!value->contains(key)) return nullptr; value = &value->at(key); }
    return value;
}
inline std::string escape(std::string_view key) {
    std::string out; for (char c : key) { if (c == '~') out += "~0"; else if (c == '/') out += "~1"; else out += c; } return out;
}
inline Json& pointer(Json& root, std::string_view path) {
    Json* value = &root;
    while (!path.empty()) {
        require(path.front() == '/', "Invalid setting scope"); path.remove_prefix(1);
        auto split = path.find('/'); auto part = path.substr(0, split); std::string key;
        for (std::size_t i=0; i<part.size(); ++i) {
            if (part[i] != '~') key += part[i];
            else { require(++i < part.size() && (part[i]=='0' || part[i]=='1'), "Invalid setting scope escape"); key += part[i]=='0'?'~':'/'; }
        }
        require(value->is_object() && value->contains(key), "Missing setting scope"); value = &value->at(key);
        if (split == std::string_view::npos) break; path.remove_prefix(split);
    }
    return *value;
}
}

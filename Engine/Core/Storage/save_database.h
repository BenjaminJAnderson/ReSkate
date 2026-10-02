#pragma once
#include "Engine/Core/Json/json.h"
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

namespace dingosdk::storage {
std::filesystem::path database_path(std::filesystem::path);
std::filesystem::path json_source_path(std::filesystem::path);

// Each row is an array matching the named SQL columns. The first key_columns
// fields form its primary key; table names and SQL come from static schemas.
struct SaveTable {
    std::string_view name, columns;
    unsigned key_columns;
    std::string_view create_sql;
};
struct SaveSchema {
    std::string_view name;
    std::span<const SaveTable> tables;
};
// A document maps table names to rows keyed by row_key(). Only changed rows
// enter each transaction; reads use the cached document.
std::string row_key(const Json& row, unsigned key_columns);
class SaveDatabase {
public:
    SaveDatabase(std::filesystem::path, SaveSchema schema);
    ~SaveDatabase();
    SaveDatabase(const SaveDatabase&) = delete;
    SaveDatabase& operator=(const SaveDatabase&) = delete;
    bool exists() const;
    const Json& document() const;
    void initialize(Json);
    void commit(Json);
    // Sparse table rows; null deletes a row. Atomic cache/disk publication.
    void patch(Json);
    // After domain validation, before the first write of an existing session.
    void backup();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

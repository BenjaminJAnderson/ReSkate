#include "mod_merge_internal.h"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace dingosdk::mods::detail {

CasStore::CasStore(fs::path baseRoot, fs::path output, const native_db::Node& layout)
    : GameArchives(std::move(baseRoot), layout), output_(std::move(output)) {}

fb::BundleFileInfo CasStore::write(std::uint32_t installChunk, std::uint16_t archive,
                                   std::span<const std::byte> encoded) {
    const auto path = output_ / L"Win32" / fs::path(directory(installChunk)) /
        fs::path(archive_file(archive));
    auto& offset = offsets_[path.wstring()];
    if (!offset) {
        fs::create_directories(path.parent_path());
        unshare(path);
        std::error_code error;
        const auto existing = fs::file_size(path, error);
        if (!error) offset = existing;
    }
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out || !out.write(reinterpret_cast<const char*>(encoded.data()),
                           static_cast<std::streamsize>(encoded.size())))
        throw std::runtime_error("Cannot append to " + path.string());
    fb::BundleFileInfo info{{true, installChunk, archive}, static_cast<std::uint32_t>(offset),
                            static_cast<std::uint32_t>(encoded.size())};
    offset += encoded.size();
    return info;
}

void CasStore::shift(fb::CasIdentifier& location, std::uint32_t& offset,
                     const ArchivePlacement* placement) const {
    if (!location.patch || !placement) return;
    const auto found = placement->at.find({directory(location.installChunk), location.archive});
    if (found == placement->at.end()) return;
    const auto moved = static_cast<std::uint64_t>(offset) + found->second.offset;
    if (moved > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Combined mod archives exceed 4 GB, which cas offsets cannot address");
    location.archive = found->second.archive;
    offset = static_cast<std::uint32_t>(moved);
}

} // namespace dingosdk::mods::detail

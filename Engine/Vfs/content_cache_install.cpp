#include "content_cache_install.h"
#include "content_cache_internal.h"
#include "https_download.h"
#include "Engine/Core/Platform/launcher_support.h"
#include <Windows.h>
#include <miniz.h>
#include <algorithm>
#include <atomic>
#include <format>
#include <fstream>
#include <vector>

namespace dingosdk::content_cache {
namespace {
constexpr std::uint64_t max_pack_bytes = 256ull * 1024 * 1024;
constexpr std::uint64_t max_entry_bytes = 64ull * 1024 * 1024;
constexpr std::uint32_t download_timeout_seconds = 15 * 60;

struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Temporary {
    std::filesystem::path path;
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

// Cache files are flat: 16-hex-digit bodies plus the index files. Entry names
// never become paths unless they are a single plain file name.
bool plain_name(std::string_view name) {
    return !name.empty() && name.size() <= 64 && name != "." && name != ".." &&
        std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-';
        });
}

bool unpack(const std::filesystem::path& archive, const std::filesystem::path& folder) {
    std::ifstream input(archive, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(std::filesystem::file_size(archive)));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return false;
    struct Zip {
        mz_zip_archive value{};
        ~Zip() { mz_zip_reader_end(&value); }
    } zip;
    if (!mz_zip_reader_init_mem(&zip.value, bytes.data(), bytes.size(), 0)) return false;
    const auto count = mz_zip_reader_get_num_files(&zip.value);
    if (!count || count > 4096) return false;
    std::filesystem::create_directories(folder);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&zip.value, index, &info) || info.m_is_encrypted) return false;
        if (info.m_is_directory) continue;
        if (!plain_name(info.m_filename) || info.m_uncomp_size > max_entry_bytes) return false;
        Handle output{CreateFileW((folder / info.m_filename).c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (output.value == INVALID_HANDLE_VALUE) return false;
        struct Sink { HANDLE file; std::uint64_t count{}, limit; } sink{output.value, 0, info.m_uncomp_size};
        const auto write = [](void* opaque, mz_uint64 offset, const void* buffer, size_t size) -> size_t {
            auto& target = *static_cast<Sink*>(opaque);
            if (offset != target.count || size > target.limit - target.count) return 0;
            DWORD written{};
            if (!WriteFile(target.file, buffer, static_cast<DWORD>(size), &written, nullptr) || written != size) return 0;
            target.count += size;
            return size;
        };
        if (!mz_zip_reader_extract_to_callback(&zip.value, index, write, &sink, 0) ||
            sink.count != info.m_uncomp_size || !FlushFileBuffers(output.value)) return false;
    }
    return true;
}
}

InstallResult ensure_installed(const std::filesystem::path& folder, const Pack& pack) noexcept {
    try {
        if (pack.url.empty() || pack.bytes < 16 || pack.bytes > max_pack_bytes || pack.sha256.size() != 64 ||
            pack.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
            return {InstallStatus::failed, 0, ERROR_INVALID_PARAMETER};
        if (installed(folder, pack)) return {InstallStatus::installed};
        std::filesystem::create_directories(folder.parent_path());
        Handle lock{CreateFileW((folder.wstring() + L".lock").c_str(), GENERIC_WRITE, 0,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr)};
        if (lock.value == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            return {error == ERROR_SHARING_VIOLATION ? InstallStatus::busy : InstallStatus::failed, 0, error};
        }
        if (installed(folder, pack)) return {InstallStatus::installed};

        static std::atomic<unsigned long> sequence{};
        const auto suffix = std::format(L".{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), sequence.fetch_add(1));
        Temporary archive{folder.wstring() + suffix + L".zip.tmp"};
        Temporary staging{folder.wstring() + suffix + L".staging"};
        const auto fetched = https::get(pack.url, archive.path, max_pack_bytes, download_timeout_seconds, L"ReSkate-Cache/1");
        if (!fetched.ok) return {InstallStatus::failed, fetched.http_status, fetched.error};
        std::error_code error;
        if (std::filesystem::file_size(archive.path, error) != pack.bytes || error ||
            launcher::sha256_file(archive.path) != pack.sha256 || !unpack(archive.path, staging.path))
            return {InstallStatus::failed, fetched.http_status, ERROR_INVALID_DATA};
        {
            std::ofstream marker(staging.path / detail::marker_name, std::ios::binary | std::ios::trunc);
            if (!(marker << pack.sha256) || !marker.flush())
                return {InstallStatus::failed, fetched.http_status, ERROR_WRITE_FAULT};
        }
        std::filesystem::remove_all(folder, error);
        if (error || !MoveFileExW(staging.path.c_str(), folder.c_str(), MOVEFILE_WRITE_THROUGH))
            return {InstallStatus::failed, fetched.http_status, error ? static_cast<unsigned long>(error.value()) : GetLastError()};
        return {InstallStatus::downloaded, fetched.http_status};
    } catch (...) { return {InstallStatus::failed, 0, ERROR_READ_FAULT}; }
}

InstallResult ensure_installed() noexcept {
    try { return ensure_installed(directory(), supported_pack()); }
    catch (...) { return {InstallStatus::failed, 0, ERROR_PATH_NOT_FOUND}; }
}
}

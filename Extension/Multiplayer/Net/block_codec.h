#pragma once
#include "protocol.h"
#include <lz4.h>
#include <zstd.h>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace dingosdk::multiplayer {
enum class BlockCodec { lz4, zstd };
struct CompressedBlock { std::vector<std::uint8_t> bytes; BlockCodec codec = BlockCodec::lz4; };
inline CompressedBlock compress_block(std::span<const std::uint8_t> raw) {
    if (raw.empty() || raw.size() > max_packet + 1024) throw std::invalid_argument("Compression input exceeds bound");
    const auto bound = static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(raw.size())));
    CompressedBlock result;
    result.bytes.resize(bound);
    const int lz4_size = LZ4_compress_default(reinterpret_cast<const char *>(raw.data()),
        reinterpret_cast<char *>(result.bytes.data()), static_cast<int>(raw.size()), static_cast<int>(bound));
    if (lz4_size <= 0) throw std::runtime_error("LZ4 compression failed");
    result.bytes.resize(static_cast<std::size_t>(lz4_size));
    // Multiplayer compression runs on the client thread. LZ4 is the hot-path
    // encoder; retaining Zstd decoding keeps older/in-flight packets compatible
    // without evaluating both encoders for every candidate block.
    return result;
}
inline bool decompress_block(std::span<const std::uint8_t> bytes, std::span<std::uint8_t> raw,
                              BlockCodec codec) {
    if (raw.empty() || raw.size() > max_packet + 1024 || bytes.empty() || bytes.size() > max_packet) return false;
    if (codec == BlockCodec::lz4)
        return LZ4_decompress_safe(reinterpret_cast<const char *>(bytes.data()), reinterpret_cast<char *>(raw.data()),
            static_cast<int>(bytes.size()), static_cast<int>(raw.size())) == static_cast<int>(raw.size());
    // One self-contained frame, known bounded output, no dictionary/trailing frames.
    if (ZSTD_getFrameContentSize(bytes.data(), bytes.size()) != raw.size() ||
        ZSTD_findFrameCompressedSize(bytes.data(), bytes.size()) != bytes.size()) return false;
    thread_local std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    if (!context) return false;
    const auto n = ZSTD_decompressDCtx(context.get(), raw.data(), raw.size(), bytes.data(), bytes.size());
    return !ZSTD_isError(n) && n == raw.size();
}
} // namespace dingosdk::multiplayer

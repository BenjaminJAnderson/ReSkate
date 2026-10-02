// Reads the build-kit thumbnails from an installed game. Only runs when given
// the game folder; a second argument compares against an old preview pack
// (the RSPRV001 file the launcher used to download).
//   dingosdk_item_thumbnails_tests <Skate folder> [ReSkate-Object-Previews.bin]
#include "Engine/Vfs/item_thumbnails.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

int main(int argc, char** argv) {
    using namespace dingosdk;
    if (argc < 2 || !*argv[1]) {
        std::cout << "No game folder given; skipped.\n";
        return 0;
    }
    const auto started = std::chrono::steady_clock::now();
    vfs::ThumbnailRead read;
    try {
        read = vfs::read_build_kit_thumbnails(argv[1], 128);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    std::cout << read.thumbnails.size() << " thumbnails, " << read.failed << " failed, " << elapsed.count() << " ms\n";
    bool ok = read.thumbnails.size() >= 400 && read.failed == 0;
    if (argc > 2) {
        std::ifstream input(argv[2], std::ios::binary);
        const std::vector<unsigned char> pack{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::map<std::string, const unsigned char*> old;
        std::uint32_t count{};
        if (pack.size() > 16) std::memcpy(&count, pack.data() + 8, 4);
        for (std::size_t at = 16, i = 0; i < count && at + 2 < pack.size(); ++i) {
            std::uint16_t size{}; std::memcpy(&size, pack.data() + at, 2); at += 2;
            old.emplace(std::string(reinterpret_cast<const char*>(pack.data() + at), size), pack.data() + at + size);
            at += size + 128 * 128 * 4;
        }
        double total{}; std::size_t compared{}, missing{};
        for (const auto& thumbnail : read.thumbnails) {
            const auto found = old.find(thumbnail.item);
            if (found == old.end()) { ++missing; continue; }
            double sum{};
            for (std::size_t i = 0; i < thumbnail.image.rgba.size(); ++i)
                sum += std::abs(int(thumbnail.image.rgba[i]) - int(found->second[i]));
            total += sum / thumbnail.image.rgba.size();
            ++compared;
        }
        std::cout << "old pack: " << old.size() << " entries; compared " << compared << ", new-only " << missing
                  << ", mean abs difference " << (compared ? total / compared : 0.0) << " / 255\n";
    }
    return ok ? 0 : 1;
}

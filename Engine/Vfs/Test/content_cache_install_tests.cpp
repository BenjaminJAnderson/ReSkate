// End-to-end install of the supported content cache pack. It downloads the real
// release, so it only runs when given an empty scratch folder:
//   dingosdk_content_cache_install_tests <folder>
#include "Engine/Vfs/content_cache_install.h"
#include "Engine/Vfs/content_catalogs.h"
#include <iostream>

int main(int argc, char** argv) {
    using namespace dingosdk::content_cache;
    if (argc < 2) {
        std::cout << "No scratch folder given; skipped (this test downloads the pack).\n";
        return 0;
    }
    const std::filesystem::path folder = std::filesystem::absolute(argv[1]) / "24855063";
    const auto first = ensure_installed(folder, supported_pack());
    if (first.status != InstallStatus::downloaded && first.status != InstallStatus::installed) {
        std::cerr << "FAIL: install (HTTP " << first.http_status << ", error " << first.error << ")\n";
        return 1;
    }
    const auto again = ensure_installed(folder, supported_pack());
    const auto catalogs = read_catalogs(folder);
    const bool ok = installed(folder, supported_pack()) && again.status == InstallStatus::installed &&
        catalogs.entitlements.size() == 381 && catalogs.items.size() >= 3000 && !catalogs.challenges.items().empty();
    std::cout << (first.status == InstallStatus::downloaded ? "Downloaded" : "Already installed") << "; "
              << catalogs.items.size() << " items, " << catalogs.challenges.size() << " challenges.\n";
    if (!ok) std::cerr << "FAIL: installed pack did not verify\n";
    return ok ? 0 : 1;
}

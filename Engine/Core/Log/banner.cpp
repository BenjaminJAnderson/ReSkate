#include "logging_internal.h"
#include <atomic>

namespace dingosdk::logging {
void startup_banner() noexcept {
    detail::PreserveError preserve;
    static std::atomic_flag shown = ATOMIC_FLAG_INIT;
    if (shown.test_and_set()) return;
    write(Level::info, Channel::runtime, R"banner(  ____       ____  _         _
 |  _ \ ___ / ___|| | ____ _| |_ ___
 | |_) / _ \\___ \| |/ / _` | __/ _ \
 |  _ <  __/ ___) |   < (_| | ||  __/
 |_| \_\___||____/|_|\_\__,_|\__\___|
                 o================o)banner");
    log(Level::info, Channel::runtime, "Skate SDK | development build | Windows x64 | MSVC {} | {} {}",
        _MSC_VER, __DATE__, __TIME__);
    try {
        const auto config = status();
        write(Level::info, Channel::runtime, L"Log: " + (config.directory / L"ReSkate.log").wstring());
        log(Level::info, Channel::runtime, "Log level: {} | Windows console: {}",
            name(config.level), config.external_console ? "on" : "off");
        write(Level::info, Channel::runtime,
            "Sources: C=client, S=server, U=UI, E=engine, F=filesystem, M=materials/rendering, A=audio; SDK=tools/runtime.");
    } catch (...) {}
}
}

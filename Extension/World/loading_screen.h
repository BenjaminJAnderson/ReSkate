#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>

namespace dingosdk::loading_screen {
bool start(std::uintptr_t base) noexcept;
// Supply the next local destination before the native unload event opens UI.
void prepare(std::uintptr_t client, std::string_view root, std::string_view detached) noexcept;
void cancel() noexcept;
// A loading screen a mod enabled since launch adds for its level. The game read
// its loading-screen configuration at launch, so these are chosen in memory.
struct LiveScreen {
    std::string level;
    std::string bundle;
    std::array<std::byte, 16> widget{};
};
void set_live_screens(std::vector<LiveScreen> screens);
}

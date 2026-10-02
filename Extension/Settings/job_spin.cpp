#include "job_spin.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/job_system.h"
#include <Windows.h>
#include <cstdint>

namespace dingosdk::job_spin {
namespace {
struct Contract { bool ok{}; double ticks_per_microsecond{}; std::int64_t stock_ticks{}; std::uintptr_t ticks{}; };
// Read from the job system's own values the first time (before ReSkate changes them): the
// configured milliseconds and the ticks the engine computed from them.
const Contract& contract() noexcept {
    static Contract value;
    if (value.ok) return value;
    value = [] {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        float milliseconds{};
        std::int64_t ticks{};
        int threads{};
        Contract result;
        if (memory::peek(base + addr::job_system::thread_count, threads) && threads > 0 && threads <= 64 &&
            memory::peek(base + addr::job_system::spin_milliseconds, milliseconds) &&
            memory::peek(base + addr::job_system::spin_ticks, ticks) &&
            milliseconds > 0.001f && milliseconds < 100.0f && ticks > 0)
            result = {true, static_cast<double>(ticks) / (milliseconds * 1000.0), ticks, base + addr::job_system::spin_ticks};
        return result;
    }();
    return value;
}
}

std::optional<double> microseconds() noexcept {
    const auto& c = contract();
    std::int64_t ticks{};
    if (!c.ok || !memory::peek(c.ticks, ticks)) return {};
    return static_cast<double>(ticks) / c.ticks_per_microsecond;
}

double stock_microseconds() noexcept {
    const auto& c = contract();
    return c.ok ? static_cast<double>(c.stock_ticks) / c.ticks_per_microsecond : 0.0;
}

std::optional<double> ticks_per_microsecond() noexcept {
    const auto& c = contract();
    if (!c.ok) return {};
    return c.ticks_per_microsecond;
}

bool set_microseconds(double value) noexcept {
    const auto& c = contract();
    if (!c.ok || !(value >= 0 && value <= 5000)) return false;
    // Both job loops read it on every pass: the new value applies at once.
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(c.ticks), static_cast<LONG64>(value * c.ticks_per_microsecond));
    return true;
}

bool apply_default() noexcept {
    static bool done = false;
    static int attempts = 0;
    if (done) return true;
    // The job system starts long before the first client tick; give it a few tries anyway.
    if (!contract().ok) {
        if (++attempts < 600) return false;
        done = true;
        logging::write(logging::Level::warning, logging::Channel::runtime,
            "Job threads: the spin setting is not where this build's address table says; left at stock.");
        return true;
    }
    done = set_microseconds(default_microseconds);
    if (done)
        logging::log(logging::Level::info, logging::Channel::runtime,
            "Job threads: idle polling {:.0f} us before sleeping (stock {:.0f} us; `perf jobspin` changes it).",
            default_microseconds, stock_microseconds());
    return done;
}
}

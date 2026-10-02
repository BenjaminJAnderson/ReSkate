#pragma once
#include "native_voice_pcm.h"
#include <memory>

namespace dingosdk::multiplayer {
class NativeVoicePlayback {
public:
    NativeVoicePlayback();
    ~NativeVoicePlayback();
    bool available() const noexcept;
    void reset(std::uint64_t generation);
    void remove(std::uint64_t id, std::uint64_t generation);
    void position(std::uint64_t id, std::uint64_t generation, const std::array<float, 3> &, float gain);
    bool submit(std::uint64_t id, std::uint64_t generation, std::span<const std::uint8_t>);
    void process(std::uintptr_t base) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

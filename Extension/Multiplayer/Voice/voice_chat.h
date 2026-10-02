#pragma once
#include "Extension/Multiplayer/Net/protocol.h"
#include "Engine/Game/Multiplayer/voice_settings.h"
#include <memory>

namespace dingosdk::multiplayer {
struct VoicePeer {
    std::uint64_t id{}, epoch{};
    Transform root;
};
struct VoiceScene {
    bool active{};
    VoicePolicy policy;
    std::uint64_t session{}, world{};
    Transform listener;
    std::array<float, 3> ear_position{}, ear_right{};
    std::vector<VoicePeer> peers;
};
class VoiceChat {
public:
    VoiceChat();
    ~VoiceChat();
    VoiceChat(const VoiceChat &) = delete;
    VoiceChat &operator=(const VoiceChat &) = delete;
    void configure(VoiceSettings);
    VoiceModel model() const;
    void mute(std::uint64_t id, bool muted);
    void volume(std::uint64_t id, float volume);
    void update(VoiceScene);
    void reset();
    void process_native(std::uintptr_t base) noexcept;
    void receive(const Packet &);
    std::vector<VoiceData> take_capture();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

#include "voice_chat.h"
#include "native_voice.h"
#include "Engine/Core/Platform/launcher_support.h"
#include "Engine/Game/Input/voice_input.h"
#include "Extension/UI/Overlay/overlay.h"
#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace dingosdk::multiplayer {
namespace {
using Clock = std::chrono::steady_clock;
constexpr UINT32 sample_rate = 48000;
constexpr std::size_t max_pcm_bytes = sample_rate;
template<class T> T symbol(HMODULE module, const char *name) {
    const auto address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error(std::string("Steam voice export unavailable: ") + name);
#pragma warning(push)
#pragma warning(disable:4191)
    return reinterpret_cast<T>(address);
#pragma warning(pop)
}
struct SteamVoice {
    void *user{};
    void (*start)(void *){};
    void (*stop)(void *){};
    int (*get)(void *, bool, void *, std::uint32_t, std::uint32_t *, bool, void *,
               std::uint32_t, std::uint32_t *, std::uint32_t){};
    int (*decode)(void *, const void *, std::uint32_t, void *, std::uint32_t, std::uint32_t *, std::uint32_t){};
    void open() {
        if (user) return;
        if (launcher::offline_mode()) throw std::runtime_error("Multiplayer is unavailable in offline mode. Start Steam and relaunch ReSkate.");
        const auto module = GetModuleHandleW(L"steam_api64.dll");
        if (!module) throw std::runtime_error("Start ReSkate through Steam to use voice chat.");
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) throw std::runtime_error("Cannot verify Steam voice library.");
        path.resize(length);
        launcher::validate_steam_api_file(std::filesystem::path(path));
        if (!symbol<int (*)()>(module, "SteamAPI_GetHSteamUser")())
            throw std::runtime_error("Steam is not initialized for voice chat.");
        start = symbol<decltype(start)>(module, "SteamAPI_ISteamUser_StartVoiceRecording");
        stop = symbol<decltype(stop)>(module, "SteamAPI_ISteamUser_StopVoiceRecording");
        get = symbol<decltype(get)>(module, "SteamAPI_ISteamUser_GetVoice");
        decode = symbol<decltype(decode)>(module, "SteamAPI_ISteamUser_DecompressVoice");
        user = symbol<void *(*)()>(module, "SteamAPI_SteamUser_v023")();
        if (!user) throw std::runtime_error("Steam Voice is unavailable.");
    }
};
struct PcmBuffer {
    std::vector<std::uint8_t> bytes;
    std::atomic<bool> done{};
};
struct Speaker final : IXAudio2VoiceCallback {
    IXAudio2SourceVoice *voice{};
    std::uint64_t epoch{};
    float radius{}, gain = 1.f;
    Clock::time_point received{};
    std::deque<std::unique_ptr<PcmBuffer>> buffers;
    std::atomic<bool> failed{};
    NativeVoicePlayback *native{};
    std::uint64_t id{}, generation{};
    ~Speaker() {
        if (voice) voice->DestroyVoice();
        if (native) native->remove(id, generation);
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void *) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void *context) override {
        static_cast<PcmBuffer *>(context)->done.store(true, std::memory_order_release);
    }
    void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
    void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override { failed.store(true); }
    void collect() {
        std::erase_if(buffers, [](const auto &buffer) { return buffer->done.load(std::memory_order_acquire); });
    }
    bool submit(std::vector<std::uint8_t> pcm) {
        collect();
        std::size_t queued{};
        for (const auto &buffer : buffers) queued += buffer->bytes.size();
        if (buffers.size() >= 16 || queued + pcm.size() > max_pcm_bytes) return false;
        auto buffer = std::make_unique<PcmBuffer>();
        buffer->bytes = std::move(pcm);
        XAUDIO2_BUFFER submission{};
        submission.AudioBytes = static_cast<UINT32>(buffer->bytes.size());
        submission.pAudioData = buffer->bytes.data();
        submission.pContext = buffer.get();
        if (FAILED(voice->SubmitSourceBuffer(&submission))) return false;
        buffers.push_back(std::move(buffer));
        return true;
    }
};
struct Playback {
    NativeVoicePlayback &native;
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice *master{};
    std::map<std::uint64_t, std::unique_ptr<Speaker>> speakers;
    explicit Playback(NativeVoicePlayback &value) : native(value) {}
    ~Playback() { close(); }
    void close() {
        speakers.clear();
        if (master) { master->DestroyVoice(); master = nullptr; }
        engine.Reset();
    }
    void open() {
        if (engine) return;
        if (FAILED(XAudio2Create(&engine))) throw std::runtime_error("Cannot start voice playback.");
        if (FAILED(engine->CreateMasteringVoice(&master, 2, sample_rate, 0, nullptr, nullptr, AudioCategory_Communications))) {
            engine.Reset();
            throw std::runtime_error("No voice playback device is available.");
        }
    }
    Speaker &speaker(std::uint64_t id, std::uint64_t epoch, std::uint64_t generation, bool use_native) {
        auto &entry = speakers[id];
        if (entry && (entry->epoch != epoch || entry->failed.load() || (entry->native != nullptr) != use_native)) entry.reset();
        if (!entry) {
            auto next = std::make_unique<Speaker>();
            next->id = id; next->generation = generation;
            if (use_native) next->native = &native;
            else {
                open();
                WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, sample_rate, sample_rate * 2, 2, 16, 0};
                if (FAILED(engine->CreateSourceVoice(&next->voice, &format, 0, 1.f, next.get())))
                    throw std::runtime_error("Cannot create a player's voice stream.");
                if (FAILED(next->voice->Start())) throw std::runtime_error("Cannot start a player's voice stream.");
            }
            next->epoch = epoch;
            entry = std::move(next);
        }
        return *entry;
    }
};
bool focused() {
    DWORD process{};
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId() && overlay::keyboard_shortcuts_allowed();
}
// A global speaker (0) is heard everywhere at full volume; otherwise the
// listener's own hearing distance decides, and with proximity off they hear
// everyone at full volume.
float radius_for(float remote, const VoiceSettings &settings) {
    return remote > 0.f && settings.proximity ? settings.distance : 0.f;
}
std::array<float, 2> stereo(const VoiceScene &scene, const VoicePeer &peer) {
    return voice_stereo(scene.ear_position, scene.ear_right, peer.root.position);
}
}
struct VoiceChat::Impl {
    NativeVoicePlayback native;
    mutable std::mutex mutex;
    std::condition_variable wake;
    VoiceModel state;
    VoiceScene scene;
    Clock::time_point updated{};
    std::uint64_t generation{};
    std::set<std::uint64_t> muted;
    std::map<std::uint64_t, float> volumes;
    struct Incoming { std::uint64_t id, epoch, generation; VoiceData voice; Clock::time_point time; };
    struct Outgoing { std::uint64_t generation; VoiceData voice; Clock::time_point time; };
    std::deque<Incoming> incoming;
    std::deque<Outgoing> outgoing;
    std::jthread worker;
    ~Impl() {
        worker.request_stop(); wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    void run(std::stop_token stop_token) noexcept {
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        SteamVoice steam;
        Playback playback(native);
        VoicePushToTalk push_to_talk;
        bool recording{}, draining{}, forward_tail{};
        std::uint64_t current_generation{};
        Clock::time_point retry{};
        std::array<std::uint8_t, 65536> compressed{};
        while (!stop_token.stop_requested()) {
            VoiceScene current;
            VoiceSettings settings;
            std::set<std::uint64_t> blocked;
            std::map<std::uint64_t, float> gains;
            std::deque<Incoming> packets;
            Clock::time_point refreshed;
            std::uint64_t revision{};
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(20));
                current = scene; settings = state.settings; refreshed = updated;
                blocked = muted; gains = volumes; revision = generation;
                packets.swap(incoming);
            }
            const auto now = Clock::now();
            const bool active = settings.enabled && current.active && current.policy.allowed &&
                now - refreshed < std::chrono::milliseconds(300);
            const bool foreground = focused();
            const bool can_transmit = active && foreground && !current.peers.empty();
            if (revision != current_generation) push_to_talk = {};
            ControllerInput controller;
            if (can_transmit && !settings.open_mic && settings.controller_combo)
                DingoSDKOverlayReadControllerInput(&controller);
            const bool held = push_to_talk.update(settings,
                can_transmit && settings.push_to_talk && (GetAsyncKeyState(settings.push_to_talk) & 0x8000) != 0,
                controller, !can_transmit || settings.open_mic);
            const bool wanted = can_transmit && (settings.open_mic || held) && settings.microphone > 0.f;
            const auto player_gain = [&](std::uint64_t id) {
                const auto found = gains.find(id);
                return found == gains.end() ? 1.f : found->second;
            };
            bool ready{};
            std::string detail = settings.enabled ? "Join a ReSkate session and load a level to use voice." : "Voice chat is off.";
            if (!current.policy.allowed) detail = "Voice chat is disabled by the host.";
            std::vector<VoicePlayer> players;
            try {
                if (revision != current_generation) {
                    playback.speakers.clear();
                    if (recording) { steam.stop(steam.user); recording = false; draining = true; }
                    forward_tail = false;
                    current_generation = revision;
                }
                if (active && now >= retry) {
                    steam.open();
                    if (!native.available()) playback.open();
                }
                ready = active && steam.user && (native.available() || playback.engine);
                if (wanted && ready && !recording && !draining) { steam.start(steam.user); recording = true; }
                if ((!wanted || !ready) && recording) {
                    steam.stop(steam.user); recording = false; draining = true;
                    forward_tail = ready && active && foreground && settings.microphone > 0.f;
                }
                if (!active || !foreground) forward_tail = false;
                if (recording || draining) {
                    std::uint32_t count{};
                    const auto result = steam.get(steam.user, true, compressed.data(), static_cast<std::uint32_t>(compressed.size()),
                                                   &count, false, nullptr, 0, nullptr, 0);
                    if (result == 2) { draining = false; recording = false; }
                    else if (result != 0 && result != 3) throw std::runtime_error("Steam microphone error " + std::to_string(result) + ". Check Steam's voice input device.");
                    if (result == 0 && count && count <= max_voice_bytes && (recording || forward_tail) &&
                        active && foreground && !current.peers.empty()) {
                        VoiceData data;
                        data.distance = settings.proximity ? settings.distance : 0.f;
                        data.gain = settings.microphone;
                        data.policy_revision = current.policy.revision;
                        data.bytes.assign(compressed.begin(), compressed.begin() + count);
                        std::lock_guard lock(mutex);
                        if (generation == revision && state.settings.enabled) {
                            while (outgoing.size() >= 8) outgoing.pop_front();
                            outgoing.push_back({revision, std::move(data), now});
                        }
                    }
                }
                if (!ready) playback.speakers.clear();
                if (ready) {
                    for (auto &packet : packets) {
                        if (packet.generation != revision || !current.policy.accepts(packet.voice) ||
                            now - packet.time > std::chrono::milliseconds(250) || blocked.contains(packet.id)) continue;
                        const auto peer = std::find_if(current.peers.begin(), current.peers.end(), [&](const auto &p) { return p.id == packet.id && p.epoch == packet.epoch; });
                        if (peer == current.peers.end() || voice_gain(current.listener.position, peer->root.position,
                            radius_for(packet.voice.distance, settings)) <= 0.f) continue;
                        const bool use_native = native.available() && radius_for(packet.voice.distance, settings) > 0.f;
                        std::vector<std::uint8_t> pcm(max_pcm_bytes);
                        std::uint32_t size{};
                        if (steam.decode(steam.user, packet.voice.bytes.data(), static_cast<std::uint32_t>(packet.voice.bytes.size()),
                                         pcm.data(), static_cast<std::uint32_t>(pcm.size()), &size,
                                         use_native ? native_voice_rate : sample_rate) != 0 ||
                            !size || size > pcm.size() || size % 2) continue;
                        pcm.resize(size);
                        auto &speaker = playback.speaker(packet.id, packet.epoch, revision, use_native);
                        speaker.radius = packet.voice.distance;
                        speaker.gain = packet.voice.gain;
                        speaker.received = now;
                        const auto gain = settings.volume * player_gain(packet.id) * speaker.gain * voice_gain(current.listener.position, peer->root.position,
                                                                       radius_for(speaker.radius, settings));
                        if (speaker.native) {
                            auto position = peer->root.position;
                            position[1] += 1.5f;
                            native.position(packet.id, revision, position, gain);
                            native.submit(packet.id, revision, pcm);
                        } else {
                            const auto matrix = stereo(current, *peer);
                            speaker.voice->SetVolume(gain);
                            speaker.voice->SetOutputMatrix(playback.master, 1, 2, matrix.data());
                            speaker.submit(std::move(pcm));
                        }
                    }
                    std::erase_if(playback.speakers, [&](const auto &item) {
                        const auto peer = std::find_if(current.peers.begin(), current.peers.end(), [&](const auto &p) { return p.id == item.first && p.epoch == item.second->epoch; });
                        return peer == current.peers.end() || blocked.contains(item.first) ||
                            now - item.second->received > std::chrono::seconds(1) ||
                            voice_gain(current.listener.position, peer->root.position, radius_for(item.second->radius, settings)) <= 0.f ||
                            (item.second->native && !native.available());
                    });
                    for (const auto &peer : current.peers) {
                        const auto found = playback.speakers.find(peer.id);
                        bool speaking{};
                        if (found != playback.speakers.end()) {
                            auto &speaker = *found->second;
                            speaker.collect();
                            const auto gain = settings.volume * player_gain(peer.id) * speaker.gain *
                                voice_gain(current.listener.position, peer.root.position, radius_for(speaker.radius, settings));
                            if (speaker.native) {
                                auto position = peer.root.position;
                                position[1] += 1.5f;
                                native.position(peer.id, revision, position, gain);
                                speaking = now - speaker.received < std::chrono::milliseconds(250);
                            } else {
                                speaker.voice->SetVolume(gain);
                                const auto matrix = stereo(current, peer);
                                speaker.voice->SetOutputMatrix(playback.master, 1, 2, matrix.data());
                                speaking = !speaker.buffers.empty();
                            }
                        }
                        players.push_back({peer.id, blocked.contains(peer.id), speaking, player_gain(peer.id)});
                    }
                    detail = recording ? "Transmitting microphone audio." : !foreground ? "Microphone paused while a menu is open or the game is unfocused." :
                        current.peers.empty() ? "Waiting for another player in this level." :
                        settings.open_mic ? "Open microphone ready. Uses Steam's voice input device." : "Push-to-talk ready. Uses Steam's voice input device.";
                    detail += native.available() ? " Proximity playback: Frostbite." : " Playback: XAudio2 fallback.";
                }
            } catch (const std::exception &error) {
                if (recording) { steam.stop(steam.user); recording = false; draining = true; }
                playback.close(); ready = false; detail = error.what();
                retry = now + std::chrono::seconds(2);
            } catch (...) {
                if (recording) { steam.stop(steam.user); recording = false; draining = true; }
                playback.close(); ready = false; detail = "Voice chat failed. Disable and enable it to retry.";
                retry = now + std::chrono::seconds(2);
            }
            {
                std::lock_guard lock(mutex);
                if (generation == revision) {
                    state.ready = ready; state.transmitting = recording;
                    state.status = std::move(detail); state.players = std::move(players);
                }
            }
        }
        if (recording) steam.stop(steam.user);
        playback.close();
        if (SUCCEEDED(com)) CoUninitialize();
    }
};
VoiceChat::VoiceChat() : impl_(std::make_unique<Impl>()) {}
VoiceChat::~VoiceChat() = default;
void VoiceChat::configure(VoiceSettings settings) {
    if (!settings.valid()) return;
    std::lock_guard lock(impl_->mutex);
    const auto &old = impl_->state.settings;
    if (old.enabled != settings.enabled || old.open_mic != settings.open_mic ||
        old.push_to_talk != settings.push_to_talk || old.controller_combo != settings.controller_combo ||
        old.microphone != settings.microphone) {
        ++impl_->generation; impl_->incoming.clear(); impl_->outgoing.clear();
        impl_->native.reset(impl_->generation);
        impl_->state.transmitting = false;
    }
    impl_->state.settings = settings;
    if (settings.enabled && !impl_->worker.joinable())
        impl_->worker = std::jthread([p = impl_.get()](std::stop_token stop) { p->run(stop); });
    impl_->wake.notify_all();
}
VoiceModel VoiceChat::model() const {
    std::lock_guard lock(impl_->mutex);
    auto result = impl_->state;
    for (const auto &[id, gain] : impl_->volumes) {
        const auto found = std::find_if(result.players.begin(), result.players.end(), [id](const auto &p) { return p.id == id; });
        if (found == result.players.end()) result.players.push_back({id, false, false, gain});
        else found->volume = gain;
    }
    for (const auto id : impl_->muted) {
        const auto found = std::find_if(result.players.begin(), result.players.end(), [id](const auto &p) { return p.id == id; });
        if (found == result.players.end()) result.players.push_back({id, true, false});
        else { found->muted = true; found->speaking = false; }
    }
    return result;
}
void VoiceChat::mute(std::uint64_t id, bool muted) {
    std::lock_guard lock(impl_->mutex);
    if (muted) impl_->muted.insert(id); else impl_->muted.erase(id);
    impl_->wake.notify_all();
}
void VoiceChat::volume(std::uint64_t id, float volume) {
    if (!valid_voice_volume(volume)) return;
    std::lock_guard lock(impl_->mutex);
    impl_->volumes[id] = volume;
    impl_->wake.notify_all();
}
void VoiceChat::update(VoiceScene scene) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->scene.session != scene.session || impl_->scene.world != scene.world || impl_->scene.active != scene.active ||
        impl_->scene.policy != scene.policy) {
        ++impl_->generation; impl_->incoming.clear(); impl_->outgoing.clear();
        impl_->native.reset(impl_->generation);
        impl_->state.transmitting = false;
    }
    impl_->state.allowed = scene.policy.allowed;
    impl_->scene = std::move(scene);
    impl_->updated = Clock::now();
}
void VoiceChat::reset() { update({}); impl_->wake.notify_all(); }
void VoiceChat::process_native(std::uintptr_t base) noexcept { impl_->native.process(base); }
void VoiceChat::receive(const Packet &packet) {
    if (packet.kind != PacketKind::voice || !valid_voice(packet.voice)) return;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->state.settings.enabled || !impl_->scene.active || !impl_->scene.policy.accepts(packet.voice) || impl_->muted.contains(packet.source) ||
        packet.session != impl_->scene.session || packet.world != impl_->scene.world) return;
    const auto peer = std::find_if(impl_->scene.peers.begin(), impl_->scene.peers.end(), [&](const auto &p) {
        return p.id == packet.source && p.epoch == packet.epoch;
    });
    if (peer == impl_->scene.peers.end() || impl_->incoming.size() >= 128) return;
    const auto count = std::count_if(impl_->incoming.begin(), impl_->incoming.end(), [&](const auto &p) { return p.id == packet.source; });
    if (count >= 8) return;
    impl_->incoming.push_back({packet.source, packet.epoch, impl_->generation, packet.voice, Clock::now()});
}
std::vector<VoiceData> VoiceChat::take_capture() {
    std::lock_guard lock(impl_->mutex);
    std::vector<VoiceData> result;
    const auto now = Clock::now();
    for (auto &packet : impl_->outgoing)
        if (impl_->state.settings.enabled && impl_->scene.active && impl_->scene.policy.accepts(packet.voice) && packet.generation == impl_->generation &&
            now - packet.time <= std::chrono::milliseconds(250)) result.push_back(std::move(packet.voice));
    impl_->outgoing.clear();
    return result;
}
}

#pragma once
#include "native_pose_layout.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
namespace dingosdk::multiplayer {
struct NativeAudioBinding {
    std::uintptr_t component{}, manager{};
    std::uint16_t handle{};
    std::uintptr_t source{}; // the local actor's sound component, whose handle must differ
};
// The component owns this handle and can replace it when its description changes.
// Resolve it each update; never retain a copied handle or add a second voice.
template <class Read>
NativeAudioBinding read_native_audio_binding(Read &&read, std::uintptr_t base, std::uintptr_t local,
                                             std::uintptr_t remote, std::uintptr_t context) {
    using namespace native_pose_detail;
    const auto ptr = [&](std::uintptr_t p, std::int64_t off = 0) {
        return value<std::uintptr_t>(read, add(p, off), "sound ownership");
    };
    require(local != remote && ptr(local) == base + addr::engine::skater_entity_vtable &&
                ptr(remote) == base + addr::engine::skater_entity_vtable &&
                ptr(local, 0xf8) && !ptr(remote, 0xf8) && ptr(local, 0x20) == context &&
                ptr(remote, 0x20) == context,
            "Remote sound actor ownership differs.");
    const auto source = read_native_component(read, local, base + addr::engine::skater_audio_component_vtable);
    NativeAudioBinding out;
    out.source = source;
    out.component = read_native_component(read, remote, base + addr::engine::skater_audio_component_vtable);
    require(source != out.component && ptr(source, 8) == ptr(out.component, 8),
            "Remote sound component assets differ.");
    out.manager = ptr(base, addr::engine::audio_manager);
    require(ptr(out.manager) == base + addr::engine::audio_manager_vtable &&
                ptr(ptr(out.manager), 0x20) == base + addr::engine::audio_manager_update,
            "Native skater sound manager differs.");
    out.handle = value<std::uint16_t>(read, add(out.component, 0x74), "sound handle");
    require(!out.handle ||
                (out.handle >= 2 &&
                 out.handle != value<std::uint16_t>(read, add(source, 0x74), "local sound handle") &&
                 value<std::uint8_t>(read, add(out.component, 0x76), "sound handle owner") == 0),
            "Remote sound handle ownership differs.");
    return out;
}
} // namespace dingosdk::multiplayer

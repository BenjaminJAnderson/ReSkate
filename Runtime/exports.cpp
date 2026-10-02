#include "Extension/Assets/native_patch_support.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/20260929/route_lookahead.h"
#include <array>

// Static PE-readable capability data. Studio checks these without loading the
// DLL; this advertises implementation support, not a claim about a live hook.
extern "C" {
__declspec(dllexport) extern constinit const dingosdk::game::build::v20260929::route_lookahead::RouteLookaheadContract
    ReSkateNpcRouteCycleGuardV1 = dingosdk::game::build::v20260929::route_lookahead::route_lookahead_contract;
__declspec(dllexport) extern constinit const dingosdk::game::build::v20260929::route_lookahead::RouteLookaheadContract
    ReSkateNpcRouteEndpointGuardV1 = dingosdk::game::build::v20260929::route_lookahead::route_endpoint_contract;
__declspec(dllexport) extern constinit const dingosdk::game::build::v20260929::route_lookahead::RouteLookaheadContract
    ReSkateNpcRouteReverseEndpointGuardV1 = dingosdk::game::build::v20260929::route_lookahead::route_reverse_endpoint_contract;
__declspec(dllexport) extern constinit const std::array<dingosdk::game::build::v20260929::route_lookahead::RouteLookaheadContract, 2>
    ReSkateNpcRouteProjectionGuardV1{dingosdk::game::build::v20260929::route_lookahead::route_array_projection_contract,
                                   dingosdk::game::build::v20260929::route_lookahead::route_vector_projection_contract};
__declspec(dllexport) extern constinit const std::uint32_t DingoSDKNativePatchSupportVersion =
    dingosdk::native_patch_support_version;
__declspec(dllexport) extern constinit const std::array<char, 65> DingoSDKNativePatchSupportGameSha256 = [] {
    std::array<char, 65> value{};
    for (std::size_t i = 0; i < 64; ++i) value[i] = dingosdk::supported_build::game_sha256[i];
    return value;
}();
}


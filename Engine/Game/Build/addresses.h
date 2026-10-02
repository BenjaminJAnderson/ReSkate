#pragma once
// Game addresses for the supported Skate.exe build, one header per area in
// Engine/Game/Build/<build>/. Features include the area headers they use and refer to
// values as addr::<area>::<name>. Supporting a new game build means adding a
// sibling folder with the same headers and pointing `addr` at it.
namespace dingosdk::game::build::v20260929 {}

namespace dingosdk {
namespace addr = game::build::v20260929;
}

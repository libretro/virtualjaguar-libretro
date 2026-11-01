// The build channel and the emulation-thread default (#967 phase 3d, design section 8; #1039 phase 5c).  Set by
// app/CMakeLists.txt (VJ_BUILD_CHANNEL: release | nightly | dev; VJ_EMU_THREAD_DEFAULT: 1 for every channel since the
// 4.0 flip) through the vj_build_channel target.
#pragma once
#include <string_view>

#ifndef VJ_BUILD_CHANNEL
#define VJ_BUILD_CHANNEL "dev"
#endif
#ifndef VJ_EMU_THREAD_DEFAULT
#define VJ_EMU_THREAD_DEFAULT 1
#endif

namespace vj {
constexpr const char* kBuildChannel = VJ_BUILD_CHANNEL;             // "release" | "nightly" | "dev"
constexpr bool kEmuThreadDefault = VJ_EMU_THREAD_DEFAULT != 0;      // every channel: true (phase 5c)
// What a channel defaults Settings > Interface > Emulation thread to for a user who never chose.  Since the 4.0 flip
// (#1039 5c) every channel defaults On; a channel this build does not know stays Off, the conservative answer.
constexpr bool emuThreadDefaultFor(std::string_view channel) {
    return channel == "release" || channel == "nightly" || channel == "dev";
}
// The setting as run: a stored choice (0 off, 1 on) always wins; only "never chosen" (any negative) follows the channel.
constexpr bool emuThreadEffective(int pref, bool channelDefault) { return pref < 0 ? channelDefault : pref == 1; }
static_assert(kEmuThreadDefault == emuThreadDefaultFor(kBuildChannel),
              "VJ_EMU_THREAD_DEFAULT disagrees with VJ_BUILD_CHANNEL (app/CMakeLists.txt)");
}  // namespace vj

// The build channel and the emulation-thread default rule (build_channel.h, #967 phase 3d).  No SDL, no core.
//
//   vj_emu_thread_default_test
#include "build_channel.h"

#include <cstdio>
#include <string_view>

using namespace vj;

namespace {

int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

}  // namespace

int main() {
    // "pref 0 is Off and pref 1 is On in every channel; only pref -1 follows the channel"  (Review Focus 4)
    for (const char* ch : {"release", "nightly", "dev"}) {
        const bool def = emuThreadDefaultFor(ch);
        CHECK(emuThreadEffective(0, def) == false);
        CHECK(emuThreadEffective(1, def) == true);
        CHECK(emuThreadEffective(-1, def) == def);
    }
    // Since the 4.0 flip (#1039 5c) every channel defaults On; an unknown channel stays Off.
    CHECK(emuThreadDefaultFor("nightly") && emuThreadDefaultFor("release") && emuThreadDefaultFor("dev"));
    CHECK(!emuThreadDefaultFor("") && !emuThreadDefaultFor("Nightly") && !emuThreadDefaultFor("nightlies"));
    CHECK(emuThreadEffective(-5, true) && !emuThreadEffective(-5, false));   // any negative is "never chosen"
    CHECK(!emuThreadEffective(2, true));                                      // only 1 is On
    const std::string_view c = kBuildChannel;                                 // what THIS build was configured with
    CHECK(c == "release" || c == "nightly" || c == "dev");
    CHECK(kEmuThreadDefault);   // the configured channel is one of the three above, and all three default On
    std::printf("emu_thread_default: channel %s, default %s\n", kBuildChannel, kEmuThreadDefault ? "on" : "off");
    if (g_fail) {
        std::printf("emu_thread_default: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("emu_thread_default: OK\n");
    return 0;
}

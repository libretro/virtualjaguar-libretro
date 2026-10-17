// TV mode (#991): the mode state machine (TvMode: who turns TV mode on and off, in every trigger order) and the
// input-ownership table (inputOwner), plus the stored-setting keys.
//
//   vj_tv_mode_test
//
// Pure logic: no window, no core, no clock.
#include "tv/tv_mode.h"

#include <cstdio>
#include <string>

using namespace vj::tv;

static int g_fail = 0, g_checks = 0;

#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_fail++;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

static void testOffAndManual() {
    TvMode m;
    m.configure({TvSetting::Off}, false);
    m.toggle();
    m.onFullscreen(true);
    m.onPadPress(true);
    CHECK(!m.active());
    m.setOn(true);
    CHECK(!m.active());

    m.configure({TvSetting::Manual}, false);
    m.onFullscreen(true);
    m.onPadPress(true);
    CHECK(!m.active());
    m.toggle();
    CHECK(m.active() && m.cause() == TvMode::Cause::Manual);
    m.onMouseMove(100, 0);                                                  // a manual TV mode ignores the mouse
    m.onNonNavKey();
    m.onFullscreen(false);
    CHECK(m.active());
    m.toggle();
    CHECK(!m.active() && m.cause() == TvMode::Cause::None);

    m.configure({TvSetting::Manual}, false);
    m.setOn(true);
    CHECK(m.active() && m.cause() == TvMode::Cause::Manual);
    m.setOn(false);
    CHECK(!m.active());
}

static void testAutoTriggers() {
    TvMode m;
    m.configure({TvSetting::Auto}, false);
    m.onFullscreen(true);
    CHECK(m.active() && m.cause() == TvMode::Cause::Fullscreen);
    m.onMouseMove(100, 0);
    CHECK(m.active());                                                      // fullscreen holds it
    m.onFullscreen(false);
    CHECK(!m.active());
    m.onPadPress(false);
    CHECK(!m.active());                                                     // the game owned the pad
    m.onPadPress(true);
    CHECK(m.active() && m.cause() == TvMode::Cause::Controller);
    m.onMouseMove(20, 0);
    CHECK(m.active());
    m.onMouseMove(0, 20);
    CHECK(!m.active());                                                     // 20 then 40 px of travel: over 32
    m.onPadPress(true);
    m.onNonNavKey();
    CHECK(!m.active());
    m.onPadPress(true);
    m.onFullscreen(true);
    m.onMouseMove(100, 0);
    CHECK(m.active());                                                      // controller-on, then fullscreen holds it
    m.toggle();
    CHECK(!m.active());
    m.onMouseMove(100, 0);
    m.onFullscreen(true);
    CHECK(m.active());                                                      // manual wins until the next edge

    TvConfig noFs{TvSetting::Auto, false, true};
    m.configure(noFs, true);
    CHECK(!m.active());
    m.onFullscreen(false);
    m.onFullscreen(true);
    CHECK(!m.active());
    m.onPadPress(true);                                                     // the controller trigger is independent
    CHECK(m.active() && m.cause() == TvMode::Cause::Controller);

    TvConfig noPad{TvSetting::Auto, true, false};
    m.configure(noPad, false);
    m.onPadPress(true);
    CHECK(!m.active());
    m.onFullscreen(true);
    CHECK(m.active());

    m.configure({TvSetting::Auto}, true);                                   // already fullscreen when configured
    CHECK(m.active() && m.cause() == TvMode::Cause::Fullscreen);
    m.configure({TvSetting::Auto}, false);                                  // a reconfigure starts from off
    CHECK(!m.active());
}

static void testMouseTravel() {
    TvMode m;
    m.configure({TvSetting::Auto}, false);
    m.onPadPress(true);
    m.onMouseMove(-10, 0);                                                  // direction does not matter
    m.onMouseMove(0, -10);
    m.onMouseMove(10, 0);
    CHECK(m.active());                                                      // 30 px
    m.onMouseMove(0, 3);
    CHECK(!m.active());                                                     // 33 px
    m.onPadPress(true);                                                     // every turn-on starts a fresh budget
    m.onMouseMove(30, 0);
    CHECK(m.active());
    m.onPadPress(true);                                                     // a pad press while on clears the travel too
    m.onMouseMove(30, 0);
    CHECK(m.active());
    m.onMouseMove(3, 0);
    CHECK(!m.active());
}

static void testFullscreenLeaveKeepsController() {
    TvMode m;
    m.configure({TvSetting::Auto}, false);
    m.onPadPress(true);
    m.onFullscreen(true);
    m.onFullscreen(false);                                                  // the pad still owns it: only the hold goes
    CHECK(m.active() && m.cause() == TvMode::Cause::Controller);
    m.onMouseMove(40, 0);
    CHECK(!m.active());
}

static void testSettingKeys() {
    TvSetting s = TvSetting::Off;
    CHECK(settingFromKey("auto", s) && s == TvSetting::Auto && !settingFromKey("bogus", s));
    CHECK(s == TvSetting::Auto);                                            // a failed parse leaves it alone
    CHECK(settingFromKey("off", s) && s == TvSetting::Off);
    CHECK(settingFromKey("manual", s) && s == TvSetting::Manual);
    CHECK(!settingFromKey("", s) && !settingFromKey("Auto", s));
    CHECK(std::string(settingKey(TvSetting::Off)) == "off" && std::string(settingKey(TvSetting::Manual)) == "manual" &&
          std::string(settingKey(TvSetting::Auto)) == "auto");
    CHECK(effectiveSetting(TvSetting::Auto) == TvSetting::Manual && effectiveSetting(TvSetting::Off) == TvSetting::Off);
    CHECK(effectiveSetting(TvSetting::Manual) == TvSetting::Manual);
}

static void testInputOwner() {
    CHECK(inputOwner(false, false, false) == Owner::DesktopUi && inputOwner(false, true, false) == Owner::Game);
    CHECK(inputOwner(true, false, false) == Owner::TvUi && inputOwner(true, true, false) == Owner::Game);
    CHECK(inputOwner(false, true, true) == Owner::TvUi && inputOwner(true, true, true) == Owner::TvUi);
    CHECK(inputOwner(false, false, true) == Owner::TvUi && inputOwner(true, false, true) == Owner::TvUi);
}

int main() {
    testOffAndManual();
    testAutoTriggers();
    testMouseTravel();
    testFullscreenLeaveKeepsController();
    testSettingKeys();
    testInputOwner();
    std::printf("tv_mode_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

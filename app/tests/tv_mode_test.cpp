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

static void testFullscreenHoldIsState() {
    TvMode m;
    m.configure({TvSetting::Auto}, false);
    m.onFullscreen(true);
    m.toggle();
    CHECK(!m.active());
    m.onPadPress(true);
    CHECK(m.active() && m.cause() == TvMode::Cause::Controller);
    m.onMouseMove(33, 0);
    CHECK(m.active());                                                      // still fullscreen: the mouse cannot end it
    m.onNonNavKey();
    CHECK(m.active());

    m.configure({TvSetting::Auto}, true);                                   // fullscreen from configure() holds too
    m.toggle();
    m.onPadPress(true);
    m.onMouseMove(33, 0);
    CHECK(m.active());

    m.configure({TvSetting::Auto}, false);                                  // control: not fullscreen, the mouse ends it
    m.onFullscreen(true);
    m.onFullscreen(false);                                                  // left fullscreen: off
    m.onPadPress(true);
    m.onMouseMove(33, 0);
    CHECK(!m.active());

    TvConfig noFs{TvSetting::Auto, false, true};                            // fullscreen does not hold without autoFullscreen
    m.configure(noFs, true);
    m.onPadPress(true);
    m.onMouseMove(33, 0);
    CHECK(!m.active());
}

static void testFullscreenLeaveTurnsOff() {
    TvMode m;
    m.configure({TvSetting::Auto}, false);
    m.onPadPress(true);
    CHECK(m.cause() == TvMode::Cause::Controller);
    m.onFullscreen(true);
    m.onFullscreen(false);
    CHECK(!m.active());                                                     // whatever the cause
    m.onFullscreen(true);
    m.toggle();
    m.toggle();
    CHECK(m.active() && m.cause() == TvMode::Cause::Manual);
    m.onFullscreen(false);
    CHECK(!m.active());                                                     // leaving ends a manual override too
    m.configure({TvSetting::Manual}, false);                                // only under Auto
    m.toggle();
    m.onFullscreen(true);
    m.onFullscreen(false);
    CHECK(m.active());
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
    CHECK(defaultSetting("manual") == TvSetting::Manual && defaultSetting("off") == TvSetting::Off && defaultSetting("auto") == TvSetting::Auto &&
          defaultSetting("bogus") == TvSetting::Auto && defaultSetting(nullptr) == TvSetting::Auto);   // the build default (VJ_TV_DEFAULT_SETTING)
}

// The one-time hint (decision 8): the pad's own TV-menu button and the keyboard's, or the fixed shortcut when fewer than two are known.
static void testFirstUseHint() {
    CHECK(firstUseHint("Share (hold)", "F4", false) == "Press Share (hold) or F4 for TV mode");
    CHECK(firstUseHint("Share (hold)", "F4", true) == "Press Share (hold) or F4 for TV mode");          // two bindings: no shortcut needed
    CHECK(firstUseHint("", "F4", false) == "Press F4 or Ctrl+Shift+T for TV mode");                      // a pad with no TV-menu button
    CHECK(firstUseHint("", "", true) == "Press Cmd+Shift+T for TV mode");                                 // nothing bound: the fixed shortcut alone
    CHECK(firstUseHint("", "", false) == "Press Ctrl+Shift+T for TV mode");
    CHECK(firstUseHint("Paddle P1", "", true) == "Press Paddle P1 or Cmd+Shift+T for TV mode");           // the keyboard binding was removed
}

// Every spec 8 trigger order, as App feeds them: edges of fullscreen only, a mapped pad press, mouse travel, a printable key.
// (testAutoTriggers, testMouseTravel and the two fullscreen tests above cover the single triggers; these are the sequences.)
static void testTriggerOrders() {
    using C = TvMode::Cause;
    TvMode m;

    m.configure({TvSetting::Auto}, false);                                  // pad -> fullscreen enter -> fullscreen leave
    m.onPadPress(true);
    CHECK(m.active() && m.cause() == C::Controller);
    m.onFullscreen(true);
    CHECK(m.active() && m.cause() == C::Controller);                        // already on: it keeps its own cause
    m.onFullscreen(false);
    CHECK(!m.active() && m.cause() == C::None);                             // leaving ends it whatever the cause

    m.configure({TvSetting::Auto}, false);                                  // fullscreen enter -> pad -> mouse 100 px -> leave
    m.onFullscreen(true);
    CHECK(m.active() && m.cause() == C::Fullscreen);
    m.onPadPress(true);
    CHECK(m.active() && m.cause() == C::Fullscreen);
    m.onMouseMove(100, 0);
    CHECK(m.active() && m.cause() == C::Fullscreen);                        // fullscreen holds it
    m.onFullscreen(false);
    CHECK(!m.active() && m.cause() == C::None);

    m.configure({TvSetting::Auto}, false);                                  // pad -> manual toggle -> pad -> key
    m.onPadPress(true);
    CHECK(m.active() && m.cause() == C::Controller);
    m.toggle();
    CHECK(!m.active() && m.cause() == C::None);
    m.onPadPress(true);
    CHECK(m.active() && m.cause() == C::Controller);                        // a pad edge ends the manual override
    m.onNonNavKey();
    CHECK(!m.active() && m.cause() == C::None);

    m.configure({TvSetting::Auto}, false);                                  // fullscreen enter -> toggle -> pad -> mouse 40 px -> leave
    m.onFullscreen(true);
    CHECK(m.active() && m.cause() == C::Fullscreen);
    m.toggle();
    CHECK(!m.active() && m.cause() == C::None);
    m.onPadPress(true);
    CHECK(m.active() && m.cause() == C::Controller);
    m.onMouseMove(40, 0);
    CHECK(m.active() && m.cause() == C::Controller);                        // fullscreen holds it
    m.onFullscreen(false);
    CHECK(!m.active() && m.cause() == C::None);

    m.configure({TvSetting::Auto}, true);                                   // fullscreen at configure -> toggle -> a repeated enter
    CHECK(m.active() && m.cause() == C::Fullscreen);
    m.toggle();
    CHECK(!m.active());
    m.onFullscreen(true);
    CHECK(m.active() && m.cause() == C::Fullscreen);                        // why App feeds edges only: a repeated enter undoes a manual off

    TvMode off;                                                             // Off: every trigger is a no-op, and so is the toggle
    off.configure({TvSetting::Off}, true);
    CHECK(!off.active());
    off.onFullscreen(true);
    off.onPadPress(true);
    off.onNonNavKey();
    off.onMouseMove(100, 0);
    off.toggle();
    CHECK(!off.active() && off.cause() == C::None);

    TvMode man;                                                             // Manual: the triggers do nothing, the toggle works
    man.configure({TvSetting::Manual}, true);
    CHECK(!man.active());
    man.onFullscreen(true);
    man.onPadPress(true);
    man.onNonNavKey();
    man.onMouseMove(100, 0);
    CHECK(!man.active() && man.cause() == C::None);
    man.toggle();
    CHECK(man.active() && man.cause() == C::Manual);
    man.onFullscreen(false);
    man.onPadPress(true);
    man.onNonNavKey();
    man.onMouseMove(100, 0);
    CHECK(man.active() && man.cause() == C::Manual);                        // none of them touches a Manual TV mode
    man.toggle();
    CHECK(!man.active() && man.cause() == C::None);
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
    testFullscreenHoldIsState();
    testFullscreenLeaveTurnsOff();
    testSettingKeys();
    testFirstUseHint();
    testTriggerOrders();
    testInputOwner();
    std::printf("tv_mode_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

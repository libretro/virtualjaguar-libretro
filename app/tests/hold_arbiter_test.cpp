// TV mode (#991): HoldArbiter (a dual-use button is a tap for the game and a 500 ms hold for the menu) and
// ReleaseGuard (an input down when ownership changes stays invisible to the new owner until it is released).
//
//   vj_hold_arbiter_test
//
// Pure logic: no window, no core, no clock.  Timestamps are passed in.
#include "hold_arbiter.h"
#include "remap.h"

#include <cstdio>

using namespace vj;

static int g_fail = 0, g_checks = 0;

#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_fail++;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

static const uint32_t DEV = 7;
static const int GUIDE = SDL_GAMEPAD_BUTTON_GUIDE;
static const int SHARE = SDL_GAMEPAD_BUTTON_MISC1;
static const int START = SDL_GAMEPAD_BUTTON_START;

static Binding G() { return Binding::button(GUIDE); }
static Binding S() { return Binding::button(SHARE); }

static DeviceSnapshot snapG() {
    DeviceSnapshot s{};
    s.btn[GUIDE] = true;
    return s;
}

// A fresh arbiter with Guide as the hold input on DEV.
static HoldArbiter guideArbiter() {
    HoldArbiter a;
    a.setHoldInputs(DEV, {G()});
    return a;
}

// Count how many of the next n emulated frames show Guide set (one apply + one frame() each).
static int pulseFrames(HoldArbiter& a, int n) {
    int seen = 0;
    for (int f = 0; f < n; f++) {
        DeviceSnapshot u{};
        a.apply(DEV, u);
        if (u.btn[GUIDE]) seen++;
        a.frame();
    }
    return seen;
}

static void testTap() {
    HoldArbiter a = guideArbiter();
    CHECK(a.isHoldInput(DEV, G()));
    CHECK(a.isHoldInput(DEV, G().withHold()));   // the hold flag is ignored when matching
    CHECK(!a.isHoldInput(DEV, S()));
    CHECK(!a.isHoldInput(8, G()));
    a.press(DEV, G(), 1000);
    DeviceSnapshot s = snapG();
    a.apply(DEV, s);
    CHECK(!s.btn[GUIDE]);   // withheld while down
    CHECK(a.takeFires(1200) == 0);
    a.release(DEV, G(), 1300);
    for (int f = 0; f < 4; f++) {
        DeviceSnapshot u{};
        a.apply(DEV, u);
        CHECK(u.btn[GUIDE]);
        a.frame();
    }
    DeviceSnapshot after{};
    a.apply(DEV, after);
    CHECK(!after.btn[GUIDE]);   // exactly 4 frames
    CHECK(a.takeFires(5000) == 0);
}

static void testHold() {
    HoldArbiter a = guideArbiter();
    a.press(DEV, G(), 0);
    CHECK(a.takeFires(499) == 0);
    CHECK(a.takeFires(500) == 1);
    CHECK(a.takeFires(900) == 0);   // once
    DeviceSnapshot s = snapG();
    a.apply(DEV, s);
    CHECK(!s.btn[GUIDE]);   // swallowed until released
    a.release(DEV, G(), 1000);
    DeviceSnapshot r{};
    a.apply(DEV, r);
    CHECK(!r.btn[GUIDE]);   // no pulse after a hold
    CHECK(pulseFrames(a, 4) == 0);
    CHECK(a.takeFires(2000) == 0);
}

static void testThresholdAtRelease() {
    // The UI frame never ticked between 500 ms and the release: the release itself carries the fire.
    HoldArbiter a = guideArbiter();
    a.press(DEV, G(), 0);
    a.release(DEV, G(), 500);
    CHECK(a.takeFires(501) == 1);
    CHECK(a.takeFires(502) == 0);
    CHECK(pulseFrames(a, 4) == 0);
    // 499 ms is still a tap.
    a.press(DEV, G(), 1000);
    a.release(DEV, G(), 1499);
    CHECK(a.takeFires(1500) == 0);
    CHECK(pulseFrames(a, 5) == 4);
}

static void testTwoHoldButtons() {
    HoldArbiter a;
    a.setHoldInputs(DEV, {S(), G()});
    a.press(DEV, S(), 0);
    a.press(DEV, G(), 300);
    a.release(DEV, G(), 400);   // Guide tapped (100 ms)
    CHECK(a.takeFires(600) == 1);   // Share reached 500 ms
    DeviceSnapshot s{};
    s.btn[SHARE] = true;
    a.apply(DEV, s);
    CHECK(!s.btn[SHARE]);   // Share swallowed
    DeviceSnapshot u{};
    a.apply(DEV, u);
    CHECK(u.btn[GUIDE]);   // Guide pulses
    CHECK(!u.btn[SHARE]);
    a.release(DEV, S(), 700);
    CHECK(a.takeFires(800) == 0);
}

static void testPerDeviceIsolation() {
    HoldArbiter a = guideArbiter();
    a.press(DEV, G(), 0);
    DeviceSnapshot other = snapG();
    a.apply(8, other);
    CHECK(other.btn[GUIDE]);   // dev 8 is not a hold device
    a.setHoldInputs(8, {G()});
    DeviceSnapshot other2 = snapG();
    a.apply(8, other2);
    CHECK(other2.btn[GUIDE]);   // dev 8 has the input but is not holding it
    a.press(8, G(), 0);
    DeviceSnapshot mine = snapG();
    a.apply(DEV, mine);
    CHECK(!mine.btn[GUIDE]);
    a.release(8, G(), 100);
    CHECK(a.takeFires(600) == 1);   // only dev 7's hold reached the threshold
}

static void testUnplug() {
    HoldArbiter a = guideArbiter();
    a.press(DEV, G(), 0);
    a.forget(DEV);
    CHECK(a.takeFires(600) == 0);
    DeviceSnapshot s = snapG();
    a.apply(DEV, s);
    CHECK(s.btn[GUIDE]);   // no longer a hold input
    CHECK(!a.isHoldInput(DEV, G()));
    // forget also drops a pulse in flight
    HoldArbiter b = guideArbiter();
    b.press(DEV, G(), 0);
    b.release(DEV, G(), 100);
    b.forget(DEV);
    b.setHoldInputs(DEV, {G()});
    CHECK(pulseFrames(b, 4) == 0);
}

static void testClear() {
    HoldArbiter a = guideArbiter();
    a.press(DEV, G(), 0);
    a.release(DEV, G(), 100);
    a.clear();
    DeviceSnapshot s{};
    a.apply(DEV, s);
    CHECK(!s.btn[GUIDE]);   // pulse gone
    // a hold in flight, a release past the threshold not yet taken: nothing fires after clear()
    HoldArbiter b = guideArbiter();
    b.press(DEV, G(), 0);
    b.release(DEV, G(), 600);
    b.press(DEV, G(), 700);
    b.clear();
    CHECK(b.takeFires(5000) == 0);
    // clear() keeps the configured hold inputs (it is a focus change, not an unplug)
    CHECK(b.isHoldInput(DEV, G()));
    b.press(DEV, G(), 6000);
    CHECK(b.takeFires(6500) == 1);
}

static void testLive() {
    {
        HoldArbiter a = guideArbiter();
        a.setLive(false);
        a.press(DEV, G(), 0);
        a.release(DEV, G(), 100);
        a.setLive(true);
        CHECK(pulseFrames(a, 4) == 0);   // a Share tap in the library must not reach the next game
    }
    {
        HoldArbiter a = guideArbiter();
        a.setLive(true);
        a.press(DEV, G(), 0);
        a.release(DEV, G(), 100);
        a.setLive(false);
        a.setLive(true);
        CHECK(pulseFrames(a, 4) == 0);   // going not-live drops the queued pulse
    }
    {
        HoldArbiter a = guideArbiter();
        a.setLive(false);
        a.press(DEV, G(), 0);
        CHECK(a.takeFires(500) == 1);   // the menu works paused / in the library
        DeviceSnapshot s = snapG();
        a.apply(DEV, s);
        CHECK(!s.btn[GUIDE]);   // still withheld
    }
    {
        // press while live, go not-live while down, release as a tap: no pulse
        HoldArbiter a = guideArbiter();
        a.press(DEV, G(), 0);
        a.setLive(false);
        a.release(DEV, G(), 100);
        a.setLive(true);
        CHECK(pulseFrames(a, 4) == 0);
    }
}

static void testNonHoldInput() {
    HoldArbiter a = guideArbiter();
    a.press(DEV, Binding::button(START), 0);   // no-op unless a hold input
    DeviceSnapshot s{};
    s.btn[START] = true;
    s.btn[GUIDE] = false;
    a.apply(DEV, s);
    CHECK(s.btn[START]);
    CHECK(a.takeFires(1000) == 0);
    a.release(DEV, Binding::button(START), 1100);
    DeviceSnapshot t{};
    t.btn[START] = true;
    a.apply(DEV, t);
    CHECK(t.btn[START] && !t.btn[GUIDE]);
}

static void testKeyboard() {
    HoldArbiter a;
    Binding f1 = Binding::key(SDL_SCANCODE_F1);
    a.setHoldInputs(kKeyboardDev, {f1});
    a.press(kKeyboardDev, f1, 0);
    DeviceSnapshot s{};
    s.key[SDL_SCANCODE_F1] = true;
    a.apply(kKeyboardDev, s);
    CHECK(!s.key[SDL_SCANCODE_F1]);
    a.release(kKeyboardDev, f1, 50);
    DeviceSnapshot u{};
    a.apply(kKeyboardDev, u);
    CHECK(u.key[SDL_SCANCODE_F1]);
}

static void testRepeatAndPressDuringPulse() {
    HoldArbiter a = guideArbiter();
    a.press(DEV, G(), 0);
    a.press(DEV, G(), 400);   // OS key repeat / duplicate event: the hold clock does not restart
    CHECK(a.takeFires(500) == 1);
    a.release(DEV, G(), 600);
    // tap, then press again inside the pulse: the new press is withheld, the old pulse is over
    a.press(DEV, G(), 1000);
    a.release(DEV, G(), 1050);
    a.press(DEV, G(), 1060);
    DeviceSnapshot s = snapG();
    a.apply(DEV, s);
    CHECK(!s.btn[GUIDE]);
}

static void testChord() {
    // Documented spec limitation: Guide is the "#" key on a pad whose Guide button is also the menu hold, and the
    // keypad chord * + # needs both at once.  The core never sees Guide while it is down, so the chord only forms
    // as Guide is released, and then as a 4-frame pulse.
    HoldArbiter a = guideArbiter();
    DeviceSnapshot star{};
    star.axis[SDL_GAMEPAD_AXIS_RIGHTY] = 30000;   // right stick down = *
    a.press(DEV, G(), 0);
    for (int f = 0; f < 10; f++) {
        DeviceSnapshot s = star;
        s.btn[GUIDE] = true;
        a.apply(DEV, s);
        CHECK(!s.btn[GUIDE] && s.axis[SDL_GAMEPAD_AXIS_RIGHTY] == 30000);   // never both
        a.frame();
    }
    a.release(DEV, G(), 100);
    int both = 0;
    for (int f = 0; f < 4; f++) {
        DeviceSnapshot s = star;
        a.apply(DEV, s);
        if (s.btn[GUIDE] && s.axis[SDL_GAMEPAD_AXIS_RIGHTY] == 30000) both++;
        a.frame();
    }
    CHECK(both == 4);
}

static void testGuard() {
    ReleaseGuard g;
    CHECK(!g.armed());
    DeviceSnapshot down{};
    down.btn[SDL_GAMEPAD_BUTTON_SOUTH] = true;
    down.axis[SDL_GAMEPAD_AXIS_LEFTX] = 30000;
    g.arm(DEV, down);
    CHECK(g.armed());
    DeviceSnapshot still = down;
    g.apply(DEV, still);
    CHECK(!still.btn[SDL_GAMEPAD_BUTTON_SOUTH] && still.axis[SDL_GAMEPAD_AXIS_LEFTX] == 0);
    DeviceSnapshot up{};
    g.apply(DEV, up);
    DeviceSnapshot again = down;
    g.apply(DEV, again);
    CHECK(again.btn[SDL_GAMEPAD_BUTTON_SOUTH]);   // re-press reaches the game
    CHECK(again.axis[SDL_GAMEPAD_AXIS_LEFTX] == 30000);
    CHECK(!g.armed());
}

static void testGuardDetails() {
    ReleaseGuard g;
    DeviceSnapshot d{};
    d.key[SDL_SCANCODE_RETURN] = true;
    d.axis[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 9000;   // past the trigger threshold (8000)
    d.axis[SDL_GAMEPAD_AXIS_LEFTY] = -20000;         // sticks mask by absolute value
    d.axis[SDL_GAMEPAD_AXIS_RIGHTX] = 15000;         // under the stick threshold: not masked
    g.arm(DEV, d);
    DeviceSnapshot s = d;
    g.apply(DEV, s);
    CHECK(!s.key[SDL_SCANCODE_RETURN]);
    CHECK(s.axis[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] == 0);
    CHECK(s.axis[SDL_GAMEPAD_AXIS_LEFTY] == 0);
    CHECK(s.axis[SDL_GAMEPAD_AXIS_RIGHTX] == 15000);
    // another device is untouched
    DeviceSnapshot other = d;
    g.apply(8, other);
    CHECK(other.key[SDL_SCANCODE_RETURN] && other.axis[SDL_GAMEPAD_AXIS_LEFTY] == -20000);
    // the stick eases back within the threshold: released, and the next push reaches the game
    DeviceSnapshot back{};
    back.axis[SDL_GAMEPAD_AXIS_LEFTY] = -15000;
    back.axis[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 7000;
    back.key[SDL_SCANCODE_RETURN] = true;
    g.apply(DEV, back);
    DeviceSnapshot push{};
    push.axis[SDL_GAMEPAD_AXIS_LEFTY] = -25000;
    push.key[SDL_SCANCODE_RETURN] = true;
    g.apply(DEV, push);
    CHECK(push.axis[SDL_GAMEPAD_AXIS_LEFTY] == -25000);
    CHECK(g.armed());   // the Return key is still masked
    DeviceSnapshot none{};
    g.apply(DEV, none);
    CHECK(!g.armed());
    // arm with nothing down masks nothing; clear() drops everything
    g.arm(DEV, DeviceSnapshot{});
    CHECK(!g.armed());
    g.arm(DEV, d);
    CHECK(g.armed());
    g.clear();
    CHECK(!g.armed());
}

static void testGuardForget() {
    ReleaseGuard g;
    DeviceSnapshot d{};
    d.btn[SDL_GAMEPAD_BUTTON_EAST] = true;
    g.arm(DEV, d);
    g.arm(DEV + 1, d);
    CHECK(g.armed());
    g.forget(DEV);
    CHECK(g.armed());   // the other device is still masked
    DeviceSnapshot s = d;
    g.apply(DEV, s);
    CHECK(s.btn[SDL_GAMEPAD_BUTTON_EAST]);   // DEV's mask is gone: East reaches the game
    g.forget(DEV + 1);
    CHECK(!g.armed());
    g.forget(DEV);   // unknown device: no-op
    CHECK(!g.armed());
}

int main() {
    testTap();
    testHold();
    testThresholdAtRelease();
    testTwoHoldButtons();
    testPerDeviceIsolation();
    testUnplug();
    testClear();
    testLive();
    testNonHoldInput();
    testKeyboard();
    testRepeatAndPressDuringPulse();
    testChord();
    testGuard();
    testGuardDetails();
    testGuardForget();
    std::printf("hold_arbiter_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

// TV mode (#991): NavInput (pad / keyboard events -> navigation intents, with held-direction repeat and stick
// hysteresis) and the button / key -> Intent tables.
//
//   vj_tv_nav_test
//
// Pure logic: no window, no core, no clock.  Timestamps are passed in.
#include "tv/tv_nav.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

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

typedef std::vector<Intent> Intents;

static void testDpadRepeat() {
    NavInput n;
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 0);
    CHECK(n.take(0) == Intents{Intent::Down});
    CHECK(n.take(349).empty());
    CHECK(n.take(350).size() == 1);
    CHECK(n.take(430).size() == 1);
    CHECK(n.take(509).empty());
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, 520);
    CHECK(n.take(2000).empty());
}

// A screen can tell a repeat from a press: lastWasRepeat() is true only after a take() whose last intent was the timer's.
static void testLastWasRepeat() {
    NavInput n;
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true, 0);
    CHECK(n.take(0) == Intents{Intent::Right} && !n.lastWasRepeat());   // the press
    CHECK(n.take(100).empty() && !n.lastWasRepeat());
    CHECK(n.take(350).size() == 1 && n.lastWasRepeat());                // the first repeat
    CHECK(n.take(360).empty() && !n.lastWasRepeat());
    CHECK(n.take(430).size() == 1 && n.lastWasRepeat());
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false, 440);
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true, 450);              // a new press arriving with the call: edge, then no repeat
    CHECK(n.take(450) == Intents{Intent::Right} && !n.lastWasRepeat());
}

static void testNoRepeatForButtons() {
    NavInput n;
    n.padButton(SDL_GAMEPAD_BUTTON_SOUTH, true, 0);
    CHECK(n.take(0) == Intents{Intent::Accept});
    CHECK(n.take(1000).empty());   // no repeat
    n.padButton(SDL_GAMEPAD_BUTTON_SOUTH, false, 1100);
    CHECK(n.take(1100).empty());
}

static void testStick() {
    NavInput n;
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 15000, 0);
    CHECK(n.take(0).empty());   // inside the dead zone
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 20000, 10);
    CHECK(n.take(10) == Intents{Intent::Down});
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 10000, 20);
    CHECK(n.take(400).size() == 1);   // hysteresis: still held
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 7000, 430);   // below the off threshold: released
    CHECK(n.take(2000).empty());
    n.padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000, 0);
    CHECK(n.take(0) == Intents{Intent::PageNext});
}

static void testStickDirections() {
    NavInput n;
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, -20000, 0);
    CHECK(n.take(0) == Intents{Intent::Up});
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 0, 10);
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTX, -20000, 20);
    CHECK(n.take(20) == Intents{Intent::Left});
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTX, 20000, 30);   // straight through to the other side
    CHECK(n.take(30) == Intents{Intent::Right});
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTX, 0, 40);
    CHECK(n.take(5000).empty());
}

static void testTriggers() {
    NavInput n;
    n.padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 30000, 0);
    CHECK(n.take(0) == Intents{Intent::PagePrev});
    n.padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32000, 5);   // still pressed: no second edge
    CHECK(n.take(5).empty() && n.take(1000).empty());
    n.padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0, 10);
    n.padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 30000, 20);
    CHECK(n.take(20) == Intents{Intent::PagePrev});
}

static void testNewestWins() {
    NavInput n;
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 0);
    CHECK(n.take(0) == Intents{Intent::Down});
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true, 100);
    CHECK(n.take(100) == Intents{Intent::Right});
    // Only the newest repeats; its delay starts at its own press.
    CHECK(n.take(400).empty());
    Intents r = n.take(450);
    CHECK(r == Intents{Intent::Right});
    // Letting go of the older one changes nothing.
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, 460);
    CHECK(n.take(530) == Intents{Intent::Right});
    // Letting go of the newest hands the repeat back to the older one, after a fresh delay.
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 600);
    CHECK(n.take(600) == Intents{Intent::Down});
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, 610);
    CHECK(n.take(620).empty() && n.take(959).empty());
    CHECK(n.take(960) == Intents{Intent::Right});
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false, 970);
    CHECK(n.take(5000).empty());
}

static void testFallback() {
    NavInput n;
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_UP, true, 0);
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true, 50);
    CHECK(n.take(50) == (Intents{Intent::Up, Intent::Left}));
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT, false, 100);   // Up is still down: it takes over, no new edge
    CHECK(n.take(100).empty());
    CHECK(n.take(449).empty());
    CHECK(n.take(450) == Intents{Intent::Up});
}

static void testKeyboard() {
    NavInput n;
    n.key(SDL_SCANCODE_RIGHT, true, false, 0);
    CHECK(n.take(0) == Intents{Intent::Right});
    n.key(SDL_SCANCODE_RIGHT, true, false, 100);   // OS key repeat: not a fresh edge, our own clock rules
    CHECK(n.take(100).empty());
    CHECK(n.take(350).size() == 1);
    n.key(SDL_SCANCODE_RIGHT, false, false, 360);
    CHECK(n.take(2000).empty());
    n.key(SDL_SCANCODE_RETURN, true, false, 0);
    CHECK(n.take(0) == Intents{Intent::Accept});
    n.key(SDL_SCANCODE_RETURN, false, false, 10);
    n.key(SDL_SCANCODE_TAB, true, true, 20);
    CHECK(n.take(20) == Intents{Intent::SectionPrev});
}

static void testReset() {
    NavInput n;
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 0);
    n.padButton(SDL_GAMEPAD_BUTTON_SOUTH, true, 0);
    n.reset();
    CHECK(n.take(0).empty() && n.take(5000).empty());
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 6000);   // usable again after a reset
    CHECK(n.take(6000) == Intents{Intent::Down});
}

static void testTables() {
    CHECK(intentForKey(SDL_SCANCODE_TAB, true) == Intent::SectionPrev && intentForKey(SDL_SCANCODE_BACKSPACE, false) == Intent::Back);
    CHECK(intentForButton(SDL_GAMEPAD_BUTTON_EAST) == Intent::Back && intentForButton(SDL_GAMEPAD_BUTTON_START) == Intent::Menu);
    CHECK(intentForButton(SDL_GAMEPAD_BUTTON_WEST) == Intent::Alt && intentForButton(SDL_GAMEPAD_BUTTON_NORTH) == Intent::Aux);
    CHECK(intentForButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == Intent::SectionPrev);
    CHECK(intentForButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == Intent::SectionNext);
    CHECK(intentForButton(SDL_GAMEPAD_BUTTON_DPAD_UP) == Intent::Up && intentForButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT) == Intent::Left);
    CHECK(intentForButton(SDL_GAMEPAD_BUTTON_GUIDE) == Intent::None);
    CHECK(intentForKey(SDL_SCANCODE_TAB, false) == Intent::SectionNext);
    CHECK(intentForKey(SDL_SCANCODE_KP_ENTER, false) == Intent::Accept && intentForKey(SDL_SCANCODE_ESCAPE, false) == Intent::Back);
    CHECK(intentForKey(SDL_SCANCODE_PAGEUP, false) == Intent::PagePrev && intentForKey(SDL_SCANCODE_PAGEDOWN, false) == Intent::PageNext);
    CHECK(intentForKey(SDL_SCANCODE_A, false) == Intent::None);
}

// The brief's sequence on ONE NavInput, in order: a button never released, a stick still held and the clock
// stepping back while a trigger fires.
static void testBriefSequence() {
    NavInput n;
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 0);
    CHECK(n.take(0) == Intents{Intent::Down});
    CHECK(n.take(349).empty());
    CHECK(n.take(350).size() == 1);
    CHECK(n.take(430).size() == 1);
    CHECK(n.take(509).empty());
    n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, 520);
    CHECK(n.take(2000).empty());
    n.padButton(SDL_GAMEPAD_BUTTON_SOUTH, true, 0);
    CHECK(n.take(0) == Intents{Intent::Accept});
    CHECK(n.take(1000).empty());   // no repeat
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 15000, 0);
    CHECK(n.take(0).empty());   // inside the dead zone
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 20000, 10);
    CHECK(n.take(10) == Intents{Intent::Down});
    n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 10000, 20);
    CHECK(n.take(400).size() == 1);   // hysteresis: still held
    n.padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000, 0);
    CHECK(n.take(0) == Intents{Intent::PageNext});
}

static void testTriggerLabels() {
    CHECK(std::string(triggerLabel(Intent::PagePrev, false)) == "LT" && std::string(triggerLabel(Intent::PageNext, false)) == "RT");
    CHECK(std::string(triggerLabel(Intent::PagePrev, true)) == "L2" && std::string(triggerLabel(Intent::PageNext, true)) == "R2");
    CHECK(std::string(triggerLabel(Intent::Accept, false)).empty() && std::string(triggerLabel(Intent::None, true)).empty());
}

// ---- #1021: per-pad sources, supplied intents, long-press Accept, Search ----

static void testLongAcceptTap() {
    NavInput n;
    n.setLongAccept(true);
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, true, 0);
    CHECK(n.take(100).empty());   // held back until the release says tap or long
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 200);
    CHECK(n.take(200) == Intents{Intent::Accept});
    CHECK(n.take(300).empty());
}

static void testLongAcceptHold() {
    NavInput n;
    n.setLongAccept(true);
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, true, 0);
    CHECK(n.take(499).empty());
    CHECK(n.take(500) == Intents{Intent::AcceptLong});
    CHECK(n.take(600).empty());   // once
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 900);
    CHECK(n.take(900).empty());   // the release is swallowed
    CHECK(NavInput::kLongMs == 500);
}

// A release that crossed the threshold between two take() calls is still a long press, not a tap.
static void testLongAcceptReleaseBeforeTake() {
    NavInput n;
    n.setLongAccept(true);
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, true, 0);
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 800);
    CHECK(n.take(800) == Intents{Intent::AcceptLong});
}

static void testLongAcceptUnseenRelease() {
    NavInput n;
    n.setLongAccept(true);
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, true, 0);
    n.reset();
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 100);
    CHECK(n.take(100).empty());
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 200);   // a release with no press at all
    CHECK(n.take(200).empty());
}

static void testLongAcceptOffUnchanged() {
    NavInput n;
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, true, 0);
    CHECK(n.take(0) == Intents{Intent::Accept});
    CHECK(n.take(1000).empty());
    n.padIntent(7, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 1100);
    CHECK(n.take(1100).empty());
}

// Long Accept covers Accept from pad buttons only; keys and the other intents are not delayed.
static void testLongAcceptScope() {
    NavInput n;
    n.setLongAccept(true);
    n.key(SDL_SCANCODE_RETURN, true, false, 0);
    CHECK(n.take(0) == Intents{Intent::Accept});
    n.padIntent(1, SDL_GAMEPAD_BUTTON_EAST, Intent::Back, true, 0);
    CHECK(n.take(0) == Intents{Intent::Back});
    n.padIntent(1, SDL_GAMEPAD_BUTTON_START, Intent::Search, true, 0);
    CHECK(n.take(0) == Intents{Intent::Search});
}

static void testTwoPadsSameButton() {
    NavInput n;
    n.setLongAccept(true);
    n.padIntent(1, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, true, 0);
    n.padIntent(2, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Back, true, 10);   // same button, other pad, other intent
    CHECK(n.take(10) == Intents{Intent::Back});
    n.padIntent(2, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Back, false, 20);
    n.padIntent(1, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 100);
    CHECK(n.take(100) == Intents{Intent::Accept});
}

static void testUnplugMidHold() {
    NavInput n;
    n.setLongAccept(true);
    n.padIntent(3, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, true, 0);
    n.padIntent(3, SDL_GAMEPAD_BUTTON_DPAD_DOWN, Intent::Down, true, 0);
    n.forgetPad(3);
    CHECK(n.take(1000) == Intents{Intent::Down});   // only the edge from before; no AcceptLong, no repeat
    CHECK(n.take(2000).empty());
    n.padIntent(3, SDL_GAMEPAD_BUTTON_SOUTH, Intent::Accept, false, 2100);   // a late release is not ours
    CHECK(n.take(2100).empty());
}

// Unplugging the newest holder hands the repeat to an older hold from another pad, after a fresh delay.
static void testUnplugHandsRepeatBack() {
    NavInput n;
    n.padIntent(1, SDL_GAMEPAD_BUTTON_DPAD_LEFT, Intent::Left, true, 0);
    n.padIntent(2, SDL_GAMEPAD_BUTTON_DPAD_DOWN, Intent::Down, true, 100);
    CHECK(n.take(100) == (Intents{Intent::Left, Intent::Down}));
    n.forgetPad(2);
    CHECK(n.take(500).empty());              // fresh delay starts at the first take after the unplug
    CHECK(n.take(849).empty());
    CHECK(n.take(850) == Intents{Intent::Left});
    n.forgetPad(1);
    CHECK(n.take(5000).empty());
}

// Unplug the newest of three holds, then let go of a non-current older one: the take-over delay still stands.
static void testUnplugThenOlderRelease() {
    NavInput n;
    n.padIntent(1, SDL_GAMEPAD_BUTTON_DPAD_LEFT, Intent::Left, true, 0);
    n.padIntent(2, SDL_GAMEPAD_BUTTON_DPAD_UP, Intent::Up, true, 10);
    n.padIntent(3, SDL_GAMEPAD_BUTTON_DPAD_DOWN, Intent::Down, true, 20);
    CHECK(n.take(20) == (Intents{Intent::Left, Intent::Up, Intent::Down}));
    n.forgetPad(3);                                                          // Up (pad 2) is current again
    n.padIntent(1, SDL_GAMEPAD_BUTTON_DPAD_LEFT, Intent::Left, false, 30);   // older, not current
    CHECK(n.take(100).empty() && n.take(449).empty());                       // fresh delay from the first take
    CHECK(n.take(450) == Intents{Intent::Up});
}

static void testPadTriggerEdges() {
    NavInput n;
    n.padTrigger(1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 9000, 0);
    CHECK(n.take(0) == Intents{Intent::PageNext});
    n.padTrigger(1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 5000, 10);   // between off and on: still on
    CHECK(n.take(10).empty() && n.take(2000).empty());                              // and no repeat
    n.padTrigger(1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 9000, 20);
    CHECK(n.take(20).empty());
    n.padTrigger(1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 3000, 30);
    n.padTrigger(1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 9000, 40);
    CHECK(n.take(40) == Intents{Intent::PageNext});
    // Pad 2's trigger is its own latch.
    n.padTrigger(2, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 9000, 50);
    CHECK(n.take(50) == Intents{Intent::PageNext});
    // A different axis of the same pad too; and an unmapped axis (None) is ignored.
    n.padTrigger(1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, Intent::PagePrev, 9000, 60);
    CHECK(n.take(60) == Intents{Intent::PagePrev});
    n.padTrigger(1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, Intent::None, 0, 65);
    n.padTrigger(1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, Intent::None, 20000, 70);
    CHECK(n.take(70).empty());
}

static void testPadTriggerForgetAndReset() {
    NavInput n;
    n.padTrigger(4, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 9000, 0);
    CHECK(n.take(0) == Intents{Intent::PageNext});
    n.forgetPad(4);   // replugged with the trigger resting low: the next pull is a fresh edge
    n.padTrigger(4, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 9000, 10);
    CHECK(n.take(10) == Intents{Intent::PageNext});
    n.reset();
    n.padTrigger(4, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Intent::PageNext, 9000, 20);
    CHECK(n.take(20) == Intents{Intent::PageNext});
}

// The old entry points are the new ones with pad 0 and the Xbox table.
static void testLegacyEntryPoints() {
    NavInput a, b;
    a.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 0);
    b.padIntent(0, SDL_GAMEPAD_BUTTON_DPAD_DOWN, intentForButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN), true, 0);
    CHECK(a.take(0) == b.take(0) && a.take(400) == b.take(400));
    a.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, 410);
    b.padIntent(0, SDL_GAMEPAD_BUTTON_DPAD_DOWN, Intent::Down, false, 410);
    CHECK(a.take(5000).empty() && b.take(5000).empty());
    CHECK(Intent::Search != Intent::PageNext && Intent::AcceptLong != Intent::Accept);
}

int main() {
    testBriefSequence();
    testDpadRepeat();
    testLastWasRepeat();
    testNoRepeatForButtons();
    testStick();
    testStickDirections();
    testTriggers();
    testNewestWins();
    testFallback();
    testKeyboard();
    testReset();
    testTables();
    testTriggerLabels();
    testLongAcceptTap();
    testLongAcceptHold();
    testLongAcceptReleaseBeforeTake();
    testLongAcceptUnseenRelease();
    testLongAcceptOffUnchanged();
    testLongAcceptScope();
    testTwoPadsSameButton();
    testUnplugMidHold();
    testUnplugHandsRepeatBack();
    testUnplugThenOlderRelease();
    testPadTriggerEdges();
    testPadTriggerForgetAndReset();
    testLegacyEntryPoints();
    std::printf("tv_nav_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

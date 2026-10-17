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

int main() {
    testBriefSequence();
    testDpadRepeat();
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
    std::printf("tv_nav_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

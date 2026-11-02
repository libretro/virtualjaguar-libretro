// SwapGuard (swap_guard.h): when the swap interval may be 1 (#1081).  No SDL, no display.
#include "swap_guard.h"

#include <cstdio>

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

using vj::SwapGuard;

void awakeAtStart() {
    SwapGuard g;
    g.contextCreated(false);
    CHECK(g.syncAllowed(true), "an awake display with vsync on syncs");
    CHECK(!g.syncAllowed(false), "vsync off never syncs");
    CHECK(!g.update(false), "no change while awake");
    CHECK(g.update(true), "sleeping is a change");
    CHECK(!g.syncAllowed(true), "a sleeping display must not sync: the swap would block");
    CHECK(!g.update(true), "still asleep is not a change");
    CHECK(g.update(false), "waking is a change");
    CHECK(g.syncAllowed(true), "vsync returns on wake when the link was made awake");
    CHECK(!g.syncAllowed(false), "...unless the user has it off");
    CHECK(!g.deadLink(), "the link is alive");
}

void asleepAtStart() {
    SwapGuard g;
    g.contextCreated(true);
    CHECK(g.deadLink(), "a context made on a sleeping display has a dead link");
    CHECK(!g.syncAllowed(true), "asleep at start: no sync");
    CHECK(!g.update(true), "the start state is already asleep: no change to report");
    CHECK(g.update(false), "wake is reported");
    CHECK(!g.syncAllowed(true), "a dead link never syncs again, awake or not");
    CHECK(g.update(true), "sleep again is reported");
    CHECK(g.update(false), "and wake again");
    CHECK(!g.syncAllowed(true), "still dead");
}

void defaults() {
    SwapGuard g;   // no contextCreated(): headless / before the window
    CHECK(g.syncAllowed(true), "default is the user's setting");
    CHECK(!g.asleep() && !g.deadLink(), "default state");
}

}  // namespace

int main() {
    awakeAtStart();
    asleepAtStart();
    defaults();
    if (g_fail) {
        std::printf("swap_guard_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("swap_guard_test: PASS\n");
    return 0;
}

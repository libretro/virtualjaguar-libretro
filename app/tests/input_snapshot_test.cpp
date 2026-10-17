// InputSnapshot (input_snapshot.h): the newest input from the UI thread to the core's input poll on the emulation
// thread (#967).  No SDL window, no core.
//
//   vj_input_snapshot_test
#include "input_snapshot.h"

#include "libretro.h"

#include <atomic>
#include <cstdio>
#include <thread>

using namespace vj;

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

CoreInput holding(Ctl c) {
    CoreInput in;
    in.beginFrame();
    ControlSet s;
    s.set(c);
    in.addControls(0, s);
    return in;
}

CoreInput released() {
    CoreInput in;
    in.beginFrame();
    return in;
}

CoreInput moved(int dx) {
    CoreInput in;
    in.beginFrame();
    in.addMouse(0, dx, 0, 0);
    return in;
}

int mouseX(const CoreInput& in) { return in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X); }

void testNextFrameSeesIt() {
    InputSnapshot s;
    s.publish(holding(C_A), 1000);
    CoreInput out;
    CHECK(s.take(out) == 1000, "the event's timestamp comes with the first take");
    CHECK(out.pad(0) == holding(C_A).pad(0) && out.pad(0) != 0, "the next frame sees A held");
    CHECK(s.take(out) == 0, "a second take: no new event");
    CHECK(out.pad(0) == holding(C_A).pad(0), "and A is still held (state, not an edge)");
}

void testNewestWins() {
    InputSnapshot s;
    s.publish(holding(C_A), 0);
    s.publish(released(), 0);
    CoreInput out;
    s.take(out);
    CHECK(out.pad(0) == 0, "the newest state wins: released");
}

void testOldestEventKept() {
    InputSnapshot s;
    s.publish(holding(C_A), 100);
    s.publish(holding(C_B), 200);
    CoreInput out;
    CHECK(s.take(out) == 100, "the latency is measured from the oldest event not yet taken");
    s.publish(holding(C_B), 300);
    CHECK(s.take(out) == 300, "then the next one");
}

void testMotionOnce() {
    InputSnapshot s;
    CoreInput out;
    s.publish(moved(5), 0);
    s.publish(moved(3), 0);
    s.take(out);
    CHECK(mouseX(out) == 8, "two publishes before a frame: both movements arrive (%d)", mouseX(out));
    s.take(out);
    CHECK(mouseX(out) == 0, "a second frame with no new motion moves nothing (%d)", mouseX(out));
    s.publish(moved(2), 0);
    s.take(out);
    CHECK(mouseX(out) == 2, "then the next movement alone (%d)", mouseX(out));
}

void testTakenSince() {
    InputSnapshot s;
    CoreInput out;
    s.publish(released(), 0);
    s.take(out);
    s.take(out);
    CHECK(s.takenSince() == 2, "two frames took input since the UI last asked");
    CHECK(s.takenSince() == 0, "and none since");
}

void testConcurrentMotion() {
    InputSnapshot s;
    const int kMoves = 20000;
    std::atomic<bool> done{false};
    std::thread ui([&] {
        for (int i = 0; i < kMoves; i++) s.publish(moved(1), 0);
        done = true;
    });
    long total = 0;
    CoreInput out;
    while (!done.load()) {
        s.take(out);
        total += mouseX(out);
    }
    ui.join();
    s.take(out);
    total += mouseX(out);
    CHECK(total == kMoves, "every movement reaches the core exactly once (%ld of %d)", total, kMoves);
}

}  // namespace

int main() {
    testNextFrameSeesIt();
    testNewestWins();
    testOldestEventKept();
    testMotionOnce();
    testTakenSince();
    testConcurrentMotion();
    std::printf(g_fail ? "input_snapshot_test: %d FAILED\n" : "input_snapshot_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}

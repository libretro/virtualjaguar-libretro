// InputSnapshot (input_snapshot.h): the newest input from the UI thread to the core's input poll on the emulation
// thread (#967).  No SDL window, no core.
//
//   vj_input_snapshot_test
#include "input_snapshot.h"

#include "libretro.h"

#include <atomic>
#include <chrono>
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

void testSequence() {
    InputSnapshot s;
    CoreInput out;
    uint64_t seq = 99;
    s.take(out, &seq);
    CHECK(seq == 0, "nothing published yet: sequence 0 (%llu)", (unsigned long long)seq);
    const uint64_t a = s.publish(released(), 5);
    const uint64_t b = s.publish(released(), 0);
    CHECK(a == 1 && b == 2, "publishes are numbered from 1 (%llu, %llu)", (unsigned long long)a, (unsigned long long)b);
    s.take(out, &seq);
    CHECK(seq == 2, "a take reports the newest publish it handed out (%llu)", (unsigned long long)seq);
    s.take(out, &seq);
    CHECK(seq == 2, "and again with nothing new (%llu)", (unsigned long long)seq);
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

ControlSet pad(Ctl c) {
    ControlSet s;
    s.set(c);
    return s;
}

void testAgentChannel() {
    InputSnapshot s;
    AgentProgram p, got;
    CHECK(!s.takeAgent(got), "nothing published: false");
    p.gen = 1;
    p.lineage = 1;
    p.enabled = true;
    p.steps = {{pad(C_B), 3}};
    s.publishAgent(p);
    CHECK(s.takeAgent(got) && got.gen == 1 && got.steps.size() == 1 && got.steps[0].frames == 3, "first take");
    CHECK(!s.takeAgent(got), "same gen: false");
    p.gen = 2;
    s.publishAgent(p);
    p.gen = 3;
    p.hold = pad(C_A);
    s.publishAgent(p);
    CHECK(s.takeAgent(got) && got.gen == 3 && got.hold == pad(C_A), "two publishes between takes: the newest only");
    AgentProgress r;
    r.gen = 3;
    r.lineage = 1;
    r.played = 2;
    r.pressed = pad(C_B);
    s.reportAgent(r);
    CHECK(s.agentProgress().played == 2 && s.agentProgress().pressed == pad(C_B), "progress round trip");
}

// The UI publishes programs whose fields are tied to the gen (steps.size() == gen % 7, hold.bits == gen); the emulation
// thread must never see a torn mix of two publishes, and gen never goes backwards.
void testAgentTear() {
    InputSnapshot s;
    const uint64_t kGens = 20000;
    std::thread ui([&] {
        for (uint64_t g = 1; g <= kGens; g++) {
            AgentProgram p;
            p.gen = g;
            p.lineage = g % 3;
            p.hold.bits = g;
            p.steps.assign(g % 7, AgentStep{pad(C_B), (unsigned)(g % 5) + 1});
            s.publishAgent(p);
        }
    });
    uint64_t last = 0, takes = 0;
    bool torn = false, backwards = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    AgentProgram got;
    while (last < kGens && std::chrono::steady_clock::now() < deadline) {
        if (s.takeAgent(got)) {
            takes++;
            if (got.steps.size() != got.gen % 7 || got.hold.bits != got.gen || got.lineage != got.gen % 3) torn = true;
            if (got.gen < last) backwards = true;
            last = got.gen;
            AgentProgress r;
            r.gen = got.gen;
            r.played = (unsigned)takes;
            s.reportAgent(r);
        }
    }
    ui.join();
    if (last < kGens && s.takeAgent(got)) last = got.gen;   // the final publish can land after the last poll
    CHECK(!torn, "every taken program is internally consistent");
    CHECK(!backwards, "gen never decreases");
    CHECK(last == kGens, "the final take has the last gen (%llu)", (unsigned long long)last);
    CHECK(s.agentProgress().gen <= kGens && s.agentProgress().played == takes, "progress is the last report");
}

}  // namespace

int main() {
    testNextFrameSeesIt();
    testNewestWins();
    testOldestEventKept();
    testMotionOnce();
    testTakenSince();
    testSequence();
    testConcurrentMotion();
    testAgentChannel();
    testAgentTear();
    std::printf(g_fail ? "input_snapshot_test: %d FAILED\n" : "input_snapshot_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}

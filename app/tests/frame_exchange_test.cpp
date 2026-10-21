// FrameExchange (frame_exchange.h): the triple buffer that hands finished frames from the emulation thread to the UI
// thread (#967).  No SDL, no GL, no core.
//
//   vj_frame_exchange_test
#include "frame_exchange.h"

#include <atomic>
#include <cstdio>
#include <thread>

using vj::FrameExchange;

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

// One frame whose every pixel is its serial, so a torn read shows up as a mixed frame.
void put(FrameExchange& x, uint64_t serial, unsigned w = 4, unsigned h = 2) {
    FrameExchange::Slot& s = x.writeBuffer();
    s.frame.width = w;
    s.frame.height = h;
    s.frame.serial = serial;
    s.frame.pixels.assign((size_t)w * h, (uint32_t)serial);
    s.aspect = 4.0f / 3.0f;
    x.publish();
}

bool uniform(const vj::Frame& f) {
    for (uint32_t p : f.pixels)
        if (p != (uint32_t)f.serial) return false;
    return true;
}

void testBasics() {
    FrameExchange x;
    CHECK(!x.takeNewest() && !x.current(), "nothing published: nothing to take");
    put(x, 1);
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 1 && uniform(s->frame), "the published frame is taken");
    CHECK(!x.takeNewest(), "nothing new: null (the UI draws current() again)");
    CHECK(x.current() && x.current()->frame.serial == 1, "current() is the last frame taken");
}

void testNewestWins() {
    FrameExchange x;
    put(x, 2);
    put(x, 3);
    put(x, 4);
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 4, "three published, the newest is taken (got %llu)",
          s ? (unsigned long long)s->frame.serial : 0ull);
}

void testReaderSlotStable() {
    FrameExchange x;
    put(x, 10);
    const FrameExchange::Slot* held = x.takeNewest();
    for (uint64_t k = 11; k <= 30; k++) put(x, k);
    CHECK(held && held->frame.serial == 10 && uniform(held->frame), "the writer never writes the slot the reader holds");
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 30, "then the newest of the twenty");
}

void testContentSwitch() {
    FrameExchange x;
    put(x, 40);
    x.clear();
    CHECK(!x.takeNewest() && !x.current(), "a frame published before the switch is never shown");
    FrameExchange::Slot& begun = x.writeBuffer();   // the emulation thread had started this one...
    begun.frame.width = 4;
    begun.frame.height = 2;
    begun.frame.serial = 41;
    begun.frame.pixels.assign(8, 41u);
    x.clear();                                      // ...when the load call switched the content
    x.publish();
    CHECK(!x.takeNewest(), "a frame begun before the switch is never shown either");
    put(x, 42);
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 42, "the new game's first frame is shown");
    CHECK(x.current() && x.current()->epoch == x.epoch(), "current() belongs to the new epoch");
}

void testNoTearing() {
    FrameExchange x;
    const uint64_t kFrames = 3000;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (uint64_t k = 1; k <= kFrames; k++) put(x, k, 160, 120);
        done = true;
    });
    uint64_t last = 0, taken = 0, torn = 0, backwards = 0;
    for (;;) {
        const bool finished = done.load();
        const FrameExchange::Slot* s = x.takeNewest();
        if (s) {
            taken++;
            if (!uniform(s->frame)) torn++;
            if (s->frame.serial <= last) backwards++;
            last = s->frame.serial;
        }
        if (finished && !s) break;
    }
    writer.join();
    CHECK(torn == 0, "%llu of %llu taken frames were torn", (unsigned long long)torn, (unsigned long long)taken);
    CHECK(backwards == 0, "serials went backwards %llu times", (unsigned long long)backwards);
    CHECK(last == kFrames, "the last frame taken is the last published (%llu)", (unsigned long long)last);
}

}  // namespace

int main() {
    testBasics();
    testNewestWins();
    testReaderSlotStable();
    testContentSwitch();
    testNoTearing();
    std::printf(g_fail ? "frame_exchange_test: %d FAILED\n" : "frame_exchange_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}

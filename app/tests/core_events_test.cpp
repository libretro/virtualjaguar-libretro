// CoreEventQueue (emu_snapshot.h): the core-event stream from the emulation thread to the UI thread (#967 phase 2).
// No core, no SDL.
//
//   vj_core_events_test
#include "emu_snapshot.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using vj::CoreEvent;
using vj::CoreEventQueue;

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

CoreEvent logLine(const std::string& t) {
    CoreEvent e;
    e.kind = CoreEvent::Kind::Log;
    e.text = t;
    return e;
}
CoreEvent event(unsigned type, unsigned a) {
    CoreEvent e;
    e.kind = CoreEvent::Kind::Event;
    e.type = type;
    e.a = a;
    return e;
}
CoreEvent option(const std::string& k, const std::string& v) {
    CoreEvent e;
    e.kind = CoreEvent::Kind::UserOption;
    e.text = k;
    e.value = v;
    return e;
}

void testOrder() {
    CoreEventQueue q;
    q.push(logLine("one"));
    q.push(event(1, 7));
    q.push(option("k", "v"));
    q.push(logLine("two"));
    std::vector<CoreEvent> got = q.take();
    CHECK(got.size() == 4, "all four come out (%zu)", got.size());
    if (got.size() == 4) {
        CHECK(got[0].text == "one" && got[1].type == 1 && got[1].a == 7 && got[2].text == "k" && got[2].value == "v" &&
                  got[3].text == "two",
              "in the order they were pushed");
    }
    CHECK(q.take().empty(), "take() empties the queue");
}

// A burst of log lines (a chatty core, a UI that stalls) must never cost an event or an option write.
void testBoundedLogs() {
    CoreEventQueue q;
    for (int i = 0; i < 300; i++) q.push(logLine("a" + std::to_string(i)));
    q.push(event(1, 1));   // LINK_UP in the middle of the burst
    q.push(option("k", "v"));
    for (int i = 0; i < 300; i++) q.push(logLine("b" + std::to_string(i)));
    std::vector<CoreEvent> got = q.take();
    size_t logs = 0, events = 0, options = 0;
    for (const CoreEvent& e : got) {
        if (e.kind == CoreEvent::Kind::Log) logs++;
        else if (e.kind == CoreEvent::Kind::Event) events++;
        else options++;
    }
    CHECK(logs == CoreEventQueue::kMaxLogs, "logs capped at %zu (%zu)", CoreEventQueue::kMaxLogs, logs);
    CHECK(events == 1 && options == 1, "the event and the option write survive (%zu, %zu)", events, options);
    CHECK(q.droppedLogs() == 600 - CoreEventQueue::kMaxLogs, "the oldest lines were dropped (%zu)", q.droppedLogs());
    CHECK(!got.empty() && got.back().text == "b299", "the newest line is kept");
    bool oldestGone = true;
    for (const CoreEvent& e : got)
        if (e.text == "a0") oldestGone = false;
    CHECK(oldestGone, "the oldest line is the one dropped");
}

void testContentSwitch() {
    CoreEventQueue q;
    q.push(logLine("old game"));
    q.push(event(2, 0));
    q.push(option("k", "v"));
    q.dropLogsAndEvents();
    std::vector<CoreEvent> got = q.take();
    CHECK(got.size() == 1 && got[0].kind == CoreEvent::Kind::UserOption, "a content switch keeps only option writes");
}

// One producer (the emulation thread), one consumer (the UI thread): nothing lost, nothing reordered.
void testTwoThreads() {
    CoreEventQueue q;
    const unsigned kN = 20000;
    std::thread producer([&] {
        for (unsigned i = 0; i < kN; i++) q.push(event(1, i));
    });
    unsigned next = 0;
    bool ordered = true;
    for (int spins = 0; next < kN && spins < 2000000; spins++) {
        for (const CoreEvent& e : q.take()) {
            if (e.a != next) ordered = false;
            next++;
        }
    }
    producer.join();
    for (const CoreEvent& e : q.take()) {
        if (e.a != next) ordered = false;
        next++;
    }
    CHECK(next == kN, "every event arrived (%u of %u)", next, kN);
    CHECK(ordered, "in order");
}

}  // namespace

int main() {
    testOrder();
    testBoundedLogs();
    testContentSwitch();
    testTwoThreads();
    std::printf("core_events_test: %s\n", g_fail ? "FAILED" : "OK");
    return g_fail ? 1 : 0;
}

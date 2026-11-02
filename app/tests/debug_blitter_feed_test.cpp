// Unit tests for the Blitter log window's data feed (app/src/debug/blitter_feed.*): the launch list and the live
// registers read from published view buffers over the mock core behind a counting interface, the core's log switched
// through marshalled calls, and the guards against a stale buffer (a restart, a closed window, another core).  No
// window, no core.
#include "blitter_feed.h"
#include "debug_mock.h"
#include "debug_ui_fixture.h"

#include <cstdio>
#include <functional>
#include <vector>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

static void testLifecycle() {
    CountingIface::reset();
    UiFixture f;
    blit::LogFeed feed;
    feed.setLog(f.s, true);
    CHECK(feed.logOn() && f.mock.blitLogOn && f.coreCalls == 1);
    feed.setLog(f.s, true);
    CHECK(f.coreCalls == 1);                                              // no change, no call
    feed.update(f.s, false);  feed.update(f.s, false);                    // the first absorbs (inline fill), the second re-requests since=5
    CHECK(feed.tracker.total() == 5 && feed.tracker.rows().size() == 5 && feed.haveRegs());
    CHECK(feed.regImage().size() == blit::kRegBytes);
    f.mock.addBlit(0x01800001u, 0x00010002);
    f.s.requestRefresh();
    feed.update(f.s, false);
    CHECK(feed.tracker.total() == 6 && feed.tracker.rows().back().seq == 5);   // 6 rows, no duplicates
    f.mock.addBlit(0x01800009u, 0x00010003);  f.s.requestRefresh();
    feed.update(f.s, true);                                               // frozen: requests but does not absorb
    CHECK(feed.tracker.total() == 6);
    feed.update(f.s, false);  feed.update(f.s, false);                    // unfrozen: catches up with launch 6, then re-requests
    CHECK(feed.tracker.total() == 7 && feed.tracker.rows().back().seq == 6);
    const UiFixture::Mark m = f.mark();
    for (int i = 0; i < 30; i++) feed.update(f.s, false);
    CHECK(f.quietSince(m) && CountingIface::offCore() == 0);
    const unsigned k = f.coreCalls;
    feed.clear(f.s);
    CHECK(f.coreCalls == k + 1 && feed.tracker.total() == 0 && f.mock.blitLogOn);   // one wait: off then on
    f.mock.blitLogOn = false;                                             // a new core: its log starts off
    f.s.setIface(f.counted.iface());                                      // (epoch bump)
    feed.setLog(f.s, true);
    CHECK(f.mock.blitLogOn && feed.logOn());                              // re-enabled on the new core
    feed.setLog(f.s, false);
    CHECK(!feed.logOn() && !f.mock.blitLogOn);
    CHECK(CountingIface::offCore() == 0);
}

// A deferred emulation thread over the fixture's session (what a threaded App does): posts queue, drain() runs them.
struct DeferredCore {
    std::vector<std::function<void()>> q;
    explicit DeferredCore(UiFixture& f) {
        f.s.corePost = [this](std::function<void()> fn) { q.push_back(std::move(fn)); };
    }
    void drain() {
        std::vector<std::function<void()>> work = std::move(q);
        q.clear();
        for (auto& fn : work) { CountingIface::OnCore g; fn(); }
    }
};

static void testThreadedFill() {
    UiFixture f;
    DeferredCore d(f);
    blit::LogFeed feed;
    feed.setLog(f.s, true);
    feed.update(f.s, false);                                   // states the requests and queues ONE fill
    CHECK(feed.tracker.total() == 0 && !feed.haveRegs() && d.q.size() == 1);
    feed.update(f.s, false);                                   // still outstanding: not queued again
    CHECK(d.q.size() == 1);
    d.drain();
    feed.update(f.s, false);
    CHECK(feed.tracker.total() == 5 && feed.haveRegs() && CountingIface::offCore() == 0);
}

static void testClearRetiresTheOldBuffer() {
    // A buffer filled for `since = 0` before the log restarted must not be taken as the answer to the same
    // `since = 0` after it: the restart changes the request, which empties the front buffer.
    UiFixture f;
    DeferredCore d(f);
    blit::LogFeed feed;
    feed.setLog(f.s, true);
    feed.update(f.s, false);  d.drain();                       // filled: launches 0..4, not read yet
    feed.clear(f.s);                                           // the restart (a no-launch list is the worst case: since stays 0)
    feed.update(f.s, false);
    CHECK(feed.tracker.total() == 0 && feed.tracker.rows().empty());   // the pre-restart buffer was not absorbed
    d.drain();
    feed.update(f.s, false);
    CHECK(feed.tracker.total() == 5);                          // the post-restart fill is
}

static void testReleaseWithAFillQueuedLeavesNoRequest() {
    UiFixture f;
    DeferredCore d(f);
    blit::LogFeed feed;
    feed.setLog(f.s, true);
    feed.update(f.s, false);                                   // queues the fill
    CHECK(f.s.views().count() == 2);
    feed.release(f.s);                                         // the window closes before it ran
    CHECK(!feed.haveRegs() && f.s.views().count() == 2);       // the clear is posted behind the fill, not made here
    d.drain();
    CHECK(f.s.views().count() == 0);
    feed.release(f.s);  feed.release(f.s);                     // a closed window calls it every frame: nothing to do
    CHECK(d.q.empty());
    feed.update(f.s, false);  d.drain();  feed.update(f.s, false);   // reopening works from scratch
    CHECK(feed.tracker.total() == 5 && feed.haveRegs() && f.s.views().count() == 2);
}

static void testAnotherCoreForgetsEverything() {
    UiFixture f;
    DeferredCore d(f);
    blit::LogFeed feed;
    feed.setLog(f.s, true);
    feed.update(f.s, false);  d.drain();  feed.update(f.s, false);
    CHECK(feed.tracker.total() == 5 && feed.haveRegs());
    f.s.views().bumpEpoch();                                   // the content switched
    const unsigned calls = f.coreCalls;
    feed.update(f.s, false);                                   // nothing of the old game is shown...
    CHECK(feed.tracker.total() == 0 && !feed.haveRegs() && !feed.logOn());
    feed.setLog(f.s, false);                                   // ...and closing does not call the new core
    CHECK(f.coreCalls == calls);
    feed.setLog(f.s, true);                                    // reopening switches its log on
    CHECK(f.coreCalls == calls + 1 && feed.logOn());
}

int main() {
    testLifecycle();
    testThreadedFill();
    testClearRetiresTheOldBuffer();
    testReleaseWithAFillQueuedLeavesNoRequest();
    testAnotherCoreForgetsEverything();
    std::printf("debug_blitter_feed_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

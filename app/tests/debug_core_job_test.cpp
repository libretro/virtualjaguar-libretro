// CoreJob (app/src/debug/core_job.h): a function posted to the emulation thread whose result the UI takes later.
// The "emulation thread" here is a real second thread draining a captured queue, so ThreadSanitizer sees the hand-off.
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

#include "core_job.h"
#include "debug_session.h"

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

int main() {
    DebugSession s;
    // A deferred corePost: the job has not run until the "emulation thread" drains the queue.
    std::vector<std::function<void()>> q;
    s.corePost = [&](std::function<void()> fn) { q.push_back(std::move(fn)); };
    CoreJob<int> job;
    job.submit(s, [](const vj_debug_iface*) { return 7; });
    int out = 0;
    CHECK(job.pending() && !job.take(out) && q.size() == 1);
    std::thread emu([&] {
        for (auto& f : q) f();   // runs on another thread
    });
    emu.join();
    CHECK(!job.pending() && job.take(out) && out == 7 && !job.take(out));

    job.submit(s, [](const vj_debug_iface*) { return 1; });
    job.submit(s, [](const vj_debug_iface*) { return 2; });
    CHECK(job.pending());
    for (size_t i = 1; i < q.size(); i++) q[i]();   // q[0] is the already-run job; the two new ones run in order
    CHECK(!job.pending());
    CHECK(job.take(out) && out == 2);                // newest wins
    CHECK(!job.take(out));

    // A job outliving its CoreJob: the posted lambda owns the shared state.
    {
        CoreJob<int> gone;
        gone.submit(s, [](const vj_debug_iface*) { return 5; });
    }
    q.back()();

    s.corePost = nullptr;   // inline: runs inside submit()
    job.submit(s, [](const vj_debug_iface* i) { return i == nullptr ? 9 : 8; });
    CHECK(!job.pending());
    CHECK(job.take(out) && out == 9);   // no core attached: the interface is null

    std::printf("debug_core_job_unit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

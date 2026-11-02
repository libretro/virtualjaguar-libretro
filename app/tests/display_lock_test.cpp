// Mode B's lock verdict (display_lock.h, #1038): which displays lock, every fallback reason, and the hysteresis.
//
//   vj_display_lock_test
#include "display_lock.h"

#include <cmath>
#include <cstdio>

using vj::DisplayLock;
using vj::DisplayLockInput;
using vj::DisplayLockVerdict;
using vj::LockReason;
using vj::PacingMode;

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

const double kNtscHz = 60.05445;   // libretro.c retro_get_system_av_info (JTRM field rates)
const double kPalHz = 50.08013;

DisplayLockInput good(double displayHz, double coreHz) {
    DisplayLockInput in;
    in.matchDisplay = true;
    in.vsync = true;
    in.latchLocked = true;
    in.displayPeriod = 1.0 / displayHz;
    in.corePeriod = 1.0 / coreHz;
    return in;
}

// Evaluate `in` for n swaps; the last verdict.
DisplayLockVerdict run(DisplayLock& lock, const DisplayLockInput& in, unsigned n) {
    DisplayLockVerdict v;
    for (unsigned i = 0; i < n; i++) v = lock.evaluate(in);
    return v;
}

void testLockTable() {
    struct Row {
        double displayHz, coreHz;
        unsigned k;   // 0 = falls back
        const char* what;
    };
    const Row rows[] = {
        {60.00, kNtscHz, 1, "NTSC on 60 Hz"},      {59.94, kNtscHz, 1, "NTSC on 59.94 Hz"},
        {120.00, kNtscHz, 2, "NTSC on 120 Hz"},    {119.88, kNtscHz, 2, "NTSC on 119.88 Hz"},
        {50.00, kPalHz, 1, "PAL on 50 Hz"},        {100.00, kPalHz, 2, "PAL on 100 Hz"},
        {144.00, kNtscHz, 0, "NTSC on 144 Hz"},    {144.00, kPalHz, 0, "PAL on 144 Hz"},
        {75.00, kNtscHz, 0, "NTSC on 75 Hz"},      {75.00, kPalHz, 0, "PAL on 75 Hz"},
        {60.00, kPalHz, 0, "PAL on 60 Hz"},        {120.00, kPalHz, 0, "PAL on 120 Hz"},
        {50.00, kNtscHz, 0, "NTSC on 50 Hz"},      {240.00, kNtscHz, 4, "NTSC on 240 Hz"},
    };
    for (const Row& r : rows) {
        DisplayLock lock;
        const DisplayLockVerdict v = run(lock, good(r.displayHz, r.coreHz), DisplayLock::kEnterSwaps);
        if (r.k) {
            CHECK(v.mode == PacingMode::B && v.reason == LockReason::Locked && v.k == r.k,
                  "%s locks at k=%u (mode %s, reason %s, k=%u)", r.what, r.k, v.mode == PacingMode::B ? "B" : "A",
                  DisplayLock::name(v.reason), v.k);
            const double want = r.displayHz / r.k / r.coreHz;
            CHECK(std::fabs(v.baseRatio - want) < 1e-9, "%s: base ratio %.6f, want %.6f", r.what, v.baseRatio, want);
            CHECK(std::fabs(v.baseRatio - 1.0) < DisplayLock::kTolerance, "%s: base ratio %.6f within 0.3%%", r.what,
                  v.baseRatio);
        } else {
            CHECK(v.mode == PacingMode::A && v.reason == LockReason::Ratio && v.baseRatio == 1.0,
                  "%s falls back for the ratio (mode %s, reason %s)", r.what, v.mode == PacingMode::B ? "B" : "A",
                  DisplayLock::name(v.reason));
        }
    }
    unsigned k = 9;
    double base = 0;
    CHECK(!DisplayLock::ratioLocks(0, 1.0 / kNtscHz, k, base) && k == 0 && base == 1.0,
          "an unmeasured display period does not lock");
    CHECK(DisplayLock::ratioLocks(1.0 / 60.0, 1.0 / kNtscHz, k, base) && std::fabs(base - 60.0 / kNtscHz) < 1e-12,
          "60 Hz on NTSC: base ratio (display / k) / core = %.6f", base);
}

void testEachReason() {
    struct Case {
        void (*breakIt)(DisplayLockInput&);
        LockReason want;
        const char* what;
    };
    const Case cases[] = {
        {[](DisplayLockInput& in) { in.matchDisplay = false; }, LockReason::Setting, "Jaguar rate chosen"},
        {[](DisplayLockInput& in) { in.vsync = false; }, LockReason::NoVsync, "vsync off"},
        {[](DisplayLockInput& in) { in.latchLocked = false; }, LockReason::Unlocked, "latch unlocked"},
        {[](DisplayLockInput& in) { in.displayPeriod = 1.0 / 144.0; }, LockReason::Ratio, "144 Hz"},
        {[](DisplayLockInput& in) { in.displayPeriod = 0; }, LockReason::Ratio, "no measured period"},
        {[](DisplayLockInput& in) { in.fastForward = true; }, LockReason::FastForward, "fast-forward"},
        {[](DisplayLockInput& in) { in.linked = true; }, LockReason::Link, "link play"},
    };
    for (const Case& c : cases) {
        DisplayLockInput in = good(60.0, kNtscHz);
        c.breakIt(in);
        DisplayLock lock;
        const DisplayLockVerdict v = run(lock, in, DisplayLock::kEnterSwaps * 2);
        CHECK(v.mode == PacingMode::A && v.reason == c.want && v.baseRatio == 1.0, "%s alone forces A with reason %s (got %s)",
              c.what, DisplayLock::name(c.want), DisplayLock::name(v.reason));
    }
}

void testHysteresis() {
    const DisplayLockInput in = good(60.0, kNtscHz);
    DisplayLock lock;
    DisplayLockVerdict v = run(lock, in, DisplayLock::kEnterSwaps - 1);
    CHECK(v.mode == PacingMode::A && v.reason == LockReason::Settling, "%u good swaps stay in A (settling), got %s",
          DisplayLock::kEnterSwaps - 1, DisplayLock::name(v.reason));
    v = lock.evaluate(in);
    CHECK(v.mode == PacingMode::B, "swap %u enters B", DisplayLock::kEnterSwaps);

    DisplayLockInput bad = in;
    bad.latchLocked = false;
    v = lock.evaluate(bad);
    CHECK(v.mode == PacingMode::A && v.reason == LockReason::Unlocked, "one swap without the latch leaves B at once");
    v = run(lock, in, DisplayLock::kEnterSwaps - 1);
    CHECK(v.mode == PacingMode::A, "after leaving, B waits a full %u swaps again", DisplayLock::kEnterSwaps);
    v = lock.evaluate(in);
    CHECK(v.mode == PacingMode::B, "and re-enters on swap %u", DisplayLock::kEnterSwaps);

    // Fast-forward and link play switch at once both ways: the display streak keeps counting under them.
    DisplayLockInput ff = in;
    ff.fastForward = true;
    v = run(lock, ff, 30);
    CHECK(v.mode == PacingMode::A && v.reason == LockReason::FastForward, "fast-forward forces A at once");
    v = lock.evaluate(in);
    CHECK(v.mode == PacingMode::B, "B resumes on the first swap after fast-forward");
    DisplayLockInput link = in;
    link.linked = true;
    CHECK(lock.evaluate(link).reason == LockReason::Link, "link play forces A at once");
    CHECK(lock.evaluate(in).mode == PacingMode::B, "B resumes on the first swap after the link closes");

    // The setting off counts nothing against the display: choosing Match display again locks at once.
    DisplayLockInput off = in;
    off.matchDisplay = false;
    CHECK(run(lock, off, 10).reason == LockReason::Setting, "Jaguar rate chosen: A");
    CHECK(lock.evaluate(in).mode == PacingMode::B, "Match display again: B at once (the display never stopped locking)");

    // A different multiple is a different clock: 60 -> 120 Hz settles again.
    const DisplayLockInput at120 = good(120.0, kNtscHz);
    v = lock.evaluate(at120);
    CHECK(v.mode == PacingMode::A && v.reason == LockReason::Settling, "60 -> 120 Hz settles again (%s)",
          DisplayLock::name(v.reason));
    v = run(lock, at120, DisplayLock::kEnterSwaps - 1);
    CHECK(v.mode == PacingMode::B && v.k == 2, "then locks at k=2");

    lock.reset();
    v = lock.evaluate(at120);
    CHECK(v.mode == PacingMode::A && v.reason == LockReason::Settling, "reset() (a display event) settles again");
}

}  // namespace

int main() {
    testLockTable();
    testEachReason();
    testHysteresis();
    if (g_fail) {
        std::printf("display_lock_test: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("display_lock_test: all passed\n");
    return 0;
}

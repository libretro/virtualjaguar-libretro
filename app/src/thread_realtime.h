// The emulation thread's scheduling class (#967; the thread-side counterpart of #1026's UI-thread QoS raise).
//
// The frame loop is a periodic deadline job: one frame of work every 16.7 ms (20 ms in PAL), due on time.  A thread of
// the default class competes with every other runnable thread on the host, and on Apple silicon it can be placed on an
// efficiency core.  Measured on an M2 Max at a 1-minute load of 250-950 (the user's machine during a build storm):
// the same Doom gameplay frame cost 5.4 ms with a performance core of its own and 18-20 ms without one, with 58-86
// involuntary context switches per frame; the game ran at 54.6 of its 60.05 frames a second.  So the thread asks the OS
// to schedule it as what it is:
//
//   macOS    THREAD_TIME_CONSTRAINT_POLICY, the policy CoreAudio's I/O thread uses: period = one frame, computation =
//            30% of it, constraint = 60%, preemptible.  Real-time threads run ahead of every QoS class and, on Apple
//            silicon, on performance cores.  A real-time thread has no QoS class (a later QoS set is a no-op), so this
//            runs before (and instead of) the user-interactive QoS raise, which stays as the fallback.
//   Windows  THREAD_PRIORITY_TIME_CRITICAL, plus the MMCSS "Games" task when avrt.dll is present.
//   Linux    SCHED_RR at a low real-time priority.  Refused (EPERM) unless RLIMIT_RTPRIO allows it or the process is
//            started through rtkit; the thread then keeps the default class and the note says so.
//
// Never fatal: every failure leaves the thread as it was.  The outcome is a one-line note the app logs after
// "[emu] emulation thread on" (and EmuThread::realtimeNote() returns), so a report can say what the thread ran as.
// --emu-priority normal skips all of it for one run (A/B).
#pragma once

#include <cstdio>
#include <string>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/qos.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__linux__)
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#endif

namespace vj {
namespace rt {

struct Outcome {
    bool realtime = false;   // the OS accepted a real-time class for the calling thread
    std::string note;        // one line: what it runs as, or why not
};

// Ask the OS to schedule the calling thread as a real-time frame loop with this period.  The period is a hint for the
// deadline policies (macOS); the priority policies ignore it.
inline Outcome makeSelfRealtime(double periodSec) {
    if (!(periodSec > 0.0005) || !(periodSec < 0.1)) periodSec = 1.0 / 60.0;
#if defined(__APPLE__)
    mach_timebase_info_data_t tb{};
    if (mach_timebase_info(&tb) != KERN_SUCCESS || tb.numer == 0 || tb.denom == 0)
        return {false, "normal (mach_timebase_info failed)"};
    const double ticksPerSec = 1e9 * (double)tb.denom / (double)tb.numer;
    const auto ticks = [&](double sec) { return (uint32_t)(sec * ticksPerSec + 0.5); };
    // Start from no QoS class: the policy below replaces it, and a std::thread may have inherited one.
    pthread_set_qos_class_self_np(QOS_CLASS_UNSPECIFIED, 0);
    thread_time_constraint_policy_data_t p{};
    p.period = ticks(periodSec);
    p.computation = ticks(periodSec * 0.3);
    p.constraint = ticks(periodSec * 0.6);
    p.preemptible = TRUE;
    const kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                                               (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (kr != KERN_SUCCESS) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "normal (thread_policy_set time-constraint refused: kern_return %d)", (int)kr);
        return {false, buf};
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "real-time (macOS time-constraint: period %.1f ms, computation %.1f ms, constraint %.1f ms)",
                  periodSec * 1e3, periodSec * 0.3 * 1e3, periodSec * 0.6 * 1e3);
    return {true, buf};
#elif defined(_WIN32)
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "normal (SetThreadPriority TIME_CRITICAL failed: %lu)", (unsigned long)GetLastError());
        return {false, buf};
    }
    std::string note = "real-time (Windows THREAD_PRIORITY_TIME_CRITICAL";
    // MMCSS: the "Games" task class, through avrt.dll at run time so the binary does not need avrt.lib.  The library
    // handle stays open: MMCSS membership lasts as long as the thread does.
    typedef HANDLE(WINAPI * AvSetFn)(LPCWSTR, LPDWORD);
    if (HMODULE avrt = LoadLibraryExW(L"avrt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (AvSetFn fn = (AvSetFn)(void*)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW")) {
            DWORD idx = 0;
            note += fn(L"Games", &idx) ? " + MMCSS Games" : "; MMCSS Games refused";
        }
    }
    note += ")";
    return {true, note};
#elif defined(__unix__) || defined(__linux__)
    const int lo = sched_get_priority_min(SCHED_RR), hi = sched_get_priority_max(SCHED_RR);
    if (lo < 0 || hi < 0) return {false, "normal (SCHED_RR unavailable)"};
    sched_param sp{};
    sp.sched_priority = lo + (hi - lo) / 10;   // low real-time priority: above every normal thread, below audio servers
    const int rc = pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
    if (rc != 0)
        return {false, std::string("normal (SCHED_RR refused: ") + std::strerror(rc) +
                           "; allow it with RLIMIT_RTPRIO or run through rtkit)"};
    return {true, "real-time (SCHED_RR priority " + std::to_string(sp.sched_priority) + ")"};
#else
    return {false, "normal (no real-time scheduling on this platform)"};
#endif
}

}  // namespace rt
}  // namespace vj

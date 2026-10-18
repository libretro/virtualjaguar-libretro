// precise_sleep.h -- a short sleep that honours sub-tick durations (#967).  Windows: a high-resolution waitable
// timer (Windows 10 1803+), whatever the process's timer resolution -- MinGW's sleeps and timed waits otherwise
// round up to the ~15.6 ms scheduler tick, which held threaded fast-forward at normal speed on the MinGW runner.
// Falls back to a plain waitable timer, then Sleep(1).  Elsewhere: std::this_thread::sleep_for.
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace vj {

inline void preciseSleepNs(uint64_t ns) {
#ifdef _WIN32
    struct Timer {
        HANDLE h = nullptr;
        Timer() {
            h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (!h) h = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        ~Timer() {
            if (h) CloseHandle(h);
        }
    };
    thread_local Timer t;
    if (t.h) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ns / 100);   // relative, in 100 ns units
        if (due.QuadPart == 0) due.QuadPart = -1;
        if (SetWaitableTimer(t.h, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(t.h, INFINITE);
            return;
        }
    }
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
#endif
}

}  // namespace vj

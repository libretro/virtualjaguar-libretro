// The UI thread's timed event waits on macOS (#1026): the late latch's wait and the threaded loop's nap.
//
// SDL_WaitEventTimeout's timeout is the Cocoa run loop's own timeout, which Core Foundation delivers from a dispatch
// worker on the global queue for qos_class_main(): the QoS the main thread was launched with, which raising the thread's
// QoS later does not change (CFRunLoop.c __CFRunLoopRun; libpthread qos_class_main).  A LaunchServices launch gets
// user-interactive; a launch from a terminal gets the default QoS, and under CPU load that worker runs 3-20 ms late
// while the UI thread sleeps in the run loop past its deadline and misses the vblank.  (A CFRunLoopTimer on the main
// run loop does not help there either: from a terminal launch it fired ~3 ms late, every time.)
//
// LatchWaker ends the wait from a thread of its own at user-interactive QoS instead: it sleeps precisely until the
// deadline and pushes an SDL event, which wakes the UI thread's run loop at once.  SDL_WaitEventTimeout keeps a
// later timeout as a backstop.  The sleep is 1 ms slices, as in SDL_DelayPrecise: one long timed condition wait got
// 3-5 ms of timer coalescing outside an app launch.
//
// UI thread: arm(deadline) before the wait, disarm() after it; isWake(e) is the wake event (a late one from an
// earlier wait too, which callers drop).  The waker thread only pushes events.  It sees a new deadline between slices,
// so a wait armed while it is mid-slice can end up to ~1 ms late (SDL's timeout was 3-20 ms).  macOS only: elsewhere
// start() returns false and the plain wait is used.
#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#ifdef __APPLE__
#include <pthread.h>
#include <sys/qos.h>
#endif

namespace vj {

class LatchWaker {
public:
    static constexpr Sint32 kCode = 0x564A4C57;   // 'VJLW': an SDL_EVENT_USER with this code is a wake
    static constexpr Sint32 kBackstopMs = 5;      // SDL_WaitEventTimeout's own timeout, this far past the deadline
    static constexpr uint64_t kSliceNs = 1000000;   // the waker sleeps in slices this long, then SDL_DelayPrecise

    ~LatchWaker() { stop(); }

    // anyPlatform: the unit test runs it everywhere (the ThreadSanitizer job is Linux); the app uses it on macOS only.
    bool start(bool anyPlatform = false) {
#ifdef __APPLE__
        (void)anyPlatform;
#else
        if (!anyPlatform) return false;
#endif
        if (!thread_.joinable()) {
            quit_.store(false);
            thread_ = std::thread([this] { run(); });
        }
        return true;
    }
    void stop() {
        if (!thread_.joinable()) return;
        {
            std::lock_guard<std::mutex> lk(m_);
            quit_.store(true);
        }
        cv_.notify_one();
        thread_.join();
    }
    bool running() const { return thread_.joinable(); }

    void arm(uint64_t atNs) {   // SDL_GetTicksNS time
        {
            std::lock_guard<std::mutex> lk(m_);
            at_ = atNs;
            gen_.fetch_add(1);
        }
        cv_.notify_one();
    }
    void disarm() {
        std::lock_guard<std::mutex> lk(m_);
        at_ = 0;
        gen_.fetch_add(1);
    }
    static bool isWake(const SDL_Event& e) { return e.type == SDL_EVENT_USER && e.user.code == kCode; }

private:
    void run() {
#ifdef __APPLE__
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
        for (;;) {
            uint64_t at, g;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return quit_.load() || at_; });
                if (quit_.load()) return;
                at = at_;
                g = gen_.load();
            }
            const auto current = [&] { return gen_.load() == g && !quit_.load(); };
            uint64_t now = SDL_GetTicksNS();
            while (current() && now + kSliceNs < at) {
                SDL_DelayNS(kSliceNs);
                now = SDL_GetTicksNS();
            }
            if (!current()) continue;   // the wait ended on its own (an event), or a new one began
            if (at > now) SDL_DelayPrecise(at - now);
            {
                std::lock_guard<std::mutex> lk(m_);
                if (gen_.load() != g) continue;
                at_ = 0;
            }
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_EVENT_USER;
            e.user.code = kCode;
            SDL_PushEvent(&e);   // from another thread: SDL posts an NSEvent, which ends the Cocoa wait
        }
    }

    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic<bool> quit_{false};
    std::atomic<uint64_t> gen_{0};   // read without the lock while sleeping
    uint64_t at_ = 0;                // (under m_)
};

}  // namespace vj

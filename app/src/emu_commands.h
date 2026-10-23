// The emulation thread's command queue (#967): any thread post()s, the emulation thread drain()s at a frame boundary,
// in the order posted (per producer).  The same mutex carries the thread's timed wait, so a post or notify() wakes a
// sleeping emulation thread at once.  closeAndDrain() runs whatever is still queued -- including what arrives while
// it runs -- and then refuses more, so no command posted before the stop is ever lost.  There are two lanes: Frame
// commands are applied at a frame boundary; Debug commands are also served inside a halt (phase 3), and each lane is
// drained on its own so a halt can run the one without the other.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#if defined(__MINGW32__)
#include <thread>

#include "precise_sleep.h"
#endif

namespace vj {

class EmuCommands {
public:
    using Fn = std::function<void()>;
    using ErrorFn = std::function<void(const std::string&)>;
    using TimePoint = std::chrono::steady_clock::time_point;
    enum class Lane : uint8_t { Frame, Debug };   // Frame: applied at a frame boundary.  Debug: also inside a halt.

    // False once closed; `fn` is then left untouched (the caller may run it itself).
    bool post(Fn&& fn, Lane lane = Lane::Frame) {
        {
            std::lock_guard<std::mutex> l(mu_);
            if (closed_) return false;
            laneQ(lane).push_back(std::move(fn));
            wake_ = true;
            if (lane == Lane::Debug) wakeDbg_ = true;
        }
        cv_.notify_one();
        return true;
    }

    // Runs every command queued in `lane`, oldest first; the other lane is untouched.  A command that throws is
    // reported and the rest still run.
    size_t drain(const ErrorFn& onError, Lane lane = Lane::Frame) {
        std::deque<Fn> batch;
        {
            std::lock_guard<std::mutex> l(mu_);
            batch.swap(laneQ(lane));
        }
        for (Fn& f : batch) runOne(f, onError);
        return batch.size();
    }

    // Sleeps until `deadline`, or until a post() to either lane / notify() since the last wait.
    void waitUntil(TimePoint deadline) {
        std::unique_lock<std::mutex> l(mu_);
#if defined(__MINGW32__)
        // MinGW's timed condition wait (winpthreads) rounds a short timeout up to the scheduler tick (~10-16 ms), even
        // with the 1 ms timer resolution SDL sets, which held threaded fast-forward below normal speed on the Windows
        // runners.  Wait on the condition only while more than 20 ms remain, then poll in 1 ms waits on a
        // high-resolution timer (MinGW's Sleep and sleep_for honour the 1 ms timer resolution only on some hosts), so
        // the frame grid holds and a post() still wakes within about 1 ms.
        const auto pending = [this] { return wake_ || !q_.empty() || !dbg_.empty(); };
        const auto coarse = deadline - std::chrono::milliseconds(20);
        if (std::chrono::steady_clock::now() < coarse) cv_.wait_until(l, coarse, pending);
        while (!pending() && std::chrono::steady_clock::now() < deadline) {
            l.unlock();
            vj::preciseSleepNs(1000000);
            l.lock();
        }
#else
        cv_.wait_until(l, deadline, [this] { return wake_ || !q_.empty() || !dbg_.empty(); });
#endif
        wake_ = false;
    }

    // The halt pump's wait: wakes only on a post to `lane` or notify(); a post to the other lane does not end it.
    // Lane::Debug is what the pump waits on (a frame-lane post made while halted -- the SRAM autosave -- must not turn
    // it into a spin).  Lane::Frame behaves like waitUntil(deadline).
    void waitUntil(TimePoint deadline, Lane lane) {
        if (lane == Lane::Frame) {
            waitUntil(deadline);
            return;
        }
        std::unique_lock<std::mutex> l(mu_);
#if defined(__MINGW32__)
        // The same coarse-then-poll shape as waitUntil(deadline) above.
        const auto pending = [this] { return wakeDbg_ || !dbg_.empty(); };
        const auto coarse = deadline - std::chrono::milliseconds(20);
        if (std::chrono::steady_clock::now() < coarse) cv_.wait_until(l, coarse, pending);
        while (!pending() && std::chrono::steady_clock::now() < deadline) {
            l.unlock();
            vj::preciseSleepNs(1000000);
            l.lock();
        }
#else
        cv_.wait_until(l, deadline, [this] { return wakeDbg_ || !dbg_.empty(); });
#endif
        wakeDbg_ = false;
    }

    void notify() {
        {
            std::lock_guard<std::mutex> l(mu_);
            wake_ = true;
            wakeDbg_ = true;
        }
        cv_.notify_one();
    }

    size_t closeAndDrain(const ErrorFn& onError) {
        size_t n = 0;
        for (;;) {
            std::deque<Fn> batch, dbgBatch;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (q_.empty() && dbg_.empty()) {
                    closed_ = true;
                    return n;
                }
                batch.swap(q_);
                dbgBatch.swap(dbg_);
            }
            for (Fn& f : batch) runOne(f, onError);
            for (Fn& f : dbgBatch) runOne(f, onError);
            n += batch.size() + dbgBatch.size();
        }
    }

    void reopen() {
        std::lock_guard<std::mutex> l(mu_);
        closed_ = false;
        wake_ = false;
        wakeDbg_ = false;
    }

    bool closed() const {
        std::lock_guard<std::mutex> l(mu_);
        return closed_;
    }

    // Anything queued in `lane`.
    bool pending(Lane lane) const {
        std::lock_guard<std::mutex> l(mu_);
        return !(lane == Lane::Debug ? dbg_ : q_).empty();
    }

private:
    friend struct EmuThreadTestAccess;   // test-only inspection of the advance/queue publication boundary
    std::deque<Fn>& laneQ(Lane lane) { return lane == Lane::Debug ? dbg_ : q_; }   // under mu_
    static void runOne(Fn& f, const ErrorFn& onError) {
        try {
            f();
        } catch (const std::exception& e) {
            if (onError) onError(e.what());
        } catch (...) {
            if (onError) onError("unknown error");
        }
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Fn> q_;     // Lane::Frame
    std::deque<Fn> dbg_;   // Lane::Debug
    bool wake_ = false;      // notify(), or a post to either lane, since the last waitUntil(deadline)
    bool wakeDbg_ = false;   // notify(), or a post to the debug lane, since the last waitUntil(deadline, Lane::Debug)
    bool closed_ = false;
};

}  // namespace vj

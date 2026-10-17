// The emulation thread's command queue (#967): any thread post()s, the emulation thread drain()s at a frame boundary,
// in the order posted (per producer).  The same mutex carries the thread's timed wait, so a post or notify() wakes a
// sleeping emulation thread at once.  closeAndDrain() runs whatever is still queued -- including what arrives while
// it runs -- and then refuses more, so no command posted before the stop is ever lost.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#if defined(__MINGW32__)
#include <thread>
#endif

namespace vj {

class EmuCommands {
public:
    using Fn = std::function<void()>;
    using ErrorFn = std::function<void(const std::string&)>;
    using TimePoint = std::chrono::steady_clock::time_point;

    // False once closed; `fn` is then left untouched (the caller may run it itself).
    bool post(Fn&& fn) {
        {
            std::lock_guard<std::mutex> l(mu_);
            if (closed_) return false;
            q_.push_back(std::move(fn));
            wake_ = true;
        }
        cv_.notify_one();
        return true;
    }

    // Runs every queued command, oldest first.  A command that throws is reported and the rest still run.
    size_t drain(const ErrorFn& onError) {
        std::deque<Fn> batch;
        {
            std::lock_guard<std::mutex> l(mu_);
            batch.swap(q_);
        }
        for (Fn& f : batch) runOne(f, onError);
        return batch.size();
    }

    // Sleeps until `deadline`, or until a post() / notify() since the last wait.
    void waitUntil(TimePoint deadline) {
        std::unique_lock<std::mutex> l(mu_);
#if defined(__MINGW32__)
        // MinGW's timed condition wait (winpthreads) rounds a short timeout up to the scheduler tick (~10-16 ms), even
        // with the 1 ms timer resolution SDL sets, which held threaded fast-forward below normal speed on the Windows
        // runners.  Wait on the condition only while more than 20 ms remain, then poll in 1 ms sleeps (Sleep honours the
        // timer resolution), so the frame grid holds and a post() still wakes within about 1 ms.
        const auto pending = [this] { return wake_ || !q_.empty(); };
        const auto coarse = deadline - std::chrono::milliseconds(20);
        if (std::chrono::steady_clock::now() < coarse) cv_.wait_until(l, coarse, pending);
        while (!pending() && std::chrono::steady_clock::now() < deadline) {
            l.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            l.lock();
        }
#else
        cv_.wait_until(l, deadline, [this] { return wake_ || !q_.empty(); });
#endif
        wake_ = false;
    }

    void notify() {
        {
            std::lock_guard<std::mutex> l(mu_);
            wake_ = true;
        }
        cv_.notify_one();
    }

    size_t closeAndDrain(const ErrorFn& onError) {
        size_t n = 0;
        for (;;) {
            std::deque<Fn> batch;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (q_.empty()) {
                    closed_ = true;
                    return n;
                }
                batch.swap(q_);
            }
            for (Fn& f : batch) runOne(f, onError);
            n += batch.size();
        }
    }

    void reopen() {
        std::lock_guard<std::mutex> l(mu_);
        closed_ = false;
        wake_ = false;
    }

    bool closed() const {
        std::lock_guard<std::mutex> l(mu_);
        return closed_;
    }

private:
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
    std::deque<Fn> q_;
    bool wake_ = false;
    bool closed_ = false;
};

}  // namespace vj

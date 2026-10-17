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
        cv_.wait_until(l, deadline, [this] { return wake_ || !q_.empty(); });
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

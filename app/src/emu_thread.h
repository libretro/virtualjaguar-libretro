// The emulation thread (#967, docs/app-emulation-thread-design.md): the one thread that calls into the core.
//
// Threaded: a std::thread runs the frame loop: apply queued commands, wait until PacingClock says a frame is due (or
// for a command while paused / idle), run one frame through the Client, repeat.  The UI thread talks to it only
// through post() (fire and forget; applied at the next frame boundary, in order), call() (runs on the emulation
// thread and waits for the result: at most one frame) and the per-frame publishers (FrameExchange, CoreHost's
// EmuSnapshot, InputSnapshot).
//
// Inline: no thread.  post() and call() run on the caller at once and inlineStep() runs one frame -- the single-thread
// loop, headless runs and the existing tests, exactly as before.
//
// stop() gives up on a thread that does not finish within its timeout (stuck inside the core): the thread is detached
// and the object is "abandoned" -- call() returns a default value without running anything, and the owner must not
// touch the core or destroy this object (App exits the process).
#pragma once

#include "emu_commands.h"
#include "pacing_clock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace vj {

class EmuThread {
public:
    // What the loop drives.  Every method runs on the emulation thread (on the caller, inline).
    struct Client {
        virtual ~Client() = default;
        virtual void threadStarted() {}               // first thing on the new thread (App: CoreHost::setOwner)
        virtual void threadStopping() {}              // last thing on it
        virtual bool canRun() = 0;                    // content is loaded
        virtual double framePeriod() = 0;             // seconds per emulated frame (1 / the core's fps)
        virtual double audioQueuedMs() = 0;           // the audio clock; < 0 = none usable (wall clock)
        virtual void runFrame() = 0;                  // one emulated frame, then publish it
        virtual void afterCommands() {}               // a batch of commands ran (App: CoreHost::publishSnapshot)
        virtual void commandFailed(const std::string& what) { (void)what; }   // a post()ed command threw
    };
    enum class Mode { Inline, Threaded };

    EmuThread() = default;
    EmuThread(const EmuThread&) = delete;
    EmuThread& operator=(const EmuThread&) = delete;
    ~EmuThread();

    // Inline: remembers the client and never fails.  Threaded: starts the thread and returns once it runs; false when
    // no thread could be created (the caller stays inline).
    bool start(Mode mode, Client& client);
    // Runs what is queued, ends the loop, joins.  False when the thread did not finish within `joinTimeout`.
    bool stop(std::chrono::milliseconds joinTimeout = std::chrono::milliseconds(3000));

    bool threaded() const {
        const State s = state_.load();
        return s == State::Running || s == State::Stopping;
    }
    bool abandoned() const { return state_.load() == State::Abandoned; }
    bool onEmuThread() const { return std::this_thread::get_id() == threadId_.load(); }

    // Applied at the next frame boundary, in order.  Inline (or from the emulation thread itself): runs now.
    void post(std::function<void()> fn);
    // Runs `fn` on the emulation thread at the next frame boundary and returns its result (rethrowing what it threw).
    // Inline, or from the emulation thread itself: runs now.  Abandoned: returns R() and runs nothing.
    template <class F>
    auto call(F&& fn) -> decltype(fn());
    // call() for shutdown: gives up after `timeout`.  On false the command may still run later, so capture by value.
    template <class F>
    bool callFor(std::chrono::milliseconds timeout, F&& fn);

    void inlineStep();          // inline: one frame now
    bool advance(unsigned n);   // n frames now, even while paused, ignoring pacing; returns once they ran

    void setPaused(bool p) {
        if (paused_.exchange(p) != p) cmds_.notify();
    }
    bool paused() const { return paused_.load(); }
    void setFastForward(bool on) {
        if (ff_.exchange(on) != on) cmds_.notify();
    }
    void setAudioTargetMs(double ms) { targetMs_.store(ms); }
    uint64_t framesRun() const { return frames_.load(); }
    uint64_t droppedFrames() const { return dropped_.load(); }

private:
    enum class State { Inline, Starting, Running, Stopping, Stopped, Abandoned };
    void loop();
    void waitFinished();   // the thread has left its loop for good (or was abandoned)
    void notifyFrame();

    Client* client_ = nullptr;
    EmuCommands cmds_;
    std::thread thread_;
    std::atomic<State> state_{State::Inline};
    std::atomic<std::thread::id> threadId_{std::thread::id()};
    std::atomic<bool> stop_{false}, paused_{false}, ff_{false};
    std::atomic<double> targetMs_{64.0};
    std::atomic<unsigned> advanceLeft_{0};
    std::atomic<uint64_t> frames_{0}, dropped_{0};
    std::mutex mu_;
    std::condition_variable cv_;          // start and finish handshakes
    std::condition_variable advanceCv_;   // a frame ran
    bool ready_ = false, go_ = false, finished_ = false;   // under mu_
};

template <class F>
auto EmuThread::call(F&& fn) -> decltype(fn()) {
    using R = decltype(fn());
    if (abandoned()) return R();
    if (!threaded() || onEmuThread()) return fn();
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    std::future<R> done = task->get_future();
    if (!cmds_.post([task] { (*task)(); })) {   // the thread is finishing: run it here once it has gone
        waitFinished();
        if (abandoned()) return R();
        (*task)();
    }
    return done.get();
}

template <class F>
bool EmuThread::callFor(std::chrono::milliseconds timeout, F&& fn) {
    if (abandoned()) return false;
    if (!threaded() || onEmuThread()) {
        fn();
        return true;
    }
    auto task = std::make_shared<std::packaged_task<void()>>(std::forward<F>(fn));
    std::future<void> done = task->get_future();
    if (!cmds_.post([task] { (*task)(); })) {
        waitFinished();
        if (abandoned()) return false;
        (*task)();
        done.get();
        return true;
    }
    if (done.wait_for(timeout) != std::future_status::ready) return false;
    done.get();
    return true;
}

}  // namespace vj

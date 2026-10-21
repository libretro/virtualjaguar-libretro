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

inline thread_local bool g_inWaitHook = false;   // EmuThread::call: the wait hook is running on this thread

// Sets g_inWaitHook for the hook's duration: cleared again if the hook throws.
struct WaitHookScope {
    WaitHookScope() { g_inWaitHook = true; }
    ~WaitHookScope() { g_inWaitHook = false; }
    WaitHookScope(const WaitHookScope&) = delete;
    WaitHookScope& operator=(const WaitHookScope&) = delete;
};

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
        // Just before a paced runFrame(): how late it starts against its due time (seconds) and the clock that paced it
        // (App: VJ_PERF_TRACE).  Frames run by advance() are not paced and do not call it.
        virtual void framePaced(double lateSec, const PacingClock& clock) { (void)lateSec, (void)clock; }
        // Where the UI takes finished frames, for PacingClock's steering: one take time (seconds, on `now`'s clock:
        // steady_clock) and the take period.  False = no take grid (vsync off, swaps not tracked): no steering.
        virtual bool takeGrid(double now, double& at, double& period) { (void)now, (void)at, (void)period; return false; }
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
    // Inline, or from the emulation thread itself: runs now.  Abandoned: returns R() and does not wait.  A call queued
    // before the abandon may still run later on the detached thread, so captures must be by value (as callFor says).
    template <class F>
    auto call(F&& fn) -> decltype(fn());
    // The debug lane (#967 phase 3): safe inside retro_run().  Inline, or on the emulation thread: runs now.  Threaded:
    // queued in Lane::Debug, drained at the next frame boundary (after the frame lane) or by the halt pump, whichever
    // comes first.  debugCall waits like call() (the same wait hook after kBusyAfter); captures must be by value when
    // the thread may be abandoned, as for call().  There is no ordering between the two lanes beyond "frame lane
    // first at each boundary".  The Tools windows rely on debugCall ALWAYS waiting, never giving up: DebugSession::attach,
    // TempBreak::arm/disarm and CpuControl::stepOver capture locals by reference inside callCore.
    template <class F>
    auto debugCall(F&& fn) -> decltype(fn());
    void debugPost(std::function<void()> fn);
    // call() for shutdown: gives up after `timeout`.  On false the command may still run later, so capture by value.
    template <class F>
    bool callFor(std::chrono::milliseconds timeout, F&& fn);

    void inlineStep();          // inline: one frame now
    bool advance(unsigned n);   // after earlier commands: n frames now, even while paused, ignoring pacing; wait until done

    void setPaused(bool p) {
        if (paused_.exchange(p) != p) cmds_.notify();
    }
    bool paused() const { return paused_.load(); }
    void setFastForward(bool on) {
        if (ff_.exchange(on) != on) cmds_.notify();
    }
    static constexpr std::chrono::milliseconds kBusyAfter{250};
    // Called on the thread waiting in call() once it has waited kBusyAfter, then about every 16 ms until the call
    // returns (tick 0 first).  See the contract in docs/app-emulation-thread-plan-phase2.md (Task 7): it may pump OS
    // events and swap a frame drawn with raw GL; it must never open an ImGui frame, call()/post(), or read a field that
    // only a call() writes.  Never called inline, on the emulation thread, or re-entered.  Set before start().
    void setWaitHook(std::function<void(double waitedSec, unsigned tick)> hook) { waitHook_ = std::move(hook); }
    void setAudioTargetMs(double ms) { targetMs_.store(ms); }
    // Default on: user-interactive QoS on Apple (set when the thread starts) and a precise last stretch before each
    // frame's due time.  Off: the plain condition wait (#967 A/B, --emu-wake plain).
    void setPreciseWake(bool on) { preciseWake_.store(on); }
    uint64_t framesRun() const { return frames_.load(); }
    uint64_t droppedFrames() const { return dropped_.load(); }
    uint64_t phaseSteps() const { return steers_.load(); }   // PacingClock::steers()

private:
    friend struct EmuThreadTestAccess;   // test-only inspection of the advance/queue publication boundary
    enum class State { Inline, Starting, Running, Stopping, Stopped, Abandoned };
    void loop();
    void waitFinished();   // the thread has left its loop for good (or was abandoned)
    void notifyFrame();
    template <class F>
    auto submit(F&& fn, EmuCommands::Lane lane) -> decltype(fn());   // call() / debugCall()

    Client* client_ = nullptr;
    std::function<void(double, unsigned)> waitHook_;   // set before start(); read only by call()'s waiter
    EmuCommands cmds_;
    std::thread thread_;
    std::atomic<State> state_{State::Inline};
    std::atomic<std::thread::id> threadId_{std::thread::id()};
    static constexpr double kPreciseSec = 0.001;   // the condition wait ends this long before a frame is due
    std::atomic<bool> stop_{false}, paused_{false}, ff_{false}, preciseWake_{true};
    std::atomic<double> targetMs_{64.0};
    std::atomic<unsigned> advanceLeft_{0};
    std::atomic<uint64_t> advanceDone_{0};   // budgeted frames run (bumped before advanceLeft_ drops)
    std::atomic<uint64_t> frames_{0}, dropped_{0}, steers_{0};
    std::mutex mu_;
    std::condition_variable cv_;          // start and finish handshakes
    std::condition_variable advanceCv_;   // a frame ran
    bool ready_ = false, go_ = false, finished_ = false;   // under mu_
};

template <class F>
auto EmuThread::call(F&& fn) -> decltype(fn()) {
    return submit(std::forward<F>(fn), EmuCommands::Lane::Frame);
}

template <class F>
auto EmuThread::debugCall(F&& fn) -> decltype(fn()) {
    return submit(std::forward<F>(fn), EmuCommands::Lane::Debug);
}

template <class F>
auto EmuThread::submit(F&& fn, EmuCommands::Lane lane) -> decltype(fn()) {
    using R = decltype(fn());
    if (abandoned()) return R();
    if (!threaded() || onEmuThread()) return fn();
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    std::future<R> done = task->get_future();
    if (!cmds_.post([task] { (*task)(); }, lane)) {   // the thread is finishing: run it here once it has gone
        waitFinished();
        if (abandoned()) return R();
        (*task)();
    }
    // The task is in the queue.  An abandoned thread never answers it: poll for that instead of blocking in get().
    // Past kBusyAfter, the wait hook runs between slices (the UI's spinner).
    const auto t0 = std::chrono::steady_clock::now();
    unsigned tick = 0;
    for (;;) {
        const bool busy = waitHook_ && !g_inWaitHook && std::chrono::steady_clock::now() - t0 >= kBusyAfter;
        if (done.wait_for(busy ? std::chrono::milliseconds(16) : std::chrono::milliseconds(50)) ==
            std::future_status::ready)
            break;
        if (abandoned()) return R();
        if (waitHook_ && !g_inWaitHook && std::chrono::steady_clock::now() - t0 >= kBusyAfter) {
            WaitHookScope inHook;
            waitHook_(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), tick++);
        }
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

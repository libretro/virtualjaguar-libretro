// The emulation thread (#967, docs/app-emulation-thread-design.md): the one thread that calls into the core.
//
// Threaded: a std::thread runs the frame loop: apply queued commands, wait until PacingClock says a frame is due (or
// for a command while paused / idle), run one frame through the Client, repeat.  Mode B (#1038): while the Client
// reports a display grid, frames start on its vblank ticks instead (PacingClock's tick path).  The UI thread talks to it only
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
        // Mode B (#1038, display lock): the display grid the UI publishes after each swap.  False = mode A (the setting,
        // no lock, fast-forward, link play, or a grid the UI has not refreshed for a while: a UI that stops swapping --
        // a modal, a live resize, a sleeping display -- must not stop the emulation).
        struct DisplayGrid {
            uint64_t tick = 0;     // the vblank the UI last swapped on (a count that only goes up)
            double tickAt = 0;     // when it was, on `now`'s clock (steady_clock seconds)
            double period = 0;     // the measured vblank period, seconds
            unsigned k = 1;        // vblanks per emulated frame
        };
        virtual bool displayGrid(double now, DisplayGrid& g) { (void)now, (void)g; return false; }
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
    // Precondition: no halt is holding the core -- the caller ends it first (App calls beginQuit(): quitting_ makes the
    // pump answer CONTINUE).  A core parked in the debugger's halt never returns to the loop, so stop() would only time out.
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
    // `giveUp` (#967 phase 3b) is consulted once BEFORE the task is queued (true: returns R() at once, nothing queued)
    // and then on the waiting thread after every wait slice (16-50 ms): true before the task has started marks it
    // cancelled (skipped when drained) and call() returns R().  A task that started before the mark runs to completion
    // and its value is returned (captures by value).  Never consulted inline, on the emulation thread, or after the task
    // started.  The App passes "a halt is up" (App::haltGiveUp): a frame-lane command cannot run inside a halt.
    template <class F>
    auto call(F&& fn, std::function<bool()> giveUp = {}) -> decltype(fn());
    // The debug lane (#967 phase 3): safe inside retro_run().  Inline, or on the emulation thread: runs now.  Threaded:
    // queued in Lane::Debug, drained at the next frame boundary (after the frame lane) or by the halt pump, whichever
    // comes first.  debugCall waits like call() (the same wait hook after kBusyAfter); captures must be by value when
    // the thread may be abandoned, as for call().  There is no ordering between the two lanes beyond "frame lane
    // first at each boundary".  The Tools windows rely on debugCall ALWAYS waiting, never giving up: DebugSession::attach,
    // TempBreak::arm/disarm and CpuControl::stepOver capture locals by reference inside callCore.  Achievements::coreCall
    // rides it too: a reply callback must never be cancelled.
    template <class F>
    auto debugCall(F&& fn) -> decltype(fn());
    void debugPost(std::function<void()> fn);
    // call() for shutdown: gives up after `timeout`.  On false the command may still run later, so capture by value.
    template <class F>
    bool callFor(std::chrono::milliseconds timeout, F&& fn);

    // Emulation thread only (#967 phase 3b): the halt pump's lane service.  Drains Lane::Debug now; if nothing was queued,
    // waits up to `maxWait` on waitUntil(deadline, Lane::Debug) -- a frame-lane post does not end that wait -- then
    // drains again.  Returns the number of commands run.  Inline: drains and returns at once.  Threaded and called off
    // the emulation thread (or abandoned): returns 0 at once.
    size_t serveDebugLane(std::chrono::milliseconds maxWait);

    void inlineStep();          // inline: one frame now
    // After earlier frame-lane commands: n frames now, even while paused, ignoring pacing; waits until they ran.  The
    // budget travels on the DEBUG lane (served at the next boundary or by the halt pump), so it is always registered.
    // `giveUp` (#967 phase 3b), polled about every 16 ms while waiting: true ends the wait and advance() returns false;
    // the frames already budgeted still run once the thread can (a halt ended).  advance(0, giveUp) waits for a budget
    // still queued (true at once when none).  Never consulted inline or on the emulation thread.
    bool advance(unsigned n, std::function<bool()> giveUp = {});

    void setPaused(bool p) {
        if (paused_.exchange(p) != p) cmds_.notify();
    }
    bool paused() const { return paused_.load(); }
    void setFastForward(bool on) {
        if (ff_.exchange(on) != on) cmds_.notify();
    }
    // Mode B: the UI published a new display tick (Client::displayGrid): start its frame now.
    void notifyTick() { cmds_.notify(); }
    // Mode B: when the UI's tick is late (it is still building, or stopped swapping), a frame starts this long after the
    // vblank the grid says it is due on, so the game keeps its rate without the UI.
    static constexpr double kTickSlack = 0.002;
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
    // Default on: when the thread starts it asks the OS to schedule it as a real-time frame loop (thread_realtime.h);
    // what it got is in realtimeNote() once start() returns.  Off: the default class (--emu-priority normal).  On Apple
    // the user-interactive QoS raise above is the fallback when the real-time policy is refused.
    void setRealtime(bool on) { realtime_.store(on); }
    const std::string& realtimeNote() const { return realtimeNote_; }   // written before the start handshake; read after start()
    uint64_t framesRun() const { return frames_.load(); }
    uint64_t droppedFrames() const { return dropped_.load(); }
    uint64_t phaseSteps() const { return steers_.load(); }   // PacingClock::steers()
    uint64_t droppedTicks() const { return droppedTicks_.load(); }   // mode B: PacingClock::droppedTicks()
    bool modeB() const { return modeB_.load(); }                     // the last paced frame started on a display tick

private:
    friend struct EmuThreadTestAccess;   // test-only inspection of the advance/queue publication boundary
    enum class State { Inline, Starting, Running, Stopping, Stopped, Abandoned };
    void loop();
    void waitFinished();   // the thread has left its loop for good (or was abandoned)
    void notifyFrame();
    template <class F>
    auto submit(F&& fn, EmuCommands::Lane lane, std::function<bool()> giveUp) -> decltype(fn());   // call() / debugCall()

    Client* client_ = nullptr;
    std::function<void(double, unsigned)> waitHook_;   // set before start(); read only by call()'s waiter
    EmuCommands cmds_;
    std::thread thread_;
    std::atomic<State> state_{State::Inline};
    std::atomic<std::thread::id> threadId_{std::thread::id()};
    static constexpr double kPreciseSec = 0.001;   // the condition wait ends this long before a frame is due
    std::atomic<bool> stop_{false}, paused_{false}, ff_{false}, preciseWake_{true}, realtime_{true};
    std::string realtimeNote_;   // loop() sets it before ready_; the starter reads it after start()
    std::atomic<double> targetMs_{64.0};
    std::atomic<unsigned> advanceLeft_{0};
    std::atomic<uint64_t> advanceDone_{0};   // budgeted frames run (bumped before advanceLeft_ drops)
    std::atomic<uint64_t> frames_{0}, dropped_{0}, steers_{0}, droppedTicks_{0};
    std::atomic<bool> modeB_{false};
    std::mutex mu_;
    std::condition_variable cv_;          // start and finish handshakes
    std::condition_variable advanceCv_;   // a frame ran
    bool ready_ = false, go_ = false, finished_ = false;   // under mu_
};

template <class F>
auto EmuThread::call(F&& fn, std::function<bool()> giveUp) -> decltype(fn()) {
    return submit(std::forward<F>(fn), EmuCommands::Lane::Frame, std::move(giveUp));
}

template <class F>
auto EmuThread::debugCall(F&& fn) -> decltype(fn()) {
    return submit(std::forward<F>(fn), EmuCommands::Lane::Debug, {});
}

template <class F>
auto EmuThread::submit(F&& fn, EmuCommands::Lane lane, std::function<bool()> giveUp) -> decltype(fn()) {
    using R = decltype(fn());
    if (abandoned()) return R();
    if (!threaded() || onEmuThread()) return fn();
    if (giveUp && giveUp()) return R();   // a halt is already up: nothing is queued
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    // 0 = queued, 1 = started, 2 = cancelled: one compare-and-swap decides between the drain and a give-up, so a task is
    // never both run and given up on (its captures may be dead once call() has returned R()).
    auto state = std::make_shared<std::atomic<int>>(0);
    std::future<R> done = task->get_future();
    if (!cmds_.post(
            [task, state] {
                int queued = 0;
                if (state->compare_exchange_strong(queued, 1)) (*task)();   // cancelled: skipped
            },
            lane)) {   // the thread is finishing: run it here once it has gone
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
        if (giveUp && state->load() == 0 && giveUp()) {
            int queued = 0;
            if (state->compare_exchange_strong(queued, 2)) return R();   // else it started in between: wait for it
        }
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

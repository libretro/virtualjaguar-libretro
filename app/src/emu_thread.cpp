#include "emu_thread.h"
#include "thread_realtime.h"

#include <algorithm>
#include <exception>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

namespace vj {

namespace {

using Steady = std::chrono::steady_clock;

double nowSec() { return std::chrono::duration<double>(Steady::now().time_since_epoch()).count(); }

Steady::time_point atSec(double s) {
    return Steady::time_point(std::chrono::duration_cast<Steady::duration>(std::chrono::duration<double>(s)));
}

}  // namespace

EmuThread::~EmuThread() {
    if (threaded()) stop();
}

bool EmuThread::start(Mode mode, Client& client) {
    const State s = state_.load();
    if (s == State::Running || s == State::Stopping || s == State::Abandoned) return false;
    client_ = &client;
    if (mode == Mode::Inline) {
        state_.store(State::Inline);
        return true;
    }
    cmds_.reopen();
    stop_.store(false);
    advanceLeft_.store(0);
    {
        std::lock_guard<std::mutex> l(mu_);
        ready_ = go_ = finished_ = false;
    }
    state_.store(State::Starting);
    try {
        thread_ = std::thread([this] { loop(); });
    } catch (const std::exception&) {   // std::system_error: the OS refused a thread
        state_.store(State::Inline);
        return false;
    }
    std::unique_lock<std::mutex> l(mu_);
    cv_.wait(l, [this] { return ready_; });
    state_.store(State::Running);
    go_ = true;
    l.unlock();
    cv_.notify_all();
    return true;
}

bool EmuThread::stop(std::chrono::milliseconds joinTimeout) {
    const State s = state_.load();
    if (s == State::Abandoned) return false;
    if (s != State::Running) return true;
    state_.store(State::Stopping);
    stop_.store(true);
    cmds_.notify();
    {
        std::unique_lock<std::mutex> l(mu_);
        if (!cv_.wait_for(l, joinTimeout, [this] { return finished_; })) {
            state_.store(State::Abandoned);
            l.unlock();
            cv_.notify_all();
            advanceCv_.notify_all();
            thread_.detach();
            return false;
        }
    }
    thread_.join();
    state_.store(State::Stopped);
    return true;
}

void EmuThread::post(std::function<void()> fn) {
    if (abandoned()) return;
    if (threaded() && !onEmuThread()) {
        if (cmds_.post(std::move(fn))) return;   // (post() leaves `fn` alone when it refuses it)
        waitFinished();
        if (abandoned()) return;
    }
    fn();
}

void EmuThread::debugPost(std::function<void()> fn) {
    if (abandoned()) return;
    if (threaded() && !onEmuThread()) {
        if (cmds_.post(std::move(fn), EmuCommands::Lane::Debug)) return;   // (post() leaves `fn` alone when it refuses it)
        waitFinished();
        if (abandoned()) return;
    }
    fn();
}

size_t EmuThread::serveDebugLane(std::chrono::milliseconds maxWait) {
    const auto onError = [this](const std::string& what) {
        if (client_) client_->commandFailed(what);
    };
    if (abandoned()) return 0;
    if (!threaded()) return cmds_.drain(onError, EmuCommands::Lane::Debug);
    if (!onEmuThread()) return 0;   // not the halt pump's thread: nothing here is ours to serve
    size_t n = cmds_.drain(onError, EmuCommands::Lane::Debug);
    if (n == 0) {
        cmds_.waitUntil(Steady::now() + maxWait, EmuCommands::Lane::Debug);
        n = cmds_.drain(onError, EmuCommands::Lane::Debug);
    }
    return n;
}

void EmuThread::waitFinished() {
    std::unique_lock<std::mutex> l(mu_);
    cv_.wait(l, [this] { return finished_ || state_.load() == State::Abandoned; });
}

void EmuThread::notifyFrame() {
    { std::lock_guard<std::mutex> l(mu_); }   // a waiter is either before its predicate check or asleep: no lost wake-up
    advanceCv_.notify_all();
}

void EmuThread::inlineStep() {
    if (threaded() || abandoned() || !client_ || !client_->canRun()) return;
    client_->runFrame();
    frames_.fetch_add(1);
}

bool EmuThread::advance(unsigned n, std::function<bool()> giveUp) {
    if (abandoned() || !client_) return false;
    if (!threaded()) {
        if (n == 0) return true;
        if (!client_->canRun()) return false;
        for (unsigned i = 0; i < n; i++) inlineStep();
        return true;
    }
    if (onEmuThread()) return false;   // a command cannot wait for frames that only run after it
    // Publish the budget through the debug lane: an atomic increment that is safe inside a halt (the halt pump serves the
    // lane), so it is always registered, and the drain order (frame lane first) keeps it behind every earlier post.
    // Count only budgeted frames, including any requests already queued ahead of this one.
    const uint64_t target = debugCall([this, n] {
        const uint64_t end = advanceDone_.load() + advanceLeft_.load() + n;
        advanceLeft_.fetch_add(n);
        return end;
    });
    if (abandoned()) return false;   // debugCall was abandoned before accepting the request
    std::unique_lock<std::mutex> l(mu_);
    const auto pred = [&] {
        return advanceDone_.load() >= target || advanceLeft_.load() == 0 || finished_ ||
               state_.load() == State::Abandoned;
    };
    for (;;) {
        if (advanceCv_.wait_for(l, std::chrono::milliseconds(16), pred)) break;
        if (giveUp) {   // outside mu_: the predicate may take any lock of its own
            l.unlock();
            const bool g = giveUp();
            l.lock();
            if (g) return false;
        }
    }
    return advanceDone_.load() >= target;
}

void EmuThread::loop() {
    threadId_.store(std::this_thread::get_id());
    // The frame loop is a periodic deadline job; schedule it as one (thread_realtime.h).  The period is a hint for the
    // deadline policies, so the NTSC field is close enough for PAL too.  Before the handshake: the starter reads the note.
    bool realtime = false;
    if (realtime_.load()) {
        const rt::Outcome o = rt::makeSelfRealtime(1.0 / 60.0);
        realtime = o.realtime;
        realtimeNote_ = o.note;
    } else {
        realtimeNote_ = "normal (not requested)";
    }
#if defined(__APPLE__)
    // Fallback when the real-time policy was refused or not asked for.  A default-QoS thread gets timer slack: on the
    // Mac Studio its frame wake-ups ran ~2 ms (p99 ~3 ms) after their due time, which widens the window of frame end
    // times the pacing grid has to keep off the UI's take point (#967).  The frame loop is what the user is watching;
    // it is as interactive as the main thread.  (A real-time thread cannot take a QoS class, and does not need one.)
    if (!realtime && preciseWake_.load()) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    client_->threadStarted();
    {
        std::unique_lock<std::mutex> l(mu_);
        ready_ = true;
        cv_.notify_all();
        cv_.wait(l, [this] { return go_; });
    }
    const auto onError = [this](const std::string& what) { client_->commandFailed(what); };
    PacingClock clock;
    bool pacing = false;   // the clock was reset since frames last stopped (a pause is never owed time)
    while (!stop_.load()) {
        size_t ran = cmds_.drain(onError, EmuCommands::Lane::Frame);
        ran += cmds_.drain(onError, EmuCommands::Lane::Debug);
        // advance()'s budget rides the debug lane: a frame-lane post made before it may have landed between the two
        // drains above, and must not be overtaken by the frame the budget starts.
        if (advanceLeft_.load() > 0) ran += cmds_.drain(onError, EmuCommands::Lane::Frame);
        if (ran > 0) client_->afterCommands();
        if (stop_.load()) break;
        const bool budget = advanceLeft_.load() > 0;
        const bool runnable = client_->canRun();
        if (budget && !runnable) {   // advance() asked for frames nothing can run: release it
            advanceLeft_.store(0);
            notifyFrame();
        }
        if (!runnable || (paused_.load() && !budget)) {
            pacing = false;
            cmds_.waitUntil(Steady::now() + std::chrono::milliseconds(250));
            continue;
        }
        PacingInput in;
        in.now = nowSec();
        in.period = client_->framePeriod();
        in.audioQueuedMs = client_->audioQueuedMs();
        in.targetMs = targetMs_.load();
        in.fastForward = ff_.load();
        if (!client_->takeGrid(in.now, in.takeAt, in.takePeriod)) in.takeAt = in.takePeriod = 0;
        if (!pacing) {
            clock.reset(in.now);
            pacing = true;
        }
        double due = in.now;
        Client::DisplayGrid g;
        const bool ticks = !budget && !in.fastForward && client_->displayGrid(in.now, g) && g.period > 0;
        if (ticks) {
            // Mode B: the UI's latest tick, or the grid's own once the UI is kTickSlack late for the next one.
            const double over = in.now - g.tickAt - kTickSlack;
            in.tickSeq = g.tick + (over > 0 ? (uint64_t)(over / g.period) : 0);
            in.ticksPerFrame = std::max(1u, g.k);
            if (!clock.tickDue(in)) {
                const double backstop = g.tickAt + (double)(clock.nextTick() - g.tick) * g.period + kTickSlack;
                cmds_.waitUntil(atSec(std::min(std::max(backstop, in.now + 0.0002), in.now + 0.1)));   // or notifyTick()
                continue;
            }
            due = g.tickAt + ((double)in.tickSeq - (double)g.tick) * g.period;   // the vblank this frame starts on
        } else if (!budget) {
            due = clock.nextDue(in);
            if (due > in.now + 0.0002) {
                // Precise wake (default): the condition wait (woken by commands) ends kPreciseSec before the due time
                // and short sleeps cover the rest, so a frame starts within ~0.2 ms of its due time instead of whenever
                // the timer slack lets the condition wait return.
                const bool precise = preciseWake_.load();
                if (!precise || due - in.now > kPreciseSec)
                    cmds_.waitUntil(atSec(std::min(precise ? due - kPreciseSec : due, in.now + 0.1)));   // <= 100 ms
                else
                    std::this_thread::sleep_for(std::chrono::duration<double>(std::min(due - in.now, 0.0002)));
                continue;
            }
        }
        if (ticks) clock.tickStarted(in);
        else clock.frameStarted(in);   // (an advance() frame in mode B leaves the ticks alone: mode A's state is reset on return)
        if (!budget) {
            modeB_.store(ticks);
            client_->framePaced(in.now - due, clock);
        }
        client_->runFrame();
        clock.frameEnded(nowSec());
        frames_.fetch_add(1);
        if (budget) {   // done before the budget drops: advance() reads advanceDone_ when the budget hits 0
            advanceDone_.fetch_add(1);
            advanceLeft_.fetch_sub(1);
        }
        dropped_.store(clock.droppedFrames());
        droppedTicks_.store(clock.droppedTicks());
        steers_.store(clock.steers());
        notifyFrame();
    }
    if (cmds_.closeAndDrain(onError) > 0) client_->afterCommands();
    client_->threadStopping();
    threadId_.store(std::thread::id());
    {
        std::lock_guard<std::mutex> l(mu_);
        finished_ = true;
    }
    cv_.notify_all();
    advanceCv_.notify_all();
}

}  // namespace vj

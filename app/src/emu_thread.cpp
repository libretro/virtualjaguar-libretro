#include "emu_thread.h"

#include <algorithm>
#include <exception>

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

bool EmuThread::advance(unsigned n) {
    if (n == 0) return true;
    if (abandoned() || !client_) return false;
    if (!threaded()) {
        if (!client_->canRun()) return false;
        for (unsigned i = 0; i < n; i++) inlineStep();
        return true;
    }
    if (onEmuThread()) return false;   // a command cannot wait for frames that only run after it
    std::unique_lock<std::mutex> l(mu_);
    // Count budgeted frames only: a frame already in flight (it read a budget of 0) must not satisfy this request.
    const uint64_t target = advanceDone_.load() + n;
    advanceLeft_.fetch_add(n);
    cmds_.notify();
    advanceCv_.wait(l, [&] {
        return advanceDone_.load() >= target || advanceLeft_.load() == 0 || finished_ ||
               state_.load() == State::Abandoned;
    });
    return advanceDone_.load() >= target;
}

void EmuThread::loop() {
    threadId_.store(std::this_thread::get_id());
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
        if (cmds_.drain(onError) > 0) client_->afterCommands();
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
        if (!pacing) {
            clock.reset(in.now);
            pacing = true;
        }
        double due = in.now;
        if (!budget) {
            due = clock.nextDue(in);
            if (due > in.now + 0.0002) {
                cmds_.waitUntil(atSec(std::min(due, in.now + 0.1)));   // re-checked at least every 100 ms
                continue;
            }
        }
        clock.frameStarted(in);
        if (!budget) client_->framePaced(in.now - due, clock);
        client_->runFrame();
        frames_.fetch_add(1);
        if (budget) {   // done before the budget drops: advance() reads advanceDone_ when the budget hits 0
            advanceDone_.fetch_add(1);
            advanceLeft_.fetch_sub(1);
        }
        dropped_.store(clock.droppedFrames());
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

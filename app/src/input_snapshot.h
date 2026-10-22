// The newest input, from the UI thread to the core's input poll on the emulation thread (#967).
//
// The UI thread runs InputManager::poll() (SDL_GetMouseState is main-thread only, and the routes, the keypad overlay
// and bind capture all live there) and publish()es the resulting CoreInput after every batch of events.  The core's
// input-poll callback take()s the newest one right before each frame, so input is never older than the last event
// batch.  Buttons and axes are states (the newest wins); pointer / rotary motion is a delta and is summed until a frame
// takes it, then delivered exactly once, however many publishes or frames fall in between.
#pragma once

#include "remap.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace vj {

// The Neuro agent's input (neuro_actions.h: InputQueue is the UI-side queue, AgentPlayer the emulation-thread replica)
// travels as a whole program the emulation thread plays frame by frame, so it needs no per-frame round trip.
struct AgentStep {
    ControlSet held;
    unsigned frames = 1;           // 1..600
};
struct AgentProgram {
    uint64_t gen = 0;              // 0 = no program; bumped on every publish (takeAgent: only a newer gen)
    uint64_t lineage = 0;          // bumped only by InputQueue::clear() (release_all, link drop, hardcore, input block,
                                   // game change, Neuro off): a new lineage restarts the replica's count
    uint64_t firstFrame = 0;       // frame index, inside the lineage, of steps[0]'s first frame (the UI trims played
                                   // frames and raises it), so a new gen of the same lineage neither replays nor drops
    bool enabled = false;          // Agent::allowed() && actions_.blocked().empty()
    ControlSet hold;               // added to every frame
    std::vector<AgentStep> steps;  // run in order after the frames already played
};
struct AgentProgress {
    uint64_t gen = 0;              // the program this reports on
    uint64_t lineage = 0;
    unsigned played = 0;           // frames consumed from the steps, ABSOLUTE in the lineage (not the hold)
    bool done = false;             // steps exhausted
    ControlSet pressed;            // what the last frame pressed (hold | step)
};

class InputSnapshot {
public:
    // UI thread.  `eventNs`: SDL timestamp (SDL_GetTicksNS base) of the oldest input event this state includes that
    // was not published before, 0 = none.  Returns this publish's sequence number (from 1), which the frame that
    // polls it carries to the UI (FrameExchange::Slot::inputSeq) for the event -> present measurement.
    uint64_t publish(const CoreInput& in, uint64_t eventNs) {
        std::lock_guard<std::mutex> l(mu_);
        CoreInput next = in;
        next.carryMotion(latest_);   // motion the emulation has not taken yet is kept, not replaced
        latest_ = next;
        if (eventNs && !eventNs_) eventNs_ = eventNs;
        return ++seq_;
    }

    // Emulation thread (the core's input-poll callback).  Returns the oldest untaken event's timestamp (0 = none),
    // for the input-latency measurement; `seq` (optional) gets the newest publish handed out (0 = none yet).
    uint64_t take(CoreInput& out, uint64_t* seq = nullptr) {
        std::lock_guard<std::mutex> l(mu_);
        out = latest_;
        latest_.clearMotion();
        taken_++;
        if (seq) *seq = seq_;
        const uint64_t ns = eventNs_;
        eventNs_ = 0;
        return ns;
    }

    // UI thread: frames that took input since the last call (the keypad overlay ticks once per emulated frame).
    unsigned takenSince() {
        std::lock_guard<std::mutex> l(mu_);
        const unsigned n = taken_;
        taken_ = 0;
        return n;
    }

    // The agent channel: its own lock, so the per-event mutex above is not touched.
    void publishAgent(const AgentProgram& p) {      // UI thread; the whole program, newest wins
        {
            std::lock_guard<std::mutex> l(agentMu_);
            agent_ = p;
        }
        agentGen_.store(p.gen, std::memory_order_release);
    }
    bool takeAgent(AgentProgram& out) {             // emulation thread: true (and `out` filled) only when gen changed
        if (agentGen_.load(std::memory_order_acquire) == takenAgentGen_) return false;
        std::lock_guard<std::mutex> l(agentMu_);
        out = agent_;
        takenAgentGen_ = out.gen;
        return true;
    }
    void reportAgent(const AgentProgress& p) {      // emulation thread, after each frame()
        std::lock_guard<std::mutex> l(agentMu_);
        progress_ = p;
    }
    AgentProgress agentProgress() const {           // UI thread
        std::lock_guard<std::mutex> l(agentMu_);
        return progress_;
    }

private:
    std::mutex mu_;
    CoreInput latest_;
    uint64_t eventNs_ = 0;
    unsigned taken_ = 0;
    uint64_t seq_ = 0;   // publishes so far

    mutable std::mutex agentMu_;
    AgentProgram agent_;
    AgentProgress progress_;
    std::atomic<uint64_t> agentGen_{0};
    uint64_t takenAgentGen_ = 0;   // emulation thread only
};

}  // namespace vj

// CoreJob: work for the emulation thread whose result the UI picks up later (#967 phase 3a).  A window that needs
// something computed from the core that is not a periodic view (the CPU window's backwards disassembly walk) submits
// a function, keeps drawing, and take()s the answer on a later frame.  The function runs on the emulation thread
// (DebugSession::postCore) with the core's interface; inline it has run before submit() returns.
//
// The state is shared with the posted lambda, so a CoreJob may be destroyed (or reset by a content switch) while
// its work is still queued.  Header-only, no ImGui.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "debug_session.h"

namespace vj {

template <class T>
class CoreJob {
public:
    // UI thread.  Posts fn(session.core().iface()) through postCore; inline it has run before submit() returns.
    // The interface is null when no core is attached.
    void submit(DebugSession& s, std::function<T(const vj_debug_iface*)> fn) {
        {
            std::lock_guard<std::mutex> lk(st_->mu);
            st_->inFlight++;
        }
        std::shared_ptr<State> st = st_;
        DebugCore* core = &s.core();
        s.postCore([st, core, fn = std::move(fn)] {
            T v = fn(core->iface());
            std::lock_guard<std::mutex> lk(st->mu);
            st->value = std::move(v);   // newest wins: the posts run in order, so a later one overwrites
            st->has = true;
            st->inFlight--;
        });
    }
    // UI thread: true once per delivered result; a result is never given twice.
    bool take(T& out) {
        std::lock_guard<std::mutex> lk(st_->mu);
        if (!st_->has) return false;
        out = std::move(st_->value);
        st_->has = false;
        return true;
    }
    bool pending() const {   // submitted, not yet delivered (taken or not)
        std::lock_guard<std::mutex> lk(st_->mu);
        return st_->inFlight != 0;
    }

private:
    struct State {
        std::mutex mu;
        bool has = false;
        T value{};
        unsigned inFlight = 0;
    };
    std::shared_ptr<State> st_ = std::make_shared<State>();
};

}  // namespace vj

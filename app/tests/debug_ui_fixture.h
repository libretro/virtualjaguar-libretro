// Shared rig for the debug tool-window tests (#967 phase 3a): a mock core behind a COUNTING copy of its interface,
// and a DebugSession whose coreCall / corePost hooks count the waits and posts a window makes.
//
// A window that draws only from published view buffers makes no coreCall, no corePost and no interface call once
// its data has arrived.  Tests pin that with quietSince() and CountingIface::offCore():
//
//   UiFixture fx;                                  // fx.mock is the core, fx.s the session (content loaded)
//   ... draw frames, call fx.fill() after each one (what App::fillDebugViews does on the emulation thread) ...
//   const UiFixture::Mark m = fx.mark();
//   ... idle frames ...
//   CHECK(fx.quietSince(m));                       // no call, no wait, no post
//   CHECK(CountingIface::offCore() == 0);          // and nothing reached the interface off the emulation side
//
// The hooks run the work right where they are called (one thread), holding a CountingIface::OnCore guard: that is
// "the emulation thread" here.  A call inside a call is the emulation side's own work and is not counted again.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>

#include "debug_mock.h"
#include "debug_session.h"
#include "vj_debug_iface.h"

namespace vj {

// A copy of an iface whose every entry point forwards to the original and counts.  One instance at a time (the iface
// has no context pointer, so the thunks reach the original through a static).  The counters are static too: call
// reset() at the top of a test that compares counts.
class CountingIface {
public:
    enum Fn { Region, Peek, Poke, GetRegs, SetReg, DisasmRisc, Attach, Detach, BreakSet, BreakClear, HaltNext,
              HaltedTarget, HaltReason, OpArm, OpGet, CryToRgb, BlitEnable, BlitHead, BlitGet, TexReload, FrameNo, kFns };

    explicit CountingIface(const vj_debug_iface* inner) {
        in() = inner;
        std::memset(&iface_, 0, sizeof iface_);
        const size_t n = std::min<size_t>(inner->size, sizeof iface_);   // a core built with an older, shorter struct
        std::memcpy(&iface_, inner, n);
        iface_.size = (unsigned)n;
#define VJ_CI_WRAP(field, Name) \
    if (iface_.field) iface_.field = &t##Name
        VJ_CI_WRAP(region, Region);
        VJ_CI_WRAP(peek, Peek);
        VJ_CI_WRAP(poke, Poke);
        VJ_CI_WRAP(get_regs, GetRegs);
        VJ_CI_WRAP(set_reg, SetReg);
        VJ_CI_WRAP(disasm_risc, DisasmRisc);
        VJ_CI_WRAP(attach, Attach);
        VJ_CI_WRAP(detach, Detach);
        VJ_CI_WRAP(break_set, BreakSet);
        VJ_CI_WRAP(break_clear, BreakClear);
        VJ_CI_WRAP(halt_next, HaltNext);
        VJ_CI_WRAP(halted_target, HaltedTarget);
        VJ_CI_WRAP(halt_reason, HaltReason);
        VJ_CI_WRAP(op_snapshot_arm, OpArm);
        VJ_CI_WRAP(op_snapshot_get, OpGet);
        VJ_CI_WRAP(cry_to_rgb, CryToRgb);
        VJ_CI_WRAP(blit_log_enable, BlitEnable);
        VJ_CI_WRAP(blit_log_head, BlitHead);
        VJ_CI_WRAP(blit_log_get, BlitGet);
        VJ_CI_WRAP(texreplace_reload, TexReload);
        VJ_CI_WRAP(frame_number, FrameNo);
#undef VJ_CI_WRAP
    }
    CountingIface(const CountingIface&) = delete;
    CountingIface& operator=(const CountingIface&) = delete;

    const vj_debug_iface* iface() const { return &iface_; }
    static unsigned count(Fn f) { return counts()[f].load(); }
    static unsigned total() { return total_().load(); }
    static unsigned offCore() { return off_().load(); }   // calls made while no OnCore guard was alive on the calling thread
    static void reset() {
        for (int i = 0; i < kFns; i++) counts()[i] = 0;
        total_() = 0;
        off_() = 0;
    }
    // "This is the emulation thread": a guard alive on the calling thread (they nest).
    struct OnCore {
        OnCore() { depth()++; }
        ~OnCore() { depth()--; }
        OnCore(const OnCore&) = delete;
        OnCore& operator=(const OnCore&) = delete;
    };
    static bool onCore() { return depth() > 0; }

private:
    static int& depth() {
        static thread_local int d = 0;
        return d;
    }
    static const vj_debug_iface*& in() {
        static const vj_debug_iface* p = nullptr;
        return p;
    }
    static std::atomic<unsigned>* counts() {
        static std::atomic<unsigned> c[kFns];
        return c;
    }
    static std::atomic<unsigned>& total_() {
        static std::atomic<unsigned> t{0};
        return t;
    }
    static std::atomic<unsigned>& off_() {
        static std::atomic<unsigned> o{0};
        return o;
    }
    static void hit(Fn f) {
        counts()[f]++;
        total_()++;
        if (!onCore()) off_()++;
    }

    static int tRegion(int idx, vj_debug_region* o) { hit(Region); return in()->region(idx, o); }
    static int tPeek(unsigned a, unsigned char* d, unsigned n) { hit(Peek); return in()->peek(a, d, n); }
    static int tPoke(unsigned a, const unsigned char* s, unsigned n, unsigned fl) { hit(Poke); return in()->poke(a, s, n, fl); }
    static int tGetRegs(int t, vj_debug_regs* o) { hit(GetRegs); return in()->get_regs(t, o); }
    static int tSetReg(int t, int r, unsigned v) { hit(SetReg); return in()->set_reg(t, r, v); }
    static int tDisasmRisc(int t, unsigned a, char* out, int max, unsigned* len) {
        hit(DisasmRisc);
        return in()->disasm_risc(t, a, out, max, len);
    }
    static int tAttach(const vj_debug_host* h) { hit(Attach); return in()->attach(h); }
    static void tDetach() { hit(Detach); in()->detach(); }
    static int tBreakSet(int t, unsigned a) { hit(BreakSet); return in()->break_set(t, a); }
    static int tBreakClear(int t, unsigned a) { hit(BreakClear); return in()->break_clear(t, a); }
    static void tHaltNext(int t) { hit(HaltNext); in()->halt_next(t); }
    static int tHaltedTarget() { hit(HaltedTarget); return in()->halted_target(); }
    static int tHaltReason() { hit(HaltReason); return in()->halt_reason(); }
    static void tOpArm() { hit(OpArm); in()->op_snapshot_arm(); }
    static int tOpGet(vj_debug_op_obj* o, int max, unsigned* hl) { hit(OpGet); return in()->op_snapshot_get(o, max, hl); }
    static unsigned tCryToRgb(unsigned v) { hit(CryToRgb); return in()->cry_to_rgb(v); }
    static void tBlitEnable(int on) { hit(BlitEnable); in()->blit_log_enable(on); }
    static unsigned tBlitHead() { hit(BlitHead); return in()->blit_log_head(); }
    static int tBlitGet(unsigned i, vj_debug_blit* o) { hit(BlitGet); return in()->blit_log_get(i, o); }
    static int tTexReload() { hit(TexReload); return in()->texreplace_reload(); }
    static unsigned tFrameNo() { hit(FrameNo); return in()->frame_number(); }

    vj_debug_iface iface_;
};

// The mock core behind the counting iface, wired into a DebugSession the way App wires the emulation thread: the
// hooks count the outermost calls and posts, and run them at once under an OnCore guard.
struct UiFixture {
    MockDebugIface mock;
    CountingIface counted{mock.iface()};
    DebugSession s;
    unsigned coreCalls = 0, corePosts = 0;   // outermost only: a call inside a call is the emulation thread's own

    UiFixture() {
        s.coreCall = [this](std::function<void()> fn) {
            if (!CountingIface::onCore()) coreCalls++;
            CountingIface::OnCore on;
            fn();
        };
        s.corePost = [this](std::function<void()> fn) {
            if (!CountingIface::onCore()) corePosts++;
            CountingIface::OnCore on;
            fn();
        };
        s.setIface(counted.iface());
        s.setContentLoaded(true);
    }
    UiFixture(const UiFixture&) = delete;
    UiFixture& operator=(const UiFixture&) = delete;

    // What App::fillDebugViews does on the emulation thread, once per frame.
    void fill() {
        CountingIface::OnCore on;
        s.views().fill(s.core().iface(), s.refreshSerial());
    }

    struct Mark {
        unsigned total, coreCalls, corePosts;
    };
    Mark mark() const { return {CountingIface::total(), coreCalls, corePosts}; }
    bool quietSince(const Mark& m) const {
        return CountingIface::total() == m.total && coreCalls == m.coreCalls && corePosts == m.corePosts;
    }
};

}  // namespace vj

// DebugCore: the emulation-thread half of the debug tools (#967 phase 3a).  It owns the pointer to the core's
// vj_debug_iface and the vj_debug_host the app hands the core, and wraps every iface entry point so a caller
// (DebugSession's marshalled actions, DebugViews' fills) never repeats the null / size checks.
//
// Threading: every method that calls into the core runs on the emulation thread (inline mode: the UI thread, which
// is the same thing).  The few getters marked "atomic" (iface, loaded, ifaceGen, attached) are readable from any
// thread; they are what the UI polls without a round trip.  No ImGui, no SDL.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {

const char* dbgTargetName(int target);   // "68000" / "GPU" / "DSP" / "?"
const char* dbgReasonName(int reason);   // "breakpoint" / "step" / "break" / ""

// What the core's halt looks like from outside.  The UI mirrors it once per frame (DebugSession::syncHalt).
struct HaltState {
    bool halted = false;
    int target = -1;                 // VJ_DBG_TARGET_*
    unsigned pc = 0;                 // kept after the halt ends (the bar's last value)
    int reason = VJ_DBG_STOP_NONE;   // VJ_DBG_STOP_*
    uint64_t serial = 0;             // bumps on every ENTRY into a halt (a step's re-halt included) and on CONTINUE /
                                     // DETACH / a foreign resume; the STEP return itself does not bump it
};

class DebugCore {
public:
    static constexpr std::chrono::milliseconds kPumpWait{4};   // the pump's bounded wait per iteration

    DebugCore();                                         // fills host_ (size, user, halt_pump thunk)
    DebugCore(const DebugCore&) = delete;                // host_.user points at this
    DebugCore& operator=(const DebugCore&) = delete;

    // Replaces (and forgets) the old interface: the old one belongs to a core being replaced or torn down, so it is
    // never called.  A no-op when the pointer is unchanged; otherwise drops the attach and bumps ifaceGen().
    void setIface(const vj_debug_iface* i);
    const vj_debug_iface* iface() const { return iface_.load(); }   // atomic
    bool loaded() const { return iface_.load() != nullptr; }        // atomic: a debug interface is present
    // Bumps on every setIface() that changes the pointer (the core's struct is static, so the pointer alone repeats
    // across a reload: null in between is what the generation sees).
    uint64_t ifaceGen() const { return gen_.load(); }               // atomic

    void markDetached() { attached_ = false; }           // clears attached() without calling the core (content unloaded)

    // ---- iface wrappers: each returns the iface's own result; a missing iface or entry point returns the failure value --
    int peek(unsigned addr, uint8_t* dst, unsigned n) const;                 // VJ_DBG_ERR_STATE without one
    int poke(unsigned addr, const uint8_t* src, unsigned n, unsigned flags); // VJ_DBG_ERR_STATE without one
    bool getRegs(int target, vj_debug_regs& out) const;
    bool setReg(int target, int reg, unsigned v);
    bool disasmRisc(int target, unsigned addr, std::string& out, unsigned& len) const;
    bool attach();                                       // false: no iface, or the core refused; true if already attached
    void detach();                                       // clears the core's breakpoints and the host (only when attached)
    bool attached() const { return attached_.load(); }   // atomic
    bool breakSet(int target, unsigned addr);
    bool breakClear(int target, unsigned addr);
    void haltNext(int target);
    void opSnapshotArm();
    int opSnapshotGet(std::vector<vj_debug_op_obj>& out, unsigned& halfline) const;   // -1 while armed, not taken; 0 without
    unsigned cryToRgb(unsigned v16) const;
    bool buildCryTable(std::vector<uint32_t>& out) const;   // 65536 entries, 0x00RRGGBB; false without cry_to_rgb
    void blitLogEnable(bool on);
    unsigned blitLogHead() const;
    bool blitLogGet(unsigned i, vj_debug_blit& out) const;
    int texReplaceReload();                              // 1 active, 0 inactive, -1 not supported (size check)
    unsigned frameNumber() const;
    bool region(int idx, vj_debug_region& out) const;

    // ---- the halt (#967 phase 3b) -------------------------------------------------------------------------
    // The pump runs on the emulation thread inside the core's GDBHalt() loop, which owns the socket service, the halt
    // timeout and a 2 ms sleep.  So pump() never waits for a verdict itself: it calls waitVerdict (threaded) or
    // runUiFrame (inline) once and returns VJ_DBG_RESUME_NONE when nothing is decided; the core calls it again.
    HaltState halt() const;                              // any thread: a copy under a mutex
    uint64_t haltSerial() const { return serial_.load(); }   // any thread: atomic, == halt().serial
    // The UI's verdict (VJ_DBG_RESUME_CONTINUE / _STEP / _DETACH) for the halt whose serial is `forSerial` (the serial
    // the UI mirrored).  Stored as one atomic word (forSerial << 2 | v), then notifyVerdict() if set.  The pump takes it
    // only while the current halt's serial equals forSerial; otherwise it is dropped (a verdict for a dead halt never
    // resumes the next one).  A later offer replaces an earlier one.  Any thread.
    void offerVerdict(int v, uint64_t forSerial);
    int pendingVerdict() const;                          // tests: the stored verdict's value, NONE when none
    std::function<void()> notifyVerdict;                 // App: emu.debugPost([]{})
    std::function<void()> waitVerdict;                   // threaded: App::haltWait (serve the lane <= kPumpWait); inline: empty
    std::function<void()> runUiFrame;                    // inline only: App::haltedFrame
    std::function<bool()> abortHalt;                     // polled each iteration; true = CONTINUE, host kept attached (the app is quitting; see pump)
    // Emulation thread, on every ENTRY into a halt (a step's re-halt included).  Walked on copies, outside the mutex.
    std::vector<std::function<void(int target, unsigned pc, int reason)>> onHalt;
    std::vector<std::function<void()>> onResume;         // emulation thread, on CONTINUE / DETACH / frameEnded() (not STEP)
    int pump(int target, unsigned pc);                   // the halt_pump thunk's target; returns the verdict or NONE
    // Emulation thread, right after every retro_run().  If the core left GDBHalt() without one of our verdicts (gdb
    // continued, the socket dropped, the timeout fired) the halt is still open here: closes it, bumps the serial,
    // drops a verdict written for it and fires onResume.  `attached` is untouched (a foreign continue is not a
    // detach).  The same for a STEP in flight: pump() returned STEP (no serial bump, the bar stays up) and the frame
    // ended before the core re-halted (the stepped instruction ended it, or the target had stopped); the mirror would
    // otherwise stay "halted" and, inline, stepEmulation() (gated on halted()) would never run the frame that re-halts.
    // A no-op when neither.  Also where a Detach decided inside a halt completes: detachPending() -> detach(), outside
    // retro_run() (App puts the silent gdb host straight back when the stub is on).
    void frameEnded();

    // ---- leaving a halt (#1069, #1081 M1) ------------------------------------------------------------------------
    // The app never answers the pump with DETACH: inside GDBHalt() a dropped host lets a gdb client's still-armed Z0 park
    // the core again with nobody to pump.  Emulation thread (a lane post: inside the pump while halted, else at a frame
    // boundary).  Halted: offers CONTINUE for the live halt, and with `dropHost` marks the host to be dropped by
    // frameEnded() -- unless a new halt begins first (pump() cancels it: that halt needs the host).  Not halted and
    // `dropHost`: detaches now (outside retro_run).  Either way releaseSerial() becomes the current serial.  Returns
    // whether a live halt was released.  The caller disarms the app's breakpoints first (DebugSession), so the frame
    // can end even with one in a hot loop.
    bool releaseHalt(bool dropHost);
    bool detachPending() const { return detachAfterFrame_.load(); }   // any thread: a frame-end drop is owed
    void keepHost() { detachAfterFrame_ = false; }   // emulation thread: an attach() superseded the owed drop (#1081 M6)
    // The serial of the halt the last releaseHalt() found (or the serial it saw when none was live).  A call that
    // follows a release gives up only on a halt NEWER than this: App::releaseForCall's predicate is
    // `releaseSerial() >= s0 && halt().halted && halt().serial > releaseSerial()`, s0 the serial the UI mirrored, so a
    // release still queued (serial < s0) is waited for, and the release of a step's re-halt is not a give-up.
    uint64_t releaseSerial() const { return releaseSerial_.load(); }   // any thread

private:
    static int haltPumpThunk(void* user, int target, unsigned pc);
    int takeVerdict(uint64_t serial);                    // exchange(0); NONE unless the stored serial == serial
    void closeHaltLocked();                              // halted = false, target = -1, reason = NONE (pc kept); under haltMu_

    mutable std::mutex haltMu_;
    HaltState halt_;                                     // under haltMu_
    std::atomic<uint64_t> serial_{0};                    // mirrors halt_.serial
    std::atomic<bool> halted_{false};                    // mirrors halt_.halted (frameEnded's fast path)
    std::atomic<bool> stepping_{false};                  // pump() returned STEP and the core has not re-halted yet
    std::atomic<uint64_t> verdict_{0};                   // (forSerial << 2) | v; 0 = none
    std::atomic<bool> detachAfterFrame_{false};          // releaseHalt(true) inside a halt: frameEnded() drops the host
    std::atomic<uint64_t> releaseSerial_{0};             // see releaseSerial()

    std::atomic<const vj_debug_iface*> iface_{nullptr};
    std::atomic<bool> attached_{false};
    std::atomic<uint64_t> gen_{0};
    vj_debug_host host_{};
};

}  // namespace vj

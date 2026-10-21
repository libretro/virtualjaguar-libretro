// DebugCore: the emulation-thread half of the debug tools (#967 phase 3a).  It owns the pointer to the core's
// vj_debug_iface and the vj_debug_host the app hands the core, and wraps every iface entry point so a caller
// (DebugSession's marshalled actions, DebugViews' fills) never repeats the null / size checks.
//
// Threading: every method that calls into the core runs on the emulation thread (inline mode: the UI thread, which
// is the same thing).  The few getters marked "atomic" (iface, loaded, ifaceGen, attached) are readable from any
// thread; they are what the UI polls without a round trip.  No ImGui, no SDL.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {

class DebugCore {
public:
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

    // 3a only: the session keeps today's halt machine and installs it here.  pump() returns pumpHook ? pumpHook(t, pc)
    // : VJ_DBG_RESUME_DETACH.  Phase 3b (B1) moves the halt machine into this class and removes the hook.
    std::function<int(int target, unsigned pc)> pumpHook;
    void markDetached() { attached_ = false; }           // clears attached() without calling the core (the DETACH verdict path)
    int pump(int target, unsigned pc);                   // the halt_pump thunk's target

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

private:
    static int haltPumpThunk(void* user, int target, unsigned pc);

    std::atomic<const vj_debug_iface*> iface_{nullptr};
    std::atomic<bool> attached_{false};
    std::atomic<uint64_t> gen_{0};
    vj_debug_host host_{};
};

}  // namespace vj

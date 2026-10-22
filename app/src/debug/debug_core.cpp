#include "debug_core.h"

#include <cstddef>
#include <cstring>

namespace vj {

DebugCore::DebugCore() {
    std::memset(&host_, 0, sizeof host_);
    host_.size = sizeof host_;
    host_.user = this;
    host_.halt_pump = &DebugCore::haltPumpThunk;
}

const char* dbgTargetName(int target) {
    switch (target) {
    case VJ_DBG_TARGET_M68K: return "68000";
    case VJ_DBG_TARGET_GPU: return "GPU";
    case VJ_DBG_TARGET_DSP: return "DSP";
    default: return "?";
    }
}

const char* dbgReasonName(int reason) {
    switch (reason) {
    case VJ_DBG_STOP_BREAKPOINT: return "breakpoint";
    case VJ_DBG_STOP_STEP: return "step";
    case VJ_DBG_STOP_USER: return "break";
    default: return "";
    }
}

void DebugCore::setIface(const vj_debug_iface* i) {
    if (i == iface_.load()) return;
    // The old interface belongs to a core that is being replaced or torn down: do not call into it.  A live halt
    // belonged to it too: forget it (no callbacks -- the old core is gone, not resumed) and move the serial so a
    // mirror never confuses the two cores; a verdict written for it dies with it.
    attached_ = false;
    {
        std::lock_guard<std::mutex> l(haltMu_);
        if (halt_.halted || stepping_.load()) {
            closeHaltLocked();
            halt_.serial++;
            serial_ = halt_.serial;
        }
        stepping_ = false;
        verdict_ = 0;
    }
    iface_ = i;
    gen_++;
}

int DebugCore::haltPumpThunk(void* user, int target, unsigned pc) {
    return static_cast<DebugCore*>(user)->pump(target, pc);
}

// ---- the halt --------------------------------------------------------------------------------------------------

HaltState DebugCore::halt() const {
    std::lock_guard<std::mutex> l(haltMu_);
    return halt_;
}

void DebugCore::closeHaltLocked() {
    halt_.halted = false;
    halt_.target = -1;
    halt_.reason = VJ_DBG_STOP_NONE;   // pc is kept for the bar's last value
    halted_ = false;
}

void DebugCore::offerVerdict(int v, uint64_t forSerial) {
    if (v < VJ_DBG_RESUME_CONTINUE || v > VJ_DBG_RESUME_DETACH) return;
    verdict_.store((forSerial << 2) | (uint64_t)v);
    if (notifyVerdict) notifyVerdict();
}

int DebugCore::pendingVerdict() const { return (int)(verdict_.load() & 3u); }

int DebugCore::takeVerdict(uint64_t serial) {
    const uint64_t w = verdict_.exchange(0);
    if (w == 0 || (w >> 2) != serial) return VJ_DBG_RESUME_NONE;
    return (int)(w & 3u);
}

int DebugCore::pump(int target, unsigned pc) {
    bool entered = false;
    int reason = VJ_DBG_STOP_NONE;
    uint64_t serial = 0;
    {
        std::lock_guard<std::mutex> l(haltMu_);
        if (!halt_.halted || halt_.target != target || halt_.pc != pc) {
            reason = VJ_DBG_STOP_BREAKPOINT;
            // halt_reason was appended after the first release of the struct: check `size` first.
            const vj_debug_iface* ifc = iface();
            if (ifc && ifc->size >= offsetof(vj_debug_iface, halt_reason) + sizeof(ifc->halt_reason) && ifc->halt_reason)
                reason = ifc->halt_reason();
            halt_.halted = true;
            halt_.target = target;
            halt_.pc = pc;
            halt_.reason = reason;
            halt_.serial++;
            serial_ = halt_.serial;
            halted_ = true;
            stepping_ = false;   // the step (if that is what this is) landed
            entered = true;
        }
        serial = halt_.serial;
    }
    if (entered) {
        auto cbs = onHalt;   // a callback may register another
        for (auto& f : cbs)
            if (f) f(target, pc, reason);
    }
    // Quitting wins over any verdict: a stale Continue must never overtake it.
    int v = (abortHalt && abortHalt()) ? VJ_DBG_RESUME_DETACH : takeVerdict(serial);
    if (v == VJ_DBG_RESUME_NONE) {
        if (waitVerdict)
            waitVerdict();
        else if (runUiFrame)
            runUiFrame();
        v = (abortHalt && abortHalt()) ? VJ_DBG_RESUME_DETACH : takeVerdict(serial);
    }
    if (v == VJ_DBG_RESUME_NONE) return VJ_DBG_RESUME_NONE;

    {
        std::lock_guard<std::mutex> l(haltMu_);
        closeHaltLocked();
        verdict_ = 0;
        stepping_ = v == VJ_DBG_RESUME_STEP;
        if (v == VJ_DBG_RESUME_DETACH) attached_ = false;   // the core drops its side once this returns
        if (v != VJ_DBG_RESUME_STEP) {
            halt_.serial++;
            serial_ = halt_.serial;
        }
    }
    if (v != VJ_DBG_RESUME_STEP) {
        auto cbs = onResume;
        for (auto& f : cbs)
            if (f) f();
    }
    return v;
}

void DebugCore::frameEnded() {
    if (!halted_.load() && !stepping_.load()) return;
    {
        std::lock_guard<std::mutex> l(haltMu_);
        closeHaltLocked();
        stepping_ = false;
        halt_.serial++;
        serial_ = halt_.serial;
        verdict_ = 0;
    }
    auto cbs = onResume;
    for (auto& f : cbs)
        if (f) f();
}

// ---- iface wrappers -----------------------------------------------------------------------------------------

int DebugCore::peek(unsigned addr, uint8_t* dst, unsigned n) const {
    const vj_debug_iface* f = iface();
    if (!f || !f->peek) return VJ_DBG_ERR_STATE;
    return f->peek(addr, dst, n);
}

int DebugCore::poke(unsigned addr, const uint8_t* src, unsigned n, unsigned flags) {
    const vj_debug_iface* f = iface();
    if (!f || !f->poke) return VJ_DBG_ERR_STATE;
    return f->poke(addr, src, n, flags);
}

bool DebugCore::getRegs(int target, vj_debug_regs& out) const {
    const vj_debug_iface* f = iface();
    return f && f->get_regs && f->get_regs(target, &out);
}

bool DebugCore::setReg(int target, int reg, unsigned v) {
    const vj_debug_iface* f = iface();
    return f && f->set_reg && f->set_reg(target, reg, v);
}

bool DebugCore::disasmRisc(int target, unsigned addr, std::string& out, unsigned& len) const {
    const vj_debug_iface* f = iface();
    if (!f || !f->disasm_risc) return false;
    char buf[128];
    buf[0] = 0;
    unsigned l = 0;
    if (!f->disasm_risc(target, addr, buf, (int)sizeof buf, &l)) return false;
    buf[sizeof buf - 1] = 0;
    out = buf;
    len = l;
    return true;
}

bool DebugCore::attach() {
    const vj_debug_iface* f = iface();
    if (!f || !f->attach) return false;
    if (attached_.load()) return true;
    if (!f->attach(&host_)) return false;
    attached_ = true;
    return true;
}

void DebugCore::detach() {
    const vj_debug_iface* f = iface();
    if (attached_.load() && f && f->detach) f->detach();
    attached_ = false;
}

bool DebugCore::breakSet(int target, unsigned addr) {
    const vj_debug_iface* f = iface();
    return f && f->break_set && f->break_set(target, addr);
}

bool DebugCore::breakClear(int target, unsigned addr) {
    const vj_debug_iface* f = iface();
    return f && f->break_clear && f->break_clear(target, addr);
}

void DebugCore::haltNext(int target) {
    const vj_debug_iface* f = iface();
    if (f && f->halt_next) f->halt_next(target);
}

void DebugCore::opSnapshotArm() {
    const vj_debug_iface* f = iface();
    if (f && f->op_snapshot_arm) f->op_snapshot_arm();
}

int DebugCore::opSnapshotGet(std::vector<vj_debug_op_obj>& out, unsigned& halfline) const {
    const vj_debug_iface* f = iface();
    if (!f || !f->op_snapshot_get) {
        out.clear();
        return 0;
    }
    std::vector<vj_debug_op_obj> buf(VJ_DBG_OP_MAX_OBJECTS);
    const int n = f->op_snapshot_get(buf.data(), VJ_DBG_OP_MAX_OBJECTS, &halfline);
    if (n > 0)
        out.assign(buf.begin(), buf.begin() + n);
    else
        out.clear();
    return n;
}

unsigned DebugCore::cryToRgb(unsigned v16) const {
    const vj_debug_iface* f = iface();
    return f && f->cry_to_rgb ? f->cry_to_rgb(v16) & 0xFFFFFFu : 0u;
}

bool DebugCore::buildCryTable(std::vector<uint32_t>& out) const {
    const vj_debug_iface* f = iface();
    if (!f || !f->cry_to_rgb) return false;
    out.resize(0x10000);
    for (unsigned v = 0; v < 0x10000; v++) out[v] = f->cry_to_rgb(v) & 0xFFFFFFu;
    return true;
}

void DebugCore::blitLogEnable(bool on) {
    const vj_debug_iface* f = iface();
    if (f && f->blit_log_enable) f->blit_log_enable(on ? 1 : 0);
}

unsigned DebugCore::blitLogHead() const {
    const vj_debug_iface* f = iface();
    return f && f->blit_log_head ? f->blit_log_head() : 0u;
}

bool DebugCore::blitLogGet(unsigned i, vj_debug_blit& out) const {
    const vj_debug_iface* f = iface();
    return f && f->blit_log_get && f->blit_log_get(i, &out);
}

int DebugCore::texReplaceReload() {
    const vj_debug_iface* f = iface();
    // texreplace_reload was appended after the first release of the struct: check `size` first.
    if (!f || f->size < offsetof(vj_debug_iface, texreplace_reload) + sizeof f->texreplace_reload || !f->texreplace_reload)
        return -1;
    return f->texreplace_reload();
}

unsigned DebugCore::frameNumber() const {
    const vj_debug_iface* f = iface();
    return f && f->frame_number ? f->frame_number() : 0u;
}

bool DebugCore::region(int idx, vj_debug_region& out) const {
    const vj_debug_iface* f = iface();
    return f && f->region && f->region(idx, &out);
}

}  // namespace vj

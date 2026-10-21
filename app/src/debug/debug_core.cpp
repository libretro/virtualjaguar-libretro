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

void DebugCore::setIface(const vj_debug_iface* i) {
    if (i == iface_.load()) return;
    // The old interface belongs to a core that is being replaced or torn down: do not call into it.
    attached_ = false;
    iface_ = i;
    gen_++;
}

int DebugCore::haltPumpThunk(void* user, int target, unsigned pc) {
    return static_cast<DebugCore*>(user)->pump(target, pc);
}

int DebugCore::pump(int target, unsigned pc) {
    return pumpHook ? pumpHook(target, pc) : VJ_DBG_RESUME_DETACH;
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

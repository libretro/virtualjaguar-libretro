#include "debug_session.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace vj {

DebugSession::DebugSession() {
    std::memset(&host_, 0, sizeof host_);
    host_.size = sizeof host_;
    host_.user = this;
    host_.halt_pump = &DebugSession::haltPumpThunk;
}

// ---- wiring ------------------------------------------------------------------------------------------

void DebugSession::dropHalt() {
    halted_ = false;
    haltedTarget_ = -1;
    haltedPc_ = 0;
    haltReason_ = VJ_DBG_STOP_NONE;
    queued_ = VJ_DBG_RESUME_NONE;
}

void DebugSession::setIface(const vj_debug_iface* i) {
    if (i == iface_) return;
    const bool hadCore = iface_ != nullptr;
    // The old interface belongs to a core that is being replaced or torn down: do not call into it.
    attached_ = false;
    dropHalt();
    regions_.clear();
    iface_ = i;
    serial_++;
    if (hadCore && !i) closeAllTools("no core");
}

void DebugSession::setContentLoaded(bool loaded) {
    if (loaded == loaded_) return;
    loaded_ = loaded;
    if (!loaded) {
        // Unloading content resets the core's breakpoint tables and drops the host with it.
        attached_ = false;
        dropHalt();
        closeAllTools("closed");
    }
    serial_++;
}

void DebugSession::setHardcore(bool on) {
    if (on == hardcore_) return;
    hardcore_ = on;
    if (on) {
        // Inside a halt this queues a detach; outside it detaches now.
        detach();
        closeAllTools("hardcore");
    }
    serial_++;
}

std::string DebugSession::disabledReason() const {
    if (!iface_) return "This core build has no debug interface";
    if (hardcore_) return "Disabled while RetroAchievements hardcore mode is on";
    if (!loaded_) return "No game is running";
    return std::string();
}

// ---- attach / halt -------------------------------------------------------------------------------------------

bool DebugSession::attach() {
    if (!enabled()) return false;
    if (attached_) return true;
    if (!iface_->attach || !iface_->attach(&host_)) return false;
    attached_ = true;
    applyBreakpoints();
    return true;
}

void DebugSession::detach() {
    if (halted_) {
        detachAndResume();   // the core is parked inside the pump: let it resume first
        return;
    }
    if (attached_ && iface_ && iface_->detach) iface_->detach();
    attached_ = false;
    queued_ = VJ_DBG_RESUME_NONE;
}

void DebugSession::breakNow(int target) {
    if (!enabled() || halted_) return;
    if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return;
    if (!attach()) return;
    if (iface_->halt_next) iface_->halt_next(target);
}

void DebugSession::resume() {
    if (halted_) queued_ = VJ_DBG_RESUME_CONTINUE;
}
void DebugSession::step() {
    if (halted_) queued_ = VJ_DBG_RESUME_STEP;
}
void DebugSession::detachAndResume() {
    if (halted_) queued_ = VJ_DBG_RESUME_DETACH;
}

int DebugSession::haltPumpThunk(void* user, int target, unsigned pc) {
    return static_cast<DebugSession*>(user)->pump(target, pc);
}

int DebugSession::pump(int target, unsigned pc) {
    if (!halted_) {
        halted_ = true;
        haltedTarget_ = target;
        haltedPc_ = pc;
        haltReason_ = VJ_DBG_STOP_BREAKPOINT;
        // halt_reason was appended after the first release of the struct: check `size` first.
        if (iface_ && iface_->size >= offsetof(vj_debug_iface, halt_reason) + sizeof(iface_->halt_reason) &&
            iface_->halt_reason)
            haltReason_ = iface_->halt_reason();
        serial_++;
        auto cbs = onHalt;   // a callback may register another
        for (auto& f : cbs)
            if (f) f(target, pc, haltReason_);
    }
    int v = queued_;
    if (v == VJ_DBG_RESUME_NONE && abortHalt && abortHalt()) v = VJ_DBG_RESUME_DETACH;
    if (v == VJ_DBG_RESUME_NONE) {
        if (runUiFrame) runUiFrame();
        v = queued_;
        if (v == VJ_DBG_RESUME_NONE && abortHalt && abortHalt()) v = VJ_DBG_RESUME_DETACH;
    }
    if (v == VJ_DBG_RESUME_NONE) return VJ_DBG_RESUME_NONE;

    queued_ = VJ_DBG_RESUME_NONE;
    halted_ = false;
    haltedTarget_ = -1;
    haltReason_ = VJ_DBG_STOP_NONE;
    if (v == VJ_DBG_RESUME_DETACH) attached_ = false;   // the core drops its side once this returns
    if (v != VJ_DBG_RESUME_STEP) {
        serial_++;
        auto cbs = onResume;
        for (auto& f : cbs)
            if (f) f();
    }
    return v;
}

// ---- breakpoints -------------------------------------------------------------------------------------------------

void DebugSession::applyBreakpoints() {
    if (!attached_ || !iface_) return;
    for (const DebugBreakpoint& b : bps_)
        if (b.enabled && iface_->break_set) iface_->break_set(b.target, b.addr);
}

bool DebugSession::hasBreakpoint(int target, unsigned addr) const {
    for (const DebugBreakpoint& b : bps_)
        if (b.target == target && b.addr == addr) return true;
    return false;
}

bool DebugSession::addBreakpoint(int target, unsigned addr) {
    if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return false;
    if (hasBreakpoint(target, addr)) return false;
    if (!attach()) return false;   // refuses while the tools are off (no core / no game / hardcore)
    if (!iface_->break_set || !iface_->break_set(target, addr)) return false;   // the core's table is full
    DebugBreakpoint b;
    b.target = target;
    b.addr = addr;
    bps_.push_back(b);
    return true;
}

void DebugSession::removeBreakpoint(int target, unsigned addr) {
    auto it = std::find_if(bps_.begin(), bps_.end(),
                           [&](const DebugBreakpoint& b) { return b.target == target && b.addr == addr; });
    if (it == bps_.end()) return;
    if (attached_ && it->enabled && iface_ && iface_->break_clear) iface_->break_clear(target, addr);
    bps_.erase(it);
}

bool DebugSession::toggleBreakpoint(int target, unsigned addr) {
    if (hasBreakpoint(target, addr)) {
        removeBreakpoint(target, addr);
        return false;
    }
    return addBreakpoint(target, addr);
}

void DebugSession::setBreakpointEnabled(int target, unsigned addr, bool on) {
    for (DebugBreakpoint& b : bps_) {
        if (b.target != target || b.addr != addr || b.enabled == on) continue;
        b.enabled = on;
        if (!attached_ || !iface_) return;
        if (on && iface_->break_set) iface_->break_set(target, addr);
        if (!on && iface_->break_clear) iface_->break_clear(target, addr);
        return;
    }
}

void DebugSession::clearBreakpoints() {
    if (attached_ && iface_ && iface_->break_clear)
        for (const DebugBreakpoint& b : bps_)
            if (b.enabled) iface_->break_clear(b.target, b.addr);
    bps_.clear();
}

// ---- refresh cadence -------------------------------------------------------------------------------------------------

void DebugSession::tick(double now, bool live) {
    if (!live || halted_) {
        // Stopped: one last refresh when it stops, then only when something asks (an edit, a halt).
        if (lastRefresh_ >= 0) {
            serial_++;
            lastRefresh_ = -1;
        }
        return;
    }
    if (lastRefresh_ < 0 || now - lastRefresh_ >= 1.0 / kRefreshHz || now < lastRefresh_) {
        serial_++;
        lastRefresh_ = now;
    }
}

// ---- memory helpers -------------------------------------------------------------------------------------------------------

bool DebugSession::read(unsigned addr, std::vector<uint8_t>& out, unsigned n) const {
    out.clear();
    if (!iface_ || !iface_->peek || n == 0) return false;
    out.resize(n);
    if (iface_->peek(addr, out.data(), n) != (int)n) {
        out.clear();
        return false;
    }
    return true;
}

bool DebugSession::readU(unsigned addr, unsigned bytes, unsigned& v) const {
    if (bytes != 1 && bytes != 2 && bytes != 4) return false;
    uint8_t b[4];
    if (!iface_ || !iface_->peek || iface_->peek(addr, b, bytes) != (int)bytes) return false;
    v = 0;
    for (unsigned i = 0; i < bytes; i++) v = (v << 8) | b[i];
    return true;
}

int DebugSession::poke(unsigned addr, const uint8_t* data, unsigned n) {
    if (!enabled() || !iface_->poke) return lastPokeError_ = VJ_DBG_ERR_STATE;
    unsigned flags = 0;
    if (allowRegisterWrites) flags |= VJ_DBG_POKE_REGS;
    if (allowRegisterWrites && allowDangerousWrites) flags |= VJ_DBG_POKE_DANGEROUS;
    int rc = iface_->poke(addr, data, n, flags);
    lastPokeError_ = rc < 0 ? rc : 0;
    if (rc >= 0) serial_++;
    return rc;
}

int DebugSession::pokeU(unsigned addr, unsigned bytes, unsigned v) {
    if (bytes != 1 && bytes != 2 && bytes != 4) return lastPokeError_ = VJ_DBG_ERR_RANGE;
    uint8_t b[4];
    for (unsigned i = 0; i < bytes; i++) b[i] = (uint8_t)(v >> (8 * (bytes - 1 - i)));
    return poke(addr, b, bytes);
}

const char* DebugSession::errorText(int e) {
    switch (e) {
    case VJ_DBG_ERR_RANGE: return "address is outside the Jaguar's memory map";
    case VJ_DBG_ERR_POLICY:
        return "register writes are off (tick \"Allow register writes\"); the CD registers are never writable";
    case VJ_DBG_ERR_READONLY: return "that region is read-only (ROM)";
    case VJ_DBG_ERR_STATE: return "no game is running";
    default: return e < 0 ? "write refused" : "";
    }
}

const std::vector<vj_debug_region>& DebugSession::regions() {
    if (regions_.empty() && iface_ && iface_->region) {
        vj_debug_region r;
        for (int i = 0; i < 64 && iface_->region(i, &r); i++) regions_.push_back(r);
    }
    return regions_;
}

// ---- Tools menu registry ---------------------------------------------------------------------------------------------------------

void DebugSession::registerTool(const DebugTool& t) {
    for (DebugTool& e : tools_)
        if (e.id == t.id) {
            e = t;
            return;
        }
    tools_.push_back(t);
}

DebugTool* DebugSession::findTool(const std::string& id) {
    for (DebugTool& e : tools_)
        if (e.id == id) return &e;
    return nullptr;
}

void DebugSession::registerPlaceholderTools() {
    static const struct {
        const char* id;
        const char* label;
    } kTools[] = {
        {"memory", "Memory viewer..."}, {"clut", "Palette (CLUT)..."}, {"scanner", "Cheat scanner..."},
        {"cpu", "CPU debugger..."},     {"objects", "Object list..."}, {"blitter", "Blitter log..."},
        {"textures", "Textures..."},
    };
    for (const auto& t : kTools) {
        if (findTool(t.id)) continue;   // a real window got there first
        DebugTool d;
        d.id = t.id;
        d.label = t.label;
        tools_.push_back(d);
    }
}

void DebugSession::closeAllTools(const char* why) {
    auto cbs = onForcedClose;
    for (auto& f : cbs)
        if (f) f(why);
}

}  // namespace vj

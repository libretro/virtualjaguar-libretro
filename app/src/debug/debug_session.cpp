#include "debug_session.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace vj {

DebugSession::DebugSession() {}

void DebugSession::callCore(std::function<void()> fn) const {
    if (coreCall)
        coreCall(std::move(fn));
    else
        fn();
}

void DebugSession::postCore(std::function<void()> fn) {
    if (corePost)
        corePost(std::move(fn));
    else
        fn();
}

// ---- wiring ------------------------------------------------------------------------------------------

void DebugSession::dropHalt() {
    mirror_ = HaltState();
    byGdb_ = userHaltPending_ = detachPending_ = attachedMirror_ = false;
}

void DebugSession::onCoreIface(const vj_debug_iface* i) {
    // Emulation thread: only the core's own atomics and the views.  The epoch moves here, on the core's side of the
    // swap, so a fill can never stamp the new epoch on the old game's bytes.
    core_.setIface(i);
    views_.bumpEpoch();
}

void DebugSession::syncCore() {
    const uint64_t gen = core_.ifaceGen();
    if (gen == seenGen_) return;
    seenGen_ = gen;
    // A fill posted before the swap that never ran must not block the next one (a lost post is harmless: fills are
    // idempotent per serial).
    wakePosted_ = false;
    dropHalt();
    regions_.clear();
    bps_.clear();   // a breakpoint never lingers into the next core instance / game
    serial_++;
    // A core that went away, or was replaced: threaded, openContent() publishes null and the new interface inside one
    // blocking call, so a still-loaded core here can be a different game.  Either way the tools close exactly as
    // inline does (null seen, then the new interface), so the cheat scanner's candidates never meet the new game's RAM.
    if (hadCore_) closeAllTools("no core");
    hadCore_ = core_.loaded();
}

void DebugSession::setIface(const vj_debug_iface* i) {
    onCoreIface(i);
    syncCore();
}

void DebugSession::setContentLoaded(bool loaded) {
    if (loaded == loaded_) return;
    loaded_ = loaded;
    if (!loaded) {
        // Unloading content resets the core's breakpoint tables and drops the host with it.
        core_.markDetached();
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

void DebugSession::setHaltAvailable(bool on) {
    if (on == haltAvailable_) return;
    haltAvailable_ = on;
    if (!on) detach();
    serial_++;
}

std::string DebugSession::haltDisabledReason() const {
    if (haltAvailable_) return std::string();
    return "Break, Step and breakpoints are not available while Settings > Interface > Emulation thread is on (this version)";
}

std::string DebugSession::disabledReason() const {
    if (!core_.loaded()) return "This core build has no debug interface";
    if (hardcore_) return "Disabled while RetroAchievements hardcore mode is on";
    if (!loaded_) return "No game is running";
    return std::string();
}

void DebugSession::syncHalt() {
    // A detach we posted but the emulation thread has not run yet keeps the mirror false (attach() orders itself behind it).
    if (detachQueued_.load() == 0) attachedMirror_ = core_.attached();
    const HaltState h = core_.halt();
    if (h.serial == mirror_.serial) return;   // current; a STEP in flight keeps the bar up (the serial does not move)
    const bool was = mirror_.halted;
    mirror_ = h;
    detachPending_ = false;                   // the pump took (or the core ended) the halt a DETACH verdict was for
    if (h.halted) {
        byGdb_ = h.reason == VJ_DBG_STOP_BREAKPOINT ? !hasBreakpoint(h.target, h.pc) : !userHaltPending_;
        userHaltPending_ = false;
        requestRefresh();   // the registers changed: the windows re-read at once
        auto cbs = onHaltUi;   // a callback may register another
        for (auto& f : cbs)
            if (f) f(h.target, h.pc, h.reason);
    } else if (was) {
        auto cbs = onResumeUi;
        for (auto& f : cbs)
            if (f) f();
    }
}

// ---- attach / halt -------------------------------------------------------------------------------------------

bool DebugSession::attach() {
    if (!enabled() || !haltAvailable_ || detachPending_) return false;
    if (attachedMirror_ && detachQueued_.load() == 0 && core_.attached()) return true;
    // The core's table is rebuilt from the list on every attach: a by-value copy of the enabled entries.
    std::vector<DebugBreakpoint> bps;
    for (const DebugBreakpoint& b : bps_)
        if (b.enabled) bps.push_back(b);
    // Decided inside the lane lambda, so a detach queued before this call runs first (the lane is FIFO) and the table
    // is rebuilt only when this call is the one that attached.
    bool ok = false;
    callCore([this, bps, &ok] {
        const bool was = core_.attached();
        ok = core_.attach();
        if (!ok || was) return;
        for (const DebugBreakpoint& b : bps) core_.breakSet(b.target, b.addr);
    });
    attachedMirror_ = ok;
    return ok;
}

// The mirrored halt is still the core's live halt: same serial AND the core is parked in the pump.  After a STEP verdict
// the serial is unchanged but the core is running (halted == false) until it re-halts, so a verdict written then would
// be dropped by the re-halt's new serial.
bool DebugSession::haltLive() const {
    if (!mirror_.halted) return false;
    const HaltState h = core_.halt();
    return h.halted && h.serial == mirror_.serial;
}

void DebugSession::postDetach() {
    detachQueued_++;
    postCore([this] {
        core_.detach();
        detachQueued_--;
        // Run by the halt pump's lane service while a halt is live (a detach that raced a re-halt): the host is gone, so
        // let the pump go too instead of leaving the core parked behind a detached debugger.
        const HaltState h = core_.halt();
        if (h.halted) core_.offerVerdict(VJ_DBG_RESUME_DETACH, h.serial);
    });
    attachedMirror_ = false;
}

void DebugSession::detach() {
    if (haltLive()) {
        detachAndResume();   // the core is parked inside the pump: the DETACH verdict lets it go (never iface->detach from here)
        return;
    }
    // Running, a STEP in flight, or the mirrored halt already ended: a lane post, unconditionally (it may follow a queued attach).
    postDetach();
}

void DebugSession::breakNow(int target) {
    if (!enabled() || halted() || !haltAvailable_ || detachPending_) return;
    if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return;
    if (!attach()) return;
    userHaltPending_ = true;
    postCore([this, target] { core_.haltNext(target); });
}

void DebugSession::resume() {
    if (!detachPending_ && haltLive()) core_.offerVerdict(VJ_DBG_RESUME_CONTINUE, haltSerial());
}

void DebugSession::step() {
    if (detachPending_ || !haltLive()) return;
    userHaltPending_ = true;
    core_.offerVerdict(VJ_DBG_RESUME_STEP, haltSerial());
}

void DebugSession::detachAndResume() {
    if (!halted()) return;
    if (!haltLive()) {   // a STEP in flight or a stale mirror: a verdict would be dropped; detach through the lane
        postDetach();
        return;
    }
    detachPending_ = true;
    core_.offerVerdict(VJ_DBG_RESUME_DETACH, haltSerial());
}

// ---- breakpoints -------------------------------------------------------------------------------------------------

bool DebugSession::hasBreakpoint(int target, unsigned addr) const {
    for (const DebugBreakpoint& b : bps_)
        if (b.target == target && b.addr == addr) return true;
    return false;
}

bool DebugSession::addBreakpoint(int target, unsigned addr) {
    if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return false;
    if (hasBreakpoint(target, addr)) return false;
    if (!attach()) return false;   // refuses while the tools are off (no core / no game / hardcore / no halt)
    bool set = false;
    callCore([this, target, addr, &set] { set = core_.breakSet(target, addr); });
    if (!set) return false;   // the core's table is full
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
    if (core_.attached() && it->enabled) postCore([this, target, addr] { core_.breakClear(target, addr); });
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
        if (!core_.attached()) return;
        postCore([this, target, addr, on] {
            if (on)
                core_.breakSet(target, addr);
            else
                core_.breakClear(target, addr);
        });
        return;
    }
}

void DebugSession::clearBreakpoints() {
    if (core_.attached()) {
        std::vector<DebugBreakpoint> bps = bps_;
        postCore([this, bps] {
            for (const DebugBreakpoint& b : bps)
                if (b.enabled) core_.breakClear(b.target, b.addr);
        });
    }
    bps_.clear();
}

// ---- refresh cadence -------------------------------------------------------------------------------------------------

void DebugSession::tick(double now, bool live) {
    if (!live || halted()) {
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

// ---- the view copier ----------------------------------------------------------------------------------------------------

size_t DebugSession::fillViews() { return views_.fill(core_.iface(), serial_.load()); }

void DebugSession::wakeViews() {
    // Not gated on hardcore: the tools it closes hold no requests, and Neuro's watches must keep filling.
    if (!core_.loaded() || !loaded_) return;
    if (!corePost) {
        fillViews();
        return;
    }
    if (views_.pending(serial_.load()) && !wakePosted_.exchange(true)) {
        corePost([this] {
            wakePosted_ = false;
            fillViews();
        });
    }
}

// ---- memory helpers -------------------------------------------------------------------------------------------------------

bool DebugSession::read(unsigned addr, std::vector<uint8_t>& out, unsigned n) const {
    out.clear();
    if (!core_.loaded() || n == 0) return false;
    out.resize(n);
    int rc = VJ_DBG_ERR_STATE;
    callCore([this, addr, n, &out, &rc] { rc = core_.peek(addr, out.data(), n); });
    if (rc != (int)n) {
        out.clear();
        return false;
    }
    return true;
}

bool DebugSession::readU(unsigned addr, unsigned bytes, unsigned& v) const {
    if (bytes != 1 && bytes != 2 && bytes != 4) return false;
    if (!core_.loaded()) return false;
    uint8_t b[4];
    int rc = VJ_DBG_ERR_STATE;
    callCore([this, addr, bytes, &b, &rc] { rc = core_.peek(addr, b, bytes); });
    if (rc != (int)bytes) return false;
    v = 0;
    for (unsigned i = 0; i < bytes; i++) v = (v << 8) | b[i];
    return true;
}

int DebugSession::poke(unsigned addr, const uint8_t* data, unsigned n) {
    if (!enabled()) return lastPokeError_ = VJ_DBG_ERR_STATE;
    unsigned flags = 0;
    if (allowRegisterWrites) flags |= VJ_DBG_POKE_REGS;
    if (allowRegisterWrites && allowDangerousWrites) flags |= VJ_DBG_POKE_DANGEROUS;
    int rc = VJ_DBG_ERR_STATE;
    callCore([this, addr, data, n, flags, &rc] { rc = core_.poke(addr, data, n, flags); });
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
    if (regions_.empty() && core_.loaded()) {
        std::vector<vj_debug_region> rs;
        callCore([this, &rs] {
            vj_debug_region r;
            for (int i = 0; i < 64 && core_.region(i, r); i++) rs.push_back(r);
        });
        regions_ = std::move(rs);
    }
    return regions_;
}

bool DebugSession::getRegsNow(int target, vj_debug_regs& out) const {
    bool ok = false;
    callCore([this, target, &out, &ok] { ok = core_.getRegs(target, out); });
    return ok;
}

bool DebugSession::setReg(int target, int reg, unsigned v) {
    bool ok = false;
    callCore([this, target, reg, v, &ok] { ok = core_.setReg(target, reg, v); });
    return ok;
}

bool DebugSession::buildCryTable(std::vector<uint32_t>& out) const {
    bool ok = false;
    callCore([this, &out, &ok] { ok = core_.buildCryTable(out); });
    return ok;
}

void DebugSession::opSnapshotArm() {
    callCore([this] { core_.opSnapshotArm(); });
}

void DebugSession::blitLogEnable(bool on) {
    callCore([this, on] { core_.blitLogEnable(on); });
}

int DebugSession::texReplaceReload() {
    int rc = -1;
    callCore([this, &rc] { rc = core_.texReplaceReload(); });
    return rc;
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
        {"textures", "Textures..."},     {"scope", "Audio scope..."},
    };
    for (const auto& t : kTools) {
        if (findTool(t.id)) continue;   // a real window got there first
        DebugTool d;
        d.id = t.id;
        d.label = t.label;
        tools_.push_back(d);
    }
}

void DebugSession::addProvider(const DebugProvider& p) {
    providers_.push_back(p);
    if (p.registerTools) p.registerTools(*this);
}

void DebugSession::drawTools() {
    // A draw hook may register or open things: walk copies of the callables.
    std::vector<DebugProvider> ps = providers_;
    for (const DebugProvider& p : ps)
        if (p.draw) p.draw(*this);
    std::vector<std::function<void()>> draws;
    for (const DebugTool& t : tools_)
        if (t.draw) draws.push_back(t.draw);
    for (auto& f : draws) f();
}

void DebugSession::closeAllTools(const char* why) {
    auto cbs = onForcedClose;
    for (auto& f : cbs)
        if (f) f(why);
}

}  // namespace vj

#include "debug_session.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
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
    byGdb_ = userHaltPending_ = attachedMirror_ = disarmed_ = false;
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
    bps_.clear();   // a breakpoint never lingers into the next core instance / game (a reload of the same program
    watch_ = DebugWatch();   // puts its list back with adopt())
    serial_++;
    // A core that went away, or was replaced: threaded, openContent() publishes null and the new interface inside one
    // blocking call, so a still-loaded core here can be a different game.  Either way the tools close exactly as
    // inline does (null seen, then the new interface), so the cheat scanner's candidates never meet the new game's RAM.
    // A reload of the same program (setKeepToolsOnNextCore) says "reload": the windows that survive a core swap stay.
    // Inline the swap is seen twice (null, then the new interface): the flag lasts until a core is back.
    const bool reload = keepTools_;
    if (core_.loaded()) keepTools_ = false;
    if (hadCore_) closeAllTools(reload ? "reload" : "no core");
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

std::string DebugSession::disabledReason() const {
    if (!core_.loaded()) return "This core build has no debug interface";
    if (hardcore_) return "Disabled while RetroAchievements hardcore mode is on";
    if (!loaded_) return "No game is running";
    return std::string();
}

void DebugSession::syncHalt() {
    // A detach we posted but the emulation thread has not run yet, or whose frame-end host drop is still owed, keeps the
    // mirror false (attach() orders itself behind it).
    if (!detachPending()) attachedMirror_ = core_.attached();
    const HaltState h = core_.halt();
    if (h.serial == mirror_.serial) return;   // current; a STEP in flight keeps the bar up (the serial does not move)
    const bool was = mirror_.halted;
    mirror_ = h;
    if (h.halted) {
        if (h.reason == VJ_DBG_STOP_BREAKPOINT) byGdb_ = !hasBreakpoint(h.target, h.pc) && !(ownsBreakAt && ownsBreakAt(h.target, h.pc));
        else if (h.reason == VJ_DBG_STOP_WATCHPOINT) byGdb_ = !watch_.set;   // one slot: ours, or a gdb client's
        else {
            // A break at boot is asked for on the emulation thread, inside the load: the core holds that claim.
            const bool owned = h.reason == VJ_DBG_STOP_USER && core_.takeOwnedHalt();
            byGdb_ = !userHaltPending_ && !owned;
        }
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

std::vector<DebugBreakpoint> DebugSession::enabledBreakpoints() const {
    std::vector<DebugBreakpoint> bps;
    for (const DebugBreakpoint& b : bps_)
        if (b.enabled) bps.push_back(b);
    return bps;
}

bool DebugSession::attach() {
    if (!enabled()) return false;
    if (attachedMirror_ && !detachPending() && core_.attached()) return true;
    // The core's table is rebuilt from the list on every attach: a by-value copy of the enabled entries.
    const std::vector<DebugBreakpoint> bps = enabledBreakpoints();
    const DebugWatch watch = watch_;
    // Decided inside the lane lambda, so a detach queued before this call runs first (the lane is FIFO) and the table
    // is rebuilt only when this call is the one that attached.  A Detach decided inside a halt leaves the host for the
    // frame end (#1069); an attach in that window supersedes the drop (#1081 M6: Detach, then Break, just works) -- the
    // table was disarmed by the Detach and the list emptied, so there is nothing stale to keep.
    bool ok = false;
    callCore([this, bps, watch, &ok] {
        const bool was = core_.attached();
        if (was) core_.keepHost();
        ok = core_.attach();
        if (!ok || was) return;
        for (const DebugBreakpoint& b : bps) core_.breakSet(b.target, b.addr);
        if (watch.set) core_.watchSet(watch.addr, watch.len, watch.kind);
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

// The post decides on the emulation thread with the core's real state, so a stale mirror (gdb continued, a STEP in
// flight) and a post that raced a re-halt all land right: served inside the pump when a halt is live (disarm, then
// CONTINUE), at a frame boundary otherwise.  It is queued unconditionally (it may follow a queued attach: the lane is FIFO).
void DebugSession::postRelease(std::vector<DebugBreakpoint> bps, bool dropHost) {
    if (dropHost) detachQueued_++;
    const bool watch = watch_.set;
    postCore([this, bps, dropHost, watch] {
        // Inside a live halt: the list's entries must go before the CONTINUE, or one in a hot loop re-halts the core
        // before the frame can end (and the host would never be dropped / the command never run).  The watchpoint too.
        if (core_.halt().halted) {
            for (const DebugBreakpoint& b : bps) core_.breakClear(b.target, b.addr);
            if (watch) core_.watchClear();
        }
        core_.releaseHalt(dropHost);
        if (dropHost) detachQueued_--;   // after releaseHalt: the frame-end drop (detachPending) takes over without a gap
    });
    if (dropHost) attachedMirror_ = false;
}

void DebugSession::detach() {
    std::vector<DebugBreakpoint> bps = enabledBreakpoints();
    clearBreakpointList();   // Detach clears every breakpoint: the list now, the core's table in the post / the host drop
    disarmed_ = false;       // nothing left to re-arm
    postRelease(std::move(bps), true);   // (reads watch_: the post clears the core's slot inside a halt; the host drop does otherwise)
    if (watch_.set) {
        watch_ = DebugWatch();
        serial_++;
    }
}

void DebugSession::releaseHalt() {
    if (!core_.loaded()) return;
    disarmed_ = true;   // the post disarms the enabled entries when it finds the core halted; rearmBreakpoints() restores them
    postRelease(enabledBreakpoints(), false);
}

void DebugSession::rearmBreakpoints() {
    if (!disarmed_) return;
    // The command replaced the core (open / close / reload): the list is per game, syncCore() drops it and this flag; a
    // breakSet now would arm the OLD game's addresses in the new core.
    if (core_.ifaceGen() != seenGen_) return;
    disarmed_ = false;
    if (!core_.attached()) return;   // (a Detach meanwhile: the list is empty anyway; attach() rebuilds the table from the list)
    const std::vector<DebugBreakpoint> bps = enabledBreakpoints();
    const DebugWatch watch = watch_;
    if (bps.empty() && !watch.set) return;
    postCore([this, bps, watch] {
        for (const DebugBreakpoint& b : bps) core_.breakSet(b.target, b.addr);   // idempotent in the core
        if (watch.set) core_.watchSet(watch.addr, watch.len, watch.kind);
    });
}

void DebugSession::breakNow(int target) {
    if (!enabled() || halted()) return;
    if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return;
    if (!attach()) return;
    userHaltPending_ = true;
    postCore([this, target] { core_.haltNext(target); });
}

void DebugSession::resume() {
    if (!detachPending() && haltLive()) core_.offerVerdict(VJ_DBG_RESUME_CONTINUE, haltSerial());
}

void DebugSession::step() {
    if (detachPending() || !haltLive()) return;
    userHaltPending_ = true;
    core_.offerVerdict(VJ_DBG_RESUME_STEP, haltSerial());
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
    if (!attach()) return false;   // refuses while the tools are off (no core / no game / hardcore); a halt is not needed
    bool set = false;
    callCore([this, target, addr, &set] { set = core_.breakSet(target, addr); });
    if (!set) return false;   // the core's table is full
    DebugBreakpoint b;
    b.target = target;
    b.addr = addr;
    bindSymbol(addr, b.symbol, b.symbolOffset);
    bps_.push_back(b);
    return true;
}

// ---- symbols, the watchpoint, and the list across a reload -----------------------------------------------------------

std::string DebugSession::boundName(const std::string& symbol, unsigned offset) {
    if (symbol.empty() || offset == 0) return symbol;
    char buf[16];
    std::snprintf(buf, sizeof buf, "+$%X", offset);
    return symbol + buf;
}

void DebugSession::bindSymbol(unsigned addr, std::string& symbol, unsigned& offset) {
    symbol.clear();
    offset = 0;
    if (!symbols_.ensure()) return;
    uint32_t off = 0;
    // Close to a label only: a breakpoint far past the last label of a table is not "in" that function.
    if (symbols_.nearest(addr, 0x400, symbol, off)) offset = off;
}

bool DebugSession::setWatch(unsigned addr, unsigned len, unsigned kind) {
    kind &= VJ_DBG_WATCH_WRITE | VJ_DBG_WATCH_READ;
    if (!kind || len == 0 || addr > 0xFFFFFFu || len > 0x1000000u - addr) return false;
    if (!core_.watchSupported() || !attach()) return false;
    bool set = false;
    callCore([this, addr, len, kind, &set] { set = core_.watchSet(addr, len, kind); });
    if (!set) return false;   // a gdb client holds the slot at another address
    watch_ = DebugWatch();
    watch_.set = true;
    watch_.addr = addr;
    watch_.len = len;
    watch_.kind = kind;
    bindSymbol(addr, watch_.symbol, watch_.symbolOffset);
    serial_++;
    return true;
}

void DebugSession::clearWatch() {
    if (!watch_.set) return;
    watch_ = DebugWatch();
    if (core_.attached()) postCore([this] { core_.watchClear(); });
    serial_++;
}

DebugCarry DebugSession::carry() const {
    DebugCarry c;
    c.bps = bps_;
    c.watch = watch_;
    return c;
}

int DebugSession::rebind(DebugCarry& c) const {
    int moved = 0;
    auto move = [this, &moved](const std::string& symbol, unsigned offset, unsigned& addr, bool code) {
        uint32_t v = 0;
        if (symbol.empty() || !symbols_.find(symbol, v)) return;
        unsigned a = (v + offset) & 0x00FFFFFFu;
        if (code) a &= ~1u;   // a program counter is even
        if (a != addr) moved++;
        addr = a;
    };
    for (DebugBreakpoint& b : c.bps) move(b.symbol, b.symbolOffset, b.addr, true);
    if (c.watch.set) {
        move(c.watch.symbol, c.watch.symbolOffset, c.watch.addr, false);
        if (c.watch.len > 0x1000000u - c.watch.addr) c.watch.len = 0x1000000u - c.watch.addr;
    }
    // Two entries can land on one address (a function that shrank): keep the first.
    std::vector<DebugBreakpoint> unique;
    for (const DebugBreakpoint& b : c.bps) {
        bool dup = false;
        for (const DebugBreakpoint& u : unique)
            if (u.target == b.target && u.addr == b.addr) dup = true;
        if (!dup) unique.push_back(b);
    }
    c.bps.swap(unique);
    return moved;
}

bool DebugSession::armOnCore(const DebugCarry& c, bool haltAtBoot) {
    if (!core_.attach()) return false;
    core_.keepHost();   // (a Detach's owed frame-end drop belongs to the previous core; setIface cleared it anyway)
    for (const DebugBreakpoint& b : c.bps)
        if (b.enabled) core_.breakSet(b.target, b.addr);
    if (c.watch.set) core_.watchSet(c.watch.addr, c.watch.len, c.watch.kind);
    if (haltAtBoot) core_.haltNextOwned(VJ_DBG_TARGET_M68K);
    return true;
}

void DebugSession::armBootHaltOnCore() {
    if (core_.attach()) core_.haltNextOwned(VJ_DBG_TARGET_M68K);
}

void DebugSession::adopt(const DebugCarry& c, bool armed) {
    syncCore();   // the new core first: it drops the previous list, and must not drop this one a frame later
    bps_ = c.bps;
    watch_ = c.watch;
    if (watch_.set && !core_.watchSupported()) watch_ = DebugWatch();
    attachedMirror_ = armed && core_.attached();
    serial_++;
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
        if (!core_.attached() || disarmed_) return;   // disarmed for a release: rearmBreakpoints() sets what is enabled then
        postCore([this, target, addr, on] {
            if (on)
                core_.breakSet(target, addr);
            else
                core_.breakClear(target, addr);
        });
        return;
    }
}

// The UI-side list only: the core's table is cleared by its own detach.
void DebugSession::clearBreakpointList() {
    if (bps_.empty()) return;
    bps_.clear();
    serial_++;
}

void DebugSession::clearBreakpoints() {
    disarmed_ = false;   // nothing left to re-arm
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
        {"cpu", "CPU debugger..."},     {"symbols", "Symbols..."},     {"objects", "Object list..."},
        {"blitter", "Blitter log..."},  {"textures", "Textures..."},   {"scope", "Audio scope..."},
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

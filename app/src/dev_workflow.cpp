// The developer loop around the debugger (docs/app-debug-tools.md, "Homebrew workflow"): Break at start, breakpoints
// that survive a rebuild, reload when the file changes, and the command line's --break / --watch / --symbols.
//
// A load has three debugger steps, because the core lives on the emulation thread and the breakpoint list on the UI's:
//   planDebugBoot       UI thread, before the load call: what to arm (the list of the program being reloaded, moved to
//                       the new build's symbols; the command line's requests; the boot halt)
//   armDebugBootOnCore  emulation thread, inside the load call, after the new core is up and before its first frame
//   finishDebugBoot     UI thread, after the call: the session adopts the list it planned
#include "app.h"

#include <cstdio>

#include "debug/dev_boot.h"
#include "debug/symbol_table.h"

namespace vj {

namespace {

constexpr double kWatchPollSeconds = 0.25;

std::string hex6(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof b, "$%06X", v);
    return b;
}

}  // namespace

bool App::toolsAllowedNextGame() const { return !debug.hardcore() && !ach.hardcoreNextGame(); }

void App::setBreakAtBoot(bool on) {
    breakAtBoot = on;
    cfg.setBool("debug.break_at_boot", on);
    saveConfig();
}

void App::setReloadOnChange(bool on) {
    reloadOnChange = on;
    cfg.setBool("debug.reload_on_change", on);
    saveConfig();
    // From what is on disk now: a build made while the switch was off is not picked up after the fact.
    if (on && host.isLoaded() && !host.loadedPath().empty()) contentWatch_.watch(host.loadedPath(), stampFile(host.loadedPath()));
}

App::DebugBoot App::planDebugBoot(const std::string& path) {
    DebugBoot b;
    SymbolStore& sym = debug.symbols();
    b.sameProgram = !path.empty() && host.isLoaded() && path == host.loadedPath();
    sym.setContent(path);
    sym.invalidate();   // read again on first use: the same path can be a new build
    if (!toolsAllowedNextGame()) return b;   // hardcore: no tools, so nothing to arm (and no halt)
    if (b.sameProgram) {
        b.carry = debug.carry();
        debug.setKeepToolsOnNextCore(true);   // the developer's windows stay where they are across the reload
    }
    if (devSpecsPending_ && !path.empty()) {   // --break / --watch: once, for the first game
        devSpecsPending_ = false;
        sym.ensure();
        for (const std::string& text : devBreaks_) {
            BreakSpec spec;
            std::string err;
            uint32_t addr = 0;
            if (!parseBreakSpec(text, spec, err)) {
                b.notes.push_back("--break " + text + ": " + err);
                continue;
            }
            if (!sym.resolve(spec.where, addr)) {
                b.notes.push_back("--break " + text + ": no symbol '" + spec.where + "' (" + sym.status() + ")");
                continue;
            }
            if (!symbolCanBreak(addr)) {
                b.notes.push_back("--break " + text + ": " + hex6(addr) + " is odd, so it is not an instruction");
                continue;
            }
            DebugBreakpoint bp;
            bp.target = spec.target >= 0 ? spec.target : symbolTarget(addr);
            bp.addr = addr;
            debug.bindSymbol(addr, bp.symbol, bp.symbolOffset);
            bool dup = false;
            for (const DebugBreakpoint& o : b.carry.bps)
                if (o.target == bp.target && o.addr == bp.addr) dup = true;
            if (!dup) b.carry.bps.push_back(bp);
        }
        if (!devWatch_.empty()) {
            WatchSpec spec;
            std::string err;
            uint32_t addr = 0;
            if (!parseWatchSpec(devWatch_, spec, err)) b.notes.push_back("--watch " + devWatch_ + ": " + err);
            else if (!sym.resolve(spec.where, addr)) b.notes.push_back("--watch " + devWatch_ + ": no symbol '" + spec.where + "' (" + sym.status() + ")");
            else if (spec.len > 0x1000000u - addr) b.notes.push_back("--watch " + devWatch_ + ": the range runs off the 24-bit bus");
            else {
                b.carry.watch = DebugWatch();
                b.carry.watch.set = true;
                b.carry.watch.addr = addr;
                b.carry.watch.len = spec.len;
                b.carry.watch.kind = spec.kind;
                debug.bindSymbol(addr, b.carry.watch.symbol, b.carry.watch.symbolOffset);
            }
        }
    }
    // A rebuild moves code: every entry that knows its symbol goes to where that symbol is now.
    if (b.sameProgram && !b.carry.empty() && sym.ensure()) b.moved = debug.rebind(b.carry);
    b.haltAtBoot = breakAtBoot;
    b.active = b.haltAtBoot || !b.carry.empty();
    return b;
}

// Emulation thread (inline: the UI thread), inside the load call.
void App::armDebugBootOnCore(DebugBoot& b) {
    if (!b.active) return;
    b.armed = debug.armOnCore(b.carry, b.haltAtBoot);
    if (!b.armed) {
        std::fprintf(stderr, "[debug] the core refused the debugger: nothing armed for this load\n");
        return;
    }
    std::fprintf(stderr, "[debug] armed before the first instruction: %zu breakpoint%s%s%s\n", b.carry.bps.size(),
                 b.carry.bps.size() == 1 ? "" : "s", b.carry.watch.set ? ", the watchpoint" : "", b.haltAtBoot ? ", break at start" : "");
}

void App::finishDebugBoot(const DebugBoot& b, bool ok, const std::string& path) {
    for (const std::string& n : b.notes) {
        std::fprintf(stderr, "[debug] %s\n", n.c_str());
        toast(n, 8.0, 3);
    }
    if (!ok) {
        debug.setKeepToolsOnNextCore(false);
        return;
    }
    // The new core, now rather than at the next UI frame: the list below must not be dropped by a later syncCore().
    if (b.active) debug.adopt(b.carry, b.armed);
    else debug.syncCore();
    debug.setKeepToolsOnNextCore(false);
    if (b.moved > 0) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "%d breakpoint%s followed %s to the new build", b.moved, b.moved == 1 ? "" : "s",
                      b.moved == 1 ? "its symbol" : "their symbols");
        toast(msg, 4.0);
    }
    reloadPending_ = false;
    if (path.empty()) contentWatch_.clear();
    else contentWatch_.watch(path, stampFile(path));
}

// UI thread, once per UI frame (also from the inline halt's nested frame, which can only take note).
void App::pollContentChange() {
    if (!reloadOnChange || !host.isLoaded() || !contentWatch_.active()) return;
    const bool blocked = haltedNow() && !threaded();   // inline, a halted core is inside retro_run: no load from here
    if (!reloadPending_) {
        const double t = now();
        if (t < nextWatchPoll_) return;
        nextWatchPoll_ = t + kWatchPollSeconds;
        if (!contentWatch_.poll(stampFile(contentWatch_.path()))) return;
        std::fprintf(stderr, "[debug] %s changed on disk: reloading\n", contentWatch_.path().c_str());
        if (blocked) {
            reloadPending_ = true;
            toast("The program was rebuilt - Continue (F9) to reload it", 5.0);
            return;
        }
    } else if (blocked) {
        return;
    }
    reloadPending_ = false;
    if (host.loadedPath() != contentWatch_.path()) return;   // another game took its place meanwhile
    reloadContent();
}

}  // namespace vj

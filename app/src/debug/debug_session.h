// The app side of the core's debug interface (src/debug/vj_debug_iface.h): one DebugSession per
// App.  It owns everything the tool windows share -- whether the tools may be used at all, the
// attach / halt state (the UI's mirror of DebugCore's halt), the 10 Hz refresh cadence, the breakpoint list and
// the Tools menu registry -- and none of the ImGui, so the logic unit-tests against the mock
// (app/tests/debug_mock.h) with no window and no core.
//
// Threading (#967 phase 3a): the session is the UI thread's object.  The core's debug interface belongs to the
// emulation thread and lives in DebugCore (core()); every session method that must touch it goes through
// coreCall (waits) or corePost (fire and forget), which App wires to the emulation thread's debug lane.  Both are
// empty inline (headless, tests), and the action then runs right where it is called: same code, one thread.
//   UI thread only:  bps_, watch_, symbols_, regions_, tools_, providers_, lastPokeError_, loaded_, hardcore_,
//                    the halt mirror (mirror_, byGdb_, userHaltPending_, disarmed_,
//                    attachedMirror_), seenGen_, hadCore_, and the public callbacks.  Exception: regions_, lastPokeError_ and
//                    disarmed_ may be written inside a callCore / emu.call lambda (regions() / poke() nested in a window's
//                    one-shot action; rearmBreakpoints() inside App::callReleased) while the UI thread
//                    waits in that call; the call's completion orders those writes before the UI reads them.
//   any thread:      core() (its getters are atomics), views() (one mutex inside), refreshSerial() / requestRefresh().
// The halt itself lives in DebugCore (phase 3b): the emulation thread's pump decides, the UI mirrors it once per UI frame
// (syncHalt) and answers with a verdict tagged by the halt serial it saw (resume / step); a Detach or a release is a lane
// post that decides on the core's real state (detach / releaseHalt).
// The windows never touch the core: they set() a request on views(), call wakeViews() and read() the buffer.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "debug_core.h"
#include "debug_views.h"
#include "symbol_store.h"
#include "vj_debug_iface.h"

namespace vj {

struct DebugBreakpoint {
    int target = VJ_DBG_TARGET_M68K;   // VJ_DBG_TARGET_*
    unsigned addr = 0;
    bool enabled = true;
    // The label the address was at when the breakpoint was set ("" = none known): after a relink the breakpoint moves
    // to symbol + symbolOffset (DebugSession::rebind), so it follows the function instead of pointing into the old build.
    std::string symbol;
    unsigned symbolOffset = 0;
};

// The memory watchpoint: ONE slot, 68000 bus accesses only (vj_debug_iface.h, watch_set).
struct DebugWatch {
    bool set = false;
    unsigned addr = 0;
    unsigned len = 1;                      // bytes watched from addr
    unsigned kind = VJ_DBG_WATCH_WRITE;    // VJ_DBG_WATCH_WRITE | VJ_DBG_WATCH_READ
    std::string symbol;                    // as DebugBreakpoint
    unsigned symbolOffset = 0;
};

// What survives a reload of the same program (App::openContent): the breakpoint list and the watchpoint.
struct DebugCarry {
    std::vector<DebugBreakpoint> bps;
    DebugWatch watch;
    bool empty() const { return bps.empty() && !watch.set; }
};

// One Tools-menu entry.  A tool window registers itself (registerTool) with the same id as the
// placeholder to replace it; an entry with no `toggle` is a greyed placeholder.
struct DebugTool {
    std::string id;                    // "memory", "clut", "scanner", "cpu", "objects", "blitter", "textures"
    std::string label;                 // "Memory viewer..."
    std::string shortcut;              // shown on the menu item, e.g. "F9"; display only
    std::function<bool()> isOpen;      // checkmark; may be empty
    std::function<void()> toggle;      // open / close the window; empty = not available yet
    bool needsHalt = false;            // greyed unless a processor is halted (the CPU window's step buttons)
    std::function<void()> draw;        // called every UI frame (also during a halt); the window checks its own open state
};

class DebugSession;

// How a tool window plugs in (one provider per tool file, e.g. registerMemoryTools / drawMemoryTools):
// `registerTools` runs once, when the provider is added, and registers the Tools-menu entries; `draw` runs
// every UI frame (also during a halt) and the window checks its own open state.
struct DebugProvider {
    std::function<void(DebugSession&)> registerTools;
    std::function<void(DebugSession&)> draw;
};

class DebugSession {
public:
    static constexpr double kRefreshHz = 10.0;       // live views while the emulation runs
    static constexpr unsigned kDramSize = 0x200000;  // the scanner / viewer snapshot size

    DebugSession();

    // ---- wiring (App) --------------------------------------------------------------------
    DebugCore& core() { return core_; }         // the emulation-thread half (App wires its hooks)
    const DebugCore& core() const { return core_; }
    DebugViews& views() { return views_; }      // the request copier the windows use
    // How a method that must touch the core reaches the emulation thread.  Empty (inline, tests): runs fn now.
    std::function<void(std::function<void()>)> coreCall;   // waits (App: emu.debugCall<void>)
    std::function<void(std::function<void()>)> corePost;   // fire and forget (App: emu.debugPost)
    // The windows use these two, never the hooks directly.  Captures are by value, except a local the caller waits for.
    void callCore(std::function<void()> fn) const;         // coreCall ? coreCall(fn) : fn()
    void postCore(std::function<void()> fn);               // corePost ? corePost(fn) : fn()
    // The core's interface, from VJ_ENV_SET_DEBUG_IFACE (null until the core sends it, and again after
    // the core is torn down).  Replacing it detaches from the old one.  onCoreIface() is the emulation-thread half
    // (core().setIface + the views' epoch; it touches no UI state), syncCore() the UI half; setIface() is both, in
    // order (inline, tests).
    void onCoreIface(const vj_debug_iface* i);
    void syncCore();
    void setIface(const vj_debug_iface* i);
    void setContentLoaded(bool loaded);
    // RetroAchievements hardcore: the tools are off.  Turning it on detaches, drops any halt and
    // tells the windows to close (onForcedClose).
    void setHardcore(bool on);
    // Fired (in order) when the tools get switched off from outside: "hardcore", "closed", "no core" -- or "reload",
    // when the same program is being loaded again (setKeepToolsOnNextCore): a window that survives a core swap may
    // stay open for that one and only drop what it remembers about the old core.
    std::vector<std::function<void(const char* why)>> onForcedClose;
    // Fired on the UI thread by syncHalt(): a processor halted (every entry, a step's re-halt included) / the machine
    // resumed (a step is not a resume: the re-halt follows).  A halt that came and went between two UI frames fires
    // nothing.  The core-side hooks (audio, the log line) are DebugCore::onHalt / onResume.
    std::vector<std::function<void(int target, unsigned pc, int reason)>> onHaltUi;
    std::vector<std::function<void()>> onResumeUi;

    // ---- can the tools be used? -----------------------------------------------------------
    bool hasCore() const { return core_.loaded(); }
    // The core supports it, content is loaded and hardcore is off.
    bool enabled() const { return core_.loaded() && loaded_ && !hardcore_; }
    // Why not (for a tooltip / the greyed Tools menu); "" when enabled().
    std::string disabledReason() const;
    bool hardcore() const { return hardcore_; }

    // ---- attach / halt -----------------------------------------------------------------------
    // The halt mirror.  syncHalt() runs once per UI frame (App::syncDebug): it copies core().halt() and attached(); when
    // the halt serial moved it fires onHaltUi (now halted) or onResumeUi (was halted, is not).  The mirror lags the core
    // by one UI frame by design: halted() / haltedPc() / haltReason() are current only after syncHalt().
    void syncHalt();
    bool halted() const { return mirror_.halted; }
    int haltedTarget() const { return mirror_.target; }
    unsigned haltedPc() const { return mirror_.pc; }
    int haltReason() const { return mirror_.reason; }
    uint64_t haltSerial() const { return mirror_.serial; }   // the mirrored HaltState serial (not refreshSerial())
    bool haltByGdb() const { return byGdb_; }   // set on the halt edge: a breakpoint we did not set, or a USER / STEP halt we did not ask for
    // The app's own temporary breakpoints (run to cursor, step over a call) are armed in the core without being in the
    // list; the CPU window answers whether one of them is armed at (target, pc), so a halt there is not "by gdb".  UI thread.
    std::function<bool(int target, unsigned pc)> ownsBreakAt;
    // Registers the halt pump with the core and re-arms the breakpoint list.  Idempotent; false if the
    // tools are not enabled or the core refused.  Decides inside the lane (a queued detach is ordered before it; a Detach
    // whose frame-end host drop is still owed is superseded: the host stays, #1081 M6).  Tool windows that only
    // peek/poke need no attach.
    bool attach();
    // Detach: drops the breakpoint list now and posts the rest to the lane.  The post runs at a frame boundary (the host
    // and the core's table go) or, while the core is halted, inside the pump: then it disarms the list's entries in the
    // core, answers CONTINUE and leaves the host for DebugCore::frameEnded() to drop -- never DETACH from inside the halt
    // (#1069: a gdb client's armed Z0 would park the host-less core behind its socket).  docs/app-debug-tools.md, "One
    // rule for leaving a halt".
    void detach();
    // The content commands while halted (#1081 M1; App::releaseForCall): the same release as detach(), but the list and
    // the host are kept.  The enabled breakpoints are disarmed in the core so the frame can end, and rearmBreakpoints()
    // puts them back after the command.  Threaded for the content commands (inline, a halted core refuses them); inline
    // it is how reload-on-change gets the core out of a halt so the frame can end (App::pollContentChange).
    void releaseHalt();
    // After releaseHalt(): re-arms the enabled entries in the core.  Idempotent; a no-op when nothing is disarmed or the
    // command replaced the core (the list is per game: syncCore() drops it).  UI thread, or inside the emu.call the UI
    // waits in (App::callReleased: before the first frame after a Reset).
    void rearmBreakpoints();
    // The core's host is attached and no detach of ours is queued.  Current right after attach() / detach(); otherwise
    // refreshed by syncHalt().
    bool attached() const { return attachedMirror_ && core_.attached(); }
    // F9 / F10 and the CPU window's buttons.  breakNow() is "stop at the next instruction"; while halted
    // resume() / step() write the verdict for the mirrored halt (honoured by the pump's next iteration; dropped when
    // that halt already ended).
    void breakNow(int target = VJ_DBG_TARGET_M68K);
    void resume();
    void step();

    // ---- breakpoints ----------------------------------------------------------------------------
    // The list is the source of truth (the core's table is rebuilt from it on every attach).
    const std::vector<DebugBreakpoint>& breakpoints() const { return bps_; }
    bool addBreakpoint(int target, unsigned addr);          // false: duplicate, bad target, or core table full
    void removeBreakpoint(int target, unsigned addr);
    bool toggleBreakpoint(int target, unsigned addr);       // add or remove; true if now set
    bool hasBreakpoint(int target, unsigned addr) const;
    void setBreakpointEnabled(int target, unsigned addr, bool on);
    void clearBreakpoints();
    // "main+$12" for a breakpoint that is bound to a symbol, else "".
    static std::string boundName(const std::string& symbol, unsigned offset);

    // ---- the watchpoint ------------------------------------------------------------------------------------
    // One slot (the core's): halts the 68000 when it reads / writes any byte of [addr, addr + len).  The halt's
    // haltedPc() is the WATCHED ADDRESS; the 68000's registers hold the program counter.  A GPU, DSP, blitter or OP
    // access never triggers it.  setWatch replaces the slot; false: the tools are off, the core has no watchpoint, the
    // range is bad, or a gdb client holds the slot.
    bool watchSupported() const { return core_.watchSupported(); }
    bool setWatch(unsigned addr, unsigned len, unsigned kind);
    void clearWatch();
    const DebugWatch& watch() const { return watch_; }

    // ---- symbols, and keeping the list across a reload ---------------------------------------------------------
    SymbolStore& symbols() { return symbols_; }
    const SymbolStore& symbols() const { return symbols_; }
    // The list and the watchpoint as they stand (UI thread), for a reload of the same program.
    DebugCarry carry() const;
    // Moves every entry that remembers a symbol to where that symbol is now (symbols() must be current: after a relink,
    // invalidate() + ensure()).  An entry whose symbol is gone keeps its address; entries that land on one address
    // collapse to one.  Returns how many moved.
    int rebind(DebugCarry& c) const;
    // Emulation thread, inside the load call, after the new core handed over its interface and before its first frame:
    // attaches, arms the enabled entries of `c` and, with `haltAtBoot`, a halt on the 68000's first instruction (claimed
    // as the app's own: DebugCore::haltNextOwned).  Touches only core().  False when the core refused the attach.
    bool armOnCore(const DebugCarry& c, bool haltAtBoot);
    // The same boot halt alone, for a Reset (the list is already armed).  Emulation thread.
    void armBootHaltOnCore();
    // UI thread, after that load returned: adopts the new core (syncCore) and takes `c` as the list.  `armed` is what
    // armOnCore returned; when false the next attach() arms the list (the core's table is rebuilt from it).
    void adopt(const DebugCarry& c, bool armed);
    // The next core swap asks the tool windows to close with the reason "reload" instead of "no core": a reload of the
    // same program, where the CPU debugger, the memory viewer and the Symbols window stay open (they re-read through
    // the views' epoch) and the others close as usual.  Consumed by the next syncCore() that sees a core; App clears
    // it when the load failed.
    void setKeepToolsOnNextCore(bool on) { keepTools_ = on; }
    // The label `addr` is at, or just past ("" / 0 when none is close): what a breakpoint remembers.  Reads symbols().
    void bindSymbol(unsigned addr, std::string& symbol, unsigned& offset);

    // ---- refresh cadence ------------------------------------------------------------------------------
    // Called once per app frame.  `live` is "the emulation is running": views refresh at kRefreshHz.
    // Paused or halted, the serial bumps once when it stops and then only on requestRefresh() / a poke /
    // a halt, so a paused game costs nothing.
    void tick(double nowSeconds, bool live);
    // Bumps whenever the views should re-read.  A window keeps the value it last drew and compares.
    uint64_t refreshSerial() const { return serial_.load(); }
    void requestRefresh() { serial_++; }                     // after a poke / a register edit
    // ---- the view copier ---------------------------------------------------------------------------------------
    // fillViews() serves every pending request from the core (emulation thread; inline: the UI).  It has NO gate:
    // the Neuro watches depend on it in every state but "no core".  wakeViews() (UI thread, once per UI frame and
    // after a window's set()s) makes sure a paused game still fills: with no corePost (tests) it fills now; otherwise
    // it posts one fillViews() while a request is pending (one outstanding at a time).  App always sets corePost; the
    // post runs at once when EmuThread is not threaded (it runs jobs inline), at the next frame boundary when it is.
    size_t fillViews();
    void wakeViews();

    // ---- memory helpers (null-iface safe) ---------------------------------------------------------------
    // Reads n bytes (the iface's side-effect-free byte path).  False on any error.  One-shot readers only (the
    // scanner's scan pass): this is a callCore.  Periodic readers set() a view request instead.
    bool read(unsigned addr, std::vector<uint8_t>& out, unsigned n) const;
    bool readU(unsigned addr, unsigned bytes, unsigned& v) const;   // big-endian, 1/2/4 bytes
    // The 2 MB main-RAM image (a copy; what the scanner and the viewer diff against).
    bool snapshotDram(std::vector<uint8_t>& out) const { return read(0, out, kDramSize); }
    // Writes per the user's "allow register writes" choice below.  Returns n or VJ_DBG_ERR_*; a refused
    // write is remembered in lastPokeError() for the status line.
    int poke(unsigned addr, const uint8_t* data, unsigned n);
    int pokeU(unsigned addr, unsigned bytes, unsigned v);           // big-endian, 1/2/4 bytes
    bool allowRegisterWrites = false;     // Memory viewer checkbox: VJ_DBG_POKE_REGS
    bool allowDangerousWrites = false;    // ...and the second, scarier one: VJ_DBG_POKE_DANGEROUS
    int lastPokeError() const { return lastPokeError_; }
    static const char* errorText(int vjDbgErr);
    // Region table, copied once per core (names are static in the core).
    const std::vector<vj_debug_region>& regions();
    // One callCore each, for the windows' one-shot actions (edits, step-over, per-window tables).
    bool getRegsNow(int target, vj_debug_regs& out) const;
    bool setReg(int target, int reg, unsigned v);
    bool buildCryTable(std::vector<uint32_t>& out) const;
    void opSnapshotArm();
    void blitLogEnable(bool on);
    int texReplaceReload();                                  // 1 active, 0 inactive, -1 not supported

    // ---- Tools menu registry -------------------------------------------------------------------------------
    // Registers (or replaces, by id) a menu entry.  The menu itself is drawn by drawToolsMenu() (debug_ui.cpp).
    void registerTool(const DebugTool& t);
    const std::vector<DebugTool>& tools() const { return tools_; }
    DebugTool* findTool(const std::string& id);
    // Whether the menu entry is clickable: the tools are enabled, the window exists, and (CPU stepping) a processor is
    // halted when the entry needs one.  The in-window Tools menu and the native macOS one both ask this.
    bool toolAvailable(const DebugTool& t) const {
        return enabled() && t.toggle && (!t.needsHalt || halted());
    }
    // Greyed placeholders for the windows the later work packages add: Memory, CLUT, Cheat scanner, CPU,
    // Symbols, Objects, Blitter, Textures, Audio scope.
    void registerPlaceholderTools();
    // Asks every open tool window to close (hardcore, a content change).
    void closeAllTools(const char* why);
    // The generic plug-in point: App adds one provider per tool file at start-up; the UI calls drawTools() once
    // per frame (providers first, then every DebugTool::draw).
    void addProvider(const DebugProvider& p);
    void drawTools();

private:
    bool haltLive() const;   // the mirrored halt is the core's live halt (same serial, core parked in the pump)
    void clearBreakpointList();   // bps_.clear() + a refresh serial; Detach drops the list with the core's table
    // The one lane post behind detach() (dropHost) and releaseHalt(): disarms `bps` in the core when it finds the core
    // halted, then core_.releaseHalt(dropHost).  dropHost: detachQueued_ brackets the post, attachedMirror_ = false.
    void postRelease(std::vector<DebugBreakpoint> bps, bool dropHost);
    std::vector<DebugBreakpoint> enabledBreakpoints() const;
    // A Detach is in flight: its post has not run, or the frame-end host drop is still owed (DebugCore::detachPending).
    // Keeps the attached() mirror false and makes resume() / step() no-ops (the Detach already answered CONTINUE).
    bool detachPending() const { return detachQueued_.load() > 0 || core_.detachPending(); }
    void dropHalt();   // mirror_ = {}; byGdb_ = userHaltPending_ = attachedMirror_ = disarmed_ = false

    DebugCore core_;
    DebugViews views_;
    bool loaded_ = false;
    bool hardcore_ = false;
    uint64_t seenGen_ = 0;       // core_.ifaceGen() as syncCore() last adopted it
    bool hadCore_ = false;
    std::atomic<bool> wakePosted_{false};
    HaltState mirror_;                   // core_.halt() as syncHalt() last copied it
    bool attachedMirror_ = false;        // see attached()
    bool byGdb_ = false;
    bool userHaltPending_ = false;       // breakNow() / step() asked for a halt; the next halt edge consumes it
    bool disarmed_ = false;              // releaseHalt() disarmed the list in the core; rearmBreakpoints() owes the sets
    std::atomic<int> detachQueued_{0};   // detach() posts not yet run on the emulation thread
    std::vector<DebugBreakpoint> bps_;
    DebugWatch watch_;
    SymbolStore symbols_;
    bool keepTools_ = false;             // see setKeepToolsOnNextCore()
    std::vector<DebugTool> tools_;
    std::vector<DebugProvider> providers_;
    std::vector<vj_debug_region> regions_;
    std::atomic<uint64_t> serial_{1};
    double lastRefresh_ = -1;
    int lastPokeError_ = 0;
};

}  // namespace vj

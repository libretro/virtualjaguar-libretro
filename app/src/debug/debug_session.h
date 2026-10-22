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
//   UI thread only:  bps_, regions_, tools_, providers_, lastPokeError_, loaded_, hardcore_,
//                    the halt mirror (mirror_, byGdb_, userHaltPending_, detachPending_,
//                    attachedMirror_), seenGen_, hadCore_, and the public callbacks.  Exception: regions_ and lastPokeError_ may be written inside
//                    a callCore lambda (regions() / poke() nested in a window's one-shot action) while the UI thread
//                    waits in that call; the call's completion orders those writes before the UI reads them.
//   any thread:      core() (its getters are atomics), views() (one mutex inside), refreshSerial() / requestRefresh().
// The halt itself lives in DebugCore (phase 3b): the emulation thread's pump decides, the UI mirrors it once per UI frame
// (syncHalt) and answers with a verdict tagged by the halt serial it saw (resume / step / detachAndResume).
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
#include "vj_debug_iface.h"

namespace vj {

struct DebugBreakpoint {
    int target = VJ_DBG_TARGET_M68K;   // VJ_DBG_TARGET_*
    unsigned addr = 0;
    bool enabled = true;
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
    // Fired (in order) when the tools get switched off from outside: "hardcore", "closed", "no core".
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
    // tools are not enabled, the core refused, or a Detach is still in flight.  Decides inside the lane (a queued
    // detach is ordered before it).  Tool windows that only peek/poke need no attach.
    bool attach();
    void detach();                       // halted: detachAndResume(); else a lane post that clears every breakpoint in the core
    // The core's host is attached and no detach of ours is queued.  Current right after attach() / detach(); otherwise
    // refreshed by syncHalt().
    bool attached() const { return attachedMirror_ && core_.attached(); }
    // F9 / F10 and the CPU window's buttons.  breakNow() is "stop at the next instruction"; while halted
    // resume() / step() / detachAndResume() write the verdict for the mirrored halt (honoured by the pump's next
    // iteration; dropped when that halt already ended).
    void breakNow(int target = VJ_DBG_TARGET_M68K);
    void resume();
    void step();
    void detachAndResume();

    // ---- breakpoints ----------------------------------------------------------------------------
    // The list is the source of truth (the core's table is rebuilt from it on every attach).
    const std::vector<DebugBreakpoint>& breakpoints() const { return bps_; }
    bool addBreakpoint(int target, unsigned addr);          // false: duplicate, bad target, or core table full
    void removeBreakpoint(int target, unsigned addr);
    bool toggleBreakpoint(int target, unsigned addr);       // add or remove; true if now set
    bool hasBreakpoint(int target, unsigned addr) const;
    void setBreakpointEnabled(int target, unsigned addr, bool on);
    void clearBreakpoints();

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
    // Objects, Blitter, Textures, Audio scope.
    void registerPlaceholderTools();
    // Asks every open tool window to close (hardcore, a content change).
    void closeAllTools(const char* why);
    // The generic plug-in point: App adds one provider per tool file at start-up; the UI calls drawTools() once
    // per frame (providers first, then every DebugTool::draw).
    void addProvider(const DebugProvider& p);
    void drawTools();

private:
    bool haltLive() const;   // the mirrored halt is the core's live halt (same serial, core parked in the pump)
    void postDetach();       // detachQueued_++, lane post of core_.detach(), attachedMirror_ = false
    void dropHalt();   // mirror_ = {}; byGdb_ = userHaltPending_ = detachPending_ = attachedMirror_ = false

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
    bool detachPending_ = false;         // a DETACH verdict is in flight: attach() / breakNow() refuse until the serial moves
    std::atomic<int> detachQueued_{0};   // detach() posts not yet run on the emulation thread
    std::vector<DebugBreakpoint> bps_;
    std::vector<DebugTool> tools_;
    std::vector<DebugProvider> providers_;
    std::vector<vj_debug_region> regions_;
    std::atomic<uint64_t> serial_{1};
    double lastRefresh_ = -1;
    int lastPokeError_ = 0;
};

}  // namespace vj

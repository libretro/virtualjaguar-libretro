// The app side of the core's debug interface (src/debug/vj_debug_iface.h): one DebugSession per
// App.  It owns everything the tool windows share -- whether the tools may be used at all, the
// attach / halt state, the nested halt pump, the 10 Hz refresh cadence, the breakpoint list and
// the Tools menu registry -- and none of the ImGui, so the logic unit-tests against the mock
// (app/tests/debug_mock.h) with no window and no core.
//
// Threading: every call is on the emulation thread (the app's main thread), between retro_run
// calls or inside the halt pump (docs/app-debug-tools.md).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
};

class DebugSession {
public:
    static constexpr double kRefreshHz = 10.0;       // live views while the emulation runs
    static constexpr unsigned kDramSize = 0x200000;  // the scanner / viewer snapshot size

    DebugSession();

    // ---- wiring (App) --------------------------------------------------------------------
    // The core's interface, from VJ_ENV_SET_DEBUG_IFACE (null until the core sends it, and again after
    // the core is torn down).  Replacing it detaches from the old one.
    void setIface(const vj_debug_iface* i);
    const vj_debug_iface* iface() const { return iface_; }
    void setContentLoaded(bool loaded);
    // RetroAchievements hardcore: the tools are off.  Turning it on detaches, drops any halt and
    // tells the windows to close (onForcedClose).
    void setHardcore(bool on);
    // One nested UI iteration (events -> ImGui frame -> present), WITHOUT stepping the emulation.
    // Set by App; empty in tests, where the pump just returns the verdict a test queued.
    std::function<void()> runUiFrame;
    // Polled each pump: true makes the core resume and detach (the app is quitting).
    std::function<bool()> abortHalt;
    // Fired (in order) when the tools get switched off from outside: "hardcore", "closed", "no core".
    std::vector<std::function<void(const char* why)>> onForcedClose;
    // Fired when a processor halts / resumes (a step is not a resume: the next halt follows at once).
    std::vector<std::function<void(int target, unsigned pc, int reason)>> onHalt;
    std::vector<std::function<void()>> onResume;

    // ---- can the tools be used? -----------------------------------------------------------
    bool hasCore() const { return iface_ != nullptr; }
    // The core supports it, content is loaded and hardcore is off.
    bool enabled() const { return iface_ != nullptr && loaded_ && !hardcore_; }
    // Why not (for a tooltip / the greyed Tools menu); "" when enabled().
    std::string disabledReason() const;
    bool hardcore() const { return hardcore_; }

    // ---- attach / halt -----------------------------------------------------------------------
    // Registers the halt pump with the core and re-arms the breakpoint list.  Idempotent; false if the
    // tools are not enabled or the core refused.  Tool windows that only peek/poke need no attach.
    bool attach();
    void detach();                       // clears every breakpoint in the core, drops any halt
    bool attached() const { return attached_; }
    bool halted() const { return halted_; }
    int haltedTarget() const { return haltedTarget_; }
    unsigned haltedPc() const { return haltedPc_; }
    int haltReason() const { return haltReason_; }
    // F9 / F10 and the CPU window's buttons.  breakNow() is "stop at the next instruction"; while halted
    // resume() / step() / detachAndResume() give the pump its verdict (honoured on the next pump call).
    void breakNow(int target = VJ_DBG_TARGET_M68K);
    void resume();
    void step();
    void detachAndResume();
    bool resumeQueued() const { return queued_ != VJ_DBG_RESUME_NONE; }

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
    uint64_t refreshSerial() const { return serial_; }
    void requestRefresh() { serial_++; }                     // after a poke / a register edit

    // ---- memory helpers (null-iface safe) ---------------------------------------------------------------
    // Reads n bytes (the iface's side-effect-free byte path).  False on any error.
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

    // ---- Tools menu registry -------------------------------------------------------------------------------
    // Registers (or replaces, by id) a menu entry.  The menu itself is drawn by drawToolsMenu() (debug_ui.cpp).
    void registerTool(const DebugTool& t);
    const std::vector<DebugTool>& tools() const { return tools_; }
    DebugTool* findTool(const std::string& id);
    // Greyed placeholders for the windows the later work packages add: Memory, CLUT, Cheat scanner, CPU,
    // Objects, Blitter, Textures.
    void registerPlaceholderTools();
    // Asks every open tool window to close (hardcore, a content change).
    void closeAllTools(const char* why);

private:
    static int haltPumpThunk(void* user, int target, unsigned pc);
    int pump(int target, unsigned pc);
    void applyBreakpoints();
    void dropHalt();

    const vj_debug_iface* iface_ = nullptr;
    vj_debug_host host_{};
    bool loaded_ = false;
    bool hardcore_ = false;
    bool attached_ = false;
    bool halted_ = false;
    int haltedTarget_ = -1;
    unsigned haltedPc_ = 0;
    int haltReason_ = VJ_DBG_STOP_NONE;
    int queued_ = VJ_DBG_RESUME_NONE;
    std::vector<DebugBreakpoint> bps_;
    std::vector<DebugTool> tools_;
    std::vector<vj_debug_region> regions_;
    uint64_t serial_ = 1;
    double lastRefresh_ = -1;
    int lastPokeError_ = 0;
};

}  // namespace vj

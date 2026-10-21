# Emulation thread, Phase 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** With Settings > Interface > Emulation thread on, nothing is switched off any more: every Tools window, the debugger halt (F9/F10, breakpoints, step), the GDB stub and Neuro work on the emulation thread, and nightly builds default the setting On for users who never chose.

**Architecture:** Four PRs, each keeping one owner thread per piece of state.
- **3a, debug views.** The command queue gets a second lane, `Lane::Debug`, that the emulation thread drains at frame boundaries *and* from inside a halt. Every Tools window stops calling `vj_debug_iface` from the UI thread: periodic reads (memory, CLUT, registers, disassembly, the OP snapshot, the blit-log tail, the scanner's DRAM) become `DebugViews` requests the emulation thread copies into per-window double buffers after each frame; one-shot actions (poke, set_reg, breakpoints, arm, log enable, pack reload) go through `EmuThread::debugCall`. Inline, the same copier runs on the same thread, so the windows have one code path.
- **3b, the halt and the GDB stub.** `DebugSession` splits into a UI-thread mirror and `DebugCore`, the emulation-thread owner of the interface and the pump. The pump is a **bounded** wait (<= 4 ms) that serves the debug lane and returns `VJ_DBG_RESUME_NONE` to the core's own `GDBHalt()` loop, which keeps servicing the socket and the halt timeout. Verdicts are an atomic the UI writes. `App::haltedFrame` runs only inline. Frame-lane commands wait for Continue ("applies after Continue"); content-changing calls detach first; `call()` can give up when a halt begins. The app attaches a silent host whenever the stub is enabled, so a gdb halt is visible and shutdown can detach it.
- **3c, Neuro.** The UI publishes Neuro's whole input program through `InputSnapshot`; the core's input poll runs an `AgentPlayer` replica one step per emulated frame and reports progress back. RAM watches are `DebugViews` requests.
- **3d, the flag.** `VJ_BUILD_CHANNEL` (`release` | `nightly` | `dev`) sets `VJ_EMU_THREAD_DEFAULT`; the default applies only while `ui.emu_thread` is unwritten.

**Tech Stack:** C++17 (app/), SDL3, Dear ImGui, CMake/CTest, the statically linked C89 core (unchanged), ThreadSanitizer, GitHub Actions, the Python gdb probes in test/tools/.

**Spec:** docs/app-emulation-thread-design.md (sections 5, 6, 8 and 10 step 3, with the 2026-10-20 amendments). Earlier plans: docs/app-emulation-thread-plan-phase1.md ("Phase 1 boundary", "Follow-up plans"), docs/app-emulation-thread-plan-phase2.md ("Follow-up plans"). docs/neuro.md ("Emulation thread"). The scope survey behind the split (2026-10-20, develop @ abc9054e) found: every Tools window except the audio scope calls `vj_debug_iface` from the UI thread (the scanner even scans the raw 2 MB pointer `CoreHost::memoryRegion` hands out, which is owner-only); Neuro's `Agent::frame()` runs inside the core's input poll while the rest of the agent is UI state, and its watches read RAM through a raw pointer; cheats (including #1052), TV mode, screenshots, frame hashes and achievements were already correct.

## Decisions to confirm

The maintainer asked for one Phase 3 PR; this plan proposes four (3a -> 3b and 3c in parallel -> 3d), 3d merging last. Each decision below is adopted provisionally and goes to the maintainer at review.

| | Decision (proposed) | Recommendation and why |
|---|---|---|
| 1 | Four PRs: 3a debug views, 3b halt + gdb, 3c Neuro, 3d nightly default | Yes. One PR is ~30 files / ~3k LOC across 8 tool windows, a state machine, a protocol path and a build flag. The order is forced (the pump serves the views; Neuro's watches reuse the copier); 3d alone is the revertable flip. |
| 2 | `virtualjaguar_gdb_stub` locked to `disabled` while RetroAchievements hardcore is active | Yes. `DebugSession::attach()` needs `enabled()`, which hardcore denies, and decision 3 needs a host behind every halt. Today nothing blocks the stub under hardcore; a debugger is a cheat tool. |
| 3 | The app attaches a silent host whenever the stub is `enabled` and content is loaded | Yes. Every halt then flows through the app's pump: the halt bar shows "HALTED by gdb", calls give up, quit detaches. Cost: one stored pointer. Side effect (already true inline): quitting clears gdb's breakpoints. |
| 4 | `VJ_BUILD_CHANNEL=dev` defaults the setting Off | Yes. PR artifacts and local builds then behave like releases; windowed runs opt in with `--emu-thread on`. |
| 5 | The flatpak job seds `-DVJ_BUILD_CHANNEL=release` to `nightly` in the manifest before `flatpak-builder` | Yes. The manifest's `config-opts` are fixed and committed; a sed in the job is the only per-channel knob. |

## Global Constraints

- C++17 in `app/`. The core stays single-threaded C89 and is not touched: no file under `src/` or `libretro*.c` changes. The halt loop in `src/debug/gdbtarget.c` and `src/debug/vj_debug_iface.h` are consumed as they are.
- Only the emulation thread calls into the core: every `retro_*` call, every `vj_debug_iface` entry point, every read or write of core memory. A `[core_host] owner violation:` line in any test log is a failure.
- Four PRs, each branched off `libretro/develop` (never `master`), each targeting `develop` with `Refs #967` in the body (phases 4 and 5 remain, so no `Closes`). 3b and 3c branch after 3a merges; 3d branches after both merge. Worktrees under `~/Workspace/Provenance/vj-wt/` (`emu-p3a`, `emu-p3b`, `emu-p3c`, `emu-p3d`), removed when their PR merges.
- App CMake builds never set `DEVELOPER_DIR`. The configure line is `env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release`; the `DEVELOPER_DIR=/Library/Developer/CommandLineTools` prefix is for the core's Makefile build only.
- Never launch the app with a window on the maintainer's Mac. Windowed checks and measurements run on the Mac Studio only, through `~/Workspace/Provenance/vj-eval/remote/vjr.sh` (`bash $VJR run <tag> env -u DEVELOPER_DIR bash -lc '<script>'`, ssh alias `studio-runner`), after `uptime` shows a load under 2. Headless runs are fine anywhere, but check `uptime` first on the Mac too and do not start builds while it is above 10.
- The setting stays Off by default in every build through 3a, 3b and 3c. 3d flips the nightly default and nothing else. Headless runs stay inline unless `--emu-thread on` is given (the smoke hashes are inline hashes).
- Every existing test keeps passing unchanged in inline mode. The three limit tests (`emu_thread_limits`, `gdb_locked_threaded`, `neuro_off_threaded`) are rewritten by the sub-phase that lifts each limit, never deleted without a replacement that pins the lifted behaviour.
- New tests that touch two threads carry `LABELS emu_thread` (the ThreadSanitizer job runs `ctest -L emu_thread`). `app/tests/tsan.supp` does not grow: a report is fixed by routing the reader through a published value.
- Conventional commits (`feat(app):`, `fix(app):`, `test(app):`, `ci(app):`, `docs(app):`), each message ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Commits are signed through 1Password; on a signing error wait and retry, never disable signing.
- Delete with `trash`, or `command rm -f`. Never a bare `rm`, `cp` or `mv` (aliased to `-i`; with no TTY they hang).

## Review Focus

Six failure modes most likely to slip past the per-task tests. Each is pinned by the test named, in the task that owns the code.

1. **A `call()` posted an instant before the core halts hangs the UI.** The call waits behind the halted frame; the busy spinner replaces the picture and cannot draw ImGui, so the user cannot press Continue. Pinned by `emu_thread_unit` `testCallGivesUp` "the call returned R() ... the cancelled task never ran" (Task B3) and `debug_e2e_threaded` section 4 "a save state requested just before a halt returns false within 300 ms" (Task B5).
2. **A view buffer read across a content switch shows the previous game.** The copier filled the buffer for game A; the window draws it after game B loaded. Pinned by `debug_views_unit` `testEpoch` "a buffer filled before bumpEpoch() is not readable after it" (Task A2) and `debug_views_threaded` case 4 "after a reload the Memory window's first read is the new ROM's bytes" (Task A9).
3. **A Neuro press lasts a different number of emulated frames threaded vs inline.** `press_buttons frames=3` must press on exactly 3 core polls at any display rate and under fast-forward, and an action appended mid-play must neither replay nor drop frames. Pinned by `neuro_unit` `testAgentPlayer` "AgentPlayer presses for exactly N polls" and "append and hold change mid-play equal one uninterrupted queue" (Task C1), `neuro_core_e2e` "9 polls, free-running, at 600 Hz" (Task C2) and `neuro_loopback_threaded` against the inline run's frame-numbered vector (Task C4).
4. **The nightly default overrides a user's explicit Off (or a release overrides an explicit On).** A headless run is always inline, so the stored-choice log line, not "this run", is what the smoke reads. Pinned by `emu_thread_default_unit` "pref 0 is Off and pref 1 is On in every channel; only pref -1 follows the channel" (Task D1) and `emu_thread_default_smoke_false` / `_true` / `_unset` reading `ui.emu_thread` from a data dir, derived from the configured channel and run once under the nightly configure (Task D2).
5. **The debug lane is drained while the frame lane is mid-frame outside the pump**, or the frame lane is drained inside the pump (a load inside `retro_run`). Pinned by `emu_commands_unit` `testLanes` "drain(Lane::Debug) leaves the frame lane untouched and vice versa" (Task A1) and `debug_e2e_threaded` section 3 "a frame-lane option post made while halted applies only after Continue; `optionValueFor` reads the old value until then" (Task B5).
6. **Quitting while halted exceeds the 3 s join** (the pump never sees the detach: `quit` read non-atomically, the lane not notified, or a gdb-held headless run with no way to quit). Pinned by `debug_session_unit` `testDetachEndsPumpWithinOneWait` and `testAbortHaltDetaches` (Task B1), `debug_e2e_threaded` section 7 "stop() returns true within 500 ms while halted" (Task B5) and `gdb_threaded_halt` step 5 "SIGTERM while gdb holds the halt: exit 0 within 3 s" (Task B5).
7. **The app stays "halted" after gdb continued** (gdb's `c`, a dropped socket or the halt timeout leave `GDBHalt()` without a verdict of ours; the mirror never clears, F9 then resumes the *next* halt). Pinned by `debug_session_unit` `testFrameEndedClosesForeignResume` and `testStaleVerdictDropped` (Tasks B1, B2).

## Resolved spec ambiguities

| Spec says | Phase 3 does | Why |
|---|---|---|
| "blocks on a condition variable for Continue / Step / Detach" (section 6) | The pump waits at most `DebugCore::kPumpWait` (4 ms) per call and returns `VJ_DBG_RESUME_NONE`; the core calls it again. | `GDBHalt()` (src/debug/gdbtarget.c) owns the socket service (`GDBPumpOnce`), the halt timeout and a 2 ms sleep in its own loop, and the core is not changed. An unbounded wait would starve both. |
| "`App::haltedFrame` and its nested event loop are removed" (section 6) | `haltedFrame` is never used when threaded (`DebugCore::waitVerdict` replaces `runUiFrame`); inline it stays until phase 5 retires the old loop. | Inline, the core is parked on the UI thread; the only way the UI can draw is the nested frame. |
| "Commands that cannot run mid-frame ... are held until Continue" (section 6) | Frame-lane posts (options, cheats, controller device, SRAM autosave, key events) sit in the queue until the frame ends: held by construction. Save/load state, boot, frame advance **refuse** with a toast while halted. Open / close / reset / disc insert / drop / quit **detach first**, then proceed. | Serializing inside `retro_run` is not a state. A detach clears every breakpoint, so the frame after it cannot re-halt and the call waits at most one frame. |
| "If both gdb and the app debugger are attached, the first verdict wins, as today" (section 6) | Unchanged in the core; the app attaches a host whenever the stub is enabled (Decision 3) and locks the stub under hardcore (Decision 2). | Without a host the app cannot see a gdb halt: calls would block until gdb continues and quit would wait out the join. |
| "Hardcore mode still detaches the debugger" (section 6) | `setHardcore(true)` on the UI posts the detach to the debug lane (or writes the DETACH verdict when halted) and closes the tools. | `iface->detach` must run on the emulation thread. |
| neuro.md: "press through the input snapshot the UI thread publishes, not `agentControls`" | `InputSnapshot::publishAgent(AgentProgram)`; the core's poll runs `neuro::AgentPlayer` one step per frame; `InputManager::agentControls` is removed, inline and threaded alike. | A per-UI-frame advance would drift under fast-forward and on 120 Hz panels; a mutex around `InputQueue` would spread over the whole `Agent`. |
| "Each open debug window registers a request ... copies just those into that tool's double buffer" (section 5) | `DebugViews` (app/src/debug/debug_views.h): requests keyed by an id, filled at `DebugSession::kRefreshHz` while running, on every pump iteration whose serial changed while halted, and on the same thread inline. While no frame runs (paused inline or threaded) the UI's `wakeViews()` posts one fill to the debug lane, so a paused window never goes blank. | One code path for both modes; closed windows cost nothing. |
| "peek() never has a side effect" / the scanner's scan pass | The Cheat scanner's scan pass stays a one-shot 2 MB `snapshotDram()`; it is a `DebugSession::read` and therefore a `callCore` (`EmuThread::debugCall`) that runs on the emulation thread. The raw `CoreHost::memoryRegion` pointer path is deleted. The live column reads the `Dram` request. | A scan pass must see RAM as of one instant; a debug-lane call does, and it blocks the UI for at most one frame plus the 2 MB copy. |
| `vj_debug_iface.disasm_risc` serves RISC only | `DebugViews` fills a `Disasm` request through an injected `DisasmFn` (`setDisasm`, installed by the CPU tool: `disasmRows`, which runs Musashi for the 68000 and `disasm_risc` for GPU/DSP, all on the emulation thread); without one, RISC targets are served by `disasm_risc` directly and an M68K request answers `ok = false`. | The CPU window keeps one code path for its three tabs and `debug_views.cpp` does not link Musashi. |
| "the halt mirror" and `HaltState.serial` | The serial bumps on every *entry* into a halt (a step's re-halt included) and on CONTINUE / DETACH; the STEP return itself does not bump it. A new halt is `!halted || target != target || pc != pc` (a gdb step landing on the same pc is the same halt). `DebugCore::frameEnded()`, called after every `retro_run`, closes a halt the core left without one of our verdicts (gdb continued, socket dropped, timeout) and drops a verdict written for it. | Without it the app stays "halted" after a gdb continue, inline today too. |
| "Shutdown: detach any debugger halt ... join" (section 7) | `App::quitting_` has two writers: the window's quit and `SIGINT`/`SIGTERM` handlers in `main.cpp` (`g_signalQuit`). A headless run that gdb holds halted sits in `emu.advance()`; the signal is its only exit, and it exits 0 with `headless: quit requested, exiting cleanly`. | `gdb_threaded_halt` needs exactly that exit; it is also the right behaviour for a Ctrl-C. |

## Interfaces (frozen)

Every section uses these declarations verbatim. Names follow the existing headers; where a header is extended, the existing members are unchanged.

### `app/src/emu_commands.h` (A1)

```cpp
class EmuCommands {
public:
    using Fn = std::function<void()>;
    using ErrorFn = std::function<void(const std::string&)>;
    using TimePoint = std::chrono::steady_clock::time_point;
    enum class Lane : uint8_t { Frame, Debug };   // Frame: applied at a frame boundary.  Debug: also inside a halt.

    bool post(Fn&& fn, Lane lane = Lane::Frame);          // false once closed (fn left untouched)
    size_t drain(const ErrorFn& onError, Lane lane = Lane::Frame);   // one lane, oldest first; throwing commands reported
    void waitUntil(TimePoint deadline);                   // wakes on a post to either lane, or notify()
    void notify();
    size_t closeAndDrain(const ErrorFn& onError);         // both lanes, Frame then Debug each round, then closed
    void reopen();
    bool closed() const;
    bool pending(Lane lane) const;                        // anything queued in that lane
private:
    std::deque<Fn> q_;     // Lane::Frame (as today)
    std::deque<Fn> dbg_;   // Lane::Debug
    // mu_, cv_, wake_, closed_ as today
};
```

### `app/src/emu_thread.h` (A1 adds the lane; B3 adds the give-up)

```cpp
class EmuThread {
public:
    // A frame-lane call (as today).  `giveUp` (B3), polled on the waiting thread about every 16-50 ms: when it returns
    // true before the task has started, the task is marked cancelled, is skipped when drained, and call() returns R().
    // A task that started before the mark still runs to completion (captures must be by value).  Never consulted
    // inline, on the emulation thread, or after the task started.
    template <class F>
    auto call(F&& fn, std::function<bool()> giveUp = {}) -> decltype(fn());
    template <class F>
    bool callFor(std::chrono::milliseconds timeout, F&& fn);   // unchanged

    // The debug lane (A1): safe inside retro_run().  Inline, or on the emulation thread: runs now.  Threaded: queued in
    // Lane::Debug, drained at the next frame boundary or by the halt pump (serveDebugLane), whichever comes first.
    // debugCall waits like call() (same wait hook after kBusyAfter); it never takes a giveUp.
    template <class F>
    auto debugCall(F&& fn) -> decltype(fn());
    void debugPost(std::function<void()> fn);

    // Emulation thread only (B1): what the halt pump does per iteration.  Drains Lane::Debug now; if nothing was
    // queued, waits up to `maxWait` for a post to the debug lane or a notify(), then drains again.  Returns the number
    // of commands run.  Inline: drains and returns at once (never waits).
    size_t serveDebugLane(std::chrono::milliseconds maxWait);
    // ...everything else unchanged (start, stop, post, inlineStep, advance, setPaused, setWaitHook, ...)
private:
    // loop(): after `cmds_.drain(onError, Lane::Frame)` comes `cmds_.drain(onError, Lane::Debug)`; closeAndDrain
    // covers both.  A debug post from the UI must wake a parked (paused / idle) thread: post() notifies as the frame
    // lane does.
};
```

### `app/src/debug/debug_views.h` (A2; no ImGui, no SDL, no core include beyond `vj_debug_iface.h`)

```cpp
namespace vj {

struct DisasmLine {
    unsigned addr = 0;
    unsigned len = 0;      // bytes (2, or 6 for MOVEI; the 68000's own lengths through Musashi); 0 = could not disassemble
    std::string text;
    std::string bytes;     // the hex bytes of the instruction (what cpu_view's disasmForward returns), "" when len == 0
};
// A5 injects this: runs on the emulation thread inside fill().  Without one, Disasm serves RISC targets through
// iface->disasm_risc and answers ok = false (no lines) for VJ_DBG_TARGET_M68K.
using DisasmFn = std::function<std::vector<DisasmLine>(const vj_debug_iface*, int target, unsigned addr, unsigned rows)>;

struct ViewRequest {
    enum class Kind : uint8_t { Memory, Dram, Regs, Disasm, OpSnapshot, BlitLog };
    Kind kind = Kind::Memory;
    unsigned addr = 0;                  // Memory: first bus address; Disasm: first instruction
    unsigned len = 0;                   // Memory: bytes (<= 64 KiB per request); Dram: ignored (DebugSession::kDramSize)
    int target = VJ_DBG_TARGET_M68K;    // Regs, Disasm
    unsigned rows = 0;                  // Disasm: instructions wanted (<= 512)
    unsigned since = 0;                 // BlitLog: first launch index wanted (blit::LogTracker's next_)
};

struct ViewBuffer {
    ViewRequest req;                    // the request this buffer answers (a window compares addr / since / target)
    uint64_t serial = 0;                // the refresh serial the fill ran under (0 = never filled)
    uint64_t epoch = 0;                 // DebugViews::epoch() at the fill; a content switch bumps it (read() then fails)
    unsigned frame = 0;                 // iface->frame_number() at the fill
    bool ok = false;                    // the iface answered every call
    std::vector<uint8_t> bytes;         // Memory, Dram (big-endian bus order)
    vj_debug_regs regs{};               // Regs
    std::vector<DisasmLine> lines;      // Disasm
    std::vector<vj_debug_op_obj> objs;  // OpSnapshot (opCount entries)
    int opCount = -1;                   // OpSnapshot: -1 = armed, not taken yet
    unsigned halfline = 0;              // OpSnapshot
    std::vector<vj_debug_blit> blits;   // BlitLog: entries since..head-1 that were still in the ring
    unsigned blitHead = 0;              // BlitLog: iface->blit_log_head() at the fill
    unsigned blitLost = 0;              // BlitLog: entries between `since` and the oldest kept
};

class DebugViews {
public:
    using Id = uint32_t;
    static constexpr Id kNoId = 0;
    static constexpr unsigned kMaxMemoryBytes = 65536;
    static constexpr unsigned kMaxDisasmRows = 512;

    // ---- UI thread (set / clear may also be called from a posted lambda on the emulation thread: one mutex) ----
    // kNoId allocates a new id; an existing id is replaced.  Returns the id.  A request equal to the stored one
    // changes nothing (windows re-state theirs every draw); a different one clears the front buffer (read() fails
    // until the next fill) and is pending at the same serial.
    Id set(Id id, const ViewRequest& r);
    void clear(Id id);                         // no-op for an unknown id
    void setDisasm(DisasmFn fn);               // A5: the CPU tool's disassembler; empty = the built-in RISC-only fill
    // Copies the front buffer.  False (and `out` untouched) when the id is unknown, never filled, or filled in an
    // older epoch.  Cheap: the mutex is held for the copy only.
    bool read(Id id, ViewBuffer& out) const;
    size_t count() const;                      // live requests (tests)

    // ---- emulation thread (inline: the same thread) -------------------------------------------
    // Fills every request whose front buffer carries a serial other than `serial` (or none).  Copies the request
    // table out first; no lock is held while the iface is called.  Returns how many were filled.  A null iface fills
    // nothing.  Fills run in id order.
    size_t fill(const vj_debug_iface* iface, uint64_t serial);
    bool pending(uint64_t serial) const;       // some request is not yet filled at `serial` (the pump asks this)
    void bumpEpoch();                          // a content switch: every buffer becomes unreadable until refilled
    uint64_t epoch() const;                    // atomic
};

}  // namespace vj
```

Request limits are enforced by `set()` (a larger `len`/`rows` is clamped). `Dram` requests copy `DebugSession::kDramSize` bytes from address 0.

### `app/src/debug/debug_core.h` (A3 creates it with the iface wrappers; B1 adds the halt)

The emulation-thread owner of `vj_debug_iface` and `vj_debug_host`. Every method runs on the emulation thread (inline: the UI thread). No ImGui, no SDL.

```cpp
namespace vj {

struct HaltState {
    bool halted = false;
    int target = -1;                 // VJ_DBG_TARGET_*
    unsigned pc = 0;
    int reason = VJ_DBG_STOP_NONE;   // VJ_DBG_STOP_*
    uint64_t serial = 0;             // bumps on every ENTRY into a halt (a step's re-halt included) and on CONTINUE /
                                     // DETACH; the STEP return itself does not bump it
};

class DebugCore {
public:
    static constexpr std::chrono::milliseconds kPumpWait{4};   // the pump's bounded wait per iteration (B1)

    DebugCore();                                         // fills host_ (size, user, halt_pump thunk)
    void setIface(const vj_debug_iface* i);              // replaces (and forgets) the old one; drops attach + halt; bumps ifaceGen
    const vj_debug_iface* iface() const;                 // atomic: readable from any thread, written here only
    bool loaded() const;                                 // atomic: a debug interface is present (CoreHost::onDebugIface)
    uint64_t ifaceGen() const;                           // atomic: bumps on every setIface() that changes the pointer (the
                                                         // core's struct is static, so the pointer alone repeats across a reload)
    // 3a only (A3), removed by B1: the session keeps today's halt machine and installs it here.
    std::function<int(int target, unsigned pc)> pumpHook;   // pump() returns pumpHook ? pumpHook(t, pc) : VJ_DBG_RESUME_DETACH
    void markDetached();                                 // clears attached() without calling the core (the DETACH verdict path)

    // ---- iface wrappers (A3): each returns the iface's own result; a null iface returns 0 / false / VJ_DBG_ERR_STATE --
    int peek(unsigned addr, uint8_t* dst, unsigned n) const;
    int poke(unsigned addr, const uint8_t* src, unsigned n, unsigned flags);
    bool getRegs(int target, vj_debug_regs& out) const;
    bool setReg(int target, int reg, unsigned v);
    bool disasmRisc(int target, unsigned addr, std::string& out, unsigned& len) const;
    bool attach();                                       // false: no iface, or the core refused
    void detach();                                       // clears the core's breakpoints and the host
    bool attached() const;
    bool breakSet(int target, unsigned addr);
    bool breakClear(int target, unsigned addr);
    void haltNext(int target);
    void opSnapshotArm();
    int opSnapshotGet(std::vector<vj_debug_op_obj>& out, unsigned& halfline) const;   // -1 while armed
    unsigned cryToRgb(unsigned v16) const;
    bool buildCryTable(std::vector<uint32_t>& out) const;   // 65536 entries, 0x00RRGGBB; false without cry_to_rgb
    void blitLogEnable(bool on);
    unsigned blitLogHead() const;
    bool blitLogGet(unsigned i, vj_debug_blit& out) const;
    int texReplaceReload();                              // 1 active, 0 inactive, -1 not supported (size check)
    unsigned frameNumber() const;
    bool region(int idx, vj_debug_region& out) const;

    // ---- the halt (B1) ------------------------------------------------------------------------
    HaltState halt() const;                              // any thread: a copy under a mutex
    uint64_t haltSerial() const;                         // any thread: atomic, == halt().serial
    // The UI writes the verdict (VJ_DBG_RESUME_CONTINUE / _STEP / _DETACH) and calls notifyVerdict(); the pump
    // consumes it.  VJ_DBG_RESUME_NONE = none pending.
    std::atomic<int> verdict{VJ_DBG_RESUME_NONE};
    std::function<void()> notifyVerdict;                 // App: a debug-lane notify (EmuThread::debugPost of a no-op)
    // Threaded (App sets it when the thread is on): serve the lane and wait <= kPumpWait, then return.  Inline: empty,
    // and the pump runs `runUiFrame` instead (one nested UI frame).
    std::function<void()> waitVerdict;
    std::function<void()> runUiFrame;                    // inline only
    std::function<bool()> abortHalt;                     // polled each iteration; true = DETACH (the app is quitting)
    std::function<void(int target, unsigned pc, int reason)> onHalt;   // emulation thread, first iteration of a halt
    std::vector<std::function<void()>> onResume;         // emulation thread, on CONTINUE / DETACH (not STEP)
    int pump(int target, unsigned pc);                   // the halt_pump thunk's target; returns the verdict or NONE
    // Emulation thread, right after every retro_run() (App::emuRunOneFrame, App::stepEmulation).  If halt().halted is
    // still true the core left GDBHalt() without one of our verdicts (gdb continued, the socket dropped, the timeout
    // fired): clears the state, bumps the serial, fires onResume, and verdict.exchange(NONE) so a verdict written for
    // the dead halt never resumes the next one.  A no-op when not halted.
    void frameEnded();
private:
    static int haltPumpThunk(void* user, int target, unsigned pc);
};

}  // namespace vj
```

`onHalt` and `onResume` are vectors (as today's `DebugSession::onHalt`), walked on copies. `pump()` contract: a **new halt** is `!st.halted || st.target != target || st.pc != pc`; on one it records target/pc, `reason` from `iface->halt_reason()` when the iface's `size` covers it (else `VJ_DBG_STOP_BREAKPOINT`), bumps the serial and fires `onHalt(target, pc, reason)`. Then: if `abortHalt()` -> DETACH; else take `verdict.exchange(NONE)`; if NONE and `waitVerdict` is set, call it once and re-take; else if `runUiFrame` is set, call it once and re-take; if still NONE and `abortHalt()` -> DETACH. NONE -> return NONE with the state untouched. Otherwise `halted = false`, `target = -1`, `reason = NONE` (pc kept for the bar's last value); DETACH -> `attached` cleared; CONTINUE / DETACH -> serial bump and `onResume`; return the verdict. Never waits longer than `waitVerdict` does. The mutex guards `HaltState`; the serial is mirrored in an atomic for `haltSerial()`.

### `app/src/debug/debug_session.h` (A3 rewires; B1/B2 add the mirror)

The UI-thread object the windows and menus use. Its public surface is today's (`enabled`, `attach`, `detach`, `breakNow`, `resume`, `step`, `detachAndResume`, `halted`, `haltedTarget`, `haltedPc`, `haltReason`, breakpoints, `tick`, `refreshSerial`, `requestRefresh`, `read`, `readU`, `poke`, `pokeU`, `regions`, the tools registry) with these changes:

```cpp
class DebugSession {
public:
    // ---- wiring ----
    DebugCore& core();                                   // the emulation-thread half (App wires its hooks)
    DebugViews& views();                                 // the request copier the windows use
    // A3: how a method that must touch the core reaches the emulation thread.  Empty (inline, tests): runs fn now.
    std::function<void(std::function<void()>)> coreCall;   // waits (App: emu.debugCall<void>)
    std::function<void(std::function<void()>)> corePost;   // fire and forget (App: emu.debugPost)
    void callCore(std::function<void()> fn) const;       // coreCall ? coreCall(fn) : fn()   (the windows use these two,
    void postCore(std::function<void()> fn);             // corePost ? corePost(fn) : fn()    never the hooks directly)
    // A3: the view fills.  fillViews() has NO gate (Neuro's watches depend on it under every state but "no core").
    size_t fillViews();                                  // emulation thread (inline: UI): views().fill(core().iface(), refreshSerial())
    void wakeViews();                                    // UI thread, once per UI frame (App::syncDebug) and by every window draw
                                                         // after its set()s: returns unless hasCore() && content loaded; inline
                                                         // (corePost empty) fills now; threaded posts ONE fillViews() to the debug
                                                         // lane while views().pending(refreshSerial()) (single-outstanding flag)
    void onCoreIface(const vj_debug_iface* i);           // emulation thread (inline: UI): core().setIface(i) + views().bumpEpoch().
                                                         // Touches no UI state (CoreHost::onDebugIface fires on the emulation thread)
    void syncCore();                                     // UI thread (App::syncDebug, first): when core().ifaceGen() moved, forget
                                                         // bps_, regions_, the halt mirror; fire onForcedClose("no core") if the core
                                                         // went away.  setIface(i) == onCoreIface(i); syncCore() (tests, inline)
    uint64_t refreshSerial() const;                      // atomic from A3 on (read on the emulation thread by fillViews)
    // B2: the halt mirror.  Read by the UI every frame; refreshed from core().halt() by syncHalt().
    void syncHalt();                                     // UI thread, once per UI frame (App::syncDebug): copies the HaltState
                                                         // and attached(); when the serial moved fires onHaltUi (now halted) or
                                                         // onResumeUi (was halted, is not); a halt that came and went between two
                                                         // UI frames fires nothing
    bool halted() const;                                 // the mirror (B1 reads core().halt().halted until B2 lands the mirror)
    int haltedTarget() const;  unsigned haltedPc() const;  int haltReason() const;
    uint64_t haltSerial() const;                         // the mirror's serial
    bool haltByGdb() const;                              // set on the halt edge: reason == BREAKPOINT ? !hasBreakpoint(target, pc)
                                                         // : !userHaltPending_ (breakNow()/step() set it; the edge clears it)
    std::vector<std::function<void(int target, unsigned pc, int reason)>> onHaltUi;   // UI thread (toast, pacer reset)
    std::vector<std::function<void()>> onResumeUi;
    // A3: the halt-availability gate that replaces setThreadBlocked while 3a is in and 3b is not.  TRANSITIONAL: B4
    // deletes all three (and the lines that test them) once nothing is switched off; G1a's bar greps for them.
    void setHaltAvailable(bool on);                      // false: Break / Step / breakpoints greyed and attach() refuses; windows
                                                         // still live; turning it off detaches.  Does not close the tools
    bool haltAvailable() const;
    std::string haltDisabledReason() const;              // "" when available, else "Break, Step and breakpoints are not available
                                                         // while Settings > Interface > Emulation thread is on (this version)"
    bool toolAvailable(const DebugTool& t) const;        // enabled() && t.toggle && (!t.needsHalt || (haltAvailable() && halted()))
                                                         // (after B4: ... || halted())
    // ---- marshalled actions (each uses callCore / postCore; the bps_ list stays the UI-side source of truth) ----
    bool attach();                                       // false unless enabled() && haltAvailable(); callCore: core().attach() then
                                                         // breakSet for a by-value copy of every enabled bps_ entry
    void detach();                                       // halted: the DETACH verdict; else postCore core().detach()
    void breakNow(int target = VJ_DBG_TARGET_M68K);      // attach(), then postCore haltNext; sets userHaltPending_ (B2)
    void resume();  void step();  void detachAndResume();   // B1: core().verdict.store + notifyVerdict, only while halted() and
                                                         // core().haltSerial() == haltSerial() (a verdict for a dead halt is dropped)
    bool read(unsigned addr, std::vector<uint8_t>& out, unsigned n) const;   // callCore peek (one-shot readers only; the scanner's
                                                                             // 2 MB scan pass is one of them)
    int poke(unsigned addr, const uint8_t* data, unsigned n);                // callCore poke; bumps the serial
    bool getRegsNow(int target, vj_debug_regs& out) const;                   // callCore (edits / step-over only)
    bool setReg(int target, int reg, unsigned v);                            // callCore
    bool buildCryTable(std::vector<uint32_t>& out) const;                    // callCore, once per core per window
    void opSnapshotArm();  void blitLogEnable(bool on);  int texReplaceReload();   // callCore
    std::string disabledReason() const;                  // the "Not available while ... Emulation thread" line is gone after A9
};
```

Sequencing of the old members: `iface()` (now `core().iface()`), `setThreadBlocked()` / `threadBlocked()` and the inline halt machine (`halted_`, `queued_`, `pump`, `runUiFrame`, `abortHalt`, `onHalt`, `onResume`, `resumeQueued`) stay on the session through A3-A8 (the windows migrate one at a time and `debug_e2e.cpp` uses `dbg.onHalt`); A9 removes `iface()` / `setThreadBlocked()`; B1 moves the halt machine to `DebugCore` (its `onHalt` / `onResume` fire on the emulation thread) and rewires `debug_e2e.cpp` and `cpu_ui.cpp`'s hook to `core().onHalt`; B2 adds `onHaltUi` / `onResumeUi` and moves `cpu_ui.cpp`'s hook there. The "same core" token the windows compare is `views().epoch()` (bumped by `onCoreIface`), never an iface pointer; `ifaceGen()` is for `syncCore()` only. Periodic readers never call `read()`/`getRegsNow()`; they `views().set()` a request, call `wakeViews()` once per draw, and `views().read()` the buffer, comparing `buf.serial` with `refreshSerial()` and `buf.req` with what they asked for.

### `App` hooks (A9, B2)

```cpp
// app.h
void fillDebugViews();          // emulation thread (inline: UI): debug.fillViews().  No gate: not debug.enabled(), not
                                // hardcore (Neuro's watches ride it); the 10 Hz cadence is the serial's (DebugSession::tick)
void haltWait();                // emulation thread, threaded: emu.serveDebugLane(DebugCore::kPumpWait); then
                                // fillDebugViews() if debug.views().pending(debug.refreshSerial())
std::atomic<bool> quitting_;    // what DebugCore::abortHalt reads (B2), with g_signalQuit (B5); `quit` stays the UI's flag
bool quitRequested() const;     // quit || g_signalQuit (B5): every run loop's exit test
std::function<bool()> haltGiveUp() const;   // B3: [this]{ return debug.core().halt().halted; }
std::function<bool()> detachForCall();      // B3: threaded && debug.halted(): detachAndResume() now, then a predicate that
                                            // gives up only on a NEW halt (haltSerial moved); else haltGiveUp()
void syncGdbHost();             // B4, UI thread from syncDebug(): once per load, when virtualjaguar_gdb_stub is enabled
                                // and debug.enabled(), debug.attach() and log "[debug] gdb host attached (silent)"
std::atomic<bool> gdbLockedByHardcore_;     // B4: wireHost's optionLock forces virtualjaguar_gdb_stub=disabled while set
// app.h (file scope) / main.cpp (B5)
extern std::atomic<bool> g_signalQuit;      // SIGINT / SIGTERM handlers store true
```

Call sites: `fillDebugViews()` at the end of `App::emuRunOneFrame()` (after `host.publishSnapshot()`) and once after the frame loop in `App::stepEmulation()` (not per frame: fast-forward runs four); `haltedFrame()` needs none because it runs `syncDebug()`, whose `wakeViews()` fills inline. `debug.core().frameEnded()` right after `host.runFrame()` in both. `syncDebug()` runs, in order, `debug.syncCore()`, `debug.syncHalt()`, the three existing lines, `syncGdbHost()`, `debug.wakeViews()`. `DebugCore::waitVerdict = [this]{ haltWait(); }` is set in `startEmuThread()` on success and cleared in its failure branch. `limitFeaturesForEmuThread()` shrinks in A9 to `debug.setHaltAvailable(false)` + the gdb option lock and is deleted in B4 (`host.announceDebug` stays true from A9 on).

### `app/src/input_snapshot.h` (C1; `remap.h` provides `ControlSet`, `CoreInput`)

```cpp
struct AgentStep {
    ControlSet held;
    unsigned frames = 1;           // 1..600
};
struct AgentProgram {
    uint64_t gen = 0;              // 0 = no program; the UI bumps it on every publish (takeAgent is true only for a newer gen)
    uint64_t lineage = 0;          // bumped only by InputQueue::clear() (release_all, link drop, hardcore, input block, game
                                   // change, Neuro off): a new lineage restarts the replica's count
    uint64_t firstFrame = 0;       // frame index, inside the lineage, of steps[0]'s first frame (the UI trims played frames
                                   // and raises it), so a new gen of the same lineage neither replays nor drops frames
    bool enabled = false;          // Agent::allowed() && actions_.blocked().empty()
    ControlSet hold;               // added to every frame
    std::vector<AgentStep> steps;  // run in order after the frames already played
};
struct AgentProgress {
    uint64_t gen = 0;              // the program this reports on
    uint64_t lineage = 0;
    unsigned played = 0;           // frames consumed from the steps, ABSOLUTE inside the lineage (not counting the hold)
    bool done = false;             // steps exhausted
    ControlSet pressed;            // what the last frame pressed (hold | step)
};
class InputSnapshot {
public:
    // ...publish / take / takenSince unchanged (their mutex is not touched: the agent channel has its own)...
    void publishAgent(const AgentProgram& p);      // UI thread; the whole program, newest wins
    bool takeAgent(AgentProgram& out);              // emulation thread: true (and `out` filled) only when gen changed
    void reportAgent(const AgentProgress& p);       // emulation thread, after each frame()
    AgentProgress agentProgress() const;            // UI thread
};
```

### `app/src/neuro/neuro_actions.h` (C1)

```cpp
// The emulation-thread replica of InputQueue (pure; no sockets, no core).
class AgentPlayer {
public:
    void load(const AgentProgram& p);     // a new lineage restarts played at 0; the same lineage keeps it and skips
                                          // played - firstFrame frames into the new steps (clamped up to firstFrame)
    ControlSet frame();                   // one emulated frame: hold | current step; advances; empty and NOT advancing when !enabled
    AgentProgress progress() const;
    uint64_t gen() const;
};
// The one function the app's input poll and the tests call: takeAgent + load on a new gen, frame(), reportAgent.
ControlSet pollAgent(InputSnapshot& s, AgentPlayer& p);
// InputQueue (UI side) gains:
uint64_t version() const;             // bumped by every successful enqueue, every setHold that changes the hold, every clear
uint64_t lineage() const;  uint64_t firstFrame() const;
void trim(unsigned frames);           // drops the first `frames` frames of the steps (clamped); raises firstFrame
AgentProgram program(uint64_t gen, bool enabled) const;   // its steps + hold + lineage + firstFrame as a program
```

`Agent` (neuro_agent.h): `frame()` is removed. `void publishTo(InputSnapshot&)` (a no-op when `version()`, `lineage()` and the enabled flag are unchanged) and `void absorb(const AgentProgress&)` (trims the queue by matching-lineage progress; `pressedNow()` = that progress's `pressed`, empty when not allowed) are called from `neuroTick()` (absorb before `tick()`, publish after it, unconditionally, so a clear made while `!allowed()` still reaches the emulation thread) and `neuroShutdown()`. `uint64_t framesPlayed() const` sums the absorbed progress. `InputManager::agentControls` goes; `void InputManager::addAgentControls(ControlSet c)` (`if (c.any()) core_.addControls(0, c);`) keeps the inline hashes unmoved. `GameState::ram` / `ramSize` are replaced by `std::vector<uint8_t> watchBytes; uint32_t watchBase = 0;` (the bytes of one `Memory` request covering the profile's watches, `watchBase` = its first address; empty = unknown, the state text says "?"); `watchValue(const Watch&, const uint8_t* bytes, size_t size, uint32_t base)` is the 4-argument overload (the 3-argument form is `base = 0`). `bool Agent::watchRange(uint32_t& base, unsigned& len) const` returns the covering range (false without watches); `constexpr unsigned kMaxWatchSpan = 65536` (`== DebugViews::kMaxMemoryBytes`, static_asserted): a wider span is served by a `Kind::Dram` request instead of a clamp. `void neuroHeadlessReport(App&)` (neuro_app.cpp) prints `[neuro] headless: N frames of input played` and `[neuro] state: <stateText with newlines as "|">` at the end of a headless `--neuro` run, inline and threaded.

### `app/src/build_channel.h` (D1)

```cpp
#ifndef VJ_BUILD_CHANNEL
#define VJ_BUILD_CHANNEL "dev"
#endif
#ifndef VJ_EMU_THREAD_DEFAULT
#define VJ_EMU_THREAD_DEFAULT 0
#endif
namespace vj {
constexpr const char* kBuildChannel = VJ_BUILD_CHANNEL;             // "release" | "nightly" | "dev"
constexpr bool kEmuThreadDefault = VJ_EMU_THREAD_DEFAULT != 0;      // nightly: true
constexpr bool emuThreadDefaultFor(std::string_view channel);        // channel == "nightly"
constexpr bool emuThreadEffective(int pref, bool channelDefault);    // pref < 0 ? channelDefault : pref == 1
}
```

The header ends with `static_assert(kEmuThreadDefault == emuThreadDefaultFor(kBuildChannel), "VJ_EMU_THREAD_DEFAULT disagrees with VJ_BUILD_CHANNEL (app/CMakeLists.txt)")`. `App::emuThreadWanted()` (app.cpp:282) becomes `return emuThreadEffective(emuThreadPref, kEmuThreadDefault);`. CMake (app/CMakeLists.txt): `set(VJ_BUILD_CHANNEL "dev" CACHE STRING "release | nightly | dev: the build channel (nightlies default the emulation thread on)")`, `set_property(CACHE VJ_BUILD_CHANNEL PROPERTY STRINGS release nightly dev)`, a `FATAL_ERROR` on any other value, and an INTERFACE target `vj_build_channel` carrying `VJ_BUILD_CHANNEL="${VJ_BUILD_CHANNEL}"` and `VJ_EMU_THREAD_DEFAULT=<1 for nightly, else 0>`, linked into `virtualjaguar` and the default test. Start-up prints two lines on every run, headless included: `[emu] channel=<c>, emulation thread default <on|off>, this run <on|off>` and `[emu] ui.emu_thread <unset|on|off> -> wanted <on|off>` (the stored choice and `emuThreadWanted()`; headless is always inline, so the second line is what pins the rule). Workflow: `app-package.yml`'s `meta` job emits `channel` (`nightly` -> `nightly`, `v*` -> `release`, else `dev`); the three `cmake -S app` lines add `-DVJ_BUILD_CHANNEL="$CHANNEL"` from a job env var; the flatpak manifest gains `- -DVJ_BUILD_CHANNEL=release` and the flatpak job seds it to `nightly` when `needs.meta.outputs.label == 'nightly'`. Test-support flags for the Studio gate (G1b, not part of the frozen interfaces): `--show-window tools` opens every Tools window and logs `[app] tool <id> open=<0|1>`; `--perf-halt` does F9's break at UI iteration 120, re-opens the tools at 240 (logging `open=... (halted)` and `[perf] halted=<0|1>`) and resumes at 420.

## File Structure

| File | Status | Sub-phase | Responsibility |
|---|---|---|---|
| `app/src/emu_commands.h` | modify | 3a | `Lane`, the debug deque, per-lane drain, `pending` |
| `app/src/emu_thread.h`, `app/src/emu_thread.cpp` | modify | 3a, 3b | `debugCall`/`debugPost`, the lane drain in `loop()`, `serveDebugLane`; the `giveUp` predicate (3b) |
| `app/src/debug/debug_views.h`, `app/src/debug/debug_views.cpp` | new | 3a | request table, double buffers, `fill`, epoch |
| `app/src/debug/debug_core.h`, `app/src/debug/debug_core.cpp` | new | 3a, 3b | the emulation-thread half: iface wrappers; the halt pump, verdict, `HaltState` (3b) |
| `app/src/debug/debug_session.h`, `app/src/debug/debug_session.cpp` | modify | 3a, 3b | UI mirror, `coreCall`/`corePost` + `callCore`/`postCore`, `views()`, `fillViews`/`wakeViews`/`onCoreIface`/`syncCore`, `setHaltAvailable`/`haltDisabledReason`; `syncHalt`, `haltByGdb`, `onHaltUi` (3b) |
| `app/src/debug/core_job.h` | new | 3a | `CoreJob<T>`: work posted to the emulation thread whose result the UI takes later (CPU navigation walks, the OP render and the fallback walk) |
| `app/src/debug/blitter_feed.h`, `app/src/debug/blitter_feed.cpp` | new | 3a | `blit::LogFeed`: the blitter window's requests, absorb and the log enable/clear calls |
| `app/tests/debug_ui_fixture.h`, `app/cmake/CheckNoDirectIface.cmake` | new | 3a | `CountingIface` / `UiFixture` (every window smoke counts core calls and off-core calls); the static "no direct iface" guard (`debug_no_direct_iface`) |
| `app/tests/debug_core_job_test.cpp`, `debug_cpu_ui_smoke.cpp`, `debug_blitter_feed_test.cpp`, `debug_blitter_ui_smoke.cpp`, `debug_scope_gate_test.cpp` | new | 3a | per-window unit and smoke tests |
| `app/src/main.cpp` | modify | 3b, 3d | `SIGINT`/`SIGTERM` handlers (`g_signalQuit`, 3b); `--show-window tools`, `--perf-halt` (G1b) |
| `app/tests/neuro_core_e2e.cpp` | new | 3c | the real core's joypad matrix with a program, inline and threaded, lock-step and free-running |
| `app/tests/no_threaded_limits.sh`, `app/tests/threaded_branch_allowlist.txt` | new | G1a | the mechanical bar: no gate, no "off while threaded" text, no old limit test, no unlisted `threaded()` branch, `tsan.supp` unchanged |
| `app/tests/emu_thread_gate_check.sh`, `app/tests/emu_thread_gate_gdb.py`, `app/tests/neuro_gate_server.py` | new | G1b | the Studio gate: tools + F9 halt + Neuro, gdb halt with the tools open, plain; a stdlib WebSocket Neuro server; a gdb probe |
| `app/packaging/README.md` | modify | 3d | the build channel |
| `app/src/debug/memory_ui.cpp`, `memory_view.cpp/.h` | modify | 3a | window range + cry table through views / coreCall |
| `app/src/debug/clut_ui.cpp` | modify | 3a | two Memory requests |
| `app/src/debug/cpu_ui.cpp`, `cpu_view.cpp/.h` | modify | 3a | Regs + Disasm requests; edits, temp breakpoints, step-over through the session |
| `app/src/debug/op_ui.cpp`, `op_view.cpp/.h`, `op_render.cpp/.h` | modify | 3a | OpSnapshot request; arm / render / patch through coreCall; "step one frame" = `emu.advance(1)` |
| `app/src/debug/blitter_ui.cpp`, `blitter_log.cpp/.h` | modify | 3a | BlitLog + regs requests; enable/clear through coreCall |
| `app/src/debug/texture_ui.cpp`, `texture_pack.cpp/.h` | modify | 3a | reload through coreCall; `replaceTile` takes a reload callable instead of the iface |
| `app/src/debug/scanner_ui.cpp`, `scanner_actions.cpp` | modify | 3a | Dram request replaces `memoryRegion`; pokes through the session |
| `app/src/debug/scope_ui.cpp` | modify | 3a | gate lifted only |
| `app/src/debug/debug_ui.cpp` | modify | 3a, 3b | Tools menu uses `toolAvailable`; the halt bar's "applies after Continue" line and "by gdb" (3b) |
| `app/src/app.h`, `app/src/app.cpp` | modify | all | `fillDebugViews`, limits narrowed (3a); `haltWait`, `quitting_`, `syncHalt`, give-up at call sites, detach-first, shutdown detach, gdb unlock + silent attach, `haltedFrame` inline-only (3b); `inputPoll` with `AgentPlayer`, `publishInput` agent program, watches (3c); `emuThreadWanted`, the start-up log line (3d) |
| `app/src/core_host.cpp` | modify | 3b | refusal texts only ("resume it first" also names F9) |
| `app/src/achievements.cpp` | modify | 3b | hardcore locks `virtualjaguar_gdb_stub` to `disabled` (Decision 2) |
| `app/src/ui.cpp` | modify | 3b, 3d | options tab "applies after Continue" while halted; the Interface hint and default note |
| `app/src/desktop.cpp` | modify | 3b | F9/F10 menu commands through the mirror |
| `app/src/input_snapshot.h` | modify | 3c | `AgentStep`, `AgentProgram`, `AgentProgress`, the four agent methods |
| `app/src/input.h`, `app/src/input.cpp` | modify | 3c | `agentControls` removed |
| `app/src/neuro/neuro_actions.h/.cpp`, `neuro_agent.h/.cpp`, `neuro_app.cpp` | modify | 3c | `AgentPlayer`, `program()`, `publishTo`/`absorb`, watch range, Settings hint |
| `app/src/build_channel.h` | new | 3d | the channel and the default rule |
| `app/CMakeLists.txt` | modify | 3d | `VJ_BUILD_CHANNEL`, the defines |
| `.github/workflows/app-package.yml`, `app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml`, `.github/workflows/app.yml` | modify | 3d | channel plumbing; one nightly-channel CI configure |
| `app/cmake/VJEmuThreadTests.cmake`, `VJDebugTests.cmake`, `VJNeuroTests.cmake` | modify | all | new units, threaded twins, the rewritten limit tests |
| `app/tests/emu_commands_test.cpp`, `emu_thread_test.cpp`, `debug_session_test.cpp`, `debug_mock.h` | modify | 3a, 3b | lanes, debugCall, give-up; `DebugCore` with the mock, bounded pump |
| `app/tests/debug_views_test.cpp`, `debug_views_threaded.cpp`, `debug_e2e_threaded.cpp`, `gdb_threaded_halt.py`, `emu_thread_default_test.cpp` | new | 3a, 3b, 3d | unit and twin tests named in Review Focus |
| `app/tests/neuro_test.cpp` | modify | 3c | `AgentPlayer`, program/progress, the loopback twin |
| `app/README.md`, `docs/agent/testing.md`, `docs/app-debug-tools.md`, `docs/neuro.md`, `docs/app-emulation-thread-design.md` | modify | all | each sub-phase's docs task |

## Task numbering

- **3a** (debug views): A1 lanes + `debugCall`; A2 `DebugViews`; A3 `DebugCore` wrappers + session marshalling (inline unchanged, `debug_session_unit` green); A4 Memory viewer + Palette; A5 CPU debugger; A6 Object list; A7 Blitter log + Textures; A8 Scanner + Scope; A9 App wiring, limits narrowed, `debug_views_threaded`, docs, PR.
- **3b** (halt + gdb): B1 `HaltState`, verdict, bounded `pump()` (mock tests); B2 App threaded halt wiring (`waitVerdict`, `syncHalt`, `quitting_`, shutdown detach); B3 `call()` give-up + call-site policy ("applies after Continue", detach-first, refusals); B4 gdb unlock, silent attach, hardcore lock; B5 `debug_e2e_threaded`, `gdb_threaded_halt`, limit tests rewritten; B6 docs, Studio checks, PR.
- **3c** (Neuro): C1 `AgentProgram`/`AgentPlayer` + `InputSnapshot` methods (unit); C2 `Agent` publishes/absorbs, `agentControls` removed, `inputPoll`; C3 watches through `DebugViews`; C4 loopback twin, `neuro_on_threaded`, docs, PR.
- **3d** (flag): D1 CMake + `build_channel.h` + `emuThreadWanted` + unit test; D2 Settings/About/log line + smoke; D3 workflows, flatpak sed, nightly CI configure, docs, PR.
- **G1** (gate, on develop after 3a, 3b and 3c merged, before 3d's PR opens; its two code commits are the first on the 3d branch): G1a `no_threaded_limits`, the mechanical bar kept in CI; G1b the Studio drivers (`--show-window tools`, `--perf-halt`) and `emu_thread_gate_check.sh` with a stdlib Neuro server and a gdb probe; G1c full suite, TSan, the windowed gate (Release and TSan builds), evenness A/B/B/A with the Memory window open; G1d a whole-branch architecture review by a model that did not write 3a-3c. Results into 3d's PR body.

Each task's steps follow the Phase 2 style: failing test, run it, implement the named signature, run it, commit.

---

## Sub-phase 3a: debug views on the emulation thread

Branch `feat/967-phase3a-debug-views` off `libretro/develop`, worktree `~/Workspace/Provenance/vj-wt/emu-p3a` (create it with `git -C ~/Workspace/Provenance/virtualjaguar-libretro fetch libretro develop && git -C ~/Workspace/Provenance/virtualjaguar-libretro worktree add ~/Workspace/Provenance/vj-wt/emu-p3a -b feat/967-phase3a-debug-views libretro/develop`; all commands below run from its root). After this PR, with Settings > Interface > Emulation thread on, every Tools window works (Memory, Palette, Cheat scanner, CPU debugger, Object list, Blitter log, Textures, Audio scope) and `--show-window <tool>` opens it threaded. It leaves off: the halt (Break, Step, breakpoints are greyed with a reason; `announceDebug` is back **on** so the core hands over its interface, but `attach()` refuses while `haltAvailable()` is false, so the core can never halt for the app), the GDB stub (option still locked to `disabled`), Neuro, and the nightly default. The setting stays Off by default.

Every task ends with a green build and the existing suite passing in inline mode; a task never leaves a window half-migrated. A1-A3 are plumbing and change no behaviour; A4-A8 (separate section) migrate one window each; A9 wires the App and lifts the thread gate.

Build and test commands used throughout (`T` = a target, `R` = a ctest regex):

```bash
env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4 --target T && ctest --test-dir build -R R --output-on-failure
```

Check `uptime` first; do not start a build while the load is above 10.

### Task A1: `Lane::Debug` in `EmuCommands`, and `EmuThread::debugCall` / `debugPost`

**Files:**
- Modify: `app/src/emu_commands.h` (whole class, lines 23-128: second deque, per-lane `drain`, `pending`, `closeAndDrain`, `waitUntil` predicates at :61 and :70)
- Modify: `app/src/emu_thread.h` (`call` :84-90 and its body :144-172 become one private `submit`; new `debugCall`, `debugPost`)
- Modify: `app/src/emu_thread.cpp` (`post` :83-91 gets a twin; `loop()` :154 drains both lanes)
- Test: `app/tests/emu_commands_test.cpp`, `app/tests/emu_thread_test.cpp`, `app/cmake/VJEmuThreadTests.cmake` (header comment lines 6 and 8 only)

**Interfaces:**
- Consumes: nothing new.
- Produces (verbatim from the Interfaces section): `EmuCommands::Lane { Frame, Debug }`, `post(Fn&&, Lane = Frame)`, `drain(const ErrorFn&, Lane = Frame)`, `pending(Lane) const`, `closeAndDrain` over both lanes; `EmuThread::debugCall(F&&) -> decltype(fn())`, `EmuThread::debugPost(std::function<void()>)`. Not in this task: `serveDebugLane` and the `giveUp` argument (B1, B3).

- [ ] **Step 1: Write the failing tests.** In `app/tests/emu_commands_test.cpp`, before `main`, add:

```cpp
using Lane = EmuCommands::Lane;

void testLanes() {
    EmuCommands q;
    std::string ran;
    q.post([&ran] { ran += 'F'; });
    q.post([&ran] { ran += 'D'; }, Lane::Debug);
    CHECK(q.pending(Lane::Frame) && q.pending(Lane::Debug), "each lane holds its command");
    CHECK(q.drain(nullptr, Lane::Debug) == 1 && ran == "D" && q.pending(Lane::Frame),
          "drain(Lane::Debug) leaves the frame lane untouched and vice versa (debug first)");
    q.post([&ran] { ran += 'd'; }, Lane::Debug);
    CHECK(q.drain(nullptr, Lane::Frame) == 1 && ran == "DF" && q.pending(Lane::Debug),
          "drain(Lane::Frame) leaves the debug lane untouched (frame first)");
    CHECK(q.drain(nullptr) == 0, "the default lane is the frame lane: nothing left there");
    CHECK(q.drain(nullptr, Lane::Debug) == 1 && ran == "DFd" && !q.pending(Lane::Debug), "the debug lane is FIFO");
}

void testDebugLaneWakesAndCloses() {
    EmuCommands q;
    Clock::time_point t0 = Clock::now();
    std::thread poster([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.post([] {}, Lane::Debug);
    });
    q.waitUntil(t0 + std::chrono::seconds(5));
    CHECK(since(t0) < 1.0, "a debug-lane post wakes the wait early (%.3f s)", since(t0));
    poster.join();
    q.drain(nullptr, Lane::Debug);

    std::string ran;
    q.post([&ran] { ran += 'F'; });
    q.post([&q, &ran] {   // a debug command that queues another one while the queue closes: it still runs
        ran += 'D';
        q.post([&ran] { ran += 'E'; }, Lane::Debug);
    }, Lane::Debug);
    CHECK(q.closeAndDrain(nullptr) == 3 && ran == "FDE", "closeAndDrain runs both lanes, frame then debug each round (%s)", ran.c_str());
    CHECK(q.closed() && !q.post([] {}, Lane::Debug) && !q.post([] {}), "then both lanes refuse commands");
    q.reopen();
    CHECK(q.post([] {}, Lane::Debug) && q.pending(Lane::Debug), "reopen() takes debug commands again");
}
```

Call `testLanes(); testDebugLaneWakesAndCloses();` from `main` after `testProducers();`.

In `app/tests/emu_thread_test.cpp`, before `main`, add (`sleepMs`, `since`, `FakeClient` already exist; `holdAt` holds `runFrame` after it counted its frame):

```cpp
// #967 phase 3: the debug lane. Inline it runs at once; threaded it runs on the emulation thread, wakes a parked
// (paused) loop, waits for a frame in progress and runs at the boundary BEFORE the next frame starts, and runs the
// wait hook like call().
void testDebugLane() {
    {
        EmuThread e;
        FakeClient c;
        CHECK(e.start(EmuThread::Mode::Inline, c), "inline start");
        int x = 0;
        e.debugPost([&x] { x = 1; });
        CHECK(x == 1, "inline debugPost runs at once");
        CHECK(e.debugCall([&x] { return x + 1; }) == 2, "inline debugCall runs at once");
    }
    EmuThread e;
    FakeClient c;
    e.setPaused(true);   // no frames: only a command can wake the loop
    CHECK(e.start(EmuThread::Mode::Threaded, c), "threaded start");
    CHECK(e.debugCall([&e] { return e.onEmuThread(); }), "a debugCall runs on the emulation thread");
    CHECK(e.debugCall([&e] { return e.debugCall([] { return 7; }); }) == 7,
          "a debugCall made on the emulation thread runs inline");
    std::atomic<bool> ran{false};
    const Clock::time_point t0 = Clock::now();
    e.debugPost([&ran] { ran = true; });
    for (int i = 0; i < 400 && !ran.load(); i++) sleepMs(1);
    CHECK(ran.load() && since(t0) < 0.2, "a debugPost runs on a paused thread well inside its 250 ms idle wait (%.3f s)", since(t0));
    CHECK(c.frames == 0, "...without running a frame");
    std::atomic<bool> late{false};
    e.debugPost([&late] { late = true; });
    CHECK(e.stop() && late.load(), "stop() drains the debug lane too");
}

void testDebugCallWaitsForTheFrame() {
    EmuThread e;
    FakeClient c;
    c.holdAt = 1;   // the first frame counts itself, then holds
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && c.frames.load() < 1; i++) sleepMs(1);
    std::atomic<int> framesWhenRun{-1};
    std::atomic<bool> returned{false};
    std::thread caller([&] {
        e.debugCall([&] { framesWhenRun = c.frames.load(); });
        returned = true;
    });
    sleepMs(80);
    CHECK(!returned.load() && framesWhenRun.load() == -1, "a debugCall waits while a frame is in progress");
    c.holdAt = -1;
    caller.join();
    CHECK(framesWhenRun.load() == 1, "it runs at the frame boundary, before the next frame starts (frames %d)",
          framesWhenRun.load());
    CHECK(e.stop(), "stop");
}

void testDebugCallWaitHook() {
    EmuThread e;
    FakeClient c;
    std::atomic<int> calls{0};
    e.setWaitHook([&](double, unsigned) { calls++; });
    c.holdAt = 1;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && c.frames.load() < 1; i++) sleepMs(1);
    std::thread release([&c] {
        sleepMs(450);
        c.holdAt = -1;
    });
    e.debugCall([] { return 0; });
    release.join();
    CHECK(calls.load() >= 3, "a debugCall behind a slow frame runs the wait hook like call() (%d)", calls.load());
    CHECK(e.stop(), "stop");
}
```

Call all three from `main` after `testWaitHook();`. In `testJoinTimeout`, after the existing `CHECK(!e->callFor(... [&ran] ...` line, add:

```cpp
    CHECK(e->debugCall([&ran] {
        ran = true;
        return 1;
    }) == 0 && !ran, "after abandoning, debugCall() runs nothing and returns a default value");
```

- [ ] **Step 2: Run to see them fail.**

Run: `cmake --build build -j4 --target vj_emu_commands_test vj_emu_thread_test`
Expected: compile errors, `no matching member function for call to 'post'` (Lane argument) and `no member named 'debugCall' in 'vj::EmuThread'`.

- [ ] **Step 3: Implement.**
  - `app/src/emu_commands.h`: add `enum class Lane : uint8_t { Frame, Debug };` (`<cstdint>`), `std::deque<Fn> dbg_;` beside `q_`. `post(Fn&&, Lane = Lane::Frame)` pushes into that lane's deque and sets `wake_` as today. `drain(const ErrorFn&, Lane = Lane::Frame)` swaps out that lane's deque only. Both `waitUntil` predicates (the generic one and the MinGW `pending` lambda) become `wake_ || !q_.empty() || !dbg_.empty()`. `closeAndDrain`: each round swaps **both** deques out under the lock; if both were empty set `closed_` and return; else run the frame batch then the debug batch, add both sizes to `n`. `bool pending(Lane) const` locks and tests that deque. Header comment: one sentence on the two lanes.
  - `app/src/emu_thread.h`: rename the body of `call()` into `template <class F> auto submit(F&& fn, EmuCommands::Lane lane) -> decltype(fn())` (private; the code is unchanged except `cmds_.post([task] { (*task)(); }, lane)`); `call(F&& fn)` returns `submit(std::forward<F>(fn), EmuCommands::Lane::Frame)` and `debugCall(F&& fn)` returns `submit(..., EmuCommands::Lane::Debug)`. `callFor` is untouched. Declare `void debugPost(std::function<void()> fn);` with the frozen comment (safe inside `retro_run()`; the wait hook contract is `setWaitHook`'s). Document at `debugCall`: captures must be by value when the thread may be abandoned (the same rule as `call`), and there is no ordering between the two lanes beyond "frame lane first at each boundary".
  - `app/src/emu_thread.cpp`: `debugPost` is `post()`'s body with `cmds_.post(std::move(fn), EmuCommands::Lane::Debug)`. In `loop()` replace line 154 with `size_t ran = cmds_.drain(onError, EmuCommands::Lane::Frame); ran += cmds_.drain(onError, EmuCommands::Lane::Debug); if (ran > 0) client_->afterCommands();`. `closeAndDrain` already covers both lanes.
  - `app/cmake/VJEmuThreadTests.cmake`: header lines 6 and 8 mention the lanes and `debugCall` (comment only).

- [ ] **Step 4: Run to see them pass.**

Run: `cmake --build build -j4 --target vj_emu_commands_test vj_emu_thread_test && ctest --test-dir build -R "emu_commands_unit|emu_thread_unit" --output-on-failure`
Expected: both PASS; `emu_commands_test: OK`, `emu_thread_test: OK`. Also run `ctest --test-dir build -L emu_thread -R "core_host_thread|emu_thread_e2e|smoke_yarc_threaded" --output-on-failure`: PASS (the loop change touches every threaded run).

- [ ] **Step 5: Commit**

```bash
git add app/src/emu_commands.h app/src/emu_thread.h app/src/emu_thread.cpp app/tests/emu_commands_test.cpp app/tests/emu_thread_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): a debug lane in the emulation thread's command queue (#967)

EmuCommands gets Lane::Debug beside the frame lane; the loop drains both at
each frame boundary. EmuThread::debugCall / debugPost are safe inside
retro_run() once the halt pump serves the lane (phase 3b).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task A2: `DebugViews`, the request table and double buffers

**Files:**
- Create: `app/src/debug/debug_views.h`, `app/src/debug/debug_views.cpp`
- Create: `app/tests/debug_views_test.cpp`
- Modify: `app/CMakeLists.txt` (add `src/debug/debug_views.cpp` after `src/debug/debug_session.cpp` at :340), `app/cmake/VJDebugTests.cmake` (new target at the end)

**Interfaces:**
- Consumes: `vj_debug_iface` (`src/debug/vj_debug_iface.h`), `DebugSession::kDramSize` (`debug_session.h:51`; included from the `.cpp` only, so the headers do not cycle).
- Produces: `DisasmLine`, `ViewRequest`, `ViewBuffer`, `DebugViews` exactly as in the Interfaces section.

Semantics the tests pin (all in `debug_views.cpp`):
- **State.** A `std::map<Id, Slot>` under one mutex, `Slot { ViewRequest req; uint64_t rev; ViewBuffer front; }`, ids from 1, `std::atomic<uint64_t> epoch_{1}`.
- **`set()`.** Clamps `len` to `kMaxMemoryBytes` and `rows` to `kMaxDisasmRows`. A request equal to the stored one (every field) changes nothing. A different one stores it, bumps the slot's private `rev` and clears `front` (`serial = 0`).
- **`fill(iface, serial)`.** (1) Return 0 if `!iface`. (2) Under the lock copy `(id, req, rev)` of every slot whose `front.serial != serial || front.epoch != epoch()`. (3) Per copied request, with **no lock held**, read `epoch0 = epoch()` first, then fill a local `ViewBuffer` (table below), setting `serial`, `epoch = epoch0`, `frame = iface->frame_number()`. (4) Under the lock, install it only if the slot still exists and its `rev` is unchanged; otherwise drop it. (5) Return the number installed.
- **`read()`.** False when the id is unknown, `front.serial == 0`, or `front.epoch != epoch()`.
- **`pending(serial)`.** True if any slot would be copied by step (2).

| Kind | Fill | `ok` |
|---|---|---|
| Memory | `iface->peek(addr, len)` into `bytes` | peek returned `len` (else `bytes` empty) |
| Dram | `peek(0, DebugSession::kDramSize)` | same |
| Regs | `get_regs(target)` into `regs` | it returned 1 |
| Disasm | with a `DisasmFn` installed (`setDisasm`, A5): `lines = fn(iface, target, addr, rows)`; without one, RISC targets only: `rows` x `disasm_risc(target, a, ...)`, `a += len`; a failed row is `{a, 0, "", ""}` and `a += 2` | with a fn: true; without: true for GPU/DSP with the function present, an M68K request: false, no lines |
| OpSnapshot | `n = op_snapshot_get(buf, VJ_DBG_OP_MAX_OBJECTS, &halfline)`; `opCount = n`; `objs` = first `n` | true when the function is present (`n == -1` is a valid answer) |
| BlitLog | as `blit::LogTracker::sync` (blitter_log.cpp:195-206): `head = blit_log_head()`; `blitHead = head`; `head < since`: no entries, `blitLost = 0`; else `oldest = head > 256 ? head - 256 : 0`, `start = max(since, oldest)`, `blitLost = start - since` plus one per `blit_log_get` failure in `[start, head)`, `blits` = the successes | true when both functions are present |

Any kind with a null function pointer in the iface fills `ok = false`.

- [ ] **Step 1: Write the failing tests.** Create `app/tests/debug_views_test.cpp`:

```cpp
// DebugViews (src/debug/debug_views.h): the request table and per-request double buffers the emulation thread fills
// and the tool windows read (#967 phase 3a).  Mock core, no window.
//
//   vj_debug_views_test
#include "debug_mock.h"
#include "debug_session.h"   // DebugSession::kDramSize
#include "debug_views.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

using namespace vj;
using Id = DebugViews::Id;

static int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

static ViewRequest mem(unsigned addr, unsigned len) {
    ViewRequest r;
    r.kind = ViewRequest::Kind::Memory;
    r.addr = addr;
    r.len = len;
    return r;
}

// An iface whose peek() runs a hook: lets a test act "during the fill", with no lock held.
struct HookIface {
    static inline std::function<void()> onPeek;
    static int peek(unsigned, unsigned char* d, unsigned n) {
        if (onPeek) onPeek();
        for (unsigned i = 0; i < n; i++) d[i] = 0xAB;
        return (int)n;
    }
    static unsigned frame() { return 7; }
    static vj_debug_iface make() {
        vj_debug_iface i{};
        i.size = sizeof i;
        i.version = VJ_DEBUG_IFACE_VERSION;
        i.peek = &HookIface::peek;
        i.frame_number = &HookIface::frame;
        return i;
    }
};

static void testSetClampsAndIds() {
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0x1000, 1u << 20)), b = v.set(DebugViews::kNoId, mem(0x2000, 16));
    CHECK(a != DebugViews::kNoId && b != DebugViews::kNoId && a != b && v.count() == 2, "two requests, two ids");
    CHECK(v.set(a, mem(0x1000, 1u << 20)) == a && v.count() == 2, "an existing id is replaced, not added");
    ViewRequest d;
    d.kind = ViewRequest::Kind::Disasm;
    d.target = VJ_DBG_TARGET_GPU;
    d.addr = 0xF03000;
    d.rows = 9999;
    const Id c = v.set(DebugViews::kNoId, d);
    MockDebugIface m;
    v.fill(m.iface(), 1);
    ViewBuffer out;
    CHECK(v.read(a, out) && out.bytes.size() == DebugViews::kMaxMemoryBytes, "len is clamped to 64 KiB (%zu)", out.bytes.size());
    CHECK(v.read(c, out) && out.lines.size() == DebugViews::kMaxDisasmRows, "rows are clamped to 512 (%zu)", out.lines.size());
    v.clear(b);
    v.clear(12345);   // unknown: no-op
    CHECK(v.count() == 2 && !v.read(b, out), "clear() removes one request; an unknown id is a no-op");
}

static void testFillKinds() {
    MockDebugIface m;   // DRAM byte i = i & 0xFF, frame_number 1234, 4 OP objects at $1000, 5 blits
    DebugViews v;
    const Id mm = v.set(DebugViews::kNoId, mem(0x1000, 16));
    ViewRequest dr;
    dr.kind = ViewRequest::Kind::Dram;
    const Id dram = v.set(DebugViews::kNoId, dr);
    ViewRequest rg;
    rg.kind = ViewRequest::Kind::Regs;
    rg.target = VJ_DBG_TARGET_GPU;
    const Id regs = v.set(DebugViews::kNoId, rg);
    ViewRequest di;
    di.kind = ViewRequest::Kind::Disasm;
    di.target = VJ_DBG_TARGET_GPU;
    di.addr = 0xF03006;   // the mock says a 6-byte "movei" when addr % 8 == 6
    di.rows = 3;
    const Id dis = v.set(DebugViews::kNoId, di);
    di.target = VJ_DBG_TARGET_M68K;
    const Id dis68 = v.set(DebugViews::kNoId, di);
    ViewRequest op;
    op.kind = ViewRequest::Kind::OpSnapshot;
    const Id ops = v.set(DebugViews::kNoId, op);
    ViewRequest bl;
    bl.kind = ViewRequest::Kind::BlitLog;
    const Id blit = v.set(DebugViews::kNoId, bl);
    CHECK(v.fill(m.iface(), 9) == 7, "all seven are filled");
    ViewBuffer b;
    CHECK(v.read(mm, b) && b.ok && b.serial == 9 && b.frame == 1234 && b.bytes.size() == 16 && b.bytes[0] == 0x00 &&
              b.bytes[15] == 0x0F, "Memory: the bus bytes at the frame the fill ran");
    CHECK(v.read(dram, b) && b.ok && b.bytes.size() == DebugSession::kDramSize && b.bytes[0x1234] == 0x34, "Dram: 2 MiB from 0");
    CHECK(v.read(regs, b) && b.ok && b.regs.risc_pc == 0xF03000, "Regs: GPU pc");
    CHECK(v.read(dis, b) && b.ok && b.lines.size() == 3 && b.lines[0].addr == 0xF03006 && b.lines[0].len == 6 &&
              b.lines[1].addr == 0xF0300C, "Disasm: a 6-byte MOVEI advances by 6");
    CHECK(v.read(dis68, b) && !b.ok && b.lines.empty(), "Disasm of the 68000 needs the injected DisasmFn (A5 installs it)");
    v.setDisasm([](const vj_debug_iface*, int t, unsigned a, unsigned n) {
        std::vector<DisasmLine> out;
        for (unsigned i = 0; i < n; i++) out.push_back({a + 2 * i, 2, t == VJ_DBG_TARGET_M68K ? "nop" : "fake", "4E71"});
        return out;
    });
    v.set(dis68, di);   // unchanged request: the buffer stays; a new serial refills it through the fn
    v.fill(m.iface(), 10);
    CHECK(v.read(dis68, b) && b.ok && b.lines.size() == 3 && b.lines[0].text == "nop" && b.lines[0].bytes == "4E71",
          "with a DisasmFn every target is served by it");
    v.setDisasm(nullptr);
    m.opReady = false;
    v.fill(m.iface(), 11);
    CHECK(v.read(ops, b) && b.ok && b.opCount == -1, "OpSnapshot: armed, not taken yet is -1");
    m.opReady = true;
    m.iface()->op_snapshot_arm();
    v.fill(m.iface(), 12);
    CHECK(v.read(ops, b) && b.opCount == 4 && b.objs.size() == 4 && b.objs[0].address == MockDebugIface::kOpListBase &&
              b.halfline == 41, "OpSnapshot: four objects, halfline 41");
    CHECK(v.read(blit, b) && b.ok && b.blitHead == 5 && b.blits.size() == 5 && b.blitLost == 0, "BlitLog since 0: the five launches");
    m.setBlitRingForTest(4);   // launch 0 has scrolled out
    v.fill(m.iface(), 13);
    CHECK(v.read(blit, b) && b.blits.size() == 4 && b.blits.front().seq == 1 && b.blitLost == 1, "BlitLog: lost launches are counted, never replayed");
    bl.since = 9;   // past the head: the log was switched off and on again
    v.set(blit, bl);
    v.fill(m.iface(), 13);
    CHECK(v.read(blit, b) && b.blits.empty() && b.blitHead == 5 && b.blitLost == 0, "BlitLog: since > head copies nothing and reports the head");
}

static void testSerialAndPending() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0, 4));
    CHECK(v.pending(1), "a new request is pending");
    CHECK(v.fill(m.iface(), 1) == 1 && !v.pending(1), "filled at serial 1");
    CHECK(v.fill(m.iface(), 1) == 0, "fill() runs once per serial");
    CHECK(v.pending(2) && v.fill(m.iface(), 2) == 1, "a new serial refills");
    ViewBuffer b;
    CHECK(v.read(a, b) && b.serial == 2, "the buffer carries the serial it was filled under");
    CHECK(v.fill(nullptr, 3) == 0 && v.pending(3), "a null iface fills nothing and stays pending");
}

static void testSetInvalidates() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0x10, 4));
    v.fill(m.iface(), 1);
    ViewBuffer b;
    v.set(a, mem(0x10, 4));
    CHECK(v.read(a, b) && !v.pending(1), "an unchanged set() keeps the filled buffer (windows re-state their request every frame)");
    v.set(a, mem(0x20, 4));
    CHECK(!v.read(a, b) && v.pending(1), "a changed set() clears the buffer and is pending at the SAME serial (a paused window that scrolls)");
    v.fill(m.iface(), 1);
    CHECK(v.read(a, b) && b.bytes[0] == 0x20, "and the refill carries the new range");
}

static void testFillHoldsNoLock() {
    DebugViews v;
    const vj_debug_iface hi = HookIface::make();
    const Id a = v.set(DebugViews::kNoId, mem(0, 4));
    HookIface::onPeek = [&] { v.set(a, mem(0x100, 4)); };   // would deadlock if the lock were held across the iface call
    v.fill(&hi, 1);
    HookIface::onPeek = nullptr;
    ViewBuffer b;
    CHECK(!v.read(a, b) && v.pending(1), "a set() made during the fill is not overwritten by the stale result");
    v.fill(&hi, 1);
    CHECK(v.read(a, b) && b.ok, "the next fill serves the new request");
}

static void testEpoch() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0, 4));
    v.fill(m.iface(), 1);
    ViewBuffer b;
    CHECK(v.read(a, b), "readable after the fill");
    const uint64_t e0 = v.epoch();
    v.bumpEpoch();
    CHECK(v.epoch() == e0 + 1 && !v.read(a, b), "a buffer filled before bumpEpoch() is not readable after it");
    CHECK(v.pending(1) && v.fill(m.iface(), 1) == 1 && v.read(a, b) && b.epoch == v.epoch(),
          "fill() at the same serial refills a stale-epoch buffer");
    const vj_debug_iface hi = HookIface::make();
    DebugViews w;
    const Id c = w.set(DebugViews::kNoId, mem(0, 4));
    HookIface::onPeek = [&] { w.bumpEpoch(); };
    w.fill(&hi, 1);
    HookIface::onPeek = nullptr;
    CHECK(!w.read(c, b), "an epoch bump between the table copy and the swap leaves the buffer unreadable");
}

// set() and read() on one thread, fill() on another: every buffer matches the request of its own id (never torn).
static void testConcurrent() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0x100, 64)), b = v.set(DebugViews::kNoId, mem(0x400, 64));
    const Id churn = v.set(DebugViews::kNoId, mem(0, 8));
    std::atomic<bool> stop{false};
    std::thread filler([&] {
        for (uint64_t s = 1; !stop.load(); s++) v.fill(m.iface(), s);
    });
    std::thread churner([&] {
        for (unsigned i = 0; !stop.load(); i++) v.set(churn, mem(0x1000 + (i & 7) * 16, 8 + (i & 3)));
    });
    int reads = 0, torn = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < end) {
        ViewBuffer x;
        if (v.read(a, x)) {
            reads++;
            if (x.bytes.size() != 64 || x.bytes[0] != 0x00 || x.bytes[63] != 0x3F) torn++;   // DRAM i & 0xFF: $100.. -> 00..3F
        }
        if (v.read(b, x)) {
            reads++;
            if (x.bytes.size() != 64 || x.bytes[0] != 0x00 || x.bytes[63] != 0x3F) torn++;
        }
    }
    stop = true;
    filler.join();
    churner.join();
    CHECK(reads > 0 && torn == 0, "reads are never torn (%d reads, %d torn)", reads, torn);
}

int main() {
    testSetClampsAndIds();
    testFillKinds();
    testSerialAndPending();
    testSetInvalidates();
    testFillHoldsNoLock();
    testEpoch();
    testConcurrent();
    std::printf(g_fail ? "debug_views_test: %d FAILED\n" : "debug_views_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

(`debug_session.h` is included only for the constant `DebugSession::kDramSize`; at this task it does not yet include `debug_views.h`, so there is no cycle.)

Register in `app/cmake/VJDebugTests.cmake` (the test has the `emu_thread` label so the ThreadSanitizer job runs the concurrent case):

```cmake
#   debug_views_unit     DebugViews (debug_views.*): the request table and double buffers the emulation thread fills
#                        and the windows read -- clamps, per-kind fills against the mock, serials, a changed request
#                        invalidating its buffer, the epoch across a content switch, and a concurrent set/fill/read.
add_executable(vj_debug_views_test tests/debug_views_test.cpp src/debug/debug_views.cpp)
target_include_directories(vj_debug_views_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
target_link_libraries(vj_debug_views_test PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(vj_debug_views_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_views_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_views_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_views_unit COMMAND vj_debug_views_test)
set_tests_properties(debug_views_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 2: Run to see it fail.**

Run: `env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4 --target vj_debug_views_test`
Expected: FAIL, `'debug_views.h' file not found` (and the `.cpp` missing).

- [ ] **Step 3: Implement** `debug_views.h` (the Interfaces declarations verbatim, including `ViewBuffer::req`, `DisasmLine::bytes`, `DisasmFn` and `setDisasm`, plus a header comment of the lock discipline: one mutex, copied out before any iface call, installed under the lock with the slot's `rev` checked; `set()`/`clear()` may be called from a posted lambda on the emulation thread) and `debug_views.cpp` per the semantics and table above. The installed `DisasmFn` is copied out under the lock with the request table and called with no lock held. Add `src/debug/debug_views.cpp` to `VJ_APP_SOURCES` after `src/debug/debug_session.cpp`. `debug_views.h` includes `<atomic> <cstdint> <map> <mutex> <string> <vector>` and `vj_debug_iface.h`.

- [ ] **Step 4: Run to see it pass.**

Run: `cmake --build build -j4 --target vj_debug_views_test virtualjaguar && ctest --test-dir build -R debug_views_unit --output-on-failure`
Expected: PASS, `debug_views_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add app/src/debug/debug_views.h app/src/debug/debug_views.cpp app/tests/debug_views_test.cpp app/cmake/VJDebugTests.cmake app/CMakeLists.txt
git commit -F - <<'EOF'
feat(app): DebugViews, per-window requests the emulation thread fills (#967)

A request table with a double buffer per request: the UI sets and reads, the
emulation thread (inline: the same thread) fills after each frame. A changed
request or a content switch invalidates its buffer; no lock is held across a
core call.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task A3: `DebugCore` wrappers, and `DebugSession` marshalling (inline unchanged)

**Files:**
- Create: `app/src/debug/debug_core.h`, `app/src/debug/debug_core.cpp`
- Modify: `app/src/debug/debug_session.h` (members :55-183), `app/src/debug/debug_session.cpp` (every iface use: :26-37, :82-106, :122-158 `pump`, :162-218 breakpoints, :222-235 `tick`, :239-275 memory helpers, :288-294 `regions`)
- Modify: `app/CMakeLists.txt` (add `src/debug/debug_core.cpp` after `debug_session.cpp` at :340; before `include(VJDebugCpu)` at :501 add `set(VJ_DEBUG_SESSION_SRCS src/debug/debug_session.cpp src/debug/debug_core.cpp src/debug/debug_views.cpp)`), `app/cmake/VJDebugTests.cmake` (the nine `src/debug/debug_session.cpp` entries at the targets `vj_debug_session_test`, `vj_debug_e2e`, `vj_debug_memory_test`, `vj_debug_memory_ui_test`, `vj_debug_scan_test`, `vj_debug_scan_e2e`, `vj_debug_scan_ui_smoke`, `vj_debug_op_test`, `vj_debug_op_e2e` become `${VJ_DEBUG_SESSION_SRCS}`), `app/cmake/VJDebugCpu.cmake` (:37 `vj_debug_cpu_test`, :50 `vj_debug_cpu_e2e`: same replacement)
- Test: `app/tests/debug_session_test.cpp`

**Interfaces:**
- Consumes: `DebugViews` (A2).
- Produces: `DebugCore` (iface wrappers, `ifaceGen()`, the transitional `pumpHook` / `markDetached()`; the halt is B1), and on `DebugSession`: `core()`, `views()`, `coreCall`, `corePost`, the public helpers `callCore(fn)` / `postCore(fn)` (the windows call these, never the hooks), `setHaltAvailable`, `haltAvailable`, `haltDisabledReason`, `toolAvailable` (halt-aware), `getRegsNow`, `setReg`, `buildCryTable`, `opSnapshotArm`, `blitLogEnable`, `texReplaceReload`, `fillViews`, `wakeViews`, `onCoreIface`, `syncCore`; `refreshSerial()` becomes an atomic read. Signatures as in the Interfaces section. `iface()` and `setThreadBlocked()` remain, unchanged, until A9. The windows' "same core" token is `views().epoch()`; `ifaceGen()` is `syncCore()`'s only.

Behaviour of the rewired `DebugSession` (inline, with `coreCall`/`corePost` empty, every existing assertion holds):
- **Threading rule.** UI-thread-only: `bps_`, `regions_`, `tools_`, `providers_`, `lastPokeError_`, `loaded_`, `hardcore_`, `haltAvailable_`, the halt fields (`halted_`, `queued_`, ... still inline-only until B1), `seenGen_`, `hadCore_`. Any-thread: `core_` (atomics inside), `views_`, `serial_` (becomes `std::atomic<uint64_t>`; `refreshSerial()` loads it, `requestRefresh()` and the bumps `fetch_add`).
- **`hasCore()`** = `core_.loaded()`; **`enabled()`** = `core_.loaded() && loaded_ && !hardcore_ && !threadBlocked_`; **`attached()`** = `core_.attached()`.
- **`coreCall(f)` / `corePost(f)`.** Public helpers `callCore(f)` / `postCore(f)` run `f` now when the member is empty (the windows in A4-A8 call them too). Every method below that touches the iface goes through one of them with **by-value captures except locals the caller waits for** (`coreCall` is synchronous; a result local is captured by reference).

| Method | Marshalling |
|---|---|
| `attach()` | `false` unless `enabled() && haltAvailable_`; if `core_.attached()` true; else `coreCall`: `core_.attach()`, and on success `core_.breakSet` for a by-value copy of every enabled `bps_` entry |
| `detach()` | `halted_`: `detachAndResume()` as today; else `postCore(core_.detach())`, `queued_ = NONE` |
| `breakNow(t)` | returns unless `enabled() && !halted_ && haltAvailable_` and `t` valid; `attach()`; `postCore(core_.haltNext(t))` |
| `addBreakpoint` | as today, but `callCore(core_.breakSet)`; `removeBreakpoint`, `setBreakpointEnabled`, `clearBreakpoints`: `postCore(core_.breakClear / breakSet)` when `core_.attached()`; the `bps_` list stays the UI source of truth. `toggleBreakpoint` inherits the gate through `addBreakpoint` |
| `read`, `readU`, `snapshotDram` | `callCore(core_.peek)`; same results and `out.clear()` rules as today |
| `poke`, `pokeU` | `ERR_STATE` unless `enabled()`; flags as today; `callCore(core_.poke)`; `lastPokeError_` and the serial bump as today |
| `getRegsNow`, `setReg`, `buildCryTable`, `opSnapshotArm`, `blitLogEnable`, `texReplaceReload` | one `callCore` each to the `core_` method of the same meaning |
| `regions()` | filled once per core by `callCore` looping `core_.region(i, r)` (<= 64); cleared in `syncCore()` |
| `fillViews()` | `views_.fill(core_.iface(), serial_.load())`, no marshalling (already on the emulation thread) |
| `wakeViews()` | `if (!core_.loaded() \|\| !loaded_) return;` (not gated on hardcore: the tools it closes hold no requests, and Neuro's watches, 3c, must keep filling) `if (!corePost) { fillViews(); return; } if (views_.pending(serial) && !wakePosted_.exchange(true)) corePost([this]{ wakePosted_ = false; fillViews(); });` |
| `onCoreIface(i)` | `core_.setIface(i); views_.bumpEpoch();` |
| `syncCore()` | if `core_.ifaceGen() == seenGen_` return; else `seenGen_ = ...; dropHalt(); regions_.clear(); bps_.clear(); serial_++;` and `if (hadCore_ && !core_.loaded()) closeAllTools("no core"); hadCore_ = core_.loaded();` |
| `setIface(i)` | `onCoreIface(i); syncCore();` |
| `setHaltAvailable(on)` | no-op if unchanged; stores; `if (!on) detach();` bumps the serial. Does **not** close the tools |
| `toolAvailable(t)` | `enabled() && t.toggle && (!t.needsHalt || (haltAvailable_ && halted_))` |
| `pump(t, pc)` | the existing body, with `core_.iface()->halt_reason` (size-checked as today) and `core_.markDetached()` where it set `attached_ = false` on DETACH. Installed as `core_.pumpHook` in the constructor |

- [ ] **Step 1: Write the failing tests.** In `app/tests/debug_session_test.cpp` add `#include "debug_core.h"` and, before `main`:

```cpp
static void testCoreWrappers() {
    MockDebugIface m;
    DebugCore c;
    uint8_t buf[4] = {};
    vj_debug_regs none;
    CHECK(c.peek(0, buf, 4) == VJ_DBG_ERR_STATE && !c.loaded() && !c.attach() && !c.getRegs(VJ_DBG_TARGET_M68K, none),
          "a core with no interface: every wrapper returns its failure value");
    c.setIface(m.iface());
    CHECK(c.loaded() && c.iface() == m.iface() && c.ifaceGen() == 1, "setIface() stores the pointer and bumps the generation");
    c.setIface(m.iface());
    CHECK(c.ifaceGen() == 1, "the same pointer again is not a change");
    CHECK(c.peek(0x10, buf, 4) == 4 && buf[0] == 0x10 && buf[3] == 0x13, "peek: the bus bytes");
    const uint8_t w[2] = {0xAA, 0xBB};
    CHECK(c.poke(0x7000, w, 2, 0) == 2 && c.poke(0x800000, w, 2, 0) == VJ_DBG_ERR_READONLY, "poke: the iface's own policy and result codes");
    vj_debug_regs r;
    CHECK(c.getRegs(VJ_DBG_TARGET_M68K, r) && r.d[3] == 0x1003 && c.setReg(VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_D0 + 3, 7) &&
              c.getRegs(VJ_DBG_TARGET_M68K, r) && r.d[3] == 7, "registers read and write back");
    std::string text;
    unsigned len = 0;
    CHECK(c.disasmRisc(VJ_DBG_TARGET_GPU, 0xF03006, text, len) && len == 6 && !text.empty() && !c.disasmRisc(VJ_DBG_TARGET_M68K, 0, text, len),
          "disasmRisc: RISC only");
    CHECK(c.attach() && c.attached() && m.attached() && c.attach(), "attach is idempotent");
    CHECK(c.breakSet(VJ_DBG_TARGET_M68K, 0x4000) && m.breakpoints.size() == 1 && c.breakClear(VJ_DBG_TARGET_M68K, 0x4000), "break_set / break_clear");
    c.detach();
    CHECK(!c.attached() && !m.attached(), "detach() clears the core's side and ours");
    std::vector<vj_debug_op_obj> objs;
    unsigned hl = 0;
    m.opReady = false;
    CHECK(c.opSnapshotGet(objs, hl) == -1, "op snapshot: -1 while armed and not taken");
    m.opReady = true;
    c.opSnapshotArm();
    CHECK(c.opSnapshotGet(objs, hl) == 4 && objs.size() == 4 && hl == 41, "op snapshot: four objects");
    std::vector<uint32_t> cry;
    CHECK(c.buildCryTable(cry) && cry.size() == 65536 && cry[0xFFFF] == 0xFFFFFFu && c.cryToRgb(0xFFFF) == 0xFFFFFFu, "the CRY table");
    c.blitLogEnable(true);
    vj_debug_blit b;
    CHECK(m.blitLogOn && c.blitLogHead() == 5 && c.blitLogGet(4, b) && b.seq == 4 && !c.blitLogGet(5, b), "blit log");
    CHECK(c.texReplaceReload() == 1 && m.texReloads == 1 && c.frameNumber() == 1234, "reload and frame number");
    vj_debug_iface old = *m.iface();
    old.texreplace_reload = nullptr;
    DebugCore d;
    d.setIface(&old);
    CHECK(d.texReplaceReload() == -1, "texReplaceReload: -1 when the core does not offer it");
    vj_debug_region reg;
    CHECK(c.region(0, reg) && reg.name, "region");
    c.setIface(nullptr);
    CHECK(!c.loaded() && c.ifaceGen() == 2, "setIface(nullptr) drops the interface");
}

static void testMarshalling() {
    DebugSession s;
    MockDebugIface m;
    int calls = 0, posts = 0;
    s.coreCall = [&](std::function<void()> f) { calls++; f(); };
    s.corePost = [&](std::function<void()> f) { posts++; f(); };
    s.setIface(m.iface());
    s.setContentLoaded(true);
    s.registerPlaceholderTools();
    auto delta = [&](int dc, int dp, const char* what) {
        CHECK(calls == dc && posts == dp, "%s: %d coreCall / %d corePost (wanted %d / %d)", what, calls, posts, dc, dp);
        calls = posts = 0;
    };
    std::vector<uint8_t> out;
    unsigned u = 0;
    vj_debug_regs r;
    std::vector<uint32_t> cry;
    CHECK(s.read(0x10, out, 4) && out[0] == 0x10);       delta(1, 0, "read");
    CHECK(s.readU(0x10, 2, u) && u == 0x1011);           delta(1, 0, "readU");
    CHECK(s.pokeU(0x7000, 4, 0xCAFEF00D) == 4);          delta(1, 0, "poke");
    CHECK(s.getRegsNow(VJ_DBG_TARGET_M68K, r));          delta(1, 0, "getRegsNow");
    CHECK(s.setReg(VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_D0, 5)); delta(1, 0, "setReg");
    CHECK(s.buildCryTable(cry));                         delta(1, 0, "buildCryTable");
    s.opSnapshotArm();                                   delta(1, 0, "opSnapshotArm");
    s.blitLogEnable(true);                               delta(1, 0, "blitLogEnable");
    CHECK(s.texReplaceReload() == 1);                    delta(1, 0, "texReplaceReload");
    CHECK(!s.regions().empty());                         delta(1, 0, "regions (once per core)");
    s.regions();                                         delta(0, 0, "regions again: cached");
    CHECK(s.attach());                                   delta(1, 0, "attach");
    s.breakNow();                                        delta(0, 1, "breakNow when attached: only the halt_next post");
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));  delta(1, 0, "addBreakpoint");
    s.removeBreakpoint(VJ_DBG_TARGET_M68K, 0x4000);      delta(0, 1, "removeBreakpoint");
    s.detach();                                          delta(0, 1, "detach");
    // What the windows use every frame costs the core nothing.
    s.tick(1.0, true);
    s.views().set(DebugViews::kNoId, ViewRequest());
    (void)s.refreshSerial(); (void)s.breakpoints(); (void)s.toolAvailable(*s.findTool("memory")); (void)s.enabled(); (void)s.attached();
    delta(0, 0, "tick / views / serial / enabled / attached: no core call");
}

static void testHaltAvailable() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    s.registerPlaceholderTools();
    DebugTool memory = *s.findTool("memory");
    memory.toggle = [] {};
    s.registerTool(memory);
    DebugTool step;
    step.id = "step";
    step.toggle = [] {};
    step.needsHalt = true;
    CHECK(s.attach() && s.attached() && s.haltAvailable() && s.haltDisabledReason().empty(), "available by default");
    s.setHaltAvailable(false);
    CHECK(!s.attached() && !m.attached(), "turning it off detaches");
    CHECK(s.enabled() && s.toolAvailable(*s.findTool("memory")) && !s.toolAvailable(step),
          "the windows stay live, a tool that needs a halt does not");
    CHECK(!s.attach() && !m.attached(), "attach() refuses");
    s.breakNow();
    CHECK(!s.attached() && !m.attached(), "breakNow() refuses");
    CHECK(!s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000) && !s.toggleBreakpoint(VJ_DBG_TARGET_M68K, 0x4000) && s.breakpoints().empty(),
          "addBreakpoint() and toggleBreakpoint() refuse");
    CHECK(!s.haltDisabledReason().empty(), "with a reason");
    std::vector<uint8_t> out;
    CHECK(s.read(0x10, out, 4) && s.pokeU(0x7000, 4, 1) == 4, "memory reads and writes still work");
    s.setHaltAvailable(true);
    CHECK(s.attach() && s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000), "and come back");
}

static void testCoreSyncSplit() {
    // What the emulation thread does (onCoreIface) must not touch UI state; the UI adopts it in syncCore().
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    int closed = 0;
    s.onForcedClose.push_back([&](const char*) { closed++; });
    const uint64_t epoch = s.views().epoch();
    s.onCoreIface(nullptr);
    CHECK(!s.hasCore() && !s.enabled() && !s.attached(), "the atomics already say 'no core'");
    CHECK(s.breakpoints().size() == 1 && closed == 0, "...but the UI-side list and callbacks are untouched until syncCore()");
    CHECK(s.views().epoch() == epoch + 1, "the views' epoch moved on the emulation side");
    s.syncCore();
    CHECK(s.breakpoints().empty() && closed == 1, "syncCore() forgets the breakpoints and closes the tools once");
    s.syncCore();
    CHECK(closed == 1, "and is idempotent");
    s.setIface(m.iface());   // the same static interface again, as a reload hands over
    CHECK(s.enabled() && s.attach() && m.attached(), "a new core attaches");
}

static void testViewsFill() {
    DebugSession s;
    MockDebugIface m;
    std::vector<std::function<void()>> posted;
    s.corePost = [&](std::function<void()> f) { posted.push_back(std::move(f)); };
    s.setIface(m.iface());
    s.setContentLoaded(true);
    ViewRequest r;
    r.kind = ViewRequest::Kind::Memory;
    r.addr = 0x1000;
    r.len = 8;
    const DebugViews::Id id = s.views().set(DebugViews::kNoId, r);
    s.wakeViews();
    s.wakeViews();
    CHECK(posted.size() == 1, "wakeViews() posts one fill at a time (%zu)", posted.size());
    posted[0]();
    ViewBuffer b;
    CHECK(s.views().read(id, b) && b.bytes[0] == 0x00 && b.serial == s.refreshSerial(), "the posted fill ran");
    s.wakeViews();
    CHECK(posted.size() == 1, "nothing pending: no post");
    s.requestRefresh();
    s.wakeViews();
    CHECK(posted.size() == 2, "a new serial posts again");
    posted[1]();
    DebugSession t;   // inline (no corePost): wakeViews() fills right away
    t.setIface(m.iface());
    t.setContentLoaded(true);
    const DebugViews::Id id2 = t.views().set(DebugViews::kNoId, r);
    t.wakeViews();
    CHECK(t.views().read(id2, b), "inline: filled on the spot");
}
```

Call the five new tests from `main` before `testIfaceSwap();`. (`testThreadBlocked` stays until A9.) The mock is a process-wide singleton: every new test uses the one `MockDebugIface m` of its own function and never two at once.

- [ ] **Step 2: Run to see it fail.**

Run: `cmake --build build -j4 --target vj_debug_session_test`
Expected: FAIL, `'debug_core.h' file not found`.

- [ ] **Step 3: Implement.**
  - `debug_core.h/.cpp`: the frozen `DebugCore` **minus** the halt section (`halt()`, `haltSerial()`, `verdict`, `notifyVerdict`, `waitVerdict`, `runUiFrame`, `abortHalt`, `onHalt`, `onResume`, `kPumpWait` are B1), **plus** the change-request members `ifaceGen()`, `pumpHook`, `markDetached()`. Members: `std::atomic<const vj_debug_iface*> iface_`, `std::atomic<bool> attached_`, `std::atomic<uint64_t> gen_`, `vj_debug_host host_` (size, `user = this`, `halt_pump = &haltPumpThunk`). `pump(t, pc)` returns `pumpHook ? pumpHook(t, pc) : VJ_DBG_RESUME_DETACH`. Wrapper rules: each checks the iface and the function pointer it needs and returns the failure value of the frozen comment (`peek`/`poke`: `VJ_DBG_ERR_STATE`; bools: false; `opSnapshotGet`: 0; `texReplaceReload`: -1; counts: 0). `setIface(i)`: no-op when equal; else `attached_ = false`, store, `gen_++`. `attach()`: false without iface/`attach` pointer; true if already attached; else `iface->attach(&host_)` and set the flag. `detach()`: call `iface->detach()` only when attached; clear. `opSnapshotGet`: a `VJ_DBG_OP_MAX_OBJECTS` buffer on the heap, `out.assign(buf, buf + n)` for `n > 0`, else `out.clear()`; returns the iface's `n`. `buildCryTable`: 65536 calls of `cry_to_rgb`. No ImGui, no SDL.
  - `debug_session.h/.cpp`: as in the table above. Keep `halted_`, `queued_`, `runUiFrame`, `abortHalt`, `onHalt`, `onResume`, `resumeQueued`, `setThreadBlocked`, `threadBlocked`, `iface()` (now `core_.iface()`), `disabledReason()` unchanged. Update the header comment: the threading paragraph now lists which members are UI-only and which any-thread (the rule above).
  - CMake: `VJ_DEBUG_SESSION_SRCS` and the eleven replacements listed under **Files**.

- [ ] **Step 4: Run to see it pass, and the whole debug suite.**

Run: `cmake --build build -j4 && ctest --test-dir build -R "debug_|smoke_yarc|emu_thread_limits" --output-on-failure`
Expected: all PASS (inline behaviour unchanged: `debug_session_unit`, `debug_e2e`, `debug_memory_unit`, `debug_memory_ui_smoke`, `debug_scan_unit`, `debug_scan_e2e`, `debug_scan_ui_smoke`, `debug_op_unit`, `debug_op_e2e`, `debug_cpu_unit`, `debug_cpu_e2e`, `debug_texture_unit`, `debug_blitter_unit`, `debug_views_unit`). If a debug test is skipped for a missing ROM, the ROM path is `${VJ_ROMS}`; do not edit the tests to make it pass.

- [ ] **Step 5: Commit**

```bash
git add app/src/debug/debug_core.h app/src/debug/debug_core.cpp app/src/debug/debug_session.h app/src/debug/debug_session.cpp app/tests/debug_session_test.cpp app/CMakeLists.txt app/cmake/VJDebugTests.cmake app/cmake/VJDebugCpu.cmake
git commit -F - <<'EOF'
feat(app): DebugCore owns the debug interface; DebugSession marshals to it (#967)

DebugCore wraps every vj_debug_iface entry point (emulation thread only);
DebugSession routes each action through coreCall/corePost, which run inline
until App wires them to the debug lane. setHaltAvailable gates attach, Break
and breakpoints so a threaded run can keep the windows and refuse the halt.
Inline behaviour is unchanged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task A4: Memory viewer and Palette read published buffers

The first window task also lands what A5-A8 reuse: the counting test fixture, the static "no direct iface" guard, and the rules every window follows from here on.

**Window rules (A4-A8).**
- A window draws only from `DebugSession::views().read()` buffers, the session's UI-side state, and its own state. It never touches `vj_debug_iface` and never calls `read()`/`getRegsNow()` from a path that runs every frame.
- Every draw, after its `views().set()` calls, calls `s.wakeViews()` once. Inline that runs the copier at once (so the window sees its data on the next `read()`, as it saw the synchronous peek before); threaded it is a debug-lane post that does nothing when no buffer is stale.
- One-shot user actions (poke, edit, set_reg, breakpoints, reload) go through `s.callCore(fn)` (waits) and fire-and-forget work through `s.postCore(fn)`. Never call `s.coreCall` directly: it is empty in tests.
- A window that is closed, or whose `s.enabled()` is false, clears its view ids (`views().clear`) so closed windows cost nothing.
- "Same core" is `s.views().epoch()`, not an iface pointer. A content switch therefore resets a window's selection; that is deliberate (a buffer must never show the previous game).

The names these tasks use are the Interfaces section's: `wakeViews()` (one posted fill while anything is pending; inline it fills now), `ViewBuffer::req`, `DisasmLine::bytes` and `DebugViews::setDisasm` (the CPU tool injects its disassembler, A5; without one a 68000 `Disasm` request answers `ok = false`), `DebugSession::callCore` / `postCore` (public helpers over the `coreCall` / `corePost` hooks), the session's `onHalt` / `onResume` (still on the session through 3a; B1 moves them to `DebugCore`, B2 adds `onHaltUi`), and `${VJ_DEBUG_SESSION_SRCS}` (A3 defines it in `app/CMakeLists.txt` before `include(VJDebugCpu)`; every test target that listed `src/debug/debug_session.cpp` lists it instead).

**Files:**
- Create: `app/tests/debug_ui_fixture.h`, `app/cmake/CheckNoDirectIface.cmake`
- Modify: `app/src/debug/memory_view.h` (`CryCodec` 175-189, `SearchJob` 99-125), `app/src/debug/memory_view.cpp` (`SearchJob::step` 263-297, `CryCodec` 446-457), `app/src/debug/memory_ui.h` (test hook), `app/src/debug/memory_ui.cpp` (`MemState` 25-57, `fetchWindow` 86-104, `syncCore` 107-120, `drawGrid` 384-394 and 435-454, `drawFooter` 503-545, `drawMemoryWindow` 560-591, `registerMemoryTools` 611-616), `app/src/debug/clut_ui.cpp` (`ClutState` 23-42, `refresh` 49-63, `drawClutWindow` 88-107), `app/tests/debug_memory_ui_test.cpp`, `app/tests/debug_memory_test.cpp`, `app/cmake/VJDebugTests.cmake`

**Interfaces:**
- Consumes: `DebugViews::{set, clear, read, epoch, bumpEpoch}`, `ViewBuffer::{ok, serial, req, bytes}`, `DebugSession::{views, wakeViews, callCore, postCore, buildCryTable, read, readU, poke, regions}`.
- Produces:

```cpp
// memory_view.h
class CryCodec {
public:
    bool build(const vj_debug_iface* iface);   // unchanged (emulation-thread side, and debug_memory_unit)
    // The table through DebugSession::buildCryTable (one callCore), once per s.views().epoch(); a refusal is
    // remembered for the epoch, so a core without cry_to_rgb does not retry every frame.
    bool buildFrom(DebugSession& s);
private:
    uint64_t epoch_ = ~0ull;  bool refused_ = false;   // beside the existing built_, table_
};
// SearchJob::step(budget): the whole budget loop runs inside ONE s_.callCore (nested read()s run inline on the
// emulation thread), not one wait per 64 KiB chunk.
// memory_ui.h
namespace memory_test { bool byteAt(unsigned addr, uint8_t& v); }   // a byte of the window the grid last adopted
```

- [ ] **Step 1: Test infrastructure shared by A4-A8.**

`app/tests/debug_ui_fixture.h` (header-only, `namespace vj`):

```cpp
class CountingIface {   // a copy of an iface whose every entry point forwards and counts; one instance at a time
public:
    enum Fn { Region, Peek, Poke, GetRegs, SetReg, DisasmRisc, Attach, Detach, BreakSet, BreakClear, HaltNext,
              HaltedTarget, HaltReason, OpArm, OpGet, CryToRgb, BlitEnable, BlitHead, BlitGet, TexReload, FrameNo, kFns };
    explicit CountingIface(const vj_debug_iface* inner);   // one captureless thunk per vj_debug_iface function pointer
    const vj_debug_iface* iface() const;
    static unsigned count(Fn f);
    static unsigned total();
    static unsigned offCore();       // calls made while no OnCore guard was alive on the calling thread
    static void reset();             // zero every counter (they are static)
    struct OnCore { OnCore(); ~OnCore(); };   // thread_local depth: "this is the emulation thread"
};
struct UiFixture {
    MockDebugIface mock;
    CountingIface counted{mock.iface()};
    DebugSession s;
    unsigned coreCalls = 0, corePosts = 0;   // outermost only: a call inside a call is the emulation thread's own
    UiFixture();     // s.coreCall / s.corePost = lambdas that count (depth 0 only), hold an OnCore guard and run fn;
                     // s.setIface(counted.iface()); s.setContentLoaded(true)
    void fill();     // OnCore guard + s.views().fill(s.core().iface(), s.refreshSerial()): what App::fillDebugViews does
    struct Mark { unsigned total, coreCalls, corePosts; };
    Mark mark() const;                     // CountingIface::total(), coreCalls, corePosts
    bool quietSince(const Mark& m) const;  // all three unchanged
};
```

`app/cmake/CheckNoDirectIface.cmake`: `file(READ)` each name in `FILES` under `DIR`, strip `//...` comments, and `message(SEND_ERROR "<file>: direct core access <match>")` for any match of `iface\(\)|->(region|peek|poke|get_regs|set_reg|disasm_risc|attach|detach|break_set|break_clear|halt_next|halted_target|op_snapshot_arm|op_snapshot_get|cry_to_rgb|blit_log_enable|blit_log_head|blit_log_get|texreplace_reload|frame_number|halt_reason)\(|memoryRegion` (CMake regexes have no `\b`: anchor on the call's `(`). `op_ui.cpp` legitimately asks `app.host.inFrame()` for its inline step gate, so `inFrame` is not in the pattern.

At the END of `app/cmake/VJDebugTests.cmake`:

```cmake
# #967 3a: the tool windows never touch vj_debug_iface; every window task adds its files to this list.
set(VJ_NO_IFACE_FILES memory_ui.cpp clut_ui.cpp)
add_test(NAME debug_no_direct_iface COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/src/debug
         "-DFILES=${VJ_NO_IFACE_FILES}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoDirectIface.cmake)
set_tests_properties(debug_no_direct_iface PROPERTIES LABELS emu_thread)
```

- [ ] **Step 2: Write the failing tests.**

`app/tests/debug_memory_test.cpp` (include `debug_ui_fixture.h`; call both from `main` after `testClutReadBack()`):

```cpp
static void testCryBuildFrom() {
    UiFixture f;
    CryCodec c;
    CHECK(c.buildFrom(f.s) && c.ready());
    CHECK(CountingIface::count(CountingIface::CryToRgb) == 0x10000);
    CHECK(c.decode(0xABCD) == (f.mock.iface()->cry_to_rgb(0xABCD) & 0xFFFFFF));
    CHECK(c.buildFrom(f.s));                                  // same epoch: no rebuild
    CHECK(CountingIface::count(CountingIface::CryToRgb) == 0x10000);
    f.s.views().bumpEpoch();                                  // a new core / content
    CHECK(c.buildFrom(f.s) && CountingIface::count(CountingIface::CryToRgb) == 2 * 0x10000);
    vj_debug_iface noCry = *f.mock.iface();                   // a core without cry_to_rgb
    noCry.cry_to_rgb = nullptr;
    f.s.setIface(&noCry);                                     // (epoch bump, R3)
    CryCodec none;
    CHECK(!none.buildFrom(f.s) && !none.ready());
    const unsigned k = f.coreCalls;
    CHECK(!none.buildFrom(f.s) && f.coreCalls == k);          // the refusal is remembered for the epoch
}
static void testSearchIsOneCoreCall() {
    UiFixture f;
    const std::vector<uint8_t> needle = {0xFA, 0xCE, 0xFE, 0xED};
    for (size_t i = 0; i < needle.size(); i++) f.mock.dram()[0x180000 + i] = needle[i];
    const vj_debug_region dram = f.s.regions()[0];
    SearchJob j(f.s, dram, 0, needle);
    const unsigned k = f.coreCalls;
    CHECK(j.step(4u << 20) && j.found() && j.address() == 0x180000);
    CHECK(f.coreCalls == k + 1);          // 4 MiB of 64 KiB chunks, one wait
}
```

In the `CryCodec` refusal case copy the mock's table into a `vj_debug_iface` whose `cry_to_rgb` is null and hand it to `bare.s.setIface()`; the `SearchJob` rig's region 0 is DRAM as in `testSearch`. Call `CountingIface::reset()` at the top of `testCryBuildFrom` (the counters are static). `UiFixture` sets `CountingIface`'s own table as the session's iface, so the section above compares against the raw mock only for values, never for counts.

`app/tests/debug_memory_ui_test.cpp`: replace `MockDebugIface mock; DebugSession s; s.setIface(...); s.setContentLoaded(true);` with `UiFixture fx; DebugSession& s = fx.s; MockDebugIface& mock = fx.mock;`, add a file-static `UiFixture* g_fx = &fx;` and make `frame()` call `g_fx->fill()` after `acknowledgeTextures()`. Sections 1-7 stay as they are. Insert the new sections between section 6 and section 7 (section 7 detaches the core and closes both windows):

```cpp
    // 8. #967 3a: the draw path never reaches the core.  After a settle, idle frames make no coreCall, no corePost,
    //    and the counting iface saw no call outside the emulation side at all.
    CountingIface::reset();
    memoryWindowOpen() = true;
    clutWindowOpen() = true;
    showMemoryAt(s, 0x1000);
    settle(s, 6);
    const UiFixture::Mark quiet = fx.mark();
    for (int i = 0; i < 30; i++) frame(s);
    CHECK(fx.quietSince(quiet));
    CHECK(CountingIface::offCore() == 0);

    // 9. The bytes on screen are the published ones: change RAM behind the interface; nothing shows until a refresh.
    mock.dram()[0x1002] = 0x5A;
    uint8_t v = 0;
    CHECK(memory_test::byteAt(0x1002, v) && v != 0x5A);
    s.requestRefresh();
    settle(s, 2);
    CHECK(memory_test::byteAt(0x1002, v) && v == 0x5A);
    CHECK(fx.mark().total > quiet.total);

    // 10. Scrolling far away asks for a new window, and keeps drawing the old rows until it arrives.
    showMemoryAt(s, 0x100005);
    settle(s, 8);
    CHECK(memory_test::byteAt(0x100005, v) && v == 5);
    CHECK(CountingIface::offCore() == 0);

    // 11. The CLUT bytes arrive through one request (both tables), and idle Palette frames stay quiet.
    mock.regwin()[0x400] = 0x12;
    s.requestRefresh();
    settle(s, 3);
    const UiFixture::Mark q2 = fx.mark();
    settle(s, 20);
    CHECK(fx.quietSince(q2));
```

Section 8's `offCore() == 0` covers sections 1-6 as well when `CountingIface::reset()` is moved to the top of `main` (do that): the pokes of sections 1-5 go through `callCore`, so they are on-core.

- [ ] **Step 3: Run, expect FAIL.**

```bash
env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4 --target vj_debug_memory_test vj_debug_memory_ui_test
```

Expected: compile errors (`buildFrom`, `memory_test::byteAt`, `UiFixture` members) and, once those exist, `debug_memory_ui_smoke` failing sections 8-10 (the window still peeks from the draw path). Run `ctest --test-dir build -R debug_no_direct_iface` on the still-unmodified `memory_ui.cpp` / `clut_ui.cpp` and see it FAIL naming `iface()` and `->...(`: that is the only proof the guard can fail.

- [ ] **Step 4: Implement.**

`memory_view.cpp`: `CryCodec::buildFrom` as specified. `SearchJob::step(budget)`: move the current body into a private `stepOnCore(budget)` and make `step` run it inside `s_.callCore`. `pollSearch` keeps calling `m.job->step(4u << 20)`.

`memory_ui.cpp` window state (`MemState`): drop `ifaceSeen`; add `epochSeen = ~0ull`, `DebugViews::Id winId, selId = DebugViews::kNoId`, `unsigned reqStart = 0, reqLen = 0, selReq = ~0u`. Add `void releaseViews(DebugSession&)` (clear both ids, `reqLen = 0`, `selReq = ~0u`).
- `syncCore`: compare `m.epochSeen` with `s.views().epoch()`; the reset body is unchanged plus `winSerial = 0`.
- `fetchWindow(s, r, visStart, visLen)` replaces the synchronous read. Wanted span = the visible rows plus `kMarginRows = 16` rows each side, clamped by `clampToRegion` and to `DebugViews::kMaxMemoryBytes`. Call `s.views().set(m.winId, {Memory, spanStart, spanLen})` only when `winId == kNoId`, the visible rows leave `[reqStart, reqStart+reqLen)`, or `perRow` changed. Then `ViewBuffer b; if (s.views().read(m.winId, b) && b.req.addr == m.reqStart)`: when `b.ok` and (`b.serial != m.winSerial` or `b.req.addr != m.winStart`) adopt it (`m.win = std::move(b.bytes)`, `m.winStart = b.req.addr`, `m.track.update(m.winStart, data, size, /*refresh=*/b.serial != m.winSerial)`, `winOk = true`, `winSerial = b.serial`); when `!b.ok` set `winOk = false`. A request not yet answered leaves the previous window drawn (cells outside it show `?`).
- `drawFooter`: replace the per-frame `s.read(m.sel, b, 4)` with a second Memory request `{sel, 4}` (`selId`, re-set when `m.sel` changes). Draw the cell line from the buffer when `req.addr == m.sel && ok`; "reading..." until answered; the existing "unreadable" text when answered with `!ok`. `beginEdit` and Follow pointer keep their one-shot `s.read`/`s.readU` (user actions).
- `drawMemoryWindow`: when `!m.open` or `!s.enabled()` call `releaseViews(s)` before returning; call `s.wakeViews()` after the grid and footer have set their requests. `registerMemoryTools`' forced-close lambda captures `&s` and calls `releaseViews(s)`.
- `memory_test::byteAt`: true when `winOk` and `addr` lies in `[winStart, winStart + win.size())`.

`clut_ui.cpp`: one Memory request `{kClutA, 2 * kClutEntries * 2}` (table B is table A + 0x200: `static_assert(kClutB == kClutA + kClutEntries * 2)`), set on first draw and cleared like the memory ids (closed / disabled / forced close). `refresh` adopts the buffer when `b.serial != c.serial`: `c.ok = b.ok && b.bytes.size() == 0x400`, A from bytes `[0, 0x200)`, B from `[0x200, 0x400)`. `ifaceSeen` becomes `epochSeen` on `views().epoch()`. `c.cry.buildFrom(s)` replaces `c.cry.build(s.iface())`. Call `s.wakeViews()` after the request is set.

- [ ] **Step 5: Run, expect PASS.**

```bash
cmake --build build -j4 --target vj_debug_memory_test vj_debug_memory_ui_test virtualjaguar && ctest --test-dir build -R 'debug_memory|debug_no_direct_iface|debug_session_unit' --output-on-failure
```

Expected: `debug_memory_unit`, `debug_memory_ui_smoke`, `debug_no_direct_iface`, `debug_session_unit` pass (the old checks of `debug_memory_unit` for `CryCodec::build(iface)`, `SearchJob`, `readU` are untouched).

- [ ] **Step 6: Commit.**

```bash
git add app/tests/debug_ui_fixture.h app/cmake/CheckNoDirectIface.cmake app/cmake/VJDebugTests.cmake app/src/debug/memory_view.h app/src/debug/memory_view.cpp app/src/debug/memory_ui.h app/src/debug/memory_ui.cpp app/src/debug/clut_ui.cpp app/tests/debug_memory_test.cpp app/tests/debug_memory_ui_test.cpp
git commit -m "feat(app): Memory viewer and Palette read published view buffers (#967)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task A5: CPU debugger on Regs and Disasm requests

**Files:**
- Create: `app/src/debug/core_job.h`, `app/tests/debug_core_job_test.cpp`, `app/tests/debug_cpu_ui_smoke.cpp`
- Modify: `app/src/debug/cpu_view.h` (after `parseHex` 116; `TempBreak`/`CpuControl` 118-170), `app/src/debug/cpu_view.cpp` (`TempBreak` 338-358, `CpuControl::stepIn` 400-418, `stepOver` 420-444), `app/src/debug/cpu_ui.h`, `app/src/debug/cpu_ui.cpp` (`TabState` 25-51, `refreshRegs` 74-84, `setReg` 104-113, `drawDisasm` 280-330, `drawBanner` 455-483, `drawToolbar` 485-512, `drawWindow` 545-617, `registerCpuTools` 631-669), `app/tests/debug_cpu_test.cpp`, `app/cmake/VJDebugTests.cmake`, `app/cmake/VJDebugCpu.cmake`

**Interfaces:**
- Consumes: `ViewRequest::Kind::{Regs, Disasm}`, `ViewBuffer::{regs, lines, req, ok, serial}`, `DebugViews::setDisasm` (R4), `DebugSession::{views, wakeViews, callCore, postCore, core, setReg, haltAvailable, requestRefresh}`, `DebugCore::{breakSet, breakClear, haltNext, getRegs, iface}`.
- Produces:

```cpp
// core_job.h (header-only; no ImGui): work for the emulation thread whose result the UI picks up later.
template <class T> class CoreJob {
public:
    // UI thread.  Posts fn(session.core().iface()) through postCore; inline it has run before submit() returns.
    void submit(DebugSession& s, std::function<T(const vj_debug_iface*)> fn);
    bool take(T& out);        // UI thread: true once per delivered result; newest wins; a result is never given twice
    bool pending() const;     // submitted, not yet delivered
};
// cpu_view.h
std::vector<DisasmLine> disasmRows(const vj_debug_iface* iface, int target, unsigned addr, unsigned rows);   // = disasmForward mapped to DisasmLine (len 0 for an invalid line)
class CpuFeed {   // what cpu_ui draws from: published buffers only
public:
    void want(DebugSession& s, int target, unsigned top, unsigned rows);   // views().set for Regs{target} and Disasm{target, top, rows}, only when changed
    bool update(DebugSession& s);                  // adopts the newest buffers; true when regs or rows changed
    void release(DebugSession& s);
    bool answered() const;                         // some Regs buffer has been adopted (ok or not)
    bool haveRegs() const;  int regsTarget() const;  const vj_debug_regs& regs() const;
    bool rowsFor(int target, unsigned top, unsigned rows) const;   // the adopted rows answer exactly this request
    const std::vector<DisasmLine>& rows() const;
};
class DisasmNav {  // the synchronous walks (backStart / prevAddr) moved onto the emulation thread
public:
    void back(DebugSession& s, int target, unsigned pc, int lines);    // result: backStart(iface, target, pc, lines)
    void prev(DebugSession& s, int target, unsigned from, int count);  // result: prevAddr applied `count` times
    bool take(unsigned& addr);
    bool pending() const;
};
// cpu_ui.h
namespace cpu_test { bool haveRegs(); size_t rowCount(); }
```

- [ ] **Step 1: Write the failing tests.**

`app/tests/debug_core_job_test.cpp` (new executable, `LABELS emu_thread`, links `${VJ_DEBUG_SESSION_SRCS}`, `Threads::Threads`):

```cpp
// A deferred corePost: the job has not run until the "emulation thread" drains the queue.
std::vector<std::function<void()>> q;
s.corePost = [&](std::function<void()> fn) { q.push_back(std::move(fn)); };
CoreJob<int> job;
job.submit(s, [](const vj_debug_iface*) { return 7; });
int out = 0;
CHECK(job.pending() && !job.take(out) && q.size() == 1);
std::thread emu([&] { for (auto& f : q) f(); });   // runs on another thread
emu.join();
CHECK(!job.pending() && job.take(out) && out == 7 && !job.take(out));
job.submit(s, [](const vj_debug_iface*) { return 1; });
job.submit(s, [](const vj_debug_iface*) { return 2; });
for (size_t i = 1; i < q.size(); i++) q[i]();     // q[0] is the already-run job; the two new ones run in order
CHECK(job.take(out) && out == 2);                    // newest wins
s.corePost = nullptr;                                // inline: runs inside submit()
job.submit(s, [](const vj_debug_iface* i) { return i == nullptr ? 9 : 8; });
CHECK(job.take(out) && out == 9);                    // no core attached: iface is null
```

`app/tests/debug_cpu_test.cpp` (add, call from `main`; include `debug_ui_fixture.h`; link the test with `${VJ_DEBUG_SESSION_SRCS}`):

```cpp
static void testCpuFeedReadsBuffersOnly() {
    UiFixture f;
    f.s.views().setDisasm(&disasmRows);
    CpuFeed feed;
    feed.want(f.s, VJ_DBG_TARGET_M68K, 0x1000, 8);
    f.s.wakeViews();                                     // inline: the copier runs now
    const UiFixture::Mark m = f.mark();
    CHECK(feed.update(f.s) && feed.haveRegs() && feed.regsTarget() == VJ_DBG_TARGET_M68K);
    CHECK(feed.regs().pc == 0x4000 && feed.regs().d[3] == 0x1003);
    CHECK(feed.rowsFor(VJ_DBG_TARGET_M68K, 0x1000, 8) && feed.rows().size() == 8);
    CHECK(feed.rows()[0].addr == 0x1000 && !feed.rows()[0].bytes.empty());
    for (size_t i = 1; i < 8; i++) CHECK(feed.rows()[i].addr == feed.rows()[i - 1].addr + feed.rows()[i - 1].len);
    CHECK(!feed.update(f.s) && !feed.update(f.s));         // nothing new
    CHECK(f.quietSince(m));                                // update() never reaches the core
    feed.want(f.s, VJ_DBG_TARGET_GPU, 0xF03000, 4);        // a tab switch: the old answer is not the new one
    CHECK(!feed.rowsFor(VJ_DBG_TARGET_GPU, 0xF03000, 4) && feed.regsTarget() == VJ_DBG_TARGET_M68K);
    f.s.wakeViews();
    CHECK(feed.update(f.s) && feed.regsTarget() == VJ_DBG_TARGET_GPU && feed.regs().risc_pc == 0xF03000);
    CHECK(feed.rowsFor(VJ_DBG_TARGET_GPU, 0xF03000, 4) && feed.rows()[0].text.find("fake") == 0);
}
static void testDisasmNav() {
    UiFixture f;
    std::vector<std::function<void()>> q;
    f.s.corePost = [&](std::function<void()> fn) { q.push_back(std::move(fn)); };
    DisasmNav nav;
    nav.back(f.s, VJ_DBG_TARGET_M68K, 0x1010, 4);
    unsigned a = 0;
    CHECK(nav.pending() && !nav.take(a));
    std::vector<std::function<void()>> work = std::move(q);
    for (auto& fn : work) { CountingIface::OnCore g; fn(); }
    CHECK(nav.take(a) && a == backStart(f.mock.iface(), VJ_DBG_TARGET_M68K, 0x1010, 4));
}
```

`app/tests/debug_cpu_ui_smoke.cpp` (new, modelled on `debug_memory_ui_test.cpp`; `UiFixture`; `registerCpuTools(s)`; `s.findTool("cpu")->toggle()`; INI size for "CPU debugger" 900x560):

```cpp
settle(8);                                            // frame() = NewFrame, drawCpuTools(s), Render, fx.fill()
CHECK(cpu_test::haveRegs() && cpu_test::rowCount() >= 4);
const UiFixture::Mark quiet = fx.mark();
for (int i = 0; i < 30; i++) frame(s);
CHECK(fx.quietSince(quiet) && CountingIface::offCore() == 0);
showCpuAt(s, VJ_DBG_TARGET_GPU, 0xF03000);            // the other tab: new requests, answered after a fill
settle(8);
CHECK(cpu_test::haveRegs() && CountingIface::offCore() == 0);
s.setHaltAvailable(false);                            // the threaded 3a state: windows live, Break/Step greyed
settle(4);
CHECK(CountingIface::offCore() == 0);
```

- [ ] **Step 2: Run, expect FAIL.**

```bash
cmake --build build -j4 --target vj_debug_cpu_test vj_debug_core_job_test vj_debug_cpu_ui_smoke
```

Expected: compile errors (`CoreJob`, `CpuFeed`, `DisasmNav`, `disasmRows`, `cpu_test::`); after those exist `debug_cpu_ui_smoke` fails on `offCore() == 0` while `cpu_ui.cpp` still calls `get_regs`.

- [ ] **Step 3: Register the tests.**

`VJDebugTests.cmake`: define the smoke helper (A7 and A8 use it too) near the top of the file, after `VJ_DEBUG_SESSION_SRCS` is available:

```cmake
function(vj_add_debug_ui_smoke target test)   # remaining arguments: the test's own sources
    add_executable(${target} ${ARGN} ${VJ_DEBUG_SESSION_SRCS} src/cheats.cpp src/config.cpp src/paths.cpp
        "${VJ_ROOT}/src/core/cheat.c" src/theme.cpp src/theme_style.cpp src/theme_custom.cpp)
    target_include_directories(${target} PRIVATE src src/debug tests "${VJ_ROOT}/src/debug" "${VJ_ROOT}/src/core"
        "${VJ_ROOT}/libretro-common/include")
    target_link_libraries(${target} PRIVATE vj_imgui ${VJ_SDL3_TARGET})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME ${test} COMMAND ${target})
    set_tests_properties(${test} PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL " LABELS emu_thread)
endfunction()
```

`debug_core_job_unit`: `add_executable(vj_debug_core_job_test tests/debug_core_job_test.cpp ${VJ_DEBUG_SESSION_SRCS})`, include dirs `src/debug tests "${VJ_ROOT}/src/debug"`, `Threads::Threads`, `LABELS emu_thread`, `FAIL_REGULAR_EXPRESSION "FAIL "`. `VJDebugCpu.cmake` is included before `VJDebugTests.cmake`, so the helper does not exist there: leave `vj_debug_cpu_test` and `vj_debug_cpu_e2e` in `VJDebugCpu.cmake` (they take `${VJ_DEBUG_SESSION_SRCS}` per R7) and define the new smoke in `VJDebugTests.cmake`: `vj_add_debug_ui_smoke(vj_debug_cpu_ui_smoke debug_cpu_ui_smoke tests/debug_cpu_ui_smoke.cpp src/debug/cpu_ui.cpp)` followed by `target_link_libraries(vj_debug_cpu_ui_smoke PRIVATE vj_debug_cpu)`. Add `cpu_ui.cpp` to `VJ_NO_IFACE_FILES` (not `cpu_view.cpp`: its disassembler functions take the iface by design and run on the emulation side).

- [ ] **Step 4: Implement.**

`core_job.h`: a `std::shared_ptr<State>` (mutex, optional `T`, `std::atomic<unsigned> inFlight`) captured by value in the posted lambda, which also captures `&s.core()`; `take` moves the value out under the mutex.

`cpu_view.cpp`:
- `disasmRows` = `disasmForward(iface, target, addr, rows)` mapped line by line: `{addr, valid ? len : 0, text, bytes}`.
- `CpuFeed`: ids `regsId_`, `disId_`; `want` calls `views().set` only for a changed field; `update` reads both buffers, adopts a Regs buffer when its serial differs from the adopted one, a Disasm buffer when `req` differs from the adopted request or its serial moved.
- `TempBreak::arm`: after `s.attach()`, decide `userArmed` from `s.breakpoints()` (UI list) as today, then `s.callCore([&]{ ok = s.core().breakSet(target, addr); })`; `disarm`: `if (active_ && owned_ && s.attached()) s.callCore([&]{ s.core().breakClear(target_, addr_); })`.
- `CpuControl::stepIn` (other processor): `s_.callCore([&]{ ok = s_.core().iface() != nullptr; if (ok) s_.core().haltNext(tab); })`, return false when `!ok`. `stepOver`: one `callCore` computing `getRegs` + `stepOverTarget(s_.core().iface(), tab, r.pc)` into locals, then the existing branches.
- `DisasmNav::back/prev` = `CoreJob<unsigned>` over `backStart` / `prevAddr` x count.

`cpu_ui.cpp`:
- `TabState`: drop `regsSerial`, `rowsSerial`, `rowsTop`, `rowsN`, `rowsIface`; `rows` becomes `std::vector<DisasmLine>` (`L.valid` becomes `L.len != 0`); add `unsigned wheelUp = 0; bool navBusy = false;`. One `CpuFeed feed` and one `DisasmNav nav` in `CpuState`.
- `refreshRegs` is replaced by an adopt step in `drawWindow`: `g.feed.want(s, tab, t.top, n); g.feed.update(s); if (g.feed.haveRegs() && g.feed.regsTarget() == tab) { t.regs = g.feed.regs(); t.haveRegs = true; }`; `s.wakeViews()` after `want`. While `!g.feed.answered()` the window shows "Reading registers..."; the existing "The core did not return..." text appears only for an answered `!ok` buffer.
- `drawDisasm`: `rows` come from `feed.rows()` when `feed.rowsFor(target, t.top, n)`, else the previous rows stay. Wheel down: `t.top = rows[whole].addr` from the published rows (clamped to the last row). Wheel up: add to `t.wheelUp`; when `!navBusy` post `nav.prev(s, target, t.top, t.wheelUp)` and clear it; on `nav.take` set `t.top`, clear `navBusy`, and post again when `wheelUp` accumulated meanwhile. Follow-PC: when the PC row is not visible post `nav.back(s, target, pc, min(4, n / 4))`; on take set `top`. Initial `!topValid`: post `nav.back` and draw an empty list until it returns (inline: the same call). `nextAddr`/`prevAddr`/`backStart` are no longer called from the draw path.
- `setReg` uses `s.setReg(target, reg, v)` (a `callCore` inside) and keeps `s.requestRefresh()`. `drawToolbar`: Break is enabled when `!halted && s.haltAvailable()`; Step/Continue/Step over/Run to cursor keep requiring `halted`; the tooltip of a disabled Break says "Halting is not available with the emulation thread yet" when `!s.haltAvailable()`.
- `registerCpuTools`: `s.views().setDisasm(&disasmRows)`; the `onHalt` hook (R5) keeps `t.prev = t.regs` and adds `s.requestRefresh()` in place of `t.regsSerial = 0`; the forced-close hook also calls `g.feed.release(s)`. `drawWindow` releases the feed when the window is closed or `!s.enabled()`.
- `cpu_test::haveRegs/rowCount` read `g.t[g.tab]`.

- [ ] **Step 5: Run, expect PASS.**

```bash
cmake --build build -j4 --target vj_debug_cpu_test vj_debug_core_job_test vj_debug_cpu_ui_smoke vj_debug_cpu_e2e virtualjaguar && ctest --test-dir build -R 'debug_cpu|debug_core_job|debug_no_direct_iface|debug_session_unit' --output-on-failure
```

Expected: all pass, including the untouched `debug_cpu_unit` run-control checks and `debug_cpu_e2e` (inline: `callCore` runs at once, so break / step / step over / run to cursor behave as before).

- [ ] **Step 6: Commit.**

```bash
git add app/src/debug/core_job.h app/src/debug/cpu_view.h app/src/debug/cpu_view.cpp app/src/debug/cpu_ui.h app/src/debug/cpu_ui.cpp app/tests/debug_core_job_test.cpp app/tests/debug_cpu_test.cpp app/tests/debug_cpu_ui_smoke.cpp app/cmake/VJDebugTests.cmake app/cmake/VJDebugCpu.cmake
git commit -m "feat(app): CPU debugger reads Regs and Disasm view buffers (#967)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task A6: Object list on the OpSnapshot request

**Files:**
- Modify: `app/src/debug/op_view.h` (`OpViewModel` 16-90), `app/src/debug/op_view.cpp` (all of it, 190 lines), `app/src/debug/op_render.h` (after `opColorToRgb` 62), `app/src/debug/op_render.cpp` (`opColorToRgb` 56-69), `app/src/debug/op_ui.cpp` (`drawPaintControls` 259-317, `drawObjectsTool` 472-497, `drawObjectsWindow` 504-634), `app/tests/debug_op_test.cpp`, `app/cmake/VJDebugTests.cmake`

**Interfaces:**
- Consumes: `ViewRequest::Kind::{OpSnapshot, Memory}`, `ViewBuffer::{opCount, objs, halfline, req, ok}`, `CoreJob` (A5), `DebugSession::{views, wakeViews, callCore, postCore, requestRefresh, buildCryTable, poke, regions}`, `App::emu.advance`.
- Produces:

```cpp
// op_view.h
enum class OpStep { None, Inline, Advance };
struct OpStepGate { bool threaded, paused, halted, attached, inFrame, loaded, netplay; };
// Advance: threaded && paused && !halted && loaded && !netplay (emu.advance(1); inFrame and the owner-only host
// calls are never consulted).  Inline: today's rule (!threaded && paused && !attached && !halted && !inFrame &&
// loaded && !netplay).  None otherwise.
OpStep opStepFor(const OpStepGate& g);
class OpViewModel {
public:
    void arm(DebugSession& s);                          // postCore { opSnapshotArm(); views().set(snap, since=++armGen_) }
    bool update(DebugSession& s, bool running = true);  // views().read only; may postCore a live arm, a render, a walk
    OpScreenGeom readGeom(unsigned frameW, unsigned frameH) const;   // from the last adopted $F00028 request; no session
    void releaseViews(DebugSession& s);
    const std::vector<uint32_t>* cryTable() const;      // the 64K table, or null until update() built it (one callCore per epoch, the first time a bitmap is selected)
};
// op_render.h
uint32_t opColorToRgb(const std::vector<uint32_t>* cryTable, OpColorMode mode, unsigned v16);   // same maths, table instead of iface
```

- [ ] **Step 1: Write the failing tests.**

In `app/tests/debug_op_test.cpp` (include `debug_ui_fixture.h`; link `debug_op_unit` with `${VJ_DEBUG_SESSION_SRCS}`; the existing `testViewModel*` functions stay and must pass unchanged, because inline `postCore`/`wakeViews` run at once); call the new ones from `main`:

```cpp
static void testOpStepGate() {
    OpStepGate g{};                                          // all false
    g.loaded = true; g.paused = true;
    CHECK(opStepFor(g) == OpStep::Inline);
    g.attached = true;  CHECK(opStepFor(g) == OpStep::None);  g.attached = false;
    g.inFrame = true;   CHECK(opStepFor(g) == OpStep::None);  g.inFrame = false;
    g.threaded = true;  CHECK(opStepFor(g) == OpStep::Advance);   // inFrame is not consulted threaded
    g.inFrame = true;   CHECK(opStepFor(g) == OpStep::Advance);   g.inFrame = false;
    g.halted = true;    CHECK(opStepFor(g) == OpStep::None);      g.halted = false;
    g.netplay = true;   CHECK(opStepFor(g) == OpStep::None);      g.netplay = false;
    g.paused = false;   CHECK(opStepFor(g) == OpStep::None);
}
static void testViewModelNeverTouchesTheCore() {
    UiFixture f;
    OpViewModel vm;
    vm.update(f.s);  vm.update(f.s);                          // arm + first absorb, as testViewModelLifecycle
    CHECK(vm.haveSnapshot() && vm.objects().size() == 4);
    CHECK(CountingIface::offCore() == 0);
    const UiFixture::Mark m = f.mark();
    for (int i = 0; i < 30; i++) vm.update(f.s);              // idle: no new serial, nothing waiting
    CHECK(f.quietSince(m));
    f.s.requestRefresh();
    vm.update(f.s);                                           // a live tick: one arm (post) and one fill, off-core zero
    CHECK(f.corePosts > m.corePosts && CountingIface::offCore() == 0);
}
static void testRenderLeavesTheUiThread() {
    UiFixture f;
    std::vector<std::function<void()>> q;                     // a deferred emulation thread
    f.s.corePost = [&](std::function<void()> fn) { q.push_back(std::move(fn)); };
    auto drain = [&] {                                        // what the emulation thread does at its next boundary
        std::vector<std::function<void()>> work = std::move(q);
        q.clear();
        for (auto& fn : work) { CountingIface::OnCore g; fn(); }
    };
    OpViewModel vm;
    vm.update(f.s);                                           // queues the fill (snapshot request not answered yet), then the arm
    drain();                                                  // fill, then the arm applied (the request is reset after it)
    vm.update(f.s);                                           // a fill is pending again: queued
    drain();                                                  // the snapshot is filled
    vm.update(f.s);                                           // absorbs; queues the first render
    const unsigned peeks = CountingIface::count(CountingIface::Peek);
    CHECK(vm.haveSnapshot() && vm.image() == nullptr);        // not rendered yet: nothing ran on this thread
    CHECK(CountingIface::offCore() == 0);
    drain();
    CHECK(CountingIface::count(CountingIface::Peek) > peeks);
    vm.update(f.s);
    CHECK(vm.image() != nullptr && vm.image()->w == 16);
}
static void testPaintIsOneCoreCall() {
    UiFixture f;
    OpViewModel vm;
    vm.update(f.s);  vm.update(f.s);
    std::string st;
    const unsigned k = f.coreCalls;
    CHECK(vm.paint(f.s, 3, 2, 200, st));
    CHECK(f.coreCalls == k + 1 && f.mock.dram()[MockDebugIface::kBitmapData + 35] == 200);
    CHECK(vm.image()->rawAt(3, 2) == 200 && CountingIface::offCore() == 0);
}
static void testGeomAndColourFromBuffers() {
    UiFixture f;
    f.mock.regwin()[0x28] = 0x00; f.mock.regwin()[0x29] = 0x07;            // VMODE
    f.mock.regwin()[0x38] = 0x01; f.mock.regwin()[0x39] = 0x80;            // HDB1
    OpViewModel vm;
    vm.update(f.s);  vm.update(f.s);
    OpScreenGeom g = vm.readGeom(320, 240);
    CHECK(g.vmode == 0x0007 && g.hdb1 == 0x180 && g.frameW == 320);
    std::vector<uint32_t> table;
    CHECK(f.s.buildCryTable(table));
    for (OpColorMode m : {OpColorMode::Cry, OpColorMode::Rgb16, OpColorMode::Mixed})
        for (unsigned v : {0x0000u, 0x1234u, 0x1235u, 0xFFFFu})
            CHECK(opColorToRgb(&table, m, v) == opColorToRgb(f.mock.iface(), m, v));
}
```

In `app/tests/debug_op_e2e.cpp` nothing changes (inline: the arm is applied, the fill runs in `postCore`/`wakeViews`, and `update()` re-requests while an armed snapshot is still `-1`). The existing `vm.update(dbg)` / `host.runFrame()` / `vm.update(dbg)` sequence must pass as is.

- [ ] **Step 2: Run, expect FAIL.**

```bash
cmake --build build -j4 --target vj_debug_op_test vj_debug_op_e2e
```

Expected: compile errors (`opStepFor`, `OpStep`, `readGeom(unsigned, unsigned)`, the table overload).

- [ ] **Step 3: Implement.**

`op_render.{h,cpp}`: the table overload (`Rgb16`: the existing lambda; `Cry`: `table && table->size() == 0x10000 ? (*table)[v] : 0`; `Mixed`: bit 0 picks). The `iface` overload stays (the render job and `opRender` run on the emulation thread) and now calls the table maths for `Rgb16`.

`op_view.{h,cpp}` (`OpViewModel`):
- State added: `DebugViews::Id snapId_, geomId_`; `uint64_t armGen_ = 0`; `uint64_t epochSeen_`; `CoreJob<RenderOut> render_` (`RenderOut {uint64_t gen; bool ok; OpImage img; std::string err;}`) with `renderGen_`; `CoreJob<WalkOut> walk_` (`WalkOut {uint64_t gen; std::vector<OpObject> objs;}`) with a held `pendingSnap_` (decoded snapshot objects + halfline + count) used by the fallback; `std::vector<uint32_t> cry_`; geometry bytes `geom_[0x20]` + `geomOk_`.
- `arm(s)`: returns at once when `!s.enabled()` (the hardcore checks of `testViewModelLifecycle` count `opArms`); ensures `snapId_` exists on this thread first (`snapId_ = views().set(snapId_, {OpSnapshot, since = armGen_}) `, so the lambda below never allocates an id the model does not know); then `++armGen_`; `waiting_ = true; ++arms_`; `s.postCore([=]{ s.core().opSnapshotArm(); s.views().set(snapId_, {OpSnapshot, since = gen}); })`, so the buffer is reset *after* the arm applied (a buffer filled before the arm can never be mistaken for its answer). No `requestRefresh()` here.
- `update(s, running)` runs in this order: (1) return false when `!s.enabled()`; (2) ensure `snapId_` (`since = armGen_`) and `geomId_` (`{Memory, 0xF00028, 0x20}`) exist; (3) `s.wakeViews()`; (4) read both buffers and absorb when `waiting_ && b.ok && b.req.since == armGen_ && b.opCount >= 0`; (5) while `waiting_`, `running` and the buffer still reads `opCount == -1`, re-`set()` the snapshot request with a poll counter in a field OpSnapshot ignores (`addr = ++pollGen_`): R2 makes the changed request pending, `wakeViews()` refills just that buffer (inline the walk is then visible on a second read; threaded the next frame), and no serial moves, so the other windows' change highlights and the live re-arm are untouched; (6) last, the first/live arm on a serial change exactly as today (`armLive` = the same post). The arm comes last so `waiting()` is true right after the first `update()`, as `testViewModelLifecycle` asserts. A new epoch calls `clear()`.
- Fallback walk (`followLinks && count == 0`): `walk_.submit` of a job that reads OLP lo/hi with `iface->peek(0xF00020/22, ...)` and runs `opWalkList(&peekReader, iface, lo | (hi << 16), VJ_DBG_OP_MAX_OBJECTS)`; the snapshot is committed (selection re-find, auto-select, `imageDirty_`) when `walk_.take` delivers, otherwise immediately.
- `rerender` becomes `startRender(s)` + `applyRender()`: when the selected object is not a bitmap resolve at once; else `render_.submit(s, [obj, opt, gen](const vj_debug_iface* i){ opRender(...) })`, and `applyRender()` takes the result, drops it when `gen != renderGen_`, and applies the existing `differs`/`imgSerial_` logic. `paint()` calls both in sequence (inline it completes before returning).
- `paint(s, x, y, value, status)`: one `s.callCore` computing `opPixelPatch(s.core().iface(), ...)`, the RAM-region check against `s.regions()`, and `s.poke(...)` (nested, inline on the emulation thread); the status strings are unchanged.
- `readGeom(frameW, frameH)`: big-endian words of the `{0xF00028, 0x20}` buffer at offsets `0x00` (VMODE, whole), `0x10` (HDB1), `0x16` (VP), `0x1E` (VDB), the last three `& 0x7FF` as today; default geometry when `!geomOk_`.
- `releaseViews(s)`: clears `snapId_`, `geomId_`. `cryTable()` builds `cry_` through `s.buildCryTable` once per epoch (one `callCore`) when first asked for with a bitmap object selected.

`op_ui.cpp`:
- `drawPaintControls(const std::vector<uint32_t>* cry, ...)` uses `opColorToRgb(cry, img.mode, w)`; the caller passes `vm.cryTable()`.
- `drawObjectsWindow`: `vm.update(session, ctx.emulationRunning)`; the geometry block calls `vm.readGeom(ctx.frameW, ctx.frameH)`; when the window is closed or `!session.enabled()` call `vm.releaseViews(session)`.
- `drawObjectsTool(App&)`: `ctx.stepFrame` from `opStepFor({app.threaded(), app.paused, app.debug.halted(), app.debug.attached(), /*inFrame*/ !app.threaded() && app.host.inFrame(), app.host.isLoaded(), app.netplay.configured()})`: `Advance` -> `[&app]{ app.debug.requestRefresh(); app.emu.advance(1); }` (the one-shot serial bump is what makes the armed snapshot refill after the frame); `Inline` -> the existing `runFrame()` + `video.upload(host.frame())` lambda with `app.debug.requestRefresh()` first and `app.debug.wakeViews()` after (inline it runs the copier at once); `None` -> empty. The threaded branch calls no owner-only `CoreHost` method.

- [ ] **Step 4: Register and run, expect PASS.**

Add `op_ui.cpp` to `VJ_NO_IFACE_FILES` (not `op_view.cpp`: the render and walk jobs it posts call the iface inside the job; the quiet/offCore tests above pin the model). Replace `src/debug/debug_session.cpp` in the `vj_debug_op_test` / `vj_debug_op_e2e` source lists by `${VJ_DEBUG_SESSION_SRCS}` if A3 left it.

```bash
cmake --build build -j4 --target vj_debug_op_test vj_debug_op_e2e virtualjaguar && ctest --test-dir build -R 'debug_op|debug_no_direct_iface' --output-on-failure
```

Expected: `debug_op_unit` (old lifecycle, fallback walk, paint, hardcore checks unchanged, plus the five new tests), `debug_op_e2e`, `debug_no_direct_iface` pass.

- [ ] **Step 5: Commit.**

```bash
git add app/src/debug/op_view.h app/src/debug/op_view.cpp app/src/debug/op_render.h app/src/debug/op_render.cpp app/src/debug/op_ui.cpp app/tests/debug_op_test.cpp app/cmake/VJDebugTests.cmake
git commit -m "feat(app): Object list reads the OpSnapshot request; step-a-frame via advance(1) (#967)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task A7: Blitter log and Textures

**Files:**
- Create: `app/src/debug/blitter_feed.h`, `app/src/debug/blitter_feed.cpp`, `app/tests/debug_blitter_feed_test.cpp`, `app/tests/debug_blitter_ui_smoke.cpp`
- Modify: `app/src/debug/blitter_log.h` (`LogTracker` 96-109), `app/src/debug/blitter_log.cpp` (after `sync` 194-216), `app/src/debug/blitter_ui.h`, `app/src/debug/blitter_ui.cpp` (`State` 21-34, `setLog` 38-55, `drawLiveRegs` 105-134, `blitterToolDraw` 197-249), `app/src/debug/texture_pack.h` (`ReplaceOutcome`/`replaceTile` 151-165), `app/src/debug/texture_pack.cpp` (`replaceTile` 513-534), `app/src/debug/texture_ui.cpp` (`handlePending` 282-300, "Reload packs" 458-466), `app/tests/debug_blitter_test.cpp`, `app/tests/debug_texture_test.cpp`, `app/CMakeLists.txt` (app sources 340-357), `app/cmake/VJDebugTests.cmake`

**Interfaces:**
- Consumes: `ViewRequest::Kind::{BlitLog, Memory}`, `ViewBuffer::{blits, blitHead, blitLost, req, ok, serial, bytes}`, `DebugSession::{views, wakeViews, callCore, blitLogEnable, texReplaceReload}`, `vj_debug_blit::seq` (the launch number = the core's ring index, src/debug/vjdebug.c).
- Produces:

```cpp
// blitter_log.h
size_t LogTracker::absorb(const ViewBuffer& b);   // rows added.  Ignores a buffer that does not answer `since == total()`; never adds seq < total().
// blitter_feed.h
namespace vj::blit {
class LogFeed {   // everything the blitter window reads, published buffers only
public:
    LogTracker tracker;
    void setLog(DebugSession& s, bool want);              // one callCore on a change; a new epoch forgets "on" without calling
    void update(DebugSession& s, bool frozen);            // sets BlitLog{since = tracker.total()} + Memory{kRegBase, kRegBytes}; absorbs; wakeViews
    void clear(DebugSession& s);                          // tracker.reset() + ONE callCore { enable(false); enable(true) }
    void release(DebugSession& s);
    bool logOn() const;  bool haveRegs() const;  const std::vector<uint8_t>& regImage() const;
};
}
// blitter_ui.h
namespace blitter_test { unsigned launchesSeen(); bool logOn(); }
// texture_pack.h
using ReloadFn = std::function<int()>;   // 1 active, 0 inactive, <0 not supported (DebugSession::texReplaceReload)
ReplaceOutcome replaceTile(const OptionIO& io, const ReloadFn& reload, const std::string& systemDir, uint32_t crc, const std::string& hash,
                           unsigned tileW, unsigned tileH, unsigned tileBpp, const std::string& srcPng);
// the existing iface overload stays, a thin wrapper: reload = [iface]{ size check; iface->texreplace_reload() }
struct ReloadText { std::string text; bool error = false; };
ReloadText reloadText(int r);   // 1 "Packs reloaded."; 0 "Packs reloaded; replacement is off or the folder is empty."; <0 error "This core cannot reload packs; restart the game."
```

- [ ] **Step 1: Write the failing tests.**

`app/tests/debug_blitter_test.cpp` (it already links only `blitter_log.cpp` + `audio_ring.cpp`; add `#include "debug_views.h"` and call from `main`):

```cpp
static ViewBuffer blitBuf(unsigned since, unsigned head, unsigned lost, unsigned firstSeq, unsigned n) {
    ViewBuffer b;  b.ok = true;  b.serial = 1;
    b.req.kind = ViewRequest::Kind::BlitLog;  b.req.since = since;
    b.blitHead = head;  b.blitLost = lost;
    for (unsigned i = 0; i < n; i++) { vj_debug_blit x{}; x.seq = firstSeq + i; x.cmd = 0x100 + firstSeq + i; b.blits.push_back(x); }
    return b;
}
static void testTrackerAbsorb() {
    blit::LogTracker t;
    ViewBuffer a = blitBuf(0, 5, 0, 0, 5);
    CHECK(t.absorb(a) == 5 && t.total() == 5 && t.lost() == 0 && t.rows().back().seq == 4);
    CHECK(t.absorb(a) == 0 && t.rows().size() == 5);                 // the same buffer twice: no duplicates
    ViewBuffer stale = blitBuf(0, 7, 0, 0, 7);                       // filled for an older `since` after we moved on
    CHECK(t.absorb(stale) == 0 && t.total() == 5);
    CHECK(t.absorb(blitBuf(5, 2000, 971, 976, 1024)) == 1024);       // the ring wrapped: launches 5..975 are gone
    CHECK(t.lost() == 971 && t.total() == 2000 && t.rows().back().seq == 1999);
    CHECK(t.absorb(blitBuf(2000, 3, 0, 0, 3)) == 0 && t.total() == 0 && t.rows().empty());   // head < since: the log restarted
    blit::LogTracker small(4);
    small.absorb(blitBuf(0, 9, 0, 0, 9));
    CHECK(small.rows().size() == 4 && small.rows().front().seq == 5);
    ViewBuffer bad = blitBuf(0, 5, 0, 0, 5);  bad.ok = false;
    blit::LogTracker u;
    CHECK(u.absorb(bad) == 0 && u.total() == 0);
}
```

`app/tests/debug_blitter_feed_test.cpp` (new; `UiFixture`; sources `blitter_feed.cpp blitter_log.cpp`, `${VJ_DEBUG_SESSION_SRCS}`, `LABELS emu_thread`):

```cpp
UiFixture f;
blit::LogFeed feed;
feed.setLog(f.s, true);
CHECK(feed.logOn() && f.mock.blitLogOn && f.coreCalls == 1);
feed.setLog(f.s, true);
CHECK(f.coreCalls == 1);                                              // no change, no call
feed.update(f.s, false);  feed.update(f.s, false);                    // the first absorbs (inline fill), the second re-requests since=5
CHECK(feed.tracker.total() == 5 && feed.tracker.rows().size() == 5 && feed.haveRegs());
f.mock.addBlit(0x01800001u, 0x00010002);
f.s.requestRefresh();
feed.update(f.s, false);
CHECK(feed.tracker.total() == 6 && feed.tracker.rows().back().seq == 5);   // 6 rows, no duplicates
f.mock.addBlit(0x01800009u, 0x00010003);  f.s.requestRefresh();
feed.update(f.s, true);                                               // frozen: requests but does not absorb
CHECK(feed.tracker.total() == 6);
feed.update(f.s, false);  feed.update(f.s, false);                    // unfrozen: catches up with launch 6, then re-requests
CHECK(feed.tracker.total() == 7 && feed.tracker.rows().back().seq == 6);
const UiFixture::Mark m = f.mark();
for (int i = 0; i < 30; i++) feed.update(f.s, false);
CHECK(f.quietSince(m) && CountingIface::offCore() == 0);
const unsigned k = f.coreCalls;
feed.clear(f.s);
CHECK(f.coreCalls == k + 1 && feed.tracker.total() == 0 && f.mock.blitLogOn);   // one wait: off then on
f.mock.blitLogOn = false;                                             // a new core: its log starts off
f.s.setIface(f.counted.iface());                                      // (epoch bump, R3)
feed.setLog(f.s, true);
CHECK(f.mock.blitLogOn && feed.logOn());                              // re-enabled on the new core
feed.setLog(f.s, false);
CHECK(!feed.logOn() && !f.mock.blitLogOn);
```

`app/tests/debug_blitter_ui_smoke.cpp` (new, via `vj_add_debug_ui_smoke(vj_debug_blitter_ui_smoke debug_blitter_ui_smoke tests/debug_blitter_ui_smoke.cpp src/debug/blitter_ui.cpp src/debug/blitter_feed.cpp src/debug/blitter_log.cpp src/debug/audio_ring.cpp)`):

```cpp
blitterToolInit(fx.s);  fx.s.findTool("blitter")->toggle();
settle(6);
CHECK(blitter_test::logOn() && fx.mock.blitLogOn && blitter_test::launchesSeen() == 5);
const UiFixture::Mark quiet = fx.mark();
for (int i = 0; i < 30; i++) frame();
CHECK(fx.quietSince(quiet) && CountingIface::offCore() == 0);
fx.s.findTool("blitter")->toggle();  settle(3);                       // closing switches the core's log off
CHECK(!fx.mock.blitLogOn);
```

`app/tests/debug_texture_test.cpp` additions (the five existing `replaceTile(io, mock.iface(), ...)` calls stay untouched and must still pass through the wrapper):

```cpp
static void testReplaceWithCallable() {
    std::string root = tmpRoot(), src = root + "/art.png";
    writeText(src, pngHeader(16, 8) + "payload");
    FakeOptions fo;  tex::OptionIO io = fo.io();
    int calls = 0, ret = 1;
    tex::ReloadFn reload = [&] { calls++; return ret; };
    tex::ReplaceOutcome o = tex::replaceTile(io, reload, root, 0x0e4ae2eb, kH1, 16, 8, 16, src);
    CHECK(o.ok && o.reloadCalled && o.reloadActive && calls == 1);
    ret = -1;                                                         // an older core: installs, says so, option still set
    o = tex::replaceTile(io, reload, root, 0x0e4ae2eb, kH3, 16, 8, 16, src);
    CHECK(o.ok && !o.reloadCalled && o.message.find("restart") != std::string::npos && calls == 2);
    writeText(src, pngHeader(20, 8));
    calls = 0;
    CHECK(!tex::replaceTile(io, reload, root, 0x0e4ae2eb, kH2, 16, 8, 16, src).ok && calls == 0);
    CHECK(tex::replaceTile(io, tex::ReloadFn(), root, 0x0e4ae2eb, kH3, 16, 8, 16, src).reloadCalled == false);
    CHECK(tex::reloadText(1).text == "Packs reloaded." && !tex::reloadText(1).error);
    CHECK(tex::reloadText(0).text.find("folder is empty") != std::string::npos && !tex::reloadText(0).error);
    CHECK(tex::reloadText(-1).error);
}
```

- [ ] **Step 2: Run, expect FAIL.**

```bash
cmake --build build -j4 --target vj_debug_blitter_test vj_debug_texture_test vj_debug_blitter_feed_test vj_debug_blitter_ui_smoke
```

Expected: compile errors (`absorb`, `LogFeed`, `ReloadFn`, `reloadText`, `blitter_test::`).

- [ ] **Step 3: Implement.**

`blitter_log.cpp` `LogTracker::absorb(const ViewBuffer& b)`: `if (!b.ok || b.req.since != next_) return 0;` (not an answer to the request we are on); `if (b.blitHead < next_) { reset(); return 0; }` (the log restarted); then `lost_ += b.blitLost;` push every `blit` with `seq >= next_`; `next_ = b.blitHead;` trim to `max_`; return the rows added. `sync(iface)` stays for `debug_blitter_unit`.

`blitter_feed.cpp` (`LogFeed`): ids `logId_`, `regId_`, `lastSerial_`, `lastSince_`; `epochSeen_` replaces `logIface`. `setLog(s, want)`: when `logOn_ && epochSeen_ != s.views().epoch()` forget (`logOn_ = false; tracker.reset()`), then on `want && !logOn_` -> `s.blitLogEnable(true); logOn_ = true; epochSeen_ = epoch; tracker.reset()`; on `!want && logOn_` -> `s.blitLogEnable(false)`. `update`: `s.views().set(logId_, {BlitLog, since = tracker.total()})` only when `since` changed, set the register request once, `s.wakeViews()`, read the log buffer and absorb it when `!frozen && b.serial != lastSerial_`, read the registers buffer (`req.addr == kRegBase`, `ok`, `bytes.size() == kRegBytes`). `clear`: `tracker.reset()` then one `s.callCore([&]{ s.core().blitLogEnable(false); s.core().blitLogEnable(true); })`. `release` clears both ids.

`blitter_ui.cpp`: `State` keeps `session`, `open`, `frozen`, `filter`, `selected` and one `blit::LogFeed feed`; `setLog(want)` is `g.feed.setLog(*g.session, want)`; the draw path replaces the serial-gated `tracker.sync(f)` + `read(kRegBase...)` block with `g.feed.update(*g.session, g.frozen)`; Clear calls `g.feed.clear(...)`; the forced-close hook calls `g.feed.setLog(session, false)` and `release`. Closed or `!enabled()` calls `release`. `blitter_test::{launchesSeen, logOn}` read `g.feed`.

`texture_pack.cpp`: `replaceTile(io, const ReloadFn& reload, ...)` is the current body with `if (reload) { int r = reload(); if (r >= 0) { o.reloadActive = r != 0; o.reloadCalled = true; } }`; the iface overload builds the lambda with the existing `size >= offsetof(...) + sizeof` check (returns -1 when unsupported). `reloadText` as specified.

`texture_ui.cpp`: `handlePending` passes `[s = g.session]{ return s->texReplaceReload(); }`; the "Reload packs" button becomes `ReloadText t = tex::reloadText(g.session->texReplaceReload()); setStatus(t.text, t.error);`.

- [ ] **Step 4: Register and run, expect PASS.**

`CMakeLists.txt`: add `src/debug/blitter_feed.cpp` after `blitter_log.cpp`. `VJDebugTests.cmake`: the two new executables as above (`debug_blitter_feed_unit`, `LABELS emu_thread`, `FAIL_REGULAR_EXPRESSION "FAIL "`); add `blitter_ui.cpp blitter_feed.cpp texture_ui.cpp` to `VJ_NO_IFACE_FILES`.

```bash
cmake --build build -j4 --target vj_debug_blitter_test vj_debug_texture_test vj_debug_blitter_feed_test vj_debug_blitter_ui_smoke virtualjaguar && ctest --test-dir build -R 'debug_blitter|debug_texture|debug_no_direct_iface' --output-on-failure
```

Expected: all pass; `debug_blitter_unit` and `debug_texture_unit` keep every old check.

- [ ] **Step 5: Commit.**

```bash
git add app/src/debug/blitter_feed.h app/src/debug/blitter_feed.cpp app/src/debug/blitter_log.h app/src/debug/blitter_log.cpp app/src/debug/blitter_ui.h app/src/debug/blitter_ui.cpp app/src/debug/texture_pack.h app/src/debug/texture_pack.cpp app/src/debug/texture_ui.cpp app/tests/debug_blitter_test.cpp app/tests/debug_blitter_feed_test.cpp app/tests/debug_blitter_ui_smoke.cpp app/tests/debug_texture_test.cpp app/CMakeLists.txt app/cmake/VJDebugTests.cmake
git commit -m "feat(app): Blitter log and Textures through view buffers and core calls (#967)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task A8: Cheat scanner on the Dram request; Audio scope ungated

**Files:**
- Create: `app/tests/debug_scope_gate_test.cpp`
- Modify: `app/src/debug/scanner_ui.h` (test hooks), `app/src/debug/scanner_ui.cpp` (`ScannerState` 38-69, `ramNow` 84-99, `forceClose` 115-119, `doScan` 130-166, `refreshRows` 168-183, `drawScanner` 450-473), `app/tests/debug_scan_ui_smoke.cpp`, `app/cmake/VJDebugTests.cmake`
- No change: `app/src/debug/scanner_actions.cpp` (`scanPoke` already goes through `DebugSession::poke`, a `callCore` after A3) and `app/src/debug/scope_ui.cpp` (it has no gate of its own: the gate was `DebugSession::enabled()` through `toolAvailable`).

**Interfaces:**
- Consumes: `ViewRequest::Kind::Dram`, `ViewBuffer::{bytes, ok, serial}`, `DebugSession::{views, wakeViews, snapshotDram, poke, toolAvailable, setHaltAvailable}`.
- Produces: the scanner keeps `drawScanner(DebugSession&, CheatManager&, CoreHost&)` and `scanner_test::scan(DebugSession&, CoreHost&)` unchanged (the `CoreHost&` is no longer read); new hook `namespace scanner_test { bool liveValue(size_t row, int64_t& v); }`.

- [ ] **Step 1: Write the failing tests.**

`app/tests/debug_scan_ui_smoke.cpp`: replace `MockDebugIface mock; DebugSession session;` with `UiFixture fx; MockDebugIface& mock = fx.mock; DebugSession& session = fx.s;` (the later `session.setIface(mock.iface())` becomes `session.setIface(fx.counted.iface())`); keep sections as they are and add after the Equal narrowing section (the one that leaves a result list on screen):

```cpp
    // #967 3a: the scan pass reads RAM as of now (one callCore); the live column comes from the Dram request, and idle
    // frames reach the core not at all.
    const UiFixture::Mark quiet = fx.mark();
    frames(session, cm, 40);                                  // the 10 Hz live gate passes several times at 1/60 s a frame
    CHECK(fx.quietSince(quiet) && CountingIface::offCore() == 0);
    int64_t live = 0;
    CHECK(scanner_test::liveValue(0, live));                  // row 0 is the word at $000100
    mock.dram()[0x101] = 0x7E;                                // RAM moves behind the window
    session.requestRefresh();                                 // the next fill carries it; the gate re-reads within 0.1 s
    frames(session, cm, 10);
    int64_t after = 0;
    CHECK(scanner_test::liveValue(0, after) && after == (int64_t)((mock.dram()[0x100] << 8) | 0x7E) && after != live);
    CHECK(CountingIface::offCore() == 0);
```

Insert this block right after the `CHECK(scanner_test::shownRows() == 3);` of the "Changed keeps those" section and before the Enter-commit edit of `$000100`; also insert `CountingIface::reset();` at the top of `main`.

`app/tests/debug_scope_gate_test.cpp` (new; `vj_add_debug_ui_smoke(vj_debug_scope_gate_test debug_scope_gate tests/debug_scope_gate_test.cpp src/debug/scope_ui.cpp src/debug/audio_ring.cpp src/core_host.cpp)` plus `target_link_libraries(vj_debug_scope_gate_test PRIVATE vj_core Threads::Threads)`, as `vj_debug_scan_ui_smoke` links the core; `frame()` is the test's own NewFrame / `scopeToolDraw()` / Render helper):

```cpp
UiFixture fx;
scopeToolInit(fx.s, CoreHost::instance());
DebugTool* t = fx.s.findTool("scope");
fx.s.setHaltAvailable(false);                          // the threaded 3a state
CHECK(fx.s.enabled() && t && t->toggle && fx.s.toolAvailable(*t));
t->toggle();                                           // open: the tap now fills the ring
std::atomic<bool> stop{false};
std::thread audio([&] {                                // what the emulation thread does through host.onAudio
    int16_t block[128 * 2] = {};
    for (int i = 0; i < 200 && !stop; i++) { block[0] = (int16_t)i; CoreHost::instance().onAudio(block, 128); }
});
for (int i = 0; i < 60; i++) frame();                  // the window draws while blocks arrive
audio.join();
frame();
CHECK(scope_test::framesSeen() >= 128 * 200);
CHECK(CountingIface::offCore() == 0);
```

Add `namespace scope_test { size_t framesSeen(); }` to `scope_ui.h` as a pure read of `g.ring.totalFrames()` (the only change to scope_ui). Label the test `emu_thread`.

- [ ] **Step 2: Run, expect FAIL.**

```bash
cmake --build build -j4 --target vj_debug_scan_ui_smoke vj_debug_scope_gate_test
```

Expected: compile errors (`scanner_test::liveValue`, `scope_test::framesSeen`); once present, the scanner smoke fails the `offCore() == 0` checks (`memoryRegion` and `snapshotDram` run on the UI thread in `refreshRows`).

- [ ] **Step 3: Implement.**

`scanner_ui.cpp`:
- Split `ramNow`: `scanRam(session, fb)` returns `fb.data()` after `session.snapshotDram(fb)`. That is a `DebugSession::read`, i.e. one `callCore` -> `EmuThread::debugCall`: the 2 MB `peek` runs **on the emulation thread**, between frames (or inside the halt pump), so a scan pass sees RAM as of one instant and the UI waits at most one frame plus the copy. It is never a UI-thread peek. The raw `memoryRegion` pointer path is deleted (owner-only, counted as a violation threaded), so `debug_scan_e2e`'s "narrow right after `runFrame`" holds. `liveRam(session)` reads `g.dramId` into the persistent `ViewBuffer g.dram` with `views().read` and returns `g.dram.bytes.data()` when `ok && bytes.size() == DebugSession::kDramSize`, else null.
- `doScan` uses `scanRam`. `refreshRows` uses `liveRam`, and reads the buffer only when its existing gate passes (`lastLive` interval, serial change, `rowsDirty`): a read copies 2 MB under the views mutex.
- `ScannerState` gets `DebugViews::Id dramId`. `drawScanner` sets `{Dram}` on the first draw while open and enabled, calls `session.wakeViews()` after it, and clears the id when closed, `!enabled()`, or in `forceClose`.
- `CoreHost& host` stays in the signatures and is no longer used (`[[maybe_unused]]`); `<core_host.h>` and `RETRO_MEMORY_SYSTEM_RAM` leave the file.
- `scanner_test::liveValue(row, v)`: `g.cur[row]` when it exists.

`scope_ui`: only `scope_test::framesSeen()`; nothing else changes in the window (its `AudioRing` is mutex-protected and `g.open` is atomic, which `debug_blitter_unit`'s two-thread ring test already pins).

- [ ] **Step 4: Register and run, expect PASS.**

`VJ_NO_IFACE_FILES` gains `scanner_ui.cpp scanner_actions.cpp scope_ui.cpp`.

```bash
cmake --build build -j4 --target vj_debug_scan_ui_smoke vj_debug_scan_test vj_debug_scan_e2e vj_debug_scope_gate_test virtualjaguar && ctest --test-dir build -R 'debug_scan|debug_scope|debug_no_direct_iface|debug_blitter_unit' --output-on-failure
```

Expected: `debug_scan_unit`, `debug_scan_e2e`, `debug_scan_ui_smoke`, `debug_scope_gate`, `debug_no_direct_iface` pass. Finish with the 3a window set: `ctest --test-dir build -R 'debug_' --output-on-failure` all green and `ctest --test-dir build -L emu_thread` shows the new `debug_core_job_unit`, `debug_blitter_feed_unit`, `debug_*_ui_smoke`, `debug_scope_gate`, `debug_no_direct_iface`.

- [ ] **Step 5: Commit.**

```bash
git add app/src/debug/scanner_ui.h app/src/debug/scanner_ui.cpp app/src/debug/scope_ui.h app/src/debug/scope_ui.cpp app/tests/debug_scan_ui_smoke.cpp app/tests/debug_scope_gate_test.cpp app/cmake/VJDebugTests.cmake
git commit -m "feat(app): Cheat scanner on the Dram request; Audio scope available with the thread on (#967)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---


### Task A9: App wiring, the limits narrowed, the threaded test, docs, the PR

**Files:**
- Modify: `app/src/app.h` (:335-336, :358-364), `app/src/app.cpp` (`wireHost` :463-512, `syncDebug` :522-526, `limitFeaturesForEmuThread` :533-544, `liftFeatureLimits` :547-554, `emuRunOneFrame` tail :677, `haltedFrame` :915, F9 :1654-1657, `stepEmulation` :1811-1860, `run()` :2021)
- Modify: `app/src/desktop.cpp` (:149-152), `app/src/debug/debug_ui.cpp` (:56-66), `app/src/ui.cpp` (:262-266)
- Modify: `app/src/debug/debug_session.h/.cpp` (remove `iface()`, `setThreadBlocked`, `threadBlocked`, `threadBlocked_`)
- Create: `app/tests/debug_views_threaded.cpp`
- Test/Modify: `app/tests/debug_session_test.cpp` (`testThreadBlocked` at :410-420), `app/tests/debug_e2e.cpp` (:71, :128), `app/tests/debug_cpu_e2e.cpp` (:95, :125, :260, :316, :395, :396), `app/tests/debug_op_e2e.cpp` (:114), `app/cmake/VJEmuThreadTests.cmake` (:6-24 header, :136-143, :194-198, new target)
- Docs: `docs/app-debug-tools.md`, `docs/agent/testing.md`, `app/README.md` (:548-552)

**Interfaces:**
- Consumes: everything from A1-A8.
- Produces: `void App::fillDebugViews()` (`debug.fillViews()`), the wiring below, `emu_thread_limits` / `emu_thread_start_fallback` with the new messages, test `debug_views_threaded`.

- [ ] **Step 1: Write the failing tests.**

(a) `app/cmake/VJEmuThreadTests.cmake`: change the two regexes (the message wording is fixed here):

```cmake
# emu_thread_start_fallback (:142)
    PASS_REGULAR_EXPRESSION "the debugger halt and the GDB stub are back on .the emulation thread did not start."
# emu_thread_limits (:197)
    PASS_REGULAR_EXPRESSION "the debugger halt and the GDB stub are off while the emulation thread is on \\(the Tools windows work\\)"
```

and the header list line `*_threaded limits` becomes `emu_thread_limits  the halt and the GDB stub are off while threaded; the Tools windows work`. `gdb_locked_threaded` is unchanged. Update the comment at :137 the same way.

(b) `app/tests/debug_session_test.cpp`: delete `testThreadBlocked` (and its call in `main`); the `testHaltAvailable` of Task A3 replaces it.

(c) Create `app/tests/debug_views_threaded.cpp`: the REAL core on a threaded `EmuThread`, the session wired exactly as `App` wires it. Skeleton (the case bodies follow the assertions listed):

```cpp
// The debug views with the REAL core on the emulation thread (#967 phase 3a): what App does for the Tools windows,
// minus the pixels.  usage: vj_debug_views_threaded <rom-a.j64> <rom-b.j64>
#include "core_host.h"
#include "debug/debug_session.h"
#include "emu_thread.h"
...
struct Rig : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    DebugSession dbg;
    EmuThread emu;
    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 600.0; }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override { host.runFrame(); host.publishSnapshot(); dbg.fillViews(); }   // emuRunOneFrame's tail
    void afterCommands() override { host.publishSnapshot(); }
    void wire() {   // = App::wireHost + startEmuThread
        host.onDebugIface = [this](const vj_debug_iface* i) { dbg.onCoreIface(i); if (!host.threaded()) dbg.syncCore(); };
        dbg.coreCall = [this](std::function<void()> f) { emu.debugCall(std::move(f)); };
        dbg.corePost = [this](std::function<void()> f) { emu.debugPost(std::move(f)); };
        host.post = [this](std::function<void()> f) { emu.post(std::move(f)); };
    }
};
```

Use the quoted titles below, verbatim, as the `CHECK` messages (they are the names the plan's Review Focus refers to). Cases, each a function with these `CHECK`s (threaded, `emu.setPaused(true)` so frames run only through `advance()`; `host.ownerViolations()` read at the end of the run must be 0):
  A shared helper `waitRead(id, buf)` is the UI's per-frame loop: call `dbg.wakeViews()` every 2 ms until `views().read(id, buf)` succeeds, up to 500 ms; it returns whether it did. **Every case fills through it, never through a running frame**: the fill is once per serial, so a frame run after the fill would make the buffer and a later `debugCall` peek differ (yarc rewrites DRAM every frame). The thread stays paused, so nothing moves between the fill and the comparison.
  1. **"memory, dram and regs requests equal a debugCall peek at the same frame":** load `argv[1]` with `emu.call`; `dbg.setContentLoaded(true); dbg.syncCore();`; `emu.advance(3)`; set a Memory request (`addr 0x1000, len 256`), a Dram request, a Regs (M68K) request; `dbg.requestRefresh()`; for each id `waitRead` succeeds with `ok`, `frame` equals `emu.debugCall([&]{ return dbg.core().frameNumber(); })`, and `bytes` equals a `debugCall` peek of the same range (the Dram buffer is hashed against a peek of 2 MiB), `regs.pc` equals `debugCall` `getRegs(M68K).pc`.
  2. **"a poke through the session shows in the next fill":** `dbg.pokeU(0x1F0000, 4, 0xCAFEF00D) == 4` (marshalled through `debugCall`; the poke bumps the serial), a Memory request at `0x1F0000` length 4, `waitRead`: `bytes == {CA FE F0 0D}`. No `advance()`.
  3. **"a new request is readable while paused, with no advance()":** record `emu.framesRun()`, set a `ViewRequest` at `0x2000` on the test thread, `waitRead`: `CHECK(ok, "readable within 500 ms with no frame run")` and `emu.framesRun()` equals the recorded value. Then change the request to `0x3000` (a scroll while paused) and `waitRead` again: the new range, same serial.
  4. **"after a reload the Memory window's first read is the new ROM's bytes":** Memory request `0x800000` length 65536 (the cartridge). `emu.advance(2)`, copy the buffer as `romA`. `emu.call(loadContent(argv[2]))` (`onCoreIface` fires on the emulation thread twice: null, new), then `dbg.syncCore()`. Immediately `CHECK(!views().read(id, b) || b.bytes != romA, "never the previous game's bytes")`; then `waitRead` (the epoch bump makes the old buffer pending at the same serial); `CHECK(b.bytes == (debugCall peek of 0x800000, 65536))` and `CHECK(b.bytes != romA)` (if this fails the two ROMs share 64 KiB: pick different ROMs, do not weaken it).
  5. **"inline and threaded give the same Dram view":** a helper `uint32_t dramHash(bool threaded)` that builds a **fresh `Rig`** (its own `EmuThread` and `DebugSession`; `coreCall` captures that rig's `emu`, and a stopped `EmuThread` is not restartable), wires it, loads `argv[1]`, sets a Dram request, `emu.advance(60)`, `requestRefresh()`, `waitRead`, FNV of the buffer, then `emu.call(unloadContent)` and `emu.stop()` as `session()` in `emu_thread_e2e.cpp` does. Inline is `Mode::Inline` with `corePost` unset (the helper's `wakeViews()` then fills directly and `advance` runs `inlineStep`). `CHECK(dramHash(false) == dramHash(true), "inline and threaded give the same Dram view")`. Cases 1-4, 6 and 7 share one threaded `Rig`; the owner-violation count is checked once at the end of each rig.
  6. **"no halt is possible while it is gated":** `dbg.setHaltAvailable(false)`; `dbg.breakNow()`; `emu.advance(5)`; `CHECK(!dbg.attached() && !dbg.halted())`.
  7. **"hardcore closes the tools and detaches threaded":** `dbg.setHardcore(true)`; `CHECK(!dbg.enabled())`; `dbg.setHardcore(false)`.

CMake (in `VJEmuThreadTests.cmake`, after `emu_thread_e2e`, also add a header-list line `debug_views_threaded  the Tools windows' data paths with the REAL core on the emulation thread`):

```cmake
add_executable(vj_debug_views_threaded tests/debug_views_threaded.cpp ${VJ_DEBUG_SESSION_SRCS} src/core_host.cpp
               src/emu_thread.cpp src/paths.cpp)
target_include_directories(vj_debug_views_threaded PRIVATE src)
target_link_libraries(vj_debug_views_threaded PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_debug_views_threaded PRIVATE ws2_32 iphlpapi)
endif()
vj_emu_thread_warnings(vj_debug_views_threaded)
add_test(NAME debug_views_threaded
    COMMAND vj_debug_views_threaded "${VJ_ROMS}/yarc.j64" "${VJ_ROMS}/jagniccc.j64")
set_tests_properties(debug_views_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
    FAIL_REGULAR_EXPRESSION "FAIL |owner violation|ThreadSanitizer")
```

- [ ] **Step 2: Run to see what fails.**

Run: `cmake --build build -j4 && ctest --test-dir build -R "emu_thread_limits|emu_thread_start_fallback|debug_views_threaded" --output-on-failure`
Expected: `emu_thread_limits` and `emu_thread_start_fallback` FAIL (the log still says "the debugger tools and the GDB stub ..."). `debug_views_threaded` should PASS already: A1-A3 supply every API it uses (`fillViews`, `wakeViews`, `onCoreIface`, the lanes), and it fills through `wakeViews()` exactly as `App::syncDebug` will. It guards the paths the steps below wire the App to, under ThreadSanitizer. If a case fails, read it as a plumbing bug in A1-A3 first; the cases never rely on a frame running between a fill and a comparison.

- [ ] **Step 3: Wire the App.**
  1. `app.h`: declare `void fillDebugViews();` (comment: emulation thread, inline the UI thread; `debug.fillViews()`). `app.cpp`: define it. Call it (a) at the end of `emuRunOneFrame()` after `host.publishSnapshot()` (:677); (b) in `stepEmulation()` once **after** the `for (frames)` loop, not inside it, so fast-forward does not fill four times per UI frame. `haltedFrame()` needs no separate call: it already runs `syncDebug()`, and `wakeViews()` below fills inline there (record this in the comment).
  2. `wireHost()` (:498): replace `host.onDebugIface` with `[this](const vj_debug_iface* i) { debug.onCoreIface(i); if (!host.threaded()) debug.syncCore(); }`, and add `debug.coreCall = [this](std::function<void()> f) { emu.debugCall(std::move(f)); };` and `debug.corePost = [this](std::function<void()> f) { emu.debugPost(std::move(f)); };` (before `EmuThread::start` the thread is inline, so both run at once; headless and inline runs are unchanged).
  3. `syncDebug()` (:522): first line `debug.syncCore();`, last line `debug.wakeViews();` (after `tick`).
  4. Limits: `limitFeaturesForEmuThread()` becomes `debug.setHaltAvailable(false);` plus the existing option lock for `virtualjaguar_gdb_stub`; **delete** `debug.setThreadBlocked(true)` and `host.announceDebug = false` (the core must hand its interface over; it cannot halt because `attach()` refuses and the stub is locked). Log: `[emu] the debugger halt and the GDB stub are off while the emulation thread is on (the Tools windows work)`. `liftFeatureLimits()`: `debug.setHaltAvailable(true);` (no `announceDebug` line), log `[emu] the debugger halt and the GDB stub are back on (the emulation thread did not start)`. Update the comment above `limitFeaturesForEmuThread` (:528-532) and `app.h:335`.
  5. `run()` :2021: `else if (openDebugWindow(debug, a.showWindow)) {}` (drop `!emuThreadThisRun_ &&`).
  6. Break/Step with the halt gated: `debug_ui.cpp:61` `MenuItem("Break", "F9", false, on && d.haltAvailable())` with a hover tooltip `d.haltDisabledReason()` when greyed for that reason; `app.cpp` F9 (:1655-1656) and `desktop.cpp` `debugBreakContinue()` (:149-152): `else if (debug.enabled()) { if (debug.haltAvailable()) debug.breakNow(); else toast(debug.haltDisabledReason(), 3.0); }` (desktop.cpp: `a.toast(...)`). The gate itself is inside `breakNow()`/`attach()`; this is only the explanation.
  7. `ui.cpp:262-266` hint: replace the sentence "While it is on, this version turns off the Tools (debugger) menu and the GDB stub; they come back in a later update." with "While it is on, Break, Step and breakpoints and the GDB stub are off for now; the Tools windows work."
  8. Remove `DebugSession::iface()`, `setThreadBlocked()`, `threadBlocked()` and `threadBlocked_` (and the `threadBlocked_` term in `enabled()` and the first line of `disabledReason()`). Switch the tests that called `dbg.iface()` to `dbg.core().iface()` (the files and lines under **Files**). Then prove nothing is left:

     Run: `/usr/bin/grep -rn "\.iface()\|->iface()\|setThreadBlocked\|threadBlocked" app/src app/tests | /usr/bin/grep -v "mock\.iface()\|m\.iface()\|m[0-9]*\.iface()\|\.core()\.iface()\|core_\.iface()\|c\.iface()\|big\.iface()\|small\.iface()"`
     Expected: no output.

- [ ] **Step 4: Run the whole suite.**

Run: `cmake --build build -j4 && ctest --test-dir build -j4 --output-on-failure`
Expected: all PASS, including `emu_thread_limits`, `emu_thread_start_fallback`, `gdb_locked_threaded` (unchanged), `debug_views_threaded`, and every inline `debug_*` and `smoke_*` test.

Then the sanitizer pass (CI runs it on Linux; locally it is optional on macOS, run it when `uptime` shows a load under 4):

```bash
env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVJ_APP_TSAN=ON -DVJ_APP_LTO=OFF
cmake --build build-tsan -j4
TSAN_OPTIONS="halt_on_error=1 abort_on_error=0 second_deadlock_stack=1 suppressions=app/tests/tsan.supp" ctest --test-dir build-tsan -C RelWithDebInfo -L emu_thread --output-on-failure
```

Expected: PASS, no report. `app/tests/tsan.supp` is unchanged: a report is fixed by publishing the value (an atomic or a view buffer), never by a suppression.

- [ ] **Step 5: Docs.**
  - `app/README.md` `## Emulation thread` (:548-552): replace "While it is on, this version switches off the Tools (debugger) windows and the GDB stub option; they move to the emulation thread in the next phase." with "While it is on, every Tools window works (their data is copied by the emulation thread after each frame, and their edits run on it), but Break, Step, breakpoints and the GDB stub option are off until the next phase." Keep the RetroAchievements / netplay / spinner / owner-violation sentences. Layout table: add rows `src/debug/debug_core.*` (the emulation-thread half of the debug session: the interface wrappers and, in the next phase, the halt) and `src/debug/debug_views.*` (the per-window requests the emulation thread fills).
  - `docs/app-debug-tools.md`: Pieces table gains `app/src/debug/debug_core.{h,cpp}` and `debug_views.{h,cpp}`; "Halt model" intro: "All calls are made on the thread that runs `retro_run()`" becomes "... which is the UI thread inline and the emulation thread when Settings > Interface > Emulation thread is on"; the last bullet of "How the windows plug in" ("All core calls happen on the main thread ...") becomes a new subsection **Threading**: periodic readers set a `DebugViews` request and read the buffer the emulation thread filled after the last frame (stale across a content switch by epoch); one-shot actions (poke, register edit, arm, log enable, pack reload, attach) go through `EmuThread::debugCall` and wait at most one frame; with the thread on, Break, Step and breakpoints are greyed ("not available ... this version") and the GDB stub stays locked. Testing section: add `debug_views_unit`, `debug_views_threaded`.
  - `docs/agent/testing.md` ("App emulation thread tests (#967)"): add a **Phase 3a** bullet list: `debug_views_unit` (requests, buffers, epoch; concurrent set/fill/read under TSan), the lane cases in `emu_commands_unit` / `emu_thread_unit`, `debug_views_threaded` (the REAL core: views equal a `debugCall` peek, a paused request fills with no frame, the first read after a reload is the new ROM, inline == threaded), and the reworded `emu_thread_limits` / `emu_thread_start_fallback`.
  - `docs/app-emulation-thread-design.md`: a dated (2026-10-20) amendment under section 5 recording `DebugViews`' contract (requests keyed by id, filled by `fill(iface, serial)` after each frame, a changed request or a content switch invalidates its buffer) and `Lane::Debug`.

- [ ] **Step 6: Commit**

```bash
git add app/src app/tests app/cmake docs/app-debug-tools.md docs/agent/testing.md app/README.md docs/app-emulation-thread-design.md
git commit -F - <<'EOF'
feat(app): the Tools windows work with the emulation thread on (#967)

The App fills the debug views after each emulated frame (and on the UI thread
inline), wires DebugSession to the debug lane, hands the core's interface
over again and narrows the thread gate to "halt not available": Break, Step,
breakpoints and the GDB stub stay off until phase 3b. --show-window opens a
tool threaded. DebugSession::iface()/setThreadBlocked() are gone.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 7: Studio checks.** Windowed runs happen on the Studio only; check `uptime` on it first (`bash $VJR run emu-p3a uptime`) and wait while its load is above 2.

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p3a HEAD
bash $VJR run emu-p3a env -u DEVELOPER_DIR bash -lc 'uptime; cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build app/build -j14 >/dev/null && ctest --test-dir app/build -C Release -E shaders_gl -j6 2>&1 | tail -5'
```

Expected: the full suite passes on the Studio.

Then, in one `vjr.sh run` (the Studio is a login-less host; `VJ_WINDOWED_OK=1` is the guard the script requires):

```bash
bash $VJR run emu-p3a env -u DEVELOPER_DIR VJ_WINDOWED_OK=1 bash -lc '
  out=$HOME/vj-remote/out; mkdir -p $out
  bash app/tests/emu_thread_windowed_check.sh app/build/virtualjaguar test/roms/yarc.j64 $out/emu-p3a-windowed
  for w in memory palette scanner objects blitter textures scope; do
    app/build/virtualjaguar --emu-thread on --show-window $w --perf-frames 300 test/roms/yarc.j64 >$out/emu-p3a-$w.log 2>&1
    grep -c "owner violation" $out/emu-p3a-$w.log
  done
  app/build/virtualjaguar --emu-thread on --show-window memory --screenshot $out/emu-p3a-memory-on.png --screenshot-frame 240 test/roms/yarc.j64 >$out/emu-p3a-memory-shot.log 2>&1
  app/build/virtualjaguar --emu-thread off --show-window memory --screenshot $out/emu-p3a-memory-off.png --screenshot-frame 240 test/roms/yarc.j64 >>$out/emu-p3a-memory-shot.log 2>&1
'
bash $VJR pull emu-p3a
```

Expected: `emu_thread_windowed_check: OK`; every per-window grep count is `0`; `~/Workspace/Provenance/vj-eval/remote/emu-p3a/emu-p3a-memory-on.png` shows hex bytes in the Memory window (open it with the Read tool and look: not blank, not "no data"), like the `-off.png`.

TSan windowed pass (a free-running window is the only thing that runs the UI reading buffers against a live emulation thread): configure `-DVJ_APP_TSAN=ON -DVJ_APP_LTO=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo` in `app/build-tsan` on the Studio and run `--emu-thread on --show-window memory --perf-frames 600 --perf-input test/roms/yarc.j64` with `TSAN_OPTIONS="halt_on_error=1 abort_on_error=0 suppressions=app/tests/tsan.supp"`. Expected: no `ThreadSanitizer` line. Any report is fixed by routing the reader through a published value (task A2/A3 types), never a suppression.

Evenness with the Memory window open (the copier adds per-frame cost; report it, do not tune it away). A/B/B/A on yarc.j64 at 60 Hz, 3600 UI frames each, via `even-run.sh` (which gates on load, wakes the display and kills hangs):

```bash
bash $VJR run emu-p3a env -u DEVELOPER_DIR ROMS=test/roms/yarc.j64 bash -lc '
  bash ~/vj-remote/even-run.sh emu-p3a-A1 latch
  EXTRA="--show-window memory" bash ~/vj-remote/even-run.sh emu-p3a-B1 latch
  EXTRA="--show-window memory" bash ~/vj-remote/even-run.sh emu-p3a-B2 latch
  bash ~/vj-remote/even-run.sh emu-p3a-A2 latch'
mkdir -p ~/Workspace/Provenance/vj-eval/remote/emu-p3a/even
scp -q 'studio-runner:vj-remote/out/even-emu-p3a-*' ~/Workspace/Provenance/vj-eval/remote/emu-p3a/even/
for r in A1 B1 B2 A2; do python3 app/tests/perf_trace_summary.py ~/Workspace/Provenance/vj-eval/remote/emu-p3a/even/even-emu-p3a-$r-yarc_j64-1-latch; done
```

(A = threaded, no window; B = threaded, Memory window open. Summarise each pair with `perf_trace_summary.py`: shown repeats and skips per minute, exact event -> present, and the emulation thread's frame cost, end minus start, from `-emu.csv`.) Expected: B within the A run-to-run spread on repeats + skips per minute, and a frame-cost increase under 0.2 ms. A larger cost or worse evenness goes into the PR body as a finding with its numbers.

- [ ] **Step 8: Open the PR.** Write the body to `~/Workspace/Provenance/vj-eval/remote/emu-p3a/pr-body.md`, filling every `<...>` from Step 7.

```markdown
Refs #967 (phase 3a of 5: docs/app-emulation-thread-plan-phase3.md, design: docs/app-emulation-thread-design.md)

With Settings > Interface > Emulation thread on, every Tools window now works: Memory, Palette, Cheat scanner, CPU
debugger (registers and disassembly), Object list, Blitter log, Textures and the Audio scope. Break, Step,
breakpoints and the GDB stub stay off (they are greyed with a reason; phase 3b). Still off by default in every build.

- `Lane::Debug` in the command queue and `EmuThread::debugCall` / `debugPost`: one-shot actions (poke, register
  edit, arm, log enable, pack reload) run on the emulation thread.
- `DebugViews`: each window sets a request (memory range, registers, disassembly window, OP snapshot, blit-log
  tail, the scanner's 2 MiB) and reads the buffer the emulation thread fills after each frame. Inline, the same
  copier runs on the same thread. A changed request or a content switch invalidates its buffer.
- `DebugCore` owns the interface; `DebugSession` marshals to it. `--show-window <tool>` works threaded.

Tests: `debug_views_unit`, lane cases in `emu_commands_unit` / `emu_thread_unit`, `debug_views_threaded` (the real
core), `emu_thread_limits` rewritten; the full suite and the ThreadSanitizer label pass.

Studio (M4 Max, 60 Hz): windowed check OK; each of the seven tools opened threaded with no `owner violation`; the
Memory window shows live bytes (screenshot attached); TSan windowed pass clean. Evenness A/B/B/A on yarc with the
Memory window open: A <repeats>+<skips>/min, B <repeats>+<skips>/min; emulation frame cost A <x> ms, B <y> ms.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

```bash
git push libretro HEAD:refs/heads/feat/967-phase3a-debug-views
gh pr create --repo libretro/virtualjaguar-libretro --base develop --head feat/967-phase3a-debug-views \
  --title "feat(app): debug views on the emulation thread (#967 phase 3a)" \
  --body-file ~/Workspace/Provenance/vj-eval/remote/emu-p3a/pr-body.md
```

Expected: the PR URL. Do not merge; 3b and 3c branch after it merges. Remove the worktree (`git worktree remove ~/Workspace/Provenance/vj-wt/emu-p3a`) and the Studio's (`bash $VJR drop emu-p3a`) when the PR merges.

---

## Sub-phase 3b: the halt and the GDB stub on the emulation thread

Branch `feat/967-emu-thread-phase3b` off `libretro/develop` after 3a merges, worktree `~/Workspace/Provenance/vj-wt/emu-p3b`
(`git -C ~/Workspace/Provenance/virtualjaguar-libretro fetch libretro && git worktree add ~/Workspace/Provenance/vj-wt/emu-p3b -b feat/967-emu-thread-phase3b libretro/develop`).
Assumes 3a's interfaces as frozen in the header: `Lane::Debug`, `debugCall`/`debugPost`, `DebugViews`, `DebugCore` with the iface wrappers, `DebugSession::coreCall`/`corePost`/`setHaltAvailable`, `App::fillDebugViews`.
The PR leaves off Neuro (3c) and the nightly default (3d). The setting stays Off by default; `haltedFrame()` survives for inline mode.
Tasks: B1 bounded pump + `HaltState` + verdict (mock tests); B2 App wiring (`waitVerdict`, `syncHalt`, `quitting_`, shutdown detach, F9/F10 through the mirror, "by gdb" bar); B3 `call()` give-up + call-site policy; B4 gdb unlock, silent host, hardcore lock; B5 `debug_e2e_threaded`, `gdb_threaded_halt`, the limit tests rewritten; B6 docs, Studio checks, PR.

Build and test commands throughout: `env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release` once, then `cmake --build build -j4 --target <t> && ctest --test-dir build -R <regex> --output-on-failure`. Check `uptime` first; no builds while the load is above 10.

### Task B1: `HaltState`, the verdict atomic and the bounded `pump()`

**Files:**
- Modify: `app/src/debug/debug_core.h` / `.cpp` (A3's file: add the "the halt (B1)" block of the Interfaces section plus `frameEnded()`), `app/src/emu_thread.h` / `.cpp` (`serveDebugLane`, emu_thread.h:95-117 area and the loop at emu_thread.cpp:153-205), `app/src/debug/debug_session.h` / `.cpp` (`serial_` atomic; `resume`/`step`/`detachAndResume` write `core().verdict`; `detach()` while halted writes DETACH; the old `pump`/`queued_`/`halted_` fields go)
- Modify: `app/tests/debug_e2e.cpp` (`dbg.runUiFrame` / `dbg.onHalt` / `dbg.onResume` / `dbg.abortHalt` become `dbg.core().runUiFrame` / `core().onHalt` / `core().onResume` / `core().abortHalt`; every `runUiFrame` body starts with `dbg.syncHalt()` once B2 lands it, until then `halted()` reads the core directly; the counts at :137-141 hold), `app/src/debug/cpu_ui.cpp` (`registerCpuTools`' `onHalt` hook moves to `s.core().onHalt` for this commit: halts are impossible threaded until B4, so the emulation-thread callback touching `g.t` is safe here; B2 moves it to `onHaltUi`)
- Test: `app/tests/debug_session_test.cpp` (new sections against `DebugCore` + the mock), `app/tests/emu_thread_test.cpp` (`serveDebugLane`), `app/cmake/VJDebugTests.cmake` (`vj_debug_session_test` links `${VJ_DEBUG_SESSION_SRCS}`, gets `LABELS emu_thread`), `app/tests/debug_mock.h` (no change: `simulateHalt(..., maxPumps, &pumps)` already loops on NONE)

**Interfaces:**
- Consumes: `MockDebugIface::simulateHalt` (debug_mock.h:131), `EmuCommands::waitUntil`/`drain(Lane::Debug)`/`pending` (A1), `DebugSession::coreCall`/`corePost` (A3).
- Produces (verbatim from the header's `debug_core.h` block): `struct HaltState`, `DebugCore::kPumpWait`, `halt()`, `haltSerial()`, `verdict`, `notifyVerdict`, `waitVerdict`, `runUiFrame`, `abortHalt`, `onHalt`, `onResume`, `int pump(int target, unsigned pc)`, `static int haltPumpThunk(void*, int, unsigned)`; plus change request 1: `void frameEnded();`. `size_t EmuThread::serveDebugLane(std::chrono::milliseconds maxWait)`.
- `pump()` contract (header, with requests 2-3 applied): new-halt test `!st.halted || st.target != target || st.pc != pc`; on a new halt: record target/pc, `reason = iface->halt_reason()` when the iface's `size` covers it (as debug_session.cpp:129-131) else `VJ_DBG_STOP_BREAKPOINT`, `serial++`, fire `onHalt(target, pc, reason)`. Then: `if (abortHalt && abortHalt()) v = DETACH; else v = verdict.exchange(NONE); if (v == NONE) { if (waitVerdict) waitVerdict(); else if (runUiFrame) runUiFrame(); v = verdict.exchange(NONE); if (v == NONE && abortHalt && abortHalt()) v = DETACH; }`. `v == NONE` -> return NONE with the state untouched. Otherwise `halted = false`, `target = -1`, `reason = NONE` (pc kept for the bar's last value); DETACH -> `attached_ = false`; CONTINUE/DETACH -> `serial++`, fire `onResume`; return `v`. The mutex guards `HaltState`; `serial` is also mirrored in an `std::atomic<uint64_t>` for `haltSerial()`.
- `serveDebugLane(maxWait)`: inline (`!threaded()`) -> `cmds_.drain(onError, Lane::Debug)` and return. Threaded: `n = drain(Debug)`; if `n == 0`: `cmds_.waitUntil(now + maxWait)` then `n = drain(Debug)`; return `n`. Must be called on the emulation thread (assert `onEmuThread()` in debug builds; a wrong-thread call returns 0).

- [ ] **Step 1: Write the failing tests.** In `app/tests/debug_session_test.cpp`, add `#include "debug_core.h"` and, before `main`, these sections (the file's `CHECK` macro; `MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());` opens each unless said otherwise):

```cpp
// B1: the pump is bounded.  With nothing to say it calls waitVerdict once and returns NONE; the halt stays.
static void testBoundedPumpReturnsNone() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int waits = 0, halts = 0, resumes = 0;
    c.waitVerdict = [&] { waits++; };
    c.onHalt.push_back([&](int, unsigned, int) { halts++; });
    c.onResume.push_back([&] { resumes++; });
    const uint64_t s0 = c.haltSerial();
    CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_NONE);
    CHECK(waits == 1 && halts == 1 && resumes == 0);
    CHECK(c.halt().halted && c.halt().target == VJ_DBG_TARGET_M68K && c.halt().pc == 0x4000);
    CHECK(c.haltSerial() == s0 + 1);
    CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_NONE);   // the same halt: no second onHalt
    CHECK(waits == 2 && halts == 1 && c.haltSerial() == s0 + 1);
    c.verdict.store(VJ_DBG_RESUME_CONTINUE);
    CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_CONTINUE);
    CHECK(waits == 2);                      // a pending verdict is taken before any wait
    CHECK(!c.halt().halted && resumes == 1 && c.haltSerial() == s0 + 2);
    CHECK(c.verdict.load() == VJ_DBG_RESUME_NONE);
}

// Review Focus 6: a detach verdict ends a bounded pump within one wait, through the mock's GDBHalt loop.
static void testDetachEndsPumpWithinOneWait() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int waits = 0;
    c.waitVerdict = [&] {                    // what App::haltWait does when the UI writes DETACH during the wait
        if (++waits == 2) c.verdict.store(VJ_DBG_RESUME_DETACH);
    };
    int pumps = 0;
    const int v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 100, &pumps);
    CHECK(v == VJ_DBG_RESUME_DETACH);
    CHECK(pumps == 2 && waits == 2);         // pump 1: wait, NONE; pump 2: the verdict written inside its wait is taken
    CHECK(!c.attached() && !m.attached() && !c.halt().halted);
}

// Request 2: the serial moves on every entry (a step's re-halt too) and on CONTINUE / DETACH, not on the STEP return.
static void testStepSerialAndReason() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int halts = 0, resumes = 0, lastReason = -1;
    c.onHalt.push_back([&](int, unsigned, int r) { halts++; lastReason = r; });
    c.onResume.push_back([&] { resumes++; });
    c.waitVerdict = [&] { c.verdict.store(VJ_DBG_RESUME_STEP); };
    const uint64_t s0 = c.haltSerial();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_STEP);
    CHECK(halts == 1 && resumes == 0 && c.haltSerial() == s0 + 1 && !c.halt().halted);
    c.waitVerdict = [&] { c.verdict.store(VJ_DBG_RESUME_CONTINUE); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP) == VJ_DBG_RESUME_CONTINUE);
    CHECK(halts == 2 && lastReason == VJ_DBG_STOP_STEP && resumes == 1 && c.haltSerial() == s0 + 3);
}

// Request 1: gdb continued (or the timeout fired): GDBHalt breaks without a verdict of ours.  frameEnded() ends the
// halt on our side and drops a verdict written for it meanwhile.
static void testFrameEndedClosesForeignResume() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int resumes = 0;
    c.onResume.push_back([&] { resumes++; });
    c.waitVerdict = [] {};
    int pumps = 0;
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 1, &pumps) == VJ_DBG_RESUME_NONE);
    CHECK(pumps == 1 && c.halt().halted);
    const uint64_t s1 = c.haltSerial();
    c.verdict.store(VJ_DBG_RESUME_CONTINUE);   // the UI pressed F9 against a halt gdb already ended
    c.frameEnded();
    CHECK(!c.halt().halted && resumes == 1 && c.haltSerial() == s1 + 1);
    CHECK(c.verdict.load() == VJ_DBG_RESUME_NONE);
    c.frameEnded();                            // not halted: a no-op
    CHECK(resumes == 1 && c.haltSerial() == s1 + 1);
    CHECK(c.attached());                       // a foreign continue is not a detach
}

// Request 3: a gdb step re-halts at a new pc inside the same frame: a new halt (pc, onHalt, serial).
static void testForeignStepIsNewHalt() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int halts = 0;
    c.onHalt.push_back([&](int, unsigned, int) { halts++; });
    c.waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);
    const uint64_t s1 = c.haltSerial();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP, 1) == VJ_DBG_RESUME_NONE);
    CHECK(halts == 2 && c.halt().pc == 0x4002 && c.halt().reason == VJ_DBG_STOP_STEP && c.haltSerial() == s1 + 1);
}

// Quit mid-halt: abortHalt wins over a pending CONTINUE and detaches the mock.
static void testAbortHaltDetaches() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    bool quitting = false;
    c.abortHalt = [&] { return quitting; };
    c.waitVerdict = [&] { quitting = true; c.verdict.store(VJ_DBG_RESUME_CONTINUE); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_DETACH);
    CHECK(!c.attached() && !m.attached() && !c.halt().halted);
}

// Inline: no waitVerdict; the pump runs runUiFrame once per call and takes the verdict that frame set (today's loop).
static void testInlinePumpRunsUiFrame() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int ui = 0;
    c.runUiFrame = [&] { if (++ui == 3) c.verdict.store(VJ_DBG_RESUME_CONTINUE); };
    int pumps = 0;
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 100, &pumps) == VJ_DBG_RESUME_CONTINUE);
    CHECK(ui == 3 && pumps == 3);
}

// The pump never waits longer than waitVerdict does: ten NONE pumps with a 4 ms wait take under 100 ms.
static void testPumpNeverOutwaitsHook() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    c.waitVerdict = [] { std::this_thread::sleep_for(DebugCore::kPumpWait); };
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; i++) CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_NONE);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(100));
}

// The session's verdict methods write core().verdict and call notifyVerdict; outside a halt they do nothing.
static void testSessionVerdictsReachCore() {
    DebugSession s; MockDebugIface m; s.core().setIface(m.iface()); s.setContentLoaded(true);
    int notifies = 0;
    s.core().notifyVerdict = [&] { notifies++; };
    s.resume();
    CHECK(s.core().verdict.load() == VJ_DBG_RESUME_NONE && notifies == 0);
    s.core().waitVerdict = [&] { s.syncHalt(); s.step(); };   // the UI thread, one frame later
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_STEP);
    CHECK(notifies == 1);
    s.core().waitVerdict = [&] { s.syncHalt(); s.detach(); };   // Detach while halted = the DETACH verdict
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP) == VJ_DBG_RESUME_DETACH);
    CHECK(notifies == 2 && !m.attached());
}
```

(`#include <chrono>` / `<thread>` at the top.) Register all nine in `main` after `testHaltResume`. Rewrite `testHaltResume` and `testHardcoreWhileHalted` for the split: `s.runUiFrame` -> `s.core().runUiFrame`, `s.onHalt/onResume` -> `s.core().onHalt/onResume`, `s.abortHalt` -> `s.core().abortHalt`, and each `runUiFrame` body starts with `s.syncHalt();` (the mirror is what `s.halted()` reads); `s.resumeQueued()` -> `s.core().verdict.load() != VJ_DBG_RESUME_NONE`. The checks keep their values (`pumps == 3`, `halts == 2 && resumes == 1`, DETACH on quit, `why == "hardcore"`).

In `app/tests/emu_thread_test.cpp`, before `main`:

```cpp
// B1: the halt pump's lane service.  Threaded, it drains the debug lane, waits at most maxWait for a post, drains
// again; inline it drains and returns at once.
void testServeDebugLane() {
    EmuThread e;
    FakeClient c;
    c.block = true;   // the "core" is parked inside a frame, as it is during a halt
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && !c.blocked.load(); i++) sleepMs(1);
    std::atomic<int> ran{0};
    std::atomic<size_t> served{0};
    std::atomic<double> waited{0};
    // Run serveDebugLane where the frame runs: inside the blocked frame, through a second client hook is not
    // available, so call it from the emulation thread via a debug post is circular.  Instead FakeClient::runFrame
    // calls `onBlocked` while blocked: set it to serve the lane once.
    c.onBlocked = [&] {
        const auto t0 = Clock::now();
        served = e.serveDebugLane(std::chrono::milliseconds(4));
        waited = since(t0);
    };
    sleepMs(30);
    CHECK(served.load() == 0, "nothing queued: 0 commands (%zu)", served.load());
    CHECK(waited.load() >= 0.003 && waited.load() < 0.05, "and it waited about maxWait (%.1f ms)", waited.load() * 1e3);
    c.onBlocked = [&] {
        const auto t0 = Clock::now();
        served = e.serveDebugLane(std::chrono::milliseconds(4));
        waited = since(t0);
    };
    e.debugPost([&] { ran++; });
    sleepMs(30);
    CHECK(ran.load() == 1 && served.load() == 1, "a debug post is run by the lane service (%d, %zu)", ran.load(), served.load());
    c.block = false;
    CHECK(e.stop(), "stop");
    EmuThread in;
    FakeClient ic;
    CHECK(in.start(EmuThread::Mode::Inline, ic), "inline start");
    const auto t0 = Clock::now();
    CHECK(in.serveDebugLane(std::chrono::milliseconds(50)) == 0 && since(t0) < 0.01, "inline: returns at once");
}
```

Add to `FakeClient`: `std::function<void()> onBlocked;` and in `runFrame()`'s `while (block.load())` body, after `blocked = true;`: `if (onBlocked) { auto f = onBlocked; onBlocked = nullptr; f(); }`. Register `testServeDebugLane` in `main`.

In `app/cmake/VJDebugTests.cmake`: `vj_debug_session_test` sources gain `src/debug/debug_core.cpp` (if A3 did not add it) and its `set_tests_properties` gains `LABELS emu_thread` (the mirror tests run two code paths the TSan job should see).

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build build -j4 --target vj_debug_session_test vj_emu_thread_test 2>&1 | tail -3`
Expected: compile errors (`HaltState`, `pump`, `frameEnded`, `serveDebugLane`, `onBlocked`).

- [ ] **Step 3: Implement.**
  - `debug_core.h/.cpp`: the header's "the halt (B1)" block verbatim plus `void frameEnded();`; private `mutable std::mutex haltMu_; HaltState halt_; std::atomic<uint64_t> serial_{0};`. `DebugCore()` keeps A3's `host_` fill and sets `host_.halt_pump = &haltPumpThunk`. `setIface()` drops the halt (state cleared under the mutex, no callbacks, no serial bump) as well as `attached_`. `pump()` per the contract above; `frameEnded()` per request 1 (fires `onResume` outside the mutex; copies of the callback vectors are walked, as debug_session.cpp:133 does).
  - `emu_thread.h/.cpp`: `serveDebugLane` per the Interfaces bullet.
  - `debug_session.h/.cpp`: remove `pump`, `haltPumpThunk`, `queued_`, `halted_`, `haltedTarget_`, `haltedPc_`, `haltReason_`, `runUiFrame`, `abortHalt`, `onHalt`, `onResume`, `resumeQueued` (header "Removed from this class"). `resume()` = `if (halted() && core().haltSerial() == haltSerial()) { core().verdict.store(VJ_DBG_RESUME_CONTINUE); if (core().notifyVerdict) core().notifyVerdict(); }`, `step()` the same with STEP plus `userHaltPending_ = true`, `detachAndResume()` with DETACH. `detach()` = `halted() ? detachAndResume() : corePost([this]{ core().detach(); })` then `attachedMirror_ = false` (the mirror of `attached()` is refreshed by `syncHalt()` from `core().attached()`; B2 adds `syncHalt`, so for B1 `halted()`/`attached()` read `core().halt().halted` / `core().attached()` directly and B2 swaps in the mirror). `serial_` becomes `std::atomic<uint64_t>` (request 5).

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j4 --target vj_debug_session_test vj_emu_thread_test && ctest --test-dir build -R "debug_session_unit|emu_thread_unit" --output-on-failure`
Expected: both pass; `debug_session_test: N checks, 0 failed`. Then `ctest --test-dir build -R "debug_" --output-on-failure`: every 3a debug test still green (inline unchanged).

- [ ] **Step 5: Commit**

```bash
git add app/src/debug/debug_core.h app/src/debug/debug_core.cpp app/src/debug/debug_session.h app/src/debug/debug_session.cpp \
        app/src/emu_thread.h app/src/emu_thread.cpp app/tests/debug_session_test.cpp app/tests/emu_thread_test.cpp \
        app/cmake/VJDebugTests.cmake
git commit -F - <<'EOF'
feat(app): bounded halt pump on DebugCore with an atomic verdict (#967)

DebugCore::pump serves the debug lane, waits at most kPumpWait (4 ms) and returns VJ_DBG_RESUME_NONE to GDBHalt's own
loop, which keeps the socket service and the halt timeout.  The UI writes the verdict atomically; HaltState carries a
serial the UI mirrors.  frameEnded() closes a halt the core left on gdb's say-so, so the app never stays "halted"
after a foreign continue.  Inline keeps the nested UI frame.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task B2: App wiring: `waitVerdict`, `syncHalt`, `quitting_`, shutdown detach, F9/F10 through the mirror

**Files:**
- Modify: `app/src/debug/debug_session.h` / `.cpp` (`syncHalt`, the mirror fields, `onHaltUi`/`onResumeUi`, `haltByGdb`, `haltSerial`, `userHaltPending_`, `setHardcore` through the lane), `app/src/app.h` (`haltWait`, `quitting_`, `quitRequested`, remove `haltedNow`'s comment about inFrame), `app/src/app.cpp` (`wireHost` 497-515: hooks move to `debug.core()`; `syncDebug` 522-526: `+ debug.syncHalt()`; `startEmuThread` 592-611: `debug.core().waitVerdict = [this]{ haltWait(); }`; `liftFeatureLimits` 547-554: clears it; `shutdown` 412-425 and `stopEmuThread` 633-641: detach first; `emuRunOneFrame` 654 and `stepEmulation` 1833: `debug.core().frameEnded()` after `host.runFrame()`; `haltedFrame` 915-942: the guard becomes `!imguiUp_ || !window_ || threaded()`; F9 1654-1657 and F10 1700-1703 unchanged in text, they already go through the mirror), `app/src/debug/debug_ui.cpp` (halt bar: "HALTED by gdb" when `d.haltByGdb()`, the "applies after Continue" line is B3's), `app/src/desktop.cpp:149-158` (unchanged in text; verified to compile against the mirror)
- Modify: `app/src/debug/cpu_ui.cpp` (`registerCpuTools`: the `t.prev = t.regs; s.requestRefresh()` hook moves from `s.core().onHalt` (B1) to `s.onHaltUi`, the UI thread)
- Test: `app/tests/debug_session_test.cpp` (mirror sections)

**Interfaces:**
- Consumes: B1's `DebugCore`; `EmuThread::serveDebugLane`, `debugPost`; `DebugViews::pending`; `App::fillDebugViews` (A9).
- Produces: header's `DebugSession` B2 members: `void syncHalt();`, `bool halted() const;`, `int haltedTarget() const; unsigned haltedPc() const; int haltReason() const;`, `onHaltUi`, `onResumeUi`; request 4: `bool haltByGdb() const;`, `uint64_t haltSerial() const;`. `App`: `void haltWait();` (emulation thread: `emu.serveDebugLane(DebugCore::kPumpWait); if (debug.views().pending(debug.refreshSerial())) fillDebugViews();`), `std::atomic<bool> quitting_{false};`, `bool quitRequested() const;` (B2: `quit`; B5 adds the signal).
- `syncHalt()`: `HaltState h = core().halt(); attachedMirror_ = core().attached(); if (h.serial != mirror_.serial) { const bool was = mirror_.halted; mirror_ = h; if (h.halted) { byGdb_ = h.reason == VJ_DBG_STOP_BREAKPOINT ? !hasBreakpoint(h.target, h.pc) : !userHaltPending_; userHaltPending_ = false; requestRefresh(); fire onHaltUi(h.target, h.pc, h.reason); } else if (was) fire onResumeUi; } else mirror_ = h;`. A serial that moved by two or more between two UI frames (halt and resume inside one UI frame, or step + re-halt) fires the edge for the final state only: `onHaltUi` if now halted, `onResumeUi` if it was halted and is not, nothing if neither.
- Hardcore: `setHardcore(true)` = `detach()` (B1's: DETACH verdict when halted, else a lane post) + `closeAllTools("hardcore")`, unchanged in shape; it no longer touches the iface on the UI thread.

- [ ] **Step 1: Write the failing tests.** In `app/tests/debug_session_test.cpp`, before `main`:

```cpp
// B2: the UI mirror.  syncHalt() fires onHaltUi once per halt serial and onResumeUi once per resume; a step's re-halt
// fires onHaltUi again with reason STEP; a halt that came and went between two UI frames fires nothing.
static void testSyncHaltEdges() {
    DebugSession s; MockDebugIface m; s.core().setIface(m.iface()); s.setContentLoaded(true);
    int haltUi = 0, resumeUi = 0, lastReason = -1;
    s.onHaltUi.push_back([&](int, unsigned, int r) { haltUi++; lastReason = r; });
    s.onResumeUi.push_back([&] { resumeUi++; });
    int pumps = 0;
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1, &pumps) == VJ_DBG_RESUME_NONE);
    CHECK(!s.halted());                        // not synced yet
    s.syncHalt();
    CHECK(s.halted() && s.haltedPc() == 0x4000 && haltUi == 1 && lastReason == VJ_DBG_STOP_USER);
    s.syncHalt();
    CHECK(haltUi == 1);                        // the same serial: no second edge
    s.core().verdict.store(VJ_DBG_RESUME_STEP);
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.halted() && s.haltedPc() == 0x4002 && haltUi == 2 && lastReason == VJ_DBG_STOP_STEP && resumeUi == 0);
    s.core().verdict.store(VJ_DBG_RESUME_CONTINUE);
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP) == VJ_DBG_RESUME_CONTINUE);
    s.syncHalt();
    CHECK(!s.halted() && resumeUi == 1 && haltUi == 2);
    // Halt and continue inside one UI frame: the serial moved by two, the state is "not halted": no edge.
    s.core().verdict.store(VJ_DBG_RESUME_CONTINUE);
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_CONTINUE);
    s.syncHalt();
    CHECK(haltUi == 2 && resumeUi == 1 && !s.halted());
}

// Request 4: "by gdb" = a breakpoint halt at an address not in our list, or a USER / STEP halt we did not ask for.
static void testHaltByGdb() {
    DebugSession s; MockDebugIface m; s.core().setIface(m.iface()); s.setContentLoaded(true);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.halted() && !s.haltByGdb());
    s.core().frameEnded();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5000, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.haltByGdb());                      // not our breakpoint
    s.core().frameEnded();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5002, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.haltByGdb());                      // a halt_next we did not issue (gdb_wait, or gdb's own interrupt)
    s.core().frameEnded();
    s.breakNow();                              // ours: userHaltPending_
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5004, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(!s.haltByGdb());
}

// Request 4: a verdict for a halt that already ended is dropped (the mirror lags the core by one UI frame).
static void testStaleVerdictDropped() {
    DebugSession s; MockDebugIface m; s.core().setIface(m.iface()); s.setContentLoaded(true);
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.halted());
    s.core().frameEnded();                     // gdb continued; the UI has not synced yet
    s.resume();                                // F9 against the stale mirror
    CHECK(s.core().verdict.load() == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(!s.halted());
}

// Hardcore from the UI while halted: the DETACH verdict (never iface->detach on the UI thread); while running: a lane
// post.  Inline (coreCall / corePost empty) the post runs now.
static void testHardcoreGoesThroughTheLane() {
    DebugSession s; MockDebugIface m; s.core().setIface(m.iface()); s.setContentLoaded(true);
    CHECK(s.attach());
    int posts = 0;
    s.corePost = [&](std::function<void()> fn) { posts++; fn(); };
    s.setHardcore(true);
    CHECK(posts == 1 && !m.attached() && !s.enabled());
    s.setHardcore(false);
    CHECK(s.attach());
    s.core().waitVerdict = [&] { s.syncHalt(); s.setHardcore(true); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_DETACH);
    CHECK(posts == 1 && !m.attached());        // the verdict, not a second post
}
```

Register the four in `main`.

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build build -j4 --target vj_debug_session_test 2>&1 | tail -3`
Expected: compile errors (`syncHalt`, `onHaltUi`, `haltByGdb`).

- [ ] **Step 3: Implement.**
  - `debug_session.h/.cpp`: the mirror (`HaltState mirror_; bool attachedMirror_ = false; bool byGdb_ = false; bool userHaltPending_ = false;`), `syncHalt()` per the Interfaces bullet, `halted()`/`haltedTarget()`/`haltedPc()`/`haltReason()`/`haltSerial()`/`haltByGdb()`/`attached()` read the mirror. `breakNow()` sets `userHaltPending_ = true` before the `haltNext` post. `setIface(nullptr)` and `setContentLoaded(false)` reset the mirror (no edge callbacks).
  - `app.cpp` `wireHost()`: `debug.core().abortHalt = [this]{ return quitting_.load(); };` `debug.core().runUiFrame = [this]{ haltedFrame(); };` `debug.core().onHalt.push_back([this](int, unsigned, int){ audio.setPaused(true); })` (emulation side: atomic), `debug.core().onResume.push_back([this]{ audio.clear(); audio.setPaused(paused); })` (`paused` is read on the emulation thread here: use `emuPaused_`-style atomic if one exists after Phase 2's `syncEmuFlags`, else `emu.paused()`); the toast and `pacer_.reset(); lastTick_ = now();` move to `debug.onHaltUi` / `debug.onResumeUi` (UI thread), the toast text unchanged, plus `if (debug.haltByGdb())` the toast says `"Halted by gdb: %s at $%06X - F9 continues"`. One unconditional stderr line in the emulation-side `onHalt`: `[debug] halt: %s at $%06X (%s)` with the target and reason names (B5's probe greps it).
  - `syncDebug()`: `debug.syncHalt();` first, then the existing three lines.
  - `startEmuThread()`: on success `debug.core().waitVerdict = [this]{ haltWait(); };`; `liftFeatureLimits()` sets it to `nullptr`. `haltWait()` per Interfaces.
  - `shutdown()` (412-425): before the `callFor` unload: `quitting_.store(true); if (debug.halted()) debug.detachAndResume(); emu.debugPost([]{});` (the post wakes a pump parked in `waitUntil`). `stopEmuThread()` unchanged. The "it should not happen in phase 1" comment at 418-419 becomes "a detach ends a halt within one pump wait; a core wedged elsewhere still must not hang the quit".
  - `emuRunOneFrame()` after `host.runFrame()` (654): `debug.core().frameEnded();`; `stepEmulation()` after 1833 likewise.
  - `haltedFrame()`: the guard `emu.onEmuThread()` becomes `threaded()`; it detaches via `debug.detachAndResume()` as today (with `syncHalt()` first so the mirror lets it write).
  - `debug_ui.cpp:78-80`: `ImGui::TextColored(..., d.haltByGdb() ? "HALTED by gdb" : "HALTED")`.
  - `app.h`: `void haltWait(); std::atomic<bool> quitting_{false}; bool quitRequested() const { return quit; }`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j4 && ctest --test-dir build -R "debug_|emu_thread_unit|smoke_yarc" --output-on-failure`
Expected: all green, including inline `debug_e2e` (whose `runUiFrame`/`onHalt` hooks were moved to `dbg.core()` in 3a or here; the counts in debug_e2e.cpp:137-141 hold).

- [ ] **Step 5: Commit**

```bash
git add app/src/debug/debug_session.h app/src/debug/debug_session.cpp app/src/app.h app/src/app.cpp \
        app/src/debug/debug_ui.cpp app/tests/debug_session_test.cpp app/tests/debug_e2e.cpp
git commit -F - <<'EOF'
feat(app): the UI mirrors the halt; the emulation thread pumps it (#967)

DebugSession::syncHalt copies DebugCore's HaltState once per UI frame and fires onHaltUi / onResumeUi on serial edges;
F9 / F10 / the halt bar / hardcore write the verdict or post to the debug lane.  Threaded, the pump's wait is
App::haltWait (serve the lane, refill the views); quitting sets quitting_ and detaches before the 3 s join.  The halt
bar says "HALTED by gdb" for a halt the app did not ask for.  Inline keeps haltedFrame.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task B3: `call()` gives up when a halt begins; the call-site policy

**Files:**
- Modify: `app/src/emu_thread.h:84-93, 144-172` (`call(F&&, std::function<bool()> giveUp = {})`), `app/src/app.h` / `app.cpp` (`haltGiveUp()`, `detachForCall()`; call sites: `openContent` 1064-1068, `bootBios` 1195-1199, `closeContent` 1238-1243, `reset` 1264-1270, `handleDrop` 1480-1492, `saveState` 1514-1534, `loadState` 1556-1581, `autoSave`, `runHeadlessFrames` 898-907, `setBiosChoice` 1212, the SRAM flush posts stay), `app/src/ui.cpp:554,560-566,583-591` (menu gates: "Close game"/Reset/Open tray/Disc enabled while halted again; Save/Load state stay disabled with the tooltip "resume it first (F9)"; the Core options tab gets one line), `app/src/debug/op_ui.cpp:484-488` ("step one frame" refuses while halted), `app/src/core_host.cpp:838,928,1030,1042` (texts: `"the debugger has the game halted (resume it first: F9)"`), `app/src/debug/debug_ui.cpp` (halt bar line)
- Test: `app/tests/emu_thread_test.cpp`

**Interfaces:**
- Produces: `template <class F> auto EmuThread::call(F&& fn, std::function<bool()> giveUp = {}) -> decltype(fn());` (header contract: polled on the waiting thread every slice, 16-50 ms; true before the task started -> the task is marked cancelled, skipped when drained, `call()` returns `R()`; a task already running completes; never consulted inline / on the emulation thread / after the start). Implementation shape: the queued wrapper is `[task, started, cancelled] { if (cancelled->load()) return; started->store(true); (*task)(); }` with two `std::shared_ptr<std::atomic<bool>>`; the waiter's loop checks `giveUp && !started->load() && giveUp()` after each `wait_for` slice and, when true, does `cancelled->store(true)` and re-checks `started` (a task that began between the two reads is waited for: `done.get()`); otherwise returns `R()`.
- `App`: `std::function<bool()> haltGiveUp() const;` = `[this]{ return debug.core().halt().halted; }` (any-thread read); `std::function<bool()> detachForCall();` = threaded and `debug.halted()`: `const uint64_t s0 = debug.core().haltSerial(); debug.detachAndResume(); return [this, s0]{ return debug.core().halt().halted && debug.core().haltSerial() != s0; }` (gives up only on a *new* halt; the one being detached still reads halted for a few ms), else `haltGiveUp()`.
- Policy (resolved-ambiguities table): **refuse** while `debug.halted()` with the toast `"The debugger has the game halted - resume it first (F9)"`: `saveState` (quiet variants silent), `loadState`, `bootBios`, `runHeadlessFrames`/OP step one frame (`emu.advance`), `autoSave`. **Detach first** (threaded): `openContent`, `closeContent`, `reset`, `handleDrop` (through `openContent`), disc eject/index/insert (ui.cpp:585,591 and `showDiscDialog`'s insert), quit (B2). **Inline** keeps today's refusal for all of them (a call runs at once inside `retro_run`, core_host.cpp refuses with `inFrame_`): `detachForCall()` inline returns `{}` after the existing `haltedNow()` toast path, i.e. the `if (haltedNow()) { toast; return }` blocks stay for `!threaded()`. Every remaining UI-initiated `emu.call` that is not in those lists (`setBiosChoice`, `ach` coreCalls, `audio.clear` in headless) passes `haltGiveUp()` and treats `R()` as "not now".
- "Applies after Continue": `drawDebugOverlay` adds `ImGui::TextDisabled("Settings and cheats apply after Continue")` under the buttons; `ui.cpp`'s Core options tab (`drawCoreOptions`, line 361's callee) starts with the same `TextDisabled` line when `app.debug.halted()`.

- [ ] **Step 1: Write the failing tests.** In `app/tests/emu_thread_test.cpp`, before `main`:

```cpp
// Review Focus 1: a call posted an instant before the core halts.  With a give-up predicate it returns R() once the
// predicate turns true, and the cancelled task never runs; a task that already started is waited for.
void testCallGivesUp() {
    EmuThread e;
    FakeClient c;
    c.block = true;   // a frame that never ends: the halt
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && !c.blocked.load(); i++) sleepMs(1);
    std::atomic<bool> halted{false};
    std::atomic<int> polls{0};
    auto ran = std::make_shared<std::atomic<bool>>(false);
    std::thread flip([&] { sleepMs(60); halted = true; });
    const Clock::time_point t0 = Clock::now();
    const int r = e.call([ran] { *ran = true; return 42; }, [&] { polls++; return halted.load(); });
    flip.join();
    CHECK(r == 0, "the call returned R() (%d)", r);
    CHECK(since(t0) < 0.3, "within 300 ms (%.3f s)", since(t0));
    CHECK(polls.load() >= 1, "the predicate was polled (%d)", polls.load());
    c.block = false;
    sleepMs(100);                                   // the frame ends, the queue drains
    CHECK(!ran->load(), "the cancelled task never ran");
    // A task that started before the predicate turned true runs to completion and returns its value.
    halted = false;
    const int r2 = e.call([&] { sleepMs(120); halted = true; sleepMs(30); return 7; }, [&] { return halted.load(); });
    CHECK(r2 == 7, "a started task is waited for (%d)", r2);
    // Never consulted inline or on the emulation thread.
    std::atomic<int> consulted{0};
    CHECK(e.call([&] { return e.call([] { return 1; }, [&] { consulted++; return true; }); }) == 1, "on the thread: runs");
    CHECK(consulted.load() == 0, "predicate ignored on the emulation thread (%d)", consulted.load());
    CHECK(e.stop(), "stop");
    EmuThread in;
    FakeClient ic;
    CHECK(in.start(EmuThread::Mode::Inline, ic), "inline");
    CHECK(in.call([] { return 3; }, [&] { consulted++; return true; }) == 3 && consulted.load() == 0, "inline: runs, never asks");
}
```

Register it in `main`.

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build -j4 --target vj_emu_thread_test 2>&1 | tail -3`
Expected: compile error (`call` takes one argument).

- [ ] **Step 3: Implement.**
  - `emu_thread.h`: the new `call` per Interfaces (the header comment from the Interfaces section above the declaration). `callFor` unchanged.
  - `app.cpp` call sites per the policy. `saveState`: after the `isLoaded` check, `if (debug.halted()) { if (!quiet) toast("The debugger has the game halted - resume it first (F9)", 2.5, 2); return false; }` and `emu.call([&]{...}, haltGiveUp())`; on `R()` (false) with `err.empty()`: the same toast. `loadState` the same. `openContent`: replace 1065-1068 with `if (haltedNow() && !threaded()) { toast(...); return false; } auto giveUp = detachForCall();` and `emu.call([&]{...}, giveUp)`; on false with `err.empty()`: `err = "the debugger halted the game again"`. `closeContent`, `reset`, `bootBios` (refuse: keep 1196-1199 as is, text updated), `handleDrop` (no change: `openContent` decides; its `flushSaveRam` call gets `haltGiveUp()`), `runHeadlessFrames` and op_ui.cpp:484 (`if (app.debug.halted()) return;` with the hint text "resume the debugger first (F9)"). ui.cpp:554/560: `host.isLoaded()` only for Close game, Reset, Open/Close tray, Disc (they detach first); `Save state`/`Load state`/`State manager`: `loaded && !app.haltedNow()` with `ImGui::SetItemTooltip("Resume the debugger first (F9)")` when disabled by the halt; ui.cpp:585,591 calls pass `app.detachForCall()`.
  - core_host.cpp: the four texts.
  - The halt bar and options tab lines.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j4 && ctest --test-dir build -R "emu_thread_unit|debug_|smoke_.*_threaded|smoke_state" --output-on-failure`
Expected: all green; `emu_thread_unit` repeated `--repeat until-fail:10` stays green.

- [ ] **Step 5: Commit**

```bash
git add app/src/emu_thread.h app/src/app.h app/src/app.cpp app/src/ui.cpp app/src/core_host.cpp \
        app/src/debug/op_ui.cpp app/src/debug/debug_ui.cpp app/tests/emu_thread_test.cpp
git commit -F - <<'EOF'
feat(app): a call() gives up when a halt begins; content changes detach first (#967)

EmuThread::call takes a give-up predicate: a call queued behind a frame that halts returns instead of parking the UI
behind the busy spinner.  Threaded, open / close / reset / disc / quit detach the debugger first and then proceed;
save / load state, boot and frame advance refuse with a toast; frame-lane posts wait for Continue and the halt bar
and the Core options tab say so.  Inline keeps refusing (the call would run inside retro_run).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task B4: the GDB stub unlocked, a silent host behind every halt, hardcore locks the stub

**Files:**
- Modify: `app/src/app.cpp` (`limitFeaturesForEmuThread` 533-544 and `liftFeatureLimits` 547-554 deleted with their call sites at 1953 and 605; `wireHost`'s `optionLock` 482-484 gains the hardcore rule; `syncDebug` gains `syncGdbHost()`; `startEmuThread` logs `[emu] emulation thread on` as before), `app/src/app.h` (`syncGdbHost`, `std::atomic<bool> gdbLockedByHardcore_{false}`, `bool gdbHostTried_ = false`), `app/src/achievements.cpp` (no code: Decision 2 is implemented in App's lock; the doc comment at 586-591 notes the stub), `app/src/core_host.cpp:1206-1240` (no change)
- Test: `app/cmake/VJEmuThreadTests.cmake` (`gdb_locked_threaded` 199-204 -> `gdb_enabled_threaded`; new `gdb_hardcore_locked`, `gdb_hardcore_locked_threaded`; `emu_thread_limits` 194-198 -> `debug_on_threaded`), `app/cmake/VJAchTests.cmake` (nothing; the two hardcore tests live next to the other gdb tests)

**Interfaces:**
- Produces: `void App::syncGdbHost();` UI thread, from `syncDebug()`: `if (!host.isLoaded() || !debug.enabled()) { gdbHostTried_ = false; return; } if (gdbHostTried_ || debug.attached()) return; gdbHostTried_ = true; if (host.optionValueFor("virtualjaguar_gdb_stub", "disabled") == "enabled" && debug.attach()) std::fprintf(stderr, "[debug] gdb host attached (silent): halts are visible to the app\n");`. `setContentLoaded(false)` / `setIface` paths reset `gdbHostTried_` through the `!isLoaded` branch. The core reads the stub options at load only (libretro.c:6898-6945), so one attach per load is the whole policy.
- The lock: `wireHost()`'s `host.optionLock` becomes `[this](const std::string& k, std::string& v) { if (k == "virtualjaguar_gdb_stub" && gdbLockedByHardcore_.load()) { v = "disabled"; return true; } return lockedCoreOptionFor(k, v, {{...}}); }`; `syncDebug()` writes `gdbLockedByHardcore_` from `ach.hardcoreActive()` and on a change calls `host.refreshLocks()` (core_host.cpp:243 queues it to the owner). Hardcore-on restarts the game (achievements.cpp:479-482), so the locked value is what the restarted core reads.
- Log lines (tests grep them): `[emu] emulation thread on` (exists, app.cpp:610); `[debug] gdb host attached (silent)`; the `(locked)` verbose option dump already prints `virtualjaguar_gdb_stub=disabled (locked)` (as `pro_controller_option_on` relies on).

- [ ] **Step 1: Write the failing tests.** In `app/cmake/VJEmuThreadTests.cmake`, replace lines 194-204 with:

```cmake
# 3b: nothing is switched off while threaded.  The old "are off" line must be gone, the thread must be on.
add_test(NAME debug_on_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(debug_on_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\\[emu\\] emulation thread on"
    FAIL_REGULAR_EXPRESSION "are off while the emulation thread is on|ThreadSanitizer|owner violation")
# The GDB stub is a plain core option again when threaded, and the app attaches its silent host behind it.
add_test(NAME gdb_enabled_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 --option virtualjaguar_gdb_stub=enabled
            --option virtualjaguar_gdb_port=0 "${VJ_ROMS}/yarc.j64")
set_tests_properties(gdb_enabled_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\\[debug\\] gdb host attached \\(silent\\)"
    FAIL_REGULAR_EXPRESSION "managed by the app|rejected by the core|ThreadSanitizer|owner violation")
# Decision 2: RetroAchievements hardcore locks the stub to disabled, inline and threaded.
foreach(_m off on)
    add_test(NAME gdb_hardcore_locked_${_m}
        COMMAND virtualjaguar --headless --emu-thread ${_m} --frames 10 --verbose --ra-mock "${VJ_RA_FIX}" --ra-hardcore
                --option virtualjaguar_gdb_stub=enabled "${VJ_ROMS}/yarc.j64")
    set_tests_properties(gdb_hardcore_locked_${_m} PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "virtualjaguar_gdb_stub=disabled \\(locked\\)"
        FAIL_REGULAR_EXPRESSION "gdb host attached|headless: FAIL|ThreadSanitizer|owner violation")
endforeach()
set_tests_properties(gdb_hardcore_locked_on PROPERTIES LABELS emu_thread)
```

`VJ_RA_FIX` is set in `VJAchTests.cmake:33`; include that file before this one in `app/CMakeLists.txt` if it is not already, or repeat its two lines here. Check how `--ra-hardcore` interacts with `--option`: the override of a locked key must not count as `badOverride_` (app.cpp:2519-2522 only fires on a core rejection; a lock is not one, as `pro_controller_option_on` shows). Update the header comment list at lines 23-24 (`*_threaded limits` -> `debug_on_threaded / gdb_enabled_threaded  nothing is off while threaded; gdb_hardcore_locked_*  hardcore locks the stub`). Headless: `syncDebug()` must run at least once per load for the silent attach: in `runHeadless` after the load (app.cpp:2503) add `syncDebug();` (it is cheap and inline-safe); the interactive loop already calls it every frame.

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake -S app -B build >/dev/null && cmake --build build -j4 && ctest --test-dir build -R "debug_on_threaded|gdb_enabled_threaded|gdb_hardcore_locked" --output-on-failure`
Expected: `debug_on_threaded` fails on the "are off" line, `gdb_enabled_threaded` on the missing attach line (the stub option is still locked), `gdb_hardcore_locked_*` on the missing `(locked)`.

- [ ] **Step 3: Implement** per Interfaces: delete `limitFeaturesForEmuThread`/`liftFeatureLimits` and `emu_thread_start_fallback`'s expectation changes to `"could not start the emulation thread; running emulation on the UI thread"` (the limits no longer exist; `host.announceDebug` stays true always; `debug.core().waitVerdict` is cleared in the failure branch of `startEmuThread`). `syncGdbHost()`, the lock, `refreshLocks()` on the edge. The transitional halt gate goes with the limits: delete `DebugSession::setHaltAvailable` / `haltAvailable` / `haltDisabledReason` and `haltAvailable_` (`toolAvailable` becomes `enabled() && t.toggle && (!t.needsHalt || halted())`, `attach()` / `breakNow()` / `addBreakpoint()` lose the term), the A5 tooltip "Halting is not available with the emulation thread yet" in `cpu_ui.cpp`, the F9 / `debugBreakContinue` `else toast(debug.haltDisabledReason(), ...)` branches (app.cpp, desktop.cpp) and the Break menu item's tooltip (debug_ui.cpp); in the tests delete `testHaltAvailable` (debug_session_test.cpp), the `s.setHaltAvailable(false)` lines in `debug_cpu_ui_smoke.cpp` and `debug_scope_gate_test.cpp` (their `offCore() == 0` checks stay), and case 6 of `debug_views_threaded.cpp`. `/usr/bin/grep -rn "haltAvailable\|haltDisabledReason\|setHaltAvailable" app/ docs/app-debug-tools.md app/README.md` must print nothing.

- [ ] **Step 4: Run the tests**

Run: `ctest --test-dir build -L emu_thread --output-on-failure -j4 && ctest --test-dir build -R "^ra_|debug_" --output-on-failure -j4`
Expected: all green, `emu_thread_start_fallback` with its new regex.

- [ ] **Step 5: Commit**

```bash
git add app/src/app.h app/src/app.cpp app/src/achievements.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): GDB stub unlocked while threaded; a silent host behind every halt; hardcore locks the stub (#967)

The phase 1 limits are gone: the core always announces VJ_CAP_DEBUG and virtualjaguar_gdb_stub is a plain option again
when the emulation thread is on.  Whenever the stub is enabled and a game is loaded the app attaches its host, so a gdb
halt flows through the app's pump: the halt bar shows it, calls give up, quitting detaches.  RetroAchievements hardcore
locks the stub to disabled, as it does cheats (phase 3 plan, decisions 2 and 3).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task B5: `debug_e2e_threaded`, `gdb_threaded_halt`, the signal quit

**Files:**
- Create: `app/tests/debug_e2e_threaded.cpp`, `app/tests/gdb_threaded_halt.py`
- Modify: `app/src/main.cpp:92` (signal handlers), `app/src/app.h` / `app.cpp` (`quitRequested()` reads `g_signalQuit`; `abortHalt`; the headless loop 2547-2554 breaks on `quitRequested()` and prints `headless: quit requested, exiting cleanly`; `run()` returns 0 then), `app/cmake/VJDebugTests.cmake` (`vj_debug_e2e_threaded`), `app/cmake/VJEmuThreadTests.cmake` (`gdb_threaded_halt`, python-gated), `app/CMakeLists.txt` (`find_package(Python3 COMPONENTS Interpreter)` if absent)
- Test: the two new tests; `debug_e2e` (inline) unchanged

**Interfaces:**
- Consumes: everything B1-B4 produced; `CoreHost`, `EmuThread`, `DebugSession`/`DebugCore` as App wires them (the rig below mirrors `wireHost`/`startEmuThread`/`haltWait` exactly so the e2e pins App's policy without App).
- Produces: `vj::g_signalQuit` (`app/src/app.h`: `extern std::atomic<bool> g_signalQuit;`), the handlers in `main.cpp`, installed **only for `--headless` runs** (`std::signal(SIGINT, h); std::signal(SIGTERM, h);` where `h` stores true; `<csignal>`; windowed runs keep SDL's own handlers, which turn the signal into `SDL_EVENT_QUIT`), `bool App::quitRequested() const { return quit || g_signalQuit.load(); }`, `abortHalt = [this]{ return quitting_.load() || g_signalQuit.load(); }`.

- [ ] **Step 1: Write the failing tests.**

`app/tests/debug_e2e_threaded.cpp` (modelled on `emu_thread_e2e.cpp`'s `Rig` and `debug_e2e.cpp`'s steps; `CHECK` as there; `usage: vj_debug_e2e_threaded <rom.j64>`):

```cpp
// The debugger halt on the emulation thread, with the REAL core: this (test) thread plays the UI.  Every UI-side
// operation below is timed: none may block over 50 ms (the window keeps drawing while the game is halted).
struct Rig : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    EmuThread emu;
    DebugSession dbg;
    std::atomic<bool> quitting{false};
    std::atomic<int> haltsEmu{0}, resumesEmu{0};
    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 60.0; }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override { host.runFrame(); dbg.core().frameEnded(); host.publishSnapshot(); }
    void afterCommands() override { host.publishSnapshot(); }
    void wire() {   // what App::wireHost + startEmuThread do
        host.onDebugIface = [this](const vj_debug_iface* i) { dbg.core().setIface(i); };
        host.post = [this](std::function<void()> fn) { emu.post(std::move(fn)); };
        dbg.coreCall = [this](std::function<void()> fn) { emu.debugCall([&] { fn(); }); };
        dbg.corePost = [this](std::function<void()> fn) { emu.debugPost(std::move(fn)); };
        dbg.core().notifyVerdict = [this] { emu.debugPost([] {}); };
        dbg.core().waitVerdict = [this] { emu.serveDebugLane(DebugCore::kPumpWait); };
        dbg.core().abortHalt = [this] { return quitting.load(); };
        dbg.core().onHalt.push_back([this](int, unsigned, int) { haltsEmu++; });
        dbg.core().onResume.push_back([this] { resumesEmu++; });
    }
};
// Runs `fn` on this thread and checks it took under 50 ms.
template <class F> void ui(const char* what, F fn) { const auto t0 = Clock::now(); fn(); const double s = since(t0);
    if (s > 0.05) { g_fail++; std::fprintf(stderr, "FAIL %s blocked the UI thread for %.0f ms\n", what, s * 1e3); } }
// Polls until `cond` or 5 s.
template <class F> bool until(F cond) { ... 2 ms sleeps ... }
```

Sections in `main` (one `Rig r; r.wire(); r.emu.start(Threaded, r); emu.call(loadContent)`, `dbg.setContentLoaded(true)`, `until(framesRun >= 30)`):

1. **Break / step / continue from the UI thread.** `ui("breakNow", [&]{ r.dbg.breakNow(); });` `CHECK(until([&]{ r.dbg.syncHalt(); return r.dbg.halted(); }));` `CHECK(r.dbg.haltReason() == VJ_DBG_STOP_USER && !r.dbg.haltByGdb());` `const uint64_t f = r.host.frameCount();` `sleepMs(100); CHECK(r.host.frameCount() == f);` (no frames while halted) `const unsigned pc1 = r.dbg.haltedPc(); const uint64_t s1 = r.dbg.haltSerial();` `ui("step", [&]{ r.dbg.step(); }); CHECK(until([&]{ r.dbg.syncHalt(); return r.dbg.haltSerial() != s1 && r.dbg.halted(); }));` `CHECK(r.dbg.haltReason() == VJ_DBG_STOP_STEP && r.dbg.haltedPc() != pc1 && r.resumesEmu == 0);` `ui("resume", [&]{ r.dbg.resume(); }); CHECK(until([&]{ r.dbg.syncHalt(); return !r.dbg.halted(); })); CHECK(r.resumesEmu == 1); CHECK(until([&]{ return r.host.frameCount() > f; }));`
2. **A debug call while halted answers within 50 ms** (the pump serves the lane): halt again (`breakNow` + `until halted`); `ui("read", [&]{ std::vector<uint8_t> b; CHECK(r.dbg.read(0x7000, b, 4)); });` `ui("poke", [&]{ CHECK(r.dbg.pokeU(0x7000, 4, 0xCAFEF00D) == 4); });` `vj_debug_regs regs; ui("regs", [&]{ CHECK(r.dbg.getRegsNow(VJ_DBG_TARGET_M68K, regs)); }); CHECK(regs.pc == r.dbg.haltedPc());`
3. **Review Focus 5: a frame-lane option post made while halted applies only after Continue.** `r.host.setOptionValue("virtualjaguar_blitter_timing", "enabled");` (a post, threaded) `sleepMs(60); r.host.pinSnapshot(); CHECK(r.host.optionValueFor("virtualjaguar_blitter_timing", "unset") == "unset");` `const uint64_t f3 = r.host.frameCount(); ui("resume", [&]{ r.dbg.resume(); }); CHECK(until([&]{ r.host.pinSnapshot(); return r.host.optionValueFor("virtualjaguar_blitter_timing", "unset") == "enabled"; }));` `CHECK(r.host.frameCount() >= f3 + 1);` then `resetOptionToDefault`.
4. **Review Focus 1: a save state requested just before a halt returns false within 300 ms** (deterministic: a `halt_next` armed from inside the halt re-halts on the very next instruction after Continue, before the frame can end, so the call queued in the frame lane sits behind the new halt). `breakNow` + `until halted`; `r.dbg.coreCall([&]{ r.dbg.core().haltNext(VJ_DBG_TARGET_M68K); });` `std::atomic<bool> saved{false}; std::string err; std::thread caller([&]{ const auto t0 = Clock::now(); const bool ok = r.emu.call([&]{ return r.host.saveStateToFile(stateFile, err); }, [&]{ return r.dbg.core().halt().halted && r.dbg.core().haltSerial() != s4; }); saved = ok; tCall = since(t0); });` where `s4` is the serial of the current halt taken before `resume()`; `sleepMs(5); ui("resume", [&]{ r.dbg.resume(); }); caller.join();` `CHECK(!saved && err.empty() && tCall < 0.3);` `CHECK(until([&]{ r.dbg.syncHalt(); return r.dbg.halted() && r.dbg.haltSerial() != s4; }));` `CHECK(r.dbg.haltReason() == VJ_DBG_STOP_USER);` then `resume()` and `CHECK(r.emu.call([&]{ return r.host.saveStateToFile(stateFile, err); }, [&]{ return r.dbg.core().halt().halted; }));` (no halt now: the save runs).
5. **Detach-first: a content call after `detachAndResume()` runs within one frame and nothing re-halts.** Halt at a breakpoint the game revisits: `addBreakpoint(M68K, pc1)`; `until halted`; `const uint64_t s5 = r.dbg.haltSerial(); ui("detach", [&]{ r.dbg.detachAndResume(); });` `const auto t0 = Clock::now(); CHECK(r.emu.call([&]{ return r.host.loadContent(rom, err); }, [&]{ return r.dbg.core().halt().halted && r.dbg.core().haltSerial() != s5; }));` `CHECK(since(t0) < 0.5); r.dbg.setContentLoaded(false); r.dbg.setContentLoaded(true); sleepMs(200); r.dbg.syncHalt(); CHECK(!r.dbg.halted() && !r.dbg.attached() && r.dbg.breakpoints().empty());` (the list is per core instance).
6. **Hardcore from the UI while halted.** `breakNow` + halted; `std::string closed; r.dbg.onForcedClose.push_back([&](const char* w){ closed = w; });` `ui("hardcore", [&]{ r.dbg.setHardcore(true); });` `CHECK(until([&]{ r.dbg.syncHalt(); return !r.dbg.halted(); })); CHECK(!r.dbg.attached() && closed == "hardcore"); CHECK(until([&]{ return frames advanced; })); r.dbg.setHardcore(false);`
7. **Review Focus 6: stop() returns true within 500 ms while halted.** `breakNow` + halted; `r.quitting = true; ui("detachAndResume", [&]{ r.dbg.detachAndResume(); }); r.emu.debugPost([]{});` `const auto t0 = Clock::now(); CHECK(r.emu.callFor(std::chrono::milliseconds(3000), [&]{ r.host.unloadContent(); })); CHECK(r.emu.stop()); CHECK(since(t0) < 0.5);` Then the same with `quitting` only (no verdict): a fresh `Rig` + load + halt, `r.quitting = true; r.emu.debugPost([]{});` `CHECK(r.emu.stop() && since(t0) < 0.5)` (abortHalt alone frees the join).
8. **A halt during a debugCall issued from the UI** (named deadlock case): `breakNow()` posts `haltNext` to the debug lane; the next UI action is `r.dbg.read()` (a `debugCall`): the lane is drained at the frame boundary before the frame that halts, or inside the pump once halted: either way it returns. `ui("breakNow+read", [&]{ r.dbg.breakNow(); std::vector<uint8_t> b; CHECK(r.dbg.read(0, b, 16)); }); CHECK(until(halted)); resume.`

Register in `app/cmake/VJDebugTests.cmake` next to `vj_debug_e2e`: `add_executable(vj_debug_e2e_threaded tests/debug_e2e_threaded.cpp src/debug/debug_session.cpp src/debug/debug_core.cpp src/debug/debug_views.cpp src/core_host.cpp src/emu_thread.cpp src/paths.cpp)`, the same includes/links as `vj_debug_e2e` plus `Threads::Threads` (and `ws2_32 iphlpapi` on WIN32 as `vj_emu_thread_e2e`), `add_test(NAME debug_e2e_threaded COMMAND vj_debug_e2e_threaded "${VJ_ROMS}/yarc.j64")`, `set_tests_properties(debug_e2e_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL |owner violation")`.

`app/tests/gdb_threaded_halt.py` (in the style of `test/tools/gdb_breakpoint_probe.py`: `recv_packet` with a persistent `rxbuf`, `cmd()` that discards unsolicited `T05` replies; `usage: gdb_threaded_halt.py <virtualjaguar> <rom>`):
1. Pick a port: `bind(("127.0.0.1", 0))`, read it, close (the core binds moments later; a collision fails the run, which is what the first assert reports).
2. Launch `[app, "--headless", "--emu-thread", "on", "--frames", "300", "--option", "virtualjaguar_gdb_stub=enabled", "--option", "virtualjaguar_gdb_wait=enabled", "--option", f"virtualjaguar_gdb_port={port}", rom]` with `stderr=PIPE` (a reader thread collects lines), connect within 5 s, `+`, `qSupported`, expect the boot-wait stop `T05thread:1;` within 10 s.
3. Assert the app's stderr shows `[debug] gdb host attached (silent)` and `[debug] halt: 68000 at $` within 2 s; `m0,10` answers 32 hex chars while halted; `sleep 0.5` and the app is still alive (`proc.poll() is None`).
4. `c` -> the app finishes its 300 frames: `proc.wait(timeout=30)` returns 0 and stdout ends with `headless: OK (1 load(s), emulation thread)`.
5. Second launch, same arguments; after the stop reply send `proc.send_signal(SIGTERM)` while holding the halt; `proc.wait(timeout=3)` returns 0 and stderr has `headless: quit requested, exiting cleanly` and no `did not stop within 3 s`. (Windows: skip step 5 with a printed note; `SIGTERM` there is a hard kill.)
6. Print `PASS: gdb halt on the emulation thread, memory while halted, continue, quit mid-halt` and exit 0.

Register in `app/cmake/VJEmuThreadTests.cmake` (after `find_package(Python3 COMPONENTS Interpreter)` in `app/CMakeLists.txt`):

```cmake
# 3b: a gdb client halts the app at boot while the emulation thread is on; the app's silent host sees it, memory is
# served while halted, continue finishes the run, and a quit while gdb still holds the halt exits 0 within 3 s.
if(Python3_Interpreter_FOUND)
    add_test(NAME gdb_threaded_halt
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/../tests/gdb_threaded_halt.py"
                $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64")
    set_tests_properties(gdb_threaded_halt PROPERTIES TIMEOUT 120 LABELS emu_thread
        PASS_REGULAR_EXPRESSION "^PASS: gdb halt on the emulation thread"
        FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer|did not stop within 3 s")
endif()
```

Header-comment lines for both in their cmake files.

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake -S app -B build >/dev/null && cmake --build build -j4 --target vj_debug_e2e_threaded && ctest --test-dir build -R "debug_e2e_threaded|gdb_threaded_halt" --output-on-failure`
Expected: `debug_e2e_threaded` builds and passes sections 1-7 already if B1-B4 are right (it is the integration pin; a failure here is a B1-B4 bug to fix there); `gdb_threaded_halt` fails at step 5 (no signal handler: the app is killed, exit code -15, no `quit requested` line).

- [ ] **Step 3: Implement** the signal path per Interfaces: `main.cpp` handlers before `App::run`; `App::quitRequested()`; `abortHalt`; the headless threaded loop (2547-2554) becomes `for (...; i < a.frames && !quitRequested();)` with the message and `rc = 0` after the loop when `quitRequested()`; the inline headless loop (2558-) the same check per frame. The interactive loop's `while (!quit)` becomes `while (!quitRequested())`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j4 && ctest --test-dir build -L emu_thread --output-on-failure -j4 && ctest --test-dir build -R "debug_" --output-on-failure -j4 && ctest --test-dir build -R "debug_e2e_threaded" --repeat until-fail:5`
Expected: all green, five repeats green. On a ThreadSanitizer configure (`-DVJ_APP_TSAN=ON -DVJ_APP_LTO=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo`, `TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1 suppressions=$PWD/app/tests/tsan.supp"`, build dir `build-tsan`): `ctest -L emu_thread` green with `app/tests/tsan.supp` unchanged; a report is fixed by publishing the value read (a mirror field, an atomic), never by a suppression.

- [ ] **Step 5: Commit**

```bash
git add app/tests/debug_e2e_threaded.cpp app/tests/gdb_threaded_halt.py app/src/main.cpp app/src/app.h app/src/app.cpp \
        app/cmake/VJDebugTests.cmake app/cmake/VJEmuThreadTests.cmake app/CMakeLists.txt
git commit -F - <<'EOF'
test(app): the halt and the GDB stub end to end on the emulation thread (#967)

debug_e2e_threaded drives break / step / continue / detach / hardcore / quit from a UI thread that never blocks over
50 ms, pins the option post that applies after Continue, the save state that gives up when a halt begins, and the
3 s join.  gdb_threaded_halt halts a threaded headless run from a gdb client, reads memory, continues, and quits the
app mid-halt (SIGTERM now ends a headless run cleanly, which the halted case needs).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task B6: docs, Studio checks, the PR

**Files:**
- Modify: `docs/app-debug-tools.md` ("Halt model" 23-41, "Break, continue, step" 143-149, "Hardcore" 151-155, "With the external GDB stub" 163-168, "Testing" 177-), `app/README.md:523-534` (the "Emulation thread" section: the Tools menu and the GDB stub now work threaded; the stub is locked under hardcore), `docs/agent/testing.md` ("App emulation thread tests": `debug_session_unit` label, `debug_e2e_threaded`, `gdb_threaded_halt`, `debug_on_threaded`, `gdb_enabled_threaded`, `gdb_hardcore_locked_*`, the SIGTERM quit), `docs/app-emulation-thread-design.md` (section 6 amendment: `frameEnded()` and the serial wording, three sentences), `docs/app-emulation-thread-plan-phase3.md` (this plan's "Decisions to confirm" 2 and 3 marked as shipped once the maintainer agrees)

- [ ] **Step 1: Docs.** "Halt model" point 2 becomes: with an app host attached, `GDBHalt()` calls `halt_pump` every iteration; threaded, the pump serves the debug lane, waits at most 4 ms and returns NONE (the core's loop keeps the socket and the halt timeout); inline it runs one UI frame as before. Point 3: while parked, frame-lane commands (options, cheats, controller, SRAM autosave) wait in the queue and apply after Continue; save/load state, boot and frame advance refuse with a toast; open/close/reset/disc/quit detach first. Add to the GDB section: the silent host, "HALTED by gdb", the hardcore lock, and that quitting clears gdb's breakpoints. Testing: the new tests by name. README: drop "the Tools menu and the GDB stub stay off"; add the hardcore lock sentence.

- [ ] **Step 2: Commit the docs**

```bash
git add docs/app-debug-tools.md app/README.md docs/agent/testing.md docs/app-emulation-thread-design.md docs/app-emulation-thread-plan-phase3.md
git commit -F - <<'EOF'
docs(app): the halt and the GDB stub on the emulation thread (#967)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 3: Studio checks** (windowed runs only there; `uptime` under 2 first; `VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh`):

```bash
uptime
bash $VJR push emu-p3b HEAD
bash $VJR run emu-p3b env -u DEVELOPER_DIR bash -lc 'uptime; cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build app/build -j14 >/dev/null && ctest --test-dir app/build -C Release -E shaders_gl -j6 2>&1 | tail -5'
```

Then, windowed (`app/tests/emu_thread_windowed_check.sh` as the Phase 1/2 runs, with these arguments): (a) `--emu-thread on --show-window cpu --option virtualjaguar_gdb_stub=enabled --option virtualjaguar_gdb_wait=enabled yarc.j64` with `VJ_PERF_CSV` set, 20 s: the window starts halted at boot (`HALTED by gdb`), the CPU window shows registers, `iter_ms` stays at the display period while halted (p99 under 2x the period), F9 continues, F9 breaks again, F10 steps, Detach; grep the log for `owner violation` and `ThreadSanitizer` (none). (b) The same on a `-DVJ_APP_TSAN=ON -DVJ_APP_LTO=OFF` build of the app, with `tsan.supp` as committed: no report. (c) A gdb session (`gdb_stock_gdb_check.sh`'s gdb, or `python3 app/tests/gdb_threaded_halt.py` against the windowed binary with `--headless` removed by hand) holding the halt while the app is quit from the window: the process exits within 3 s, log has no `did not stop within 3 s`.
Expected: all three as stated. Any `owner violation` or TSan report is a bug fixed in B2-B4 before the PR.

- [ ] **Step 4: Open the PR.** Body at `~/Workspace/Provenance/vj-eval/remote/emu-p3b/pr-body.md`:

```markdown
Refs #967 (phase 3b of docs/app-emulation-thread-plan-phase3.md; design: docs/app-emulation-thread-design.md section 6)

With Settings > Interface > Emulation thread on, the debugger halt (F9/F10, breakpoints, step) and the GDB stub now
work: the core's halt pump runs on the emulation thread, bounded to 4 ms per call, and the UI mirrors the halt once
per frame.  Still off by default in every build.

- `DebugCore::pump`: serves the debug lane, waits <= 4 ms, returns NONE to `GDBHalt()`'s own loop; verdicts are an
  atomic the UI writes; `frameEnded()` closes a halt gdb ended.
- Calls while halted: frame-lane posts apply after Continue (the halt bar says so); save/load state, boot and frame
  advance refuse; open/close/reset/disc/quit detach first; `EmuThread::call` gives up when a halt begins.
- GDB: the option is unlocked while threaded; the app attaches a silent host whenever the stub is enabled
  (decision 3), the halt bar says "HALTED by gdb", quitting detaches; hardcore locks the stub (decision 2).
- Headless runs end cleanly on SIGINT/SIGTERM (needed to quit a run gdb holds halted).

Decisions to confirm (plan, "Decisions to confirm" 2 and 3): the hardcore lock and the silent host.

## Studio (M4 Max, 60 Hz)

| check | result |
|---|---|
| full suite | |
| windowed: halt at boot with the CPU window, UI iter_ms p99 while halted | |
| windowed TSan run, same scenario | |
| quit while gdb holds the halt: exit time | |

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

```bash
git push -u libretro feat/967-emu-thread-phase3b
gh pr create --repo libretro/virtualjaguar-libretro --base develop --head feat/967-emu-thread-phase3b \
  --title "feat(app): emulation thread phase 3b -- the debugger halt and the GDB stub on the emulation thread (#967)" \
  --body-file ~/Workspace/Provenance/vj-eval/remote/emu-p3b/pr-body.md
```

Expected: CI green (`App *`, `ThreadSanitizer (emulation thread)`, `pr-issue-link`). The agent does not merge. After the merge: `git worktree remove ~/Workspace/Provenance/vj-wt/emu-p3b`.

#### Named races and deadlocks, and the test that pins each

| Case | Mechanism | Test |
|---|---|---|
| A call posted an instant before a halt | give-up predicate; the queued wrapper skips a cancelled task | `emu_thread_unit` testCallGivesUp; `debug_e2e_threaded` section 4 |
| The busy spinner while halted | never reached: the call gives up before `kBusyAfter` only if the predicate is given; refused calls never post | `debug_e2e_threaded` 50 ms budget on every UI op |
| Quit mid-halt | `quitting_` -> `abortHalt` -> DETACH within one pump wait; verdict + lane notify first | `debug_session_unit` testDetachEndsPumpWithinOneWait, testAbortHaltDetaches; `debug_e2e_threaded` 7; `gdb_threaded_halt` step 5 |
| gdb and app verdicts racing | first verdict wins in `GDBHalt`; a foreign resume is closed by `frameEnded()`, which also drops a stale verdict; a verdict for a dead halt is dropped by the serial check | testFrameEndedClosesForeignResume, testStaleVerdictDropped |
| gdb step inside the same frame | new-halt detection by (target, pc) | testForeignStepIsNewHalt |
| A halt during a UI `debugCall` | the lane is drained at the boundary before the halting frame or by the pump inside it | `debug_e2e_threaded` 8 |
| Inline: the pump runs a UI frame on the same thread | `waitVerdict` empty, `runUiFrame` set, verdicts written by that frame; content calls refuse (they would run inside `retro_run`) | testInlinePumpRunsUiFrame; `debug_e2e` unchanged |
| The pump outwaits the socket service | bounded by `waitVerdict` only | testPumpNeverOutwaitsHook |
| Hardcore mid-halt | DETACH verdict, no iface call on the UI thread | testHardcoreGoesThroughTheLane; `debug_e2e_threaded` 6 |
| Detach-first then re-halt on a gdb breakpoint | the serial-based predicate gives up only on a new halt; the silent host re-attaches on the next `syncGdbHost()` after the load | `debug_e2e_threaded` 5 |

---

## Sub-phase 3c: Neuro on the input snapshot

Branch `feat/967-emu-thread-phase3c` off `libretro/develop` **after 3a merges**; worktree `~/Workspace/Provenance/vj-wt/emu-p3c`. Depends on 3a for `DebugViews`, `App::fillDebugViews()` and the interface reaching the app threaded (`announceDebug` true, `debug.core().iface()` set; halts stay impossible until 3b). Independent of 3b; the shared hunks are `App::wireHost` (this PR touches only the `inputPoll` lambda) and `startEmuThread`. The PR leaves off the nightly default (3d) and everything about halts and gdb. It ends with Neuro on in every threaded run: no gate, no hint, no `neuro_off_threaded`.

The design in one paragraph: the UI keeps `InputQueue` as the append-only record of what was accepted (trimmed by progress), publishes it as an `AgentProgram`, and the core's input poll runs an `AgentPlayer` replica one step per emulated frame (`pollAgent`), inline and threaded alike. `Agent::busy` is the UI queue's: true from the moment an action is accepted until a matching-lineage progress shows it played out. Watches are one `DebugViews` request per profile; the UI reads the bytes at 4 Hz.

**What each Review Focus / #1025 item is pinned by**

| Behaviour | Test (task) |
|---|---|
| Review Focus 3: a press lasts N core polls at any display rate / fast-forward / inline vs threaded | `neuro_unit` "AgentPlayer presses for exactly N polls, hold persists, gen reload replaces" (C1); `neuro_core_e2e` "9 polls, free-running, at 600 Hz" (C2); `neuro_loopback_threaded` with the inline `loopback` frame-numbered vector (C2 harness, C4 registration) |
| An appended action / a hold change mid-play neither replays nor drops frames | `neuro_unit` "append and hold change mid-play equal one uninterrupted queue" (C1) |
| #1025 release input on drop (server drops, `release_all`, link down) | `loopback` / `loopback_threaded` `rightNow` cleared after the server closes (C2/C4); `neuro_unit` "release_all drops queue and hold at the next poll" (C2) |
| #1025 hardcore block, including `tick()`'s early return | `neuro_unit` "hardcore clears the replica within one poll" (C2); `loopback` `upNow` cleared on hardcore (C2/C4); `loopbackHardcoreFirst` unchanged |
| #1025 input block (menu / mouse), nothing played behind the menu | `testAgentGating` ported (C2); `AgentPlayer` "disabled does not advance" (C1) |
| #1025 bounded stop | `testStopIsPrompt` unchanged and green; the replica has no wait (nothing to add) |
| #1025 indicators (`driving()`, "Pressing:") | `testAgentGating` `pressedNow` assertions through progress (C2) |
| Inline smoke hashes unchanged | `.any()` guard in `addAgentControls`; every `smoke_*` test (C2 step 6) |

### Task C1: AgentPlayer, the snapshot's agent channel, InputQueue additions (unit)

**Files:**
- Modify: `app/src/input_snapshot.h` (add after `takenSince`, :48; members after :57)
- Modify: `app/src/neuro/neuro_actions.h` (`InputQueue` :56-72; new `AgentPlayer`, `pollAgent` after it; include `input_snapshot.h`)
- Modify: `app/src/neuro/neuro_actions.cpp` (`InputQueue` :158-190; new definitions after :190)
- Modify: `app/cmake/VJNeuroTests.cmake` (the `vj_neuro_test` target :9-20)
- Test: `app/tests/neuro_test.cpp` (new `testAgentPlayer`, `testQueueProgram`, registered in `main` after `testQueue`, :883), `app/tests/input_snapshot_test.cpp` (new `testAgentChannel`)

**Interfaces:**
- Consumes: `ControlSet`, `CoreInput` (remap.h), `Step`.
- Produces: `AgentStep`, `AgentProgram`, `AgentProgress` (Interfaces section; `AgentStep { ControlSet held; unsigned frames = 1; }`), `InputSnapshot::{publishAgent, takeAgent, reportAgent, agentProgress}` (Interfaces-section signatures), `InputQueue::{version, lineage, firstFrame, trim, program}`, `AgentPlayer::{load, frame, progress, gen}`, `pollAgent`.

- [ ] **Step 1: link what the snapshot needs into `vj_neuro_test`.** `InputSnapshot` holds a `CoreInput`, whose `carryMotion`/`clearMotion` live in `remap.cpp`. In `VJNeuroTests.cmake` change the target to `add_executable(vj_neuro_test tests/neuro_test.cpp src/remap.cpp src/config.cpp src/paths.cpp ${VJ_NEURO_SOURCES})` and its include line to `target_include_directories(vj_neuro_test PRIVATE src src/neuro $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)` (the same three sources and generator expression as `vj_input_snapshot_test`, VJEmuThreadTests.cmake:61-62).

- [ ] **Step 2: write the failing tests.**

  `input_snapshot_test.cpp`, `testAgentChannel()` (add to `main`; plain `CHECK(cond, "msg")` style of that file):
  ```cpp
  InputSnapshot s; AgentProgram p, got;
  CHECK(!s.takeAgent(got), "nothing published: false");
  p.gen = 1; p.lineage = 1; p.enabled = true; p.steps = {{pad(C_B), 3}};
  s.publishAgent(p);
  CHECK(s.takeAgent(got) && got.gen == 1 && got.steps.size() == 1 && got.steps[0].frames == 3, "first take");
  CHECK(!s.takeAgent(got), "same gen: false");
  p.gen = 2; s.publishAgent(p); p.gen = 3; p.hold = pad(C_A); s.publishAgent(p);
  CHECK(s.takeAgent(got) && got.gen == 3 && got.hold == pad(C_A), "two publishes between takes: the newest only");
  AgentProgress r; r.gen = 3; r.lineage = 1; r.played = 2; r.pressed = pad(C_B);
  s.reportAgent(r);
  CHECK(s.agentProgress().played == 2 && s.agentProgress().pressed == pad(C_B), "progress round trip");
  ```
  plus a two-thread tear check (label already `emu_thread`): the UI thread publishes gen 1..20000 where `steps.size() == gen % 7` and `hold.bits == gen`; the emulation thread loops `takeAgent` and `reportAgent`, asserting each taken program satisfies both relations and `gen` never decreases; final take has gen 20000.

  `neuro_test.cpp`:
  ```cpp
  static AgentProgram prog(uint64_t gen, uint64_t lineage, uint64_t first, std::vector<AgentStep> steps,
                           ControlSet hold = ControlSet(), bool enabled = true) {
      AgentProgram p; p.gen = gen; p.lineage = lineage; p.firstFrame = first; p.enabled = enabled;
      p.hold = hold; p.steps = steps; return p;
  }
  static std::string pat(AgentPlayer& pl, unsigned n, Ctl c) {   // one char per poll: the letter when c is pressed
      std::string s; for (unsigned i = 0; i < n; i++) s += pl.frame().test(c) ? '#' : '.'; return s;
  }
  ```
  `testAgentPlayer()` (every line a `CHECK`):
  1. "AgentPlayer presses for exactly N polls": `pl.load(prog(1,1,0,{{keys({C_B}),3},{ControlSet(),3},{keys({C_B}),3}}))`; `pat(pl,12,C_B) == "###...###..."`; a fresh player after 3 polls has `progress().played == 3 && !done`; after 9, `played == 9 && done`.
  2. "hold persists": `hold = keys({C_A})`, steps `{{keys({C_B}),2}}`: polls 1-2 `== keys({C_A,C_B})`, polls 3-6 `== keys({C_A})` (steps exhausted, hold stays); `progress().pressed == keys({C_A})`, `played == 2`, `done`.
  3. "gen reload replaces (new lineage)": after (1) `load(prog(9,8,0,{{keys({C_A}),2}}))`: next `frame() == keys({C_A})`, `progress().played == 1 && lineage == 8 && gen == 9`.
  4. "append and hold change mid-play equal one uninterrupted queue": reference `InputQueue ref; ref.enqueue({{B,6},{UP,2},{ControlSet(),2},{B,1}})`, `ref.setHold` not used, 11 `ref.frame()` values recorded. Player: `load(prog(1,7,0,{{B,6}}))`, 2 polls; `load(prog(2,7,0,{{B,6},{UP,2}}))`, 3 polls; `load(prog(3,7,5,{{B,1},{UP,2},{ControlSet(),2},{B,1}}))` (the UI trimmed the 5 played frames), 6 polls. The 11 polled values equal the reference's; `progress().played == 11`. Then with a hold change: `load(prog(4,7,11,{}, keys({C_C})))` -> next `frame() == keys({C_C})` and `played` unchanged.
  5. "disabled does not advance": `load(prog(1,1,0,{{B,4}}))`, 1 poll; `load(prog(2,1,0,{{B,4}}, ControlSet(), false))`: 3 polls all `!any()`, `progress().played == 1`; `load(prog(3,1,0,{{B,4}}))` resumes: `pat(3,C_B) == "###"` and `played == 4`.
  6. "firstFrame ahead of played is clamped": fresh player, `load(prog(1,1,5,{{B,3}}))`: `pat(4,C_B) == "###."`, `played == 8`.
  7. `pollAgent`: `InputSnapshot s; AgentPlayer pl; s.publishAgent(prog(1,1,0,{{B,2}}))`: `pollAgent(s,pl)` three times `== keys({C_B}), keys({C_B}), ControlSet()`, `s.agentProgress().played == 2 && done && gen == 1`.

  `testQueueProgram()`: `InputQueue q; q.version()==0&&q.lineage()==0`; `enqueue({{B,3},{A,2}})` -> `version()==1`; `setHold(keys({C_C}))` -> 2; `setHold` again with the same set -> still 2; `program(7,true)` has `gen==7, enabled, hold==C, steps.size()==2, steps[0].frames==3, firstFrame==0, lineage==q.lineage()`; `q.trim(4)` -> `queuedFrames()==1 && firstFrame()==4`, `program(8,true).steps.size()==1 && .firstFrame==4`, `peek()` has A; `q.trim(100)` -> `!busy() && firstFrame()==5`; `q.clear()` -> `lineage()==1`, `firstFrame()==0`, `version()` increased, hold gone.

- [ ] **Step 3: run, expect FAIL (does not compile).**
  ```bash
  env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j4 --target vj_neuro_test vj_input_snapshot_test
  ```
  Expected: errors "unknown type name 'AgentProgram'" / "no member named 'publishAgent'".

- [ ] **Step 4: implement.**
  - `input_snapshot.h`: the three structs exactly as in change request 1 (`AgentStep` first); the four methods with the Interfaces-section signatures. A separate `std::mutex agentMu_` (the per-event `mu_` is not touched) and `std::atomic<uint64_t> agentGen_{0}`; `takeAgent` returns false without locking when `agentGen_ == takenAgentGen_` (emulation-thread-only member), else locks, copies, sets `takenAgentGen_ = out.gen`. `publishAgent` stores under the lock, then `agentGen_.store(p.gen)`. `reportAgent`/`agentProgress` copy under `agentMu_`.
  - `neuro_actions.h/.cpp`: `InputQueue` gets members `uint64_t version_ = 0, lineage_ = 0, firstFrame_ = 0;` and the methods of change request 2 (`enqueue` success, `setHold`-when-changed and `clear` bump `version_`; `clear` also does `lineage_++, firstFrame_ = 0`; `program(gen, enabled)` converts each `Step` to `AgentStep`). Everything existing (`frame`, `peek`, `busy`, `queuedFrames`) is unchanged: the player reuses them.
  - `AgentPlayer` (members `InputQueue q_; bool enabled_ = false; uint64_t gen_ = 0, lineage_ = 0, played_ = 0; ControlSet last_;`):
    ```cpp
    void AgentPlayer::load(const AgentProgram& p) {
        if (p.lineage != lineage_) { lineage_ = p.lineage; played_ = 0; }     // a new lineage restarts the count
        if (played_ < p.firstFrame) played_ = p.firstFrame;                  // frames trimmed away can never be replayed
        q_.clear();
        std::vector<Step> st; for (const AgentStep& a : p.steps) st.push_back({a.held, a.frames});
        if (!st.empty()) q_.enqueue(st);
        q_.trim((unsigned)(played_ - p.firstFrame));                         // skip what this replica already played
        q_.setHold(p.hold); enabled_ = p.enabled; gen_ = p.gen;
    }
    ControlSet AgentPlayer::frame() {        // !enabled: empty and does NOT advance (nothing plays behind a menu)
        if (!enabled_) return last_ = ControlSet();
        const bool had = q_.busy();
        last_ = q_.frame();
        if (had) played_++;
        return last_;
    }
    // progress(): {gen_, lineage_, (unsigned)played_, !q_.busy(), last_}
    ControlSet pollAgent(InputSnapshot& s, AgentPlayer& p) {
        AgentProgram prog; if (s.takeAgent(prog)) p.load(prog);
        const ControlSet c = p.frame(); s.reportAgent(p.progress()); return c;
    }
    ```
- [ ] **Step 5: run, expect PASS.**
  ```bash
  cmake --build build -j4 --target vj_neuro_test vj_input_snapshot_test
  ctest --test-dir build -R "neuro_unit|input_snapshot_unit" --output-on-failure
  ```
  Expected: both PASS; `neuro unit: ok`.
- [ ] **Step 6: commit.**
  ```bash
  git add app/src/input_snapshot.h app/src/neuro/neuro_actions.h app/src/neuro/neuro_actions.cpp app/cmake/VJNeuroTests.cmake app/tests/neuro_test.cpp app/tests/input_snapshot_test.cpp
  git commit -m "feat(app): AgentPlayer and the snapshot's agent channel (#967)

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
  (Write the message with its trailer on its own line; commits are signed through 1Password, retry on a signing error.)

### Task C2: Agent publishes and absorbs; `agentControls` removed; `inputPoll` runs the replica

**Files:**
- Modify: `app/src/neuro/neuro_agent.h` (:34-37 `GameState` stays for now; :72 `frame()` removed; :79 `pressedNow`; new methods; header comment :1-7)
- Modify: `app/src/neuro/neuro_agent.cpp` (`configure` :52-72, `setInputBlock` :95-101, `tick` :148-163)
- Modify: `app/src/neuro/neuro_app.cpp` (`neuroTick` :124-135, `neuroShutdown` :137-140, new `neuroHeadlessReport`)
- Modify: `app/src/input.h` (:53-54 removed, `addAgentControls` added), `app/src/input.cpp` (:612 removed, definition added)
- Modify: `app/src/app.cpp` (`inputPoll` :462-478; headless loops :2534-2557 and :2557-2570; end-of-run line after :2573), `app/src/app.h` (`neuro::AgentPlayer agentPlayer_` next to `inputSnap`; `neuroHeadlessReport` declaration :540)
- Create: `app/tests/neuro_core_e2e.cpp`
- Modify: `app/cmake/VJNeuroTests.cmake`
- Test: `app/tests/neuro_test.cpp` (port `testAgentGating` :408-490 and `loopback` :765-872; new `testAgentProgram`; new `app` mode)

**Interfaces:**
- Consumes: C1 (`AgentProgram`, `pollAgent`, `InputQueue::{version,lineage,firstFrame,trim,program}`).
- Produces: `void Agent::publishTo(InputSnapshot&)`, `void Agent::absorb(const AgentProgress&)`, `uint64_t Agent::framesPlayed() const`, `ControlSet Agent::pressedNow() const` (= the last matching-lineage progress's `pressed`; empty when not allowed), `InputManager::addAgentControls(ControlSet)`, `void neuroHeadlessReport(App&)`. Removed: `Agent::frame()`, `InputManager::agentControls`.

- [ ] **Step 1: write the failing tests.**

  Test helper in `neuro_test.cpp` (shared by unit and loopback):
  ```cpp
  struct Replica {            // what the app's input poll does, for a test with no core
      InputSnapshot snap; AgentPlayer player;
      ControlSet poll(Agent& a) { a.publishTo(snap); const ControlSet c = pollAgent(snap, player); a.absorb(snap.agentProgress()); return c; }
  };
  ```
  `testAgentGating` is ported by replacing each `agent.frame()` with `rep.poll(agent)` (a `Replica rep;` at the top); every assertion stays as written, so they are the #1025 pins: the menu clears what is queued (`!rep.poll(agent).any() && !agent.pressedNow().any()` straight after `agent.tick(0.1, g)`), nothing is queued behind the menu, the mouse reason, no `actions/force` while blocked, and hardcore: after `agent.configure(s)` with `s.hardcore = true` (note: `tick()` is not called, as in `neuroTick`, which calls `configure` then `tick` which returns early) `!rep.poll(agent).any() && !agent.pressedNow().any()`.

  New `testAgentProgram()`:
  ```cpp
  Agent agent; Replica rep; std::vector<Json> sent;
  agent.link().send = [&](const std::string& m) { sent.push_back(parsed(m)); };
  AgentSettings s; s.enabled = true; s.url = "ws://127.0.0.1:9"; agent.configure(s);
  GameState g; g.loaded = true; g.title = "Tempest 2000"; agent.tick(0.0, g); agent.link().onConnected();
  const std::string hold  = "{\"command\":\"action\",\"data\":{\"id\":\"h\",\"name\":\"hold_buttons\",\"data\":\"{\\\"buttons\\\":[\\\"right\\\"]}\"}}";
  const std::string press = "...press_buttons b frames 3...";   // the string testAgentGating uses, frames:3
  ```
  1. "busy covers progress from an older gen": `agent.link().onMessage(press)` then `agent.publishTo(rep.snap)`; before any poll `agent.absorb(rep.snap.agentProgress())` (default progress: gen 0, lineage 0) leaves `queue busy`, observed as: `agent.tick(1.0, g)` with `forceInterval = 0` sends no `actions/force`. After three `rep.poll(agent)` calls the next `agent.tick(2.0, g)` does send one, and `agent.framesPlayed() == 3`.
  2. "release_all drops queue and hold at the next poll": `onMessage(hold)`, `onMessage(press)`, `rep.poll` twice (right+b pressed), `onMessage(release_all)`, `rep.poll(agent)` is empty and `!agent.pressedNow().any()`, and the following three polls too.
  3. "hardcore clears the replica within one poll": hold right, `rep.poll` presses right; `s.hardcore = true; agent.configure(s);` **without** `agent.tick`; `rep.poll(agent)` is empty and `rep.snap.takeAgent` was seen with `enabled == false` (record `rep.player` state: `rep.player.progress().pressed` empty); `agent.configure` back to `hardcore = false` and a fresh press plays again.
  4. "turning Neuro off clears the replica" (the `neuroShutdown` path): hold right, poll, `agent.configure(AgentSettings())`, `rep.poll(agent)` empty.
  5. "append mid-play does not replay": press b 6 frames, 2 polls, press up 2 frames, then 6 polls: the pressed pattern across the 8 polls is `bbbbbb` then up on 2: `B B B B B B U U` exactly (the replica kept `played`).

  `loopback()` port: the app thread's `agent.frame()` (:780) becomes `rep.poll(agent)` with `Replica rep;` local to the `app` thread (single thread, inline-shaped), everything else (`pressedB == 9`, `rightNow`, `upNow` flags, hardcore) unchanged. Additionally record the frame-numbered vector for C4: once the first `C_B` poll is seen, append `pressed.test(C_B)` per poll to a `std::vector<uint8_t> rec` until it holds 15 entries; after the `force2` wait assert `rec == {1,1,1,0,0,0,1,1,1,0,0,0,1,1,1}` (`fire` x3 = three 3-frame presses with a 3-frame gap, the Tempest macro). Extract the loop body into `static int loopbackRun(bool threadedPoll)`; this task registers only `threadedPoll == false`.

  New `app` mode in `main` (POSIX; `return 77` on Windows): `vj_neuro_test app <virtualjaguar> <rom> <on|off>` opens a `FakeServer`, spawns the app with `popen` (`<exe> --headless --emu-thread <mode> --neuro ws://127.0.0.1:<port> --frames 480 <rom> 2>&1`, a reader thread collects the output), `accept(10000)`, `waitFor(startup)`, `waitFor(actions/register)`, sends
  `{"command":"action","data":{"id":"p1","name":"press_buttons","data":"{\"buttons\":[\"b\"],\"frames\":30}"}}`, `waitFor(action/result)` must be `success`, then drains messages until the child exits (`pclose`, status 0). The output must contain `[neuro] headless: 30 frames of input played`, not contain `owner violation` or `ThreadSanitizer`, and for `on` contain `emulation thread)`.

  `neuro_core_e2e.cpp` (usage `vj_neuro_core_e2e <rom.j64>`; copy `Rig`/`session` from `emu_thread_e2e.cpp:84-137`, dropping `FrameExchange` and the pixel hash; the record per frame is `fnv(joypad0Buttons, 21)`): `host.inputPoll = [&]{ rig.snap.take(rig.cur); const ControlSet a = pollAgent(rig.snap, rig.player); if (a.any()) rig.cur.addControls(0, a); }`; `host.inputState` as there. Three sessions of 120 frames, lock-step (`emu.advance(1)` per frame, program published before frame 60):
  1. idle: no program (`base`).
  2. "9 polls, inline": `Mode::Inline`, program `prog(1,1,0,{{pad(C_B),3},{ControlSet(),3},{pad(C_B),3},{ControlSet(),3},{pad(C_B),3}})` published before frame 60. `rec[i] != base[i]` for exactly `i in {60,61,62,66,67,68,72,73,74}`.
  3. "same threaded": `Mode::Threaded`; `rec == rec(2)`.
  4. "free-running at 600 Hz": threaded, not paused, program published after `emu.framesRun() >= 30`; wait until `emu.framesRun() >= 200`; the number of frames with `rec[i] != base[i]` (compare against the idle matrix value, not indices) is exactly 9.
  5. disabled: program with `enabled=false`: `rec == base`.

- [ ] **Step 2: run, expect FAIL (does not compile).** Register in `VJNeuroTests.cmake`:
  ```cmake
  add_executable(vj_neuro_core_e2e tests/neuro_core_e2e.cpp src/core_host.cpp src/emu_thread.cpp src/remap.cpp src/config.cpp
                 src/paths.cpp src/neuro/neuro_actions.cpp src/neuro/neuro_json.cpp)
  target_include_directories(vj_neuro_core_e2e PRIVATE src src/neuro)
  target_link_libraries(vj_neuro_core_e2e PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
  if(WIN32)
      target_link_libraries(vj_neuro_core_e2e PRIVATE ws2_32 iphlpapi)
  endif()
  vj_emu_thread_warnings(vj_neuro_core_e2e)
  add_test(NAME neuro_core_e2e COMMAND vj_neuro_core_e2e "${VJ_ROMS}/yarc.j64")
  set_tests_properties(neuro_core_e2e PROPERTIES TIMEOUT 600 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
  add_dependencies(vj_neuro_test virtualjaguar)
  add_test(NAME neuro_app_inline COMMAND vj_neuro_test app $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64" off)
  set_tests_properties(neuro_app_inline PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
      FAIL_REGULAR_EXPRESSION "FAIL |ThreadSanitizer|owner violation")
  ```
  (`vj_emu_thread_warnings` is defined in `VJEmuThreadTests.cmake`; include order in app/CMakeLists.txt puts it first. If `VJNeuroTests` is included earlier, repeat its two-line body.)
  ```bash
  cmake --build build -j4 --target vj_neuro_test vj_neuro_core_e2e virtualjaguar
  ctest --test-dir build -R "neuro_unit|neuro_core_e2e|neuro_app_inline" --output-on-failure
  ```
  Expected: compile errors for `publishTo`/`absorb`/`framesPlayed`.

- [ ] **Step 3: implement `Agent`.** In `neuro_agent.h`: remove `frame()`; `pressedNow()` returns `pressed_` (a `ControlSet` member); add `void publishTo(InputSnapshot&)`, `void absorb(const AgentProgress&)`, `uint64_t framesPlayed() const`; private `uint64_t programGen_ = 0, pubVersion_ = 0, pubLineage_ = 0, playedTotal_ = 0, lastLineage_ = ~0ull, lastPlayed_ = 0; bool pubEnabled_ = false; ControlSet pressed_;`. Update the header comment (threads: `tick`, `configure`, `publishTo`, `absorb` are UI-thread; the replica is `AgentPlayer`, driven by the core's input poll). Bodies:
  ```cpp
  void Agent::publishTo(InputSnapshot& s) {
      const bool en = allowed() && actions_.blocked().empty();
      if (queue_.version() == pubVersion_ && queue_.lineage() == pubLineage_ && en == pubEnabled_) return;
      s.publishAgent(queue_.program(++programGen_, en));
      pubVersion_ = queue_.version(); pubLineage_ = queue_.lineage(); pubEnabled_ = en;
  }
  void Agent::absorb(const AgentProgress& p) {
      if (p.lineage == lastLineage_) { if (p.played > lastPlayed_) playedTotal_ += p.played - lastPlayed_; }
      else playedTotal_ += p.played;
      lastLineage_ = p.lineage; lastPlayed_ = p.played;
      const bool mine = p.lineage == queue_.lineage();       // an older lineage's progress says nothing about this queue
      pressed_ = mine && allowed() ? p.pressed : ControlSet();
      if (mine && p.played > queue_.firstFrame()) queue_.trim((unsigned)(p.played - queue_.firstFrame()));
  }
  ```
  `queue_.busy()` (used by `tick`, `canForce`) is unchanged and now means "accepted and not yet seen played out", which is why a force never goes out the instant an action is accepted. `stateText` keeps `queue_.hold()`. `setInputBlock`, `configure`, `pump`, `gameChanged` already call `queue_.clear()`; no change.

- [ ] **Step 4: implement the app side.**
  - `input.h`: delete `agentControls` and its comment (:53-54); add `void addAgentControls(ControlSet c);`. `input.cpp`: delete :612; define `void InputManager::addAgentControls(ControlSet c) { if (c.any()) core_.addControls(0, c); }`.
  - `app.h`: under `#ifdef VJ_HAVE_NEURO` a member `neuro::AgentPlayer agentPlayer_;  // emulation thread only (the input poll)`; declare `void neuroHeadlessReport(App& app);`.
  - `app.cpp` `host.inputPoll` (:462-478), both branches:
    ```cpp
    // threaded branch, after `if (ns) noteInputLatency(ns);`:
    #ifdef VJ_HAVE_NEURO
        { const ControlSet a = neuro::pollAgent(inputSnap, agentPlayer_); if (a.any()) emuInput_.addControls(0, a); }
    #endif
        return;
    // inline branch: delete `input.agentControls = neuro.frame();`; after `input.poll(!inputCaptured_);`:
    #ifdef VJ_HAVE_NEURO
        input.addAgentControls(neuro::pollAgent(inputSnap, agentPlayer_));
    #endif
    ```
  - `neuro_app.cpp`: `neuroTick` becomes: config as today, `app.neuro.configure(app.neuroCfg); app.neuro.absorb(app.inputSnap.agentProgress()); app.neuro.tick(t, gameState(app)); app.neuro.publishTo(app.inputSnap);` (absorb before tick so an action accepted inside `tick`'s pump sees a trimmed queue; publish after, unconditionally, so `tick`'s early return on `!allowed()` does not hide a clear). `neuroShutdown`: after `configure(AgentSettings())` call `app.neuro.publishTo(app.inputSnap)`. New:
    ```cpp
    void neuroHeadlessReport(App& app) {   // --neuro --headless: what the run saw, for the tests
        app.neuro.absorb(app.inputSnap.agentProgress());
        std::fprintf(stderr, "[neuro] headless: %llu frames of input played\n", (unsigned long long)app.neuro.framesPlayed());
    }
    ```
  - `app.cpp` headless: in the inline loop call `neuroHeadlessReport(*this)` once after the `TIMING` line when `!neuroCliUrl.empty()` (under `VJ_HAVE_NEURO`). The threaded headless branch is C4's.
  - Delete `Agent::frame` uses nowhere else (`grep -rn "neuro.frame\|agentControls" app/src app/tests` must be empty).

- [ ] **Step 5: run, expect PASS.**
  ```bash
  cmake --build build -j4 && ctest --test-dir build -R "neuro|input_snapshot|emu_thread_e2e" --output-on-failure
  ```
  Expected: `neuro_unit`, `neuro_loopback`, `neuro_core_e2e`, `neuro_app_inline`, `neuro_off_threaded` (still gated off, unchanged), `input_snapshot_unit`, `emu_thread_e2e` PASS.
- [ ] **Step 6: the inline hashes did not move.** `ctest --test-dir build -R "smoke_|cheat_|pro_controller" --output-on-failure`: all PASS (the `.any()` guard keeps `core_` untouched with no agent).
- [ ] **Step 7: commit.**
  ```bash
  git add app/src/neuro app/src/input.h app/src/input.cpp app/src/app.cpp app/src/app.h app/cmake/VJNeuroTests.cmake app/tests/neuro_test.cpp app/tests/neuro_core_e2e.cpp
  git commit -m "feat(app): Neuro presses through the input snapshot (#967)

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task C3: RAM watches through `DebugViews`

**Files:**
- Modify: `app/src/neuro/neuro_agent.h` (`GameState` :34-37; new `watchRange`, `kMaxWatchSpan`), `app/src/neuro/neuro_agent.cpp` (`stateText` :165-179, `watchRange`)
- Modify: `app/src/neuro/neuro_actions.h/.cpp` (`watchValue` overload :90, :295-312)
- Modify: `app/src/neuro/neuro_app.cpp` (`gameState` :95-96, the `retro_get_memory_*` externs :22-25, `neuroTick`, new watch request code)
- Modify: `app/src/app.h` (`DebugViews::Id neuroWatchId`, `uint64_t neuroWatchKey`, `std::vector<uint8_t> neuroWatchBytes`, `uint32_t neuroWatchBase`, `double neuroWatchAt` beside :185-188), `app/src/app.cpp` (headless hooks)
- Modify: `app/cmake/VJNeuroTests.cmake`
- Test: `app/tests/neuro_test.cpp` (`testWatches`), CTest `neuro_watch_inline`

**Interfaces:**
- Consumes: 3a's `DebugViews` (`set`, `clear`, `read`, `ViewRequest::Kind::{Memory,Dram}`, `ViewBuffer::{ok,bytes}`), `debug.views()`, `App::fillDebugViews()`, `App::syncDebug()`.
- Produces: `std::vector<uint8_t> GameState::watchBytes; uint32_t GameState::watchBase` (replacing `ram`, `ramSize`), `bool Agent::watchRange(uint32_t& base, unsigned& len) const` (false without watches), `constexpr unsigned kMaxWatchSpan = 65536`, the 4-argument `watchValue`.

- [ ] **Step 1: write the failing tests.** `testWatches()` in `neuro_test.cpp`:
  ```cpp
  uint8_t b[4] = {0x12, 0x34, 0x00, 0x07};   // bytes of a request whose first address is 0x1F0000
  Watch w; w.address = 0x1F0000; w.size = 2; w.format = "bcd";
  CHECK(watchValue(w, b, sizeof b, 0x1F0000) == "1234");
  w.address = 0x1F0003; w.size = 1; w.format = "dec";  CHECK(watchValue(w, b, sizeof b, 0x1F0000) == "7");
  w.address = 0x1F0004;                                  CHECK(watchValue(w, b, sizeof b, 0x1F0000) == "?");  // past the end
  w.address = 0x1EFFFF;                                  CHECK(watchValue(w, b, sizeof b, 0x1F0000) == "?");  // before the base
  CHECK(watchValue(w, nullptr, 0, 0) == "?");
  ```
  and an `Agent` test with a profile loaded from a temp directory (`std::filesystem::temp_directory_path() / "vj-neuro-watch"`, a file `t.json` = `{"game_name":"T","titles":["Tempest 2000"],"watch":[{"name":"score","address":"0x100","size":2,"format":"bcd"},{"name":"lives","address":256,"size":1}]}`; `agent.loadProfiles(dir)`; `agent.tick(0.0, g)` with `g.title = "Tempest 2000"` so `followGame` picks it): `watchRange(base,len)` is true with `base == 0x100 && len == 2`; with a third watch at `0x1F0000` size 1 in a second file `u.json` (`titles ["Wide"]`) `watchRange` gives `base == 0x100 && len == 0x1EFF01`, which is `> kMaxWatchSpan`; a profile without watches returns false. `stateText` with `g.watchBase = 0x100`, `g.watchBytes = {0x12,0x34}` contains `- score: 1234` and `- lives: 18`; with empty `watchBytes` it contains `- score: ?`.

  CTest (fixture written at configure time like `pro_controller_option_*`, VJEmuThreadTests.cmake:100-105):
  ```cmake
  set(_nw "${CMAKE_CURRENT_BINARY_DIR}/neuro-watch-data")
  file(MAKE_DIRECTORY "${_nw}/neuro")
  file(WRITE "${_nw}/neuro/yarc.json" "{\"game_name\":\"Yarc\",\"titles\":[\"yarc\"],\"watch\":[{\"name\":\"cheat byte\",\"address\":\"0x1F0000\",\"size\":1,\"format\":\"hex\"},{\"name\":\"far byte\",\"address\":\"0x100000\",\"size\":1}]}")
  add_test(NAME neuro_watch_inline COMMAND virtualjaguar --headless --emu-thread off --frames 90 --data-dir "${_nw}"
      --cheat 001F0000:AB --neuro ws://127.0.0.1:9 "${VJ_ROMS}/yarc.j64")
  set_tests_properties(neuro_watch_inline PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "\\[neuro\\] state: .*cheat byte: 0xAB"
      FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
  ```
  The two watches are 1 MiB apart, so this run takes the `Kind::Dram` path (span > 64 KiB); the fixture's second profile line is what pins "no watch is dropped".

- [ ] **Step 2: run, expect FAIL** (`cmake --build build -j4 --target vj_neuro_test` fails: no 4-argument `watchValue`, no `watchRange`).

- [ ] **Step 3: implement.**
  - `watchValue` 4-arg: index `address - base`; "?" when `bytes` is null, `address < base` or `address - base + size > n`; the 3-arg form calls it with `base = 0`.
  - `Agent::watchRange`: over the current profile's watches, `base = min(address)`, `len = max(address + size) - base`; false with no profile or no watches. `stateText` calls `watchValue(w, g.watchBytes.data(), g.watchBytes.size(), g.watchBase)`; `GameState` drops `ram`/`ramSize` and gains the two members. Remove the externs and the two `retro_get_memory_*` lines in `neuro_app.cpp`.
  - `neuro_app.cpp`, a static `void neuroWatches(App& app)` called at the end of `neuroTick` (after `publishTo`):
    ```cpp
    uint32_t base = 0; unsigned len = 0;
    const bool want = app.neuro.allowed() && app.host.isLoaded() && app.neuro.watchRange(base, len);
    const bool dram = want && len > neuro::kMaxWatchSpan;
    const uint64_t key = want ? ((uint64_t)dram << 63) | ((uint64_t)base << 32) | len : 0;
    if (key != app.neuroWatchKey) {      // a fresh id for a new range: the old buffer can never be read as the new one
        if (app.neuroWatchId) app.debug.views().clear(app.neuroWatchId);
        app.neuroWatchId = DebugViews::kNoId; app.neuroWatchBytes.clear(); app.neuroWatchKey = key;
        if (want) { ViewRequest r; r.kind = dram ? ViewRequest::Kind::Dram : ViewRequest::Kind::Memory; r.addr = dram ? 0 : base; r.len = dram ? 0 : len;
                    app.neuroWatchId = app.debug.views().set(DebugViews::kNoId, r); app.neuroWatchBase = dram ? 0 : base; }
    }
    if (app.neuroWatchId && t - app.neuroWatchAt >= 0.25) {       // 4 Hz: a 2 MiB Dram copy is not made every UI frame
        app.neuroWatchAt = t; ViewBuffer vb;
        if (app.debug.views().read(app.neuroWatchId, vb) && vb.ok) app.neuroWatchBytes = std::move(vb.bytes); else app.neuroWatchBytes.clear();
    }
    ```
    (`t` is `neuroTick`'s clock.) `neuroTick` hands the cached bytes to `tick` without a copy: after `GameState g = gameState(app);` do `g.watchBase = app.neuroWatchBase; g.watchBytes = std::move(app.neuroWatchBytes); app.neuro.tick(t, g); app.neuroWatchBytes = std::move(g.watchBytes);`. `static_assert(neuro::kMaxWatchSpan == DebugViews::kMaxMemoryBytes)` in this file. `read()` fails across a content switch (epoch) and before the first fill, so the state text says "?" rather than showing the previous game or stale bytes. Neuro never calls `iface->peek` itself.
  - Headless needs the copier too, because the headless loops bypass `emuRunOneFrame()`/`stepEmulation()`: in `headlessNeuroFrame` (app.cpp:153) add `syncDebug();` after `neuroTick(*this);` (moves `refreshSerial()` at 10 Hz as the windowed loop does; `fill()` only refills on a new serial), and in the inline headless loop after `host.runFrame()` (app.cpp:2556) add `if (!neuroCliUrl.empty()) fillDebugViews();` (3a's hook; inline it runs on this thread). `neuroHeadlessReport` additionally prints the state line:
    ```cpp
    std::string st = app.neuro.stateText(gameStateForReport(app));   // same builder neuroTick uses, with the cached bytes
    for (char& c : st) if (c == '\n') c = '|';
    std::fprintf(stderr, "[neuro] state: %s\n", st.c_str());
    ```
    (expose `gameState` to `neuroHeadlessReport` by moving the call inside `neuro_app.cpp`; it is already file-local there.)
  - `fillDebugViews()` (= `DebugSession::fillViews()`) has no gate and `wakeViews()` is gated only on "a core and content" (Interfaces, A3), so a Tools-window state never starves the watches; `debug.views()` is reachable while `debug.enabled()` is false. Nothing to add here; if a gate is found, it is a 3a bug.

- [ ] **Step 4: run, expect PASS.**
  ```bash
  cmake --build build -j4 && ctest --test-dir build -R "neuro_unit|neuro_watch_inline|neuro_app_inline" --output-on-failure
  ```
  Expected: PASS; `[neuro] state: ... cheat byte: 0xAB ...` in the log. If the profile does not match, print `titleOf`'s result once (the fixture title must match case-insensitively at the start of the title the library derives from `yarc.j64`; adjust `titles` to that, not the code).
- [ ] **Step 5: commit.**
  ```bash
  git add app/src/neuro app/src/app.h app/src/app.cpp app/cmake/VJNeuroTests.cmake app/tests/neuro_test.cpp
  git commit -m "feat(app): Neuro reads RAM watches through a DebugViews request (#967)

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task C4: lift the gate, the threaded twins, docs, PR

**Files:**
- Modify: `app/src/neuro/neuro_app.cpp` (`settingsFrom` :52, `neuroInit` :111-112, `drawNeuroTab` :171, header comment :1-10), `app/src/neuro/neuro_agent.h` (threading comment :1-7), `app/src/neuro/neuro_actions.h` (comment :15-17: `InputManager::agentControls`)
- Modify: `app/src/app.cpp` (threaded headless branch :2534-2557; the `inputPoll` comment from C2)
- Modify: `app/cmake/VJNeuroTests.cmake` (header comment :1-8; `neuro_off_threaded` :26-31)
- Modify: `app/tests/neuro_test.cpp` (`loopback-threaded` mode)
- Modify: `docs/neuro.md` (:30-34, :130-131), `app/README.md` (:847-860), `docs/agent/testing.md` (the Neuro/threaded test rows)
- Test: `neuro_on_threaded`, `neuro_watch_threaded`, `neuro_loopback_threaded` (CTest)

**Interfaces:**
- Consumes: C1-C3.
- Produces: none (the three CTest names above replace `neuro_off_threaded`).

- [ ] **Step 1: write the failing tests (CTest).** Replace the `neuro_off_threaded` block (VJNeuroTests.cmake:26-31) with:
  ```cmake
  # #967 phase 3c: with the emulation thread on, Neuro works.  The twins of the inline tests above, frame for frame.
  add_test(NAME neuro_on_threaded COMMAND vj_neuro_test app $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64" on)
  add_test(NAME neuro_watch_threaded COMMAND virtualjaguar --headless --emu-thread on --frames 90 --data-dir "${_nw}"
      --cheat 001F0000:AB --neuro ws://127.0.0.1:9 "${VJ_ROMS}/yarc.j64")
  add_test(NAME neuro_loopback_threaded COMMAND vj_neuro_test loopback-threaded)
  set_tests_properties(neuro_on_threaded neuro_loopback_threaded PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
  set_tests_properties(neuro_on_threaded neuro_watch_threaded neuro_loopback_threaded PROPERTIES LABELS emu_thread
      FAIL_REGULAR_EXPRESSION "FAIL |headless: FAIL|owner violation|ThreadSanitizer|off for this run")
  set_tests_properties(neuro_watch_threaded PROPERTIES TIMEOUT 120
      PASS_REGULAR_EXPRESSION "\\[neuro\\] state: .*cheat byte: 0xAB")
  ```
  In `neuro_test.cpp` `main`: `if (mode == "loopback-threaded") return loopbackRun(true) ...` (Windows: 77). `loopbackRun(true)` runs the C2 harness with the poll on its own thread: the UI thread keeps `configure`, `absorb(snap.agentProgress())`, `tick`, `publishTo` every 5 ms; a second thread loops `pollAgent(rep.snap, rep.player)` every 1250 us (four polls per UI tick, standing in for fast-forward) and writes the `rightNow`/`upNow` atomics and the per-poll vector. Assertions identical to inline: `pressedB == 9`, `rec == {1,1,1,0,0,0,1,1,1,0,0,0,1,1,1}` (compared with the vector the inline run produced, run both in one process: `loopbackRun(false)` first, keep its vector, `CHECK(thr == inl)`), `rightNow` false after the server drops, `upNow` false after hardcore.
  Run: expected FAIL (`neuro_on_threaded`: the run prints `[neuro] off for this run`; `neuro_watch_threaded`: no state line).
  ```bash
  cmake --build build -j4 && ctest --test-dir build -R "neuro_on_threaded|neuro_watch_threaded|neuro_loopback_threaded" --output-on-failure
  ```

- [ ] **Step 2: lift the gate.** `neuro_app.cpp`: delete `if (app.emuThreadThisRun()) s.enabled = false;` (:52) and its comment line in the header of `settingsFrom` ("and the emulation thread" -> removed); delete the `neuroInit` log block (:111-112); delete the hint (:171). Rewrite the three stale comments: `neuro_agent.h` threads paragraph -> "`tick`, `configure`, `publishTo` and `absorb` run on the UI thread; the buttons are played by `AgentPlayer` on the thread that polls input (the emulation thread when the app runs one), fed through InputSnapshot; only the WebSocket worker runs elsewhere, behind WsClient's queues."; `neuro_actions.h` :15-17 -> "...InputQueue turns accepted actions into a program the app's input poll plays one ControlSet per emulated frame (AgentPlayer, pollAgent) and ORs into Player 1"; `neuro_app.cpp` header unchanged otherwise.

- [ ] **Step 3: the threaded headless loop ticks Neuro in lock step.** In app.cpp's `if (threaded())` branch (:2534), add a third case after `ach.mockMode()`: when `!neuroCliUrl.empty()`:
  ```cpp
  for (unsigned i = 0; i < a.frames; i++) {            // one frame at a time, in real time, like the inline --neuro loop
      headlessNeuroFrame(hlT0, i);                     // neuroTick (+ syncDebug), then waits for frame i's time
      emu.advance(1);                                  // the replica runs in the core's poll on the emulation thread
      if ((i & 15) == 15) emu.call([this] { audio.clear(); });
      host.pinSnapshot();
      for (const OsdMessage& m : host.takeMessages()) std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
  }
  ```
  and after the `host.pinSnapshot();` that follows the branch (:2556) call `neuroHeadlessReport(*this)` for both modes (remove C2's inline-only call). The emulation thread fills the watch view after each frame (`emuRunOneFrame` -> `fillDebugViews`, 3a), the serial moves via `syncDebug()` inside `headlessNeuroFrame`.

- [ ] **Step 4: run, expect PASS.**
  ```bash
  cmake --build build -j4 && ctest --test-dir build -R "neuro|emu_thread_e2e|input_snapshot" --output-on-failure
  ```
  Expected: `neuro_unit`, `neuro_loopback`, `neuro_loopback_threaded`, `neuro_app_inline`, `neuro_on_threaded` (log shows `30 frames of input played` and `emulation thread)`), `neuro_watch_inline`, `neuro_watch_threaded`, `neuro_core_e2e` PASS; no `neuro_off_threaded`.
- [ ] **Step 5: full suite and ThreadSanitizer.**
  ```bash
  ctest --test-dir build -j4 --output-on-failure
  env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVJ_APP_TSAN=ON
  cmake --build build-tsan -j4 && ctest --test-dir build-tsan -L emu_thread --output-on-failure
  ```
  Expected: all PASS, `tsan.supp` unchanged (`git diff --stat app/tests/tsan.supp` empty). The reports to expect if something is wrong: `AgentPlayer`/`InputQueue` touched from the UI thread (it is emulation-only), or `GameState` read racing `neuroWatchBytes` (UI-only). Neither exists in this design.
- [ ] **Step 6: docs.**
  - `docs/neuro.md` :30-34, replace the "Emulation thread" paragraph with:
    "**Emulation thread:** Neuro works with Settings > Interface > *Emulation thread* on or off, and plays the same either way. The UI thread keeps the connection and the list of accepted moves and hands that program to the emulation thread through the input snapshot; the core's input poll plays it one step per emulated frame, so `press_buttons frames=3` is three emulated frames at any display rate and under fast-forward, never three UI frames. Game profile values (`watch`) are copied from RAM by the emulation thread after each frame (about 10 times a second) and read from there, so the state text can be up to a tenth of a second old; a profile whose watches lie more than 64 KiB apart makes the app copy the whole 2 MB of RAM at that rate while Neuro is connected. Menus, a mouse on Player 1, hardcore and a dropped connection let go of every button within one emulated frame."
  - `docs/neuro.md` :130-131: `ctest -R neuro` list -> "`neuro_unit`, `neuro_loopback` and `neuro_loopback_threaded` (a WebSocket server on 127.0.0.1; the second polls the buttons from another thread), `neuro_core_e2e` (the real core's joypad matrix, inline and threaded), `neuro_app_inline` / `neuro_on_threaded` (the app headless against that server) and `neuro_watch_*` (a profile watch read back from RAM)."
  - `app/README.md` :850-853: delete nothing; append to the bullet list "- Works with the emulation thread on or off: the buttons are played on the thread that polls input (see docs/neuro.md)." `docs/agent/testing.md`: rename the `neuro_off_threaded` entry (grep it; if absent, add the line under the emu_thread list) to the three threaded names.
  - `VJNeuroTests.cmake` header comment: replace the `neuro_off_threaded` line by the six lines of the new tests.
  - `app/src/input.h`, `input.cpp`, `app.cpp:476` comments are already gone with C2; `grep -rn "agentControls\|off for this run\|neuro_off_threaded" app docs` must be empty except `docs/app-emulation-thread-plan-phase1.md` (historical; do not edit).
- [ ] **Step 7: commit.**
  ```bash
  git add app docs/neuro.md
  git commit -m "feat(app): Neuro works with the emulation thread on (#967)

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```
- [ ] **Step 8: Studio check (windowed; only on the Studio).** After `uptime` on the Studio shows a load under 2 (`ssh studio-runner uptime`):
  ```bash
  VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
  bash $VJR run p3c-neuro env -u DEVELOPER_DIR bash -lc 'VJ_WINDOWED_OK=1 bash app/tests/emu_thread_windowed_check.sh build/virtualjaguar "$VJ_ROMS/yarc.j64" out/p3c'
  ```
  Expected: unchanged results (no `owner violation`). If Randy (`neuro-sdk/Randy`, `npm start`) is present on the Studio, also run `build/virtualjaguar --emu-thread on --neuro ws://localhost:8000 --perf-frames 1800 --show-window settings-neuro "$VJ_ROMS/yarc.j64"` and grep its log: `[neuro] ` connect line present, no `owner violation`, no `ThreadSanitizer`. Record both results for the PR body; skip Randy (say so) when it is not installed, the G1 gate covers it.
- [ ] **Step 9: PR (not merged by the agent).**
  ```bash
  git push -u libretro feat/967-emu-thread-phase3c
  gh pr create --repo libretro/virtualjaguar-libretro --base develop --head feat/967-emu-thread-phase3c \
    --title "feat(app): Neuro on the emulation thread (#967 phase 3c)" --body-file ~/Workspace/Provenance/vj-eval/remote/emu-p3c/pr-body.md
  ```
  Body (write it to that file first): what changed (the program/replica design, `lineage`/`firstFrame`, watches as a `DebugViews` request, gate and hint removed); the Review Focus 3 and #1025 pins by test name (the table above); the Studio results; `Refs #967`; ends with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`. Expected: all `App *` jobs and `ThreadSanitizer (emulation thread)` green. After merge remove the worktree: `git worktree remove ~/Workspace/Provenance/vj-wt/emu-p3c`.

---

## Gate G1: nothing is switched off

Runs on `libretro/develop` after 3a, 3b and 3c have all merged and before 3d's PR opens. It is the maintainer's rollout bar (design §1 table, amended 2026-10-16: nightlies default On "once nothing is switched off while it runs"), turned into one mechanical check that stays in CI, a TSan pass, a Studio windowed run with every feature on at once, and a whole-branch architecture review by a model that has not seen the work. Work happens on the 3d branch (`feat/967-emu-thread-phase3d`, worktree `~/Workspace/Provenance/vj-wt/emu-p3d`); G1a and G1b are its first two commits and G1c-G1d run on that head. A G1 finding is fixed in its own PR to develop (never inside 3d) and the 3d branch is rebased; 3d's flip commits are written only after G1 is green.

`<pre-3a>` below is `abc9054e` (develop when this plan was written). If unrelated `app/` commits landed after it, use the parent of 3a's merge commit instead: `git log --first-parent --oneline libretro/develop -- app/src/debug/debug_core.h | tail -1`, then `^1` of that.

### Task G1a: the mechanical bar (`no_threaded_limits`)

**Files:**
- Create: `app/tests/no_threaded_limits.sh`, `app/tests/threaded_branch_allowlist.txt`
- Modify: `app/cmake/VJEmuThreadTests.cmake` (append after `emu_thread_start_fallback`, as of abc9054e ~:177; and update the header comment list: delete the `*_threaded limits` line)

**Interfaces:**
- Consumes: the merged 3a-3c tree (the symbols and test names removed or renamed there).
- Produces: ctest `no_threaded_limits` (label `emu_thread`; registered only when a `bash` exists, so the MSVC row skips it).

- [ ] **Step 1: Branch and check that 3a-3c are all in.**

```bash
git -C ~/Workspace/Provenance/virtualjaguar-libretro fetch libretro
git -C ~/Workspace/Provenance/virtualjaguar-libretro worktree add ~/Workspace/Provenance/vj-wt/emu-p3d -b feat/967-emu-thread-phase3d libretro/develop
cd ~/Workspace/Provenance/vj-wt/emu-p3d
for f in app/src/debug/debug_views.h app/src/debug/debug_core.h; do test -f "$f" || echo "MISSING $f (3a/3b)"; done
/usr/bin/grep -c 'AgentPlayer' app/src/neuro/neuro_actions.h   # 3c
```

Expected: no `MISSING` line and a count of at least 1. Anything else: stop, 3d does not start.

- [ ] **Step 2: Write the script.** The greps use `GREP=/usr/bin/grep` (never the shell wrapper); `bad` prints the offending lines so a failure names its fix.

```bash
#!/usr/bin/env bash
# The Phase 3 bar (#967): with the emulation thread on, NOTHING is switched off.  usage: no_threaded_limits.sh <repo-root>
# 1 the gates and their symbols are gone   2 the user-facing "off while threaded" texts are gone   3 the old limit
# tests are gone and their replacements exist   4 no gdb option lock in app.cpp   5 only listed places ask "am I
# threaded?" (threaded_branch_allowlist.txt: each picks HOW something runs, never WHETHER)   6 tsan.supp did not grow
set -u
root="${1:?repo root}"; cd "$root" || exit 2
G="${GREP:-/usr/bin/grep}"; fail=0
bad() { echo "FAIL no_threaded_limits: $1"; shift; printf '    %s\n' "$@"; fail=1; }
X=(--exclude=no_threaded_limits.sh --exclude=threaded_branch_allowlist.txt)

h=$("$G" -rnE 'limitFeaturesForEmuThread|liftFeatureLimits|setThreadBlocked|threadBlocked|setHaltAvailable|haltDisabledReason' app/src app/tests app/cmake "${X[@]}")
[ -z "$h" ] || bad "a removed gate is referenced (delete it):" "$h"

h=$("$G" -rnE 'are off while the emulation thread is on|back on \(the emulation thread did not start\)|off for this run: the emulation thread|Off while the emulation thread is on|[Nn]ot available while Settings > Interface > Emulation thread|this version turns off the Tools|Halting is not available with the emulation thread' app/src app/tests app/cmake app/README.md docs/neuro.md docs/app-debug-tools.md "${X[@]}")
[ -z "$h" ] || bad "an 'off while threaded' text survives:" "$h"

h=$("$G" -rnE 'emu_thread_limits|gdb_locked_threaded|neuro_off_threaded' app/cmake app/tests docs/agent docs/neuro.md "${X[@]}")
[ -z "$h" ] || bad "a limit test is still named (delete or rename it):" "$h"
# What 3a (A9), 3b (B4, B5) and 3c (C4) register in place of the three limit tests.
for t in debug_views_threaded debug_on_threaded gdb_enabled_threaded gdb_hardcore_locked_on debug_e2e_threaded gdb_threaded_halt neuro_on_threaded neuro_loopback_threaded; do
  "$G" -rqE "NAME[[:space:]]+$t([[:space:]]|\))" app/cmake || bad "replacement test not registered: $t"
done

h=$(awk '/virtualjaguar_gdb_stub/{n=4} n>0{if ($0 ~ /= *"disabled"/) print FILENAME":"FNR": "$0; n--}' app/src/app.cpp)
[ -z "$h" ] || bad "app.cpp locks the GDB stub option (only achievements*.cpp may, under hardcore):" "$h"

P='emuThreadThisRun|(\.|->)threaded\(\)'
while read -r n f; do
  a=$(awk -v f="$f" '$1==f && $1 !~ /^#/ {print $2}' app/tests/threaded_branch_allowlist.txt); a=${a:-0}
  if [ "$n" -gt "$a" ]; then
    bad "$f asks 'am I threaded?' $n times, allowed $a (a feature must not be disabled by it; if it only picks HOW, raise the count with a reason):" \
        "$("$G" -nE "$P" "$f")"
  elif [ "$n" -lt "$a" ]; then echo "note: $f allows $a but has $n; tighten the allowlist"; fi
done < <("$G" -rE "$P" app/src --exclude='emu_thread.*' | cut -d: -f1 | sort | uniq -c | awk '{print $1, $2}')

n=$(awk '!/^[[:space:]]*(#|$)/' app/tests/tsan.supp | wc -l)
[ "$n" -le 1 ] || bad "app/tests/tsan.supp grew to $n entries (route the reader through a published value instead)"

[ "$fail" = 0 ] && echo "no_threaded_limits: OK"
exit "$fail"
```

`threaded_branch_allowlist.txt` is `<file> <max count>  # why`, one line per file. Starting content to be regenerated in Step 4:

```
# Files that may ask "am I threaded?" (emuThreadThisRun, x.threaded()), with the most hits each.  Every hit selects HOW
# something runs on the emulation thread or the UI thread; none may disable a feature (#967 Phase 3 bar).
app/src/app.cpp          0   # run(): loop choice; wireHost(): emuInput_/emuFastForward_ when threaded; start/stopEmuThread
app/src/netplay_ui.cpp   1   # self-test step: advance(1) + pinSnapshot instead of the inline update
app/src/netplay.cpp      1   # bounce count: one more emulated frame when threaded
app/src/achievements.cpp 2   # readMemory fallback guard; rc_client_idle runs on the emulation thread
app/src/ui.cpp           1   # "Restart the app to apply" (wanted != this run)
```

- [ ] **Step 3: Register it and watch it fail on the old tree.**

```cmake
# The Phase 3 bar (#967): nothing is switched off while the emulation thread is on.  A static check, so it also keeps
# the bar from coming back (docs/agent/testing.md).
find_program(VJ_BASH bash)
if(VJ_BASH AND NOT MSVC)
    add_test(NAME no_threaded_limits COMMAND ${VJ_BASH} "${CMAKE_CURRENT_LIST_DIR}/../tests/no_threaded_limits.sh" "${CMAKE_CURRENT_LIST_DIR}/../..")
    set_tests_properties(no_threaded_limits PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
endif()
```

Prove the script bites by running it on a copy of the pre-3a tree:

```bash
S=$(mktemp -d) && git archive abc9054e app | tar -x -C "$S" \
  && command cp app/tests/no_threaded_limits.sh app/tests/threaded_branch_allowlist.txt "$S/app/tests/" \
  && bash app/tests/no_threaded_limits.sh "$S"; echo "rc=$?"
```

Expected: `rc=1` with FAIL lines naming `limitFeaturesForEmuThread`, `setThreadBlocked`, `Not available while Settings > Interface > Emulation thread`, `neuro_off_threaded`, `app.cpp locks the GDB stub option`, and `app/src/neuro/neuro_app.cpp asks 'am I threaded?' 3 times, allowed 0` and `app/src/debug/op_ui.cpp ... 1 times, allowed 0` (the two disabling sites 3a/3c remove).

- [ ] **Step 4: Run it on the merged tree and regenerate the allowlist.**

```bash
bash app/tests/no_threaded_limits.sh "$PWD"; echo "rc=$?"
/usr/bin/grep -rnE 'emuThreadThisRun|(\.|->)threaded\(\)' app/src --exclude='emu_thread.*'
```

Expected on the first run: `rc=1`, with only the count line for `app/src/app.cpp` (`0` in the starting file). Read EVERY line the second command prints. Each must answer "HOW" (which mechanism runs: `host.threaded() ? emuFastForward_ : fastForward`, the `if (!emu.threaded()) return;` in `stopEmuThread`, the run decision) and none may disable, grey out, hide, refuse or lock anything because the thread is on. A line that does is a Phase 3 bug: stop, file the fix against develop. Then set the counts to the actual numbers, keep one `# why` per file, run again.

Expected: `no_threaded_limits: OK`, `rc=0`. Then `env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && ctest --test-dir build -R '^no_threaded_limits$' --output-on-failure`: `1/1 Test passed`.

- [ ] **Step 5: Commit.**

```bash
git add app/tests/no_threaded_limits.sh app/tests/threaded_branch_allowlist.txt app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
test(app): no_threaded_limits keeps every feature on while the emulation thread runs (#967)

Fails when a threaded-only gate, its user-facing text or its limit test comes back, when a file starts asking
"am I threaded?" without being listed, or when tsan.supp grows.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task G1b: the Studio drivers (`--show-window tools`, `--perf-halt`) and the gate script

The gate needs every tool open and an F9 halt in a run nobody is at the keyboard for. Test-support code, verified by the gate itself.

**Files:**
- Modify: `app/src/main.cpp:197` and the usage text at :27-33 (flag `--perf-halt`, `--show-window tools`), `app/src/app.h:104` (`bool perfHalt = false;` beside `perfPause`), `app/src/app.cpp:2021` (the `tools` branch, in the `--show-window` chain) and `:2077` (beside the `perfPause` line)
- Create: `app/tests/emu_thread_gate_check.sh`, `app/tests/emu_thread_gate_gdb.py`, `app/tests/neuro_gate_server.py`

**Interfaces:**
- Consumes: `openDebugWindow(DebugSession&, const std::string&)` (debug_ui.cpp:123), `DebugSession::findTool(id)->isOpen()`, `DebugSession::breakNow()/resume()/halted()/enabled()` (3b mirror), `perf.seen` (the loop's UI iteration counter, app.cpp:2077).
- Produces: log lines the gate asserts on, exactly:
  - `[app] tool <id> open=<0|1>` for `memory palette scanner cpu objects blitter textures scope`, once at start and once again while halted, the second time suffixed ` (halted)`;
  - `[perf] halt requested`, `[perf] halted=<0|1>`, `[perf] halt unavailable` (when `debug.enabled()` is false), `[perf] resumed`.

- [ ] **Step 1: Implement the flags.**
  - `--show-window tools`: in the chain at app.cpp:2021 add `else if (a.showWindow == "tools") openAllTools("");` where `openAllTools(const char* suffix)` is a lambda in `runInteractive` looping the eight ids above: `openDebugWindow(debug, id)` then `fprintf(stderr, "[app] tool %s open=%d%s\n", id, tool && tool->isOpen && tool->isOpen(), suffix)`. The `!emuThreadThisRun_ &&` guard on the old `openDebugWindow` line is already gone in 3a; this branch must not reintroduce it.
  - `--perf-halt`, next to the `perfPause` line: at `perf.seen == 120`: if `debug.enabled()` then `debug.breakNow()` and log `[perf] halt requested`, else log `[perf] halt unavailable`; at `== 240`: `openAllTools(" (halted)")` and log `[perf] halted=%d` from `debug.halted()`; at `== 420`: `debug.resume()` and log `[perf] resumed`. These are the calls F9 makes (app.cpp:1654-1657), nothing else.
  - Usage text: `[--perf-halt]` after `[--perf-pause]`; `--show-window memory|...|scope|tools`.

- [ ] **Step 2: Write the gate script** `app/tests/emu_thread_gate_check.sh <virtualjaguar> <rom> <out-dir>` in the style of `emu_thread_windowed_check.sh` (same `VJ_WINDOWED_OK=1` refusal, same `fail` accumulator, prints `emu_thread_gate_check: OK`). Every run greps its log for `owner violation|ThreadSanitizer|WARNING: ThreadSanitizer` (any hit fails). Three runs, all `--emu-thread on`:

  **Run A, tools + F9 halt + Neuro.** `python3 app/tests/neuro_gate_server.py 22350 "$out/neuro.txt" &` first, then
  `VJ_PERF_CSV="$out/a.csv" "$exe" --emu-thread on --perf-frames 720 --perf-input --perf-halt --show-window tools --neuro ws://127.0.0.1:22350 "$rom"`.
  Assert: eight `[app] tool <id> open=1` lines and eight `open=1 (halted)` lines; `[perf] halted=1` and `[perf] resumed`, no `halt unavailable`; `$out/neuro.txt` ends with `ok presses=<n>` where n >= 5. CSV (columns `iter_ms,emu_frames,...`, one row per UI iteration, header skipped): rows 130-410 all have `emu_frames==0` (the emulation is parked), their median `iter_ms` <= 17.5 and p99 <= 25 (60 Hz Studio display; `GATE_MEDIAN_MS` / `GATE_P99_MS` override for another refresh), and rows 440-720 have `emu_frames>0` in at least 200 of 280 (it ran again after Continue). The halted-rows criterion is "the window keeps presenting at display rate while halted".

  **Run B, gdb halt with the windows open.** Launch the app in the background with `--option virtualjaguar_gdb_stub=enabled --option virtualjaguar_gdb_wait=enabled --option virtualjaguar_gdb_port=22349 --perf-frames 900 --perf-input --show-window tools` and `VJ_PERF_CSV="$out/b.csv"`, then `python3 app/tests/emu_thread_gate_gdb.py 22349 5.0` (below), then `wait` for the app. Assert: probe exit 0; app exit 0 within 20 s; eight `open=1`; the longest run of consecutive `emu_frames==0` rows in `b.csv` is >= 200 and, after dropping its first 30 rows (the load), has median <= 17.5 and p99 <= 25; and `emu_frames>0` in at least 100 rows after it.

  **Run C, plain.** Same flags as the existing windowed check's `on` run plus `--show-window tools`, 600 frames, to prove the new default path presents with every tool open and nothing halted: >= 250 rows with `emu_frames>0`.

  `emu_thread_gate_gdb.py <port> <hold-seconds>`: `sys.path.insert(0, <repo>/test/tools)` and `import gdb_breakpoint_probe as p` for `cksum/pack/recv_packet/cmd`. Connect (retry 100 x 0.1 s), send `+`, `qSupported`, `QStartNoAckMode`; the `virtualjaguar_gdb_wait` halt means the 68K is stopped at its first instruction, so: `g` (registers, length > 0), `m4,8` (reply is 16 hex digits), `Z0,1000,2` then `z0,1000,2` (both `OK`); sleep `<hold-seconds>` printing `holding halt`; `D` (detach, which also clears breakpoints) or `c`; exit 0 printing `gdb gate: OK`. Any non-`OK` reply exits 1 with the packet.

  `neuro_gate_server.py <port> <result-file>`: stdlib only (`socket`, `hashlib`, `base64`, `json`, `struct`). Accept one connection; RFC 6455 handshake (`Sec-WebSocket-Accept` = base64(sha1(key + magic))); read masked client frames; after the client's `startup` message and its `actions/register`, every 1 s for 10 s send an unmasked text frame `{"command":"action","data":{"id":"g<N>","name":"press_buttons","data":"{\"buttons\":[\"a\"],\"frames\":3}"}}`; count `action/result` replies with `"success":true`; at the end write `ok presses=<n>` (or `FAIL presses=<n>`) to the result file and exit. Randy is not used: the Studio has `node` but no neuro-sdk checkout and the gate must not fetch code.

- [ ] **Step 3: Check it compiles and refuses to run here.**

```bash
env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j4 --target virtualjaguar 2>&1 | tail -2
bash app/tests/emu_thread_gate_check.sh build/virtualjaguar test/roms/yarc.j64 /tmp/none; echo "rc=$?"
python3 -I -c "import ast,sys; [ast.parse(open(f).read()) for f in sys.argv[1:]]" app/tests/emu_thread_gate_gdb.py app/tests/neuro_gate_server.py
```

Expected: build ok; `refusing: this opens a window; set VJ_WINDOWED_OK=1 on the measurement machine` and `rc=2`; no syntax error. Do not set `VJ_WINDOWED_OK` on this Mac.

- [ ] **Step 4: Commit.**

```bash
git add app/src/main.cpp app/src/app.h app/src/app.cpp app/tests/emu_thread_gate_check.sh app/tests/emu_thread_gate_gdb.py app/tests/neuro_gate_server.py
git commit -F - <<'EOF'
test(app): Studio gate for the emulation thread with every feature on (#967)

--show-window tools opens all eight Tools windows, --perf-halt does F9's break and continue at fixed UI frames,
and emu_thread_gate_check.sh asserts no owner violation, a presenting UI while halted by F9 and by gdb, and a
connected Neuro client, all with --emu-thread on.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task G1c: full suite, TSan, Studio windowed, evenness

No repository changes; results go into the PR body (Task D3). Windowed runs on the Studio only, after `uptime` shows a load under 2 (the Studio's ghrunner jobs push it to 20-100 without warning: wait and re-check before every arm). A sleeping display blocks the vsync swap forever: every windowed command below starts with `caffeinate -u -t 5`.

**Studio notes** (memory: reference_lan_build_runner; Phase 2 Task 11): `vjr.sh run` `%q`-quotes each argument and exports `DEVELOPER_DIR=CommandLineTools`, which breaks the app's CMake, so wrap every command as `bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc '<script>'`. `~/vj-remote/even-run.sh` already exists on the Studio.

- [ ] **Step 1: Ship and build both trees.**

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR status                      # load first; do not push while it is above 10
bash $VJR push emu-p3d HEAD
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'uptime; cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build app/build -j14 2>&1 | tail -1'
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'cmake -S app -B app/build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVJ_APP_TSAN=ON -DVJ_APP_LTO=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ >/dev/null && cmake --build app/build-tsan -j14 2>&1 | tail -1'
```

Expected: both build. (`.github/workflows/app.yml` `tsan` job, ~:213-224, is the recipe: `-DVJ_APP_TSAN=ON -DVJ_APP_LTO=OFF`, clang, RelWithDebInfo.)

- [ ] **Step 2: Full suite, then the label, Release.**

```bash
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'ctest --test-dir app/build -C Release -E shaders_gl -j6 2>&1 | tail -6; ctest --test-dir app/build -C Release -L emu_thread -j1 2>&1 | tail -4; /usr/bin/grep -c "owner violation" app/build/Testing/Temporary/LastTest.log || true'
```

Expected: `100% tests passed` for both ctest runs (the `-L emu_thread` run includes `no_threaded_limits`, `debug_views_threaded`, `debug_e2e_threaded`, `gdb_threaded_halt`, `neuro_on_threaded`, `neuro_loopback_threaded`) and a `0` from the grep. A netplay "no link within 30s" under load is load: re-run those alone first.

- [ ] **Step 3: The label under ThreadSanitizer, as CI runs it, plus macOS's abort setting.**

```bash
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'TSAN_OPTIONS="halt_on_error=1 abort_on_error=0 second_deadlock_stack=1 suppressions=$PWD/app/tests/tsan.supp" ctest --test-dir app/build-tsan -C RelWithDebInfo -L emu_thread --output-on-failure 2>&1 | tail -8'
```

Expected: `100% tests passed` (`tsan_canary` passes by failing, as designed) and no `ThreadSanitizer` text. A report is fixed by routing the reader through a published value (never `tsan.supp`; `no_threaded_limits` enforces it).

- [ ] **Step 4: Windowed gate, Release build.**

```bash
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'uptime; caffeinate -u -t 5; VJ_WINDOWED_OK=1 bash app/tests/emu_thread_gate_check.sh app/build/virtualjaguar test/roms/yarc.j64 $HOME/vj-remote/out/p3d-gate'
```

Expected: `emu_thread_gate_check: OK`. Keep the printed halted-row median / p99, the gdb-hold run's, and the Neuro `presses=` in the PR table.

- [ ] **Step 5: Windowed gate, TSan build.**

```bash
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'uptime; caffeinate -u -t 5; TSAN_OPTIONS="halt_on_error=1 abort_on_error=0 second_deadlock_stack=1 suppressions=$PWD/app/tests/tsan.supp" GATE_MEDIAN_MS=40 GATE_P99_MS=80 VJ_WINDOWED_OK=1 bash app/tests/emu_thread_gate_check.sh app/build-tsan/virtualjaguar test/roms/yarc.j64 $HOME/vj-remote/out/p3d-gate-tsan'
```

Expected: `OK` with no `ThreadSanitizer` line (the display-rate bounds are loosened: an instrumented build is several times slower; the point of this run is the report). Free-running windows are the only place the pump, the copier, the UI's request edits and Neuro's replica run concurrently for seconds.

- [ ] **Step 6: Evenness A/B/B/A with the Memory window open.** A = inline (`off`), B = threaded with the late latch (`latch`). 3600 UI frames each, yarc at 60 Hz, the Memory window open in all four so the copier's per-frame cost is in B. `even-run.sh` gates on load < 2 itself.

```bash
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'ROMS=test/roms/yarc.j64 EXTRA="--show-window memory" FRAMES=3600 bash ~/vj-remote/even-run.sh p3d off latch latch off'
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'ROMS=test/roms/yarc.j64 FRAMES=3600 bash ~/vj-remote/even-run.sh p3d-nowin latch'
bash $VJR run emu-p3d env -u DEVELOPER_DIR bash -lc 'for p in $HOME/vj-remote/out/even-p3d-yarc*-[1-4]-*.csv $HOME/vj-remote/out/even-p3d-nowin-yarc*.csv; do echo "== $p"; python3 app/tests/perf_trace_summary.py "${p%.csv}"; done'
```

Expected (the #1018 numbers for yarc, threaded, 60 Hz: 12 repeats + 31.5 skips per minute): the two B runs within their own run-to-run spread of each other and of 12 + 31.5/min; B's emulation-thread frame cost (`VJ_PERF_TRACE` end - start) with the window open versus the no-window arm is the copier's cost. A larger cost or worse evenness goes into the PR as a finding with its numbers; it is reported, not tuned, in this task.

### Task G1d: whole-branch architecture review (Fable)

No repository changes. A model that did not write 3a-3c reads the merged code against the design and tries to break it. The findings go into 3d's PR body.

- [ ] **Step 1: Collect the inputs.**

```bash
cd ~/Workspace/Provenance/vj-wt/emu-p3d
git diff --stat abc9054e..HEAD -- app/ | tail -3        # <pre-3a> as above
git diff abc9054e..HEAD -- app/ > "$(mktemp -d)/phase3.diff"   # note the path it prints below for the prompt
```

- [ ] **Step 2: Dispatch it** with the Agent tool: `subagent_type: general-purpose`, `model: fable` (not a fork, not Opus, not given any conclusion of this plan), prompt:

```
You are reviewing the merged Phase 3 work of the "emulation thread" feature of a desktop Atari Jaguar emulator
(C++17 app/, a single-threaded C89 core that must not change). Repo: ~/Workspace/Provenance/vj-wt/emu-p3d (read-only:
do not edit, build, run tests or launch anything; use /usr/bin/grep, not `grep`). The change under review is
`git diff abc9054e..HEAD -- app/` (3a: debug views, 3b: halt + gdb stub, 3c: Neuro). The spec is
docs/app-emulation-thread-design.md sections 2, 5, 6, 8 and its 2026-10-20 amendments, plus docs/neuro.md
("Emulation thread").

The invariant: with Settings > Interface > Emulation thread on, only the emulation thread calls into the core
(every retro_* call, every vj_debug_iface entry point, every read or write of core memory), and nothing is switched
off. Find every way the merged code breaks it or the spec. Specifically:
1. Every call site of vj_debug_iface members, host.memoryRegion, retro_get_memory_data and DebugCore methods: which
   thread can reach each? Anything reachable from the UI thread without DebugSession::coreCall/corePost or
   EmuThread::debugCall is a finding.
2. Every field read by one thread and written by the other (App, DebugSession, DebugCore, DebugViews, InputSnapshot,
   neuro::Agent): is it atomic, mutex-guarded, or published through a snapshot? List the ones that are not.
3. Locks: draw the acquisition order of EmuCommands::mu_, DebugViews' mutex, DebugCore's halt mutex, InputSnapshot's
   and the config/option locks. Is a lock ever held across a vj_debug_iface call, a condition-variable wait, a
   debugCall, or the halt pump? Can the UI wait (debugCall / call) on a thread that is waiting on the UI?
4. The halt: the pump must return within a bounded wait so the core's GDBHalt() loop (src/debug/gdbtarget.c) keeps
   servicing its socket and halt timeout. Check shutdown while halted (quit while halted by F9 and by gdb: does the
   join finish well inside 3 s?), the Lane::Frame vs Lane::Debug drain placement (frame lane must never be drained
   inside the pump), and every EmuThread::call that can queue behind a halted frame (the give-up predicate: look for
   a window between the "cancelled" check and the task starting).
5. Content switches: DebugViews buffers and requests across a game reload, the per-core cry table, the OP snapshot,
   scanner state and Neuro watches. Can a window draw bytes from the previous game?
6. Neuro: AgentPlayer must press for exactly N emulated frames regardless of display rate and fast-forward; check
   generation handling (a stale program replacing a newer one, or the reverse), hardcore (the program must be
   emptied), link drops, and anything left of the old agentControls path.
7. Leftovers: any remaining code that disables, hides or refuses a feature because the thread is on (grep for
   emuThreadThisRun, threaded(), "this version", "while the emulation thread"), and any inline-only path that is
   reachable threaded.
8. Divergences from the design document's text, including where the design is silent and the code made a choice.

Report: a table of findings, each with severity (blocker / major / minor), file:line, what is wrong, and the
sequence of events that triggers it. Then a separate list of what you checked and found correct, saying how you
checked it. Do not pad: no style comments, no praise.
```

- [ ] **Step 3: Triage the report.** A blocker or major with a concrete trigger sequence is reproduced by a test or by reading the cited lines, then fixed in a PR to develop (or filed as an issue, if the maintainer defers it in the PR) before 3d's PR opens; never silently dropped. Minors become issues. The table (finding, severity, disposition, PR or issue number) goes verbatim into 3d's PR body (Task D3, Step 7). If the review is clean, say that and keep its list of what was checked.

---

## Sub-phase 3d: nightly builds default the emulation thread On

Branch `feat/967-emu-thread-phase3d`, worktree `~/Workspace/Provenance/vj-wt/emu-p3d` (created in G1a), merged last, after 3a, 3b, 3c and a green G1. It adds the build channel, the default rule (a stored choice always wins), the Settings and About text, the start-up log lines, the workflow and flatpak plumbing, one nightly-channel CI configure, and docs. The setting stays Off in releases and in `dev` builds (PR artifacts, local builds); headless runs stay inline. Reverting this PR alone restores Off everywhere. No file under `src/` or `libretro*.c` changes.

### Task D1: the channel, the default rule, `emuThreadWanted`

**Files:**
- Create: `app/src/build_channel.h`, `app/tests/emu_thread_default_test.cpp`
- Modify: `app/CMakeLists.txt` (the cache variable and an INTERFACE target `vj_build_channel` after the `VJ_DISCORD_APP_ID` block, as of abc9054e ~:72-76; link it into `virtualjaguar` in the block at ~:457 that defines `VJ_APP_VERSION`), `app/src/app.cpp:282` (`emuThreadWanted`), `app/src/app.h:245-249` (comment), `app/cmake/VJEmuThreadTests.cmake`

**Interfaces:**
- Consumes: `ui.emu_thread` read at app.cpp:211 (`emuThreadPref` -1/0/1).
- Produces: `build_channel.h` exactly as in the Interfaces section (`kBuildChannel`, `kEmuThreadDefault`, `emuThreadDefaultFor`, `emuThreadEffective`), with `#include <string_view>` and, at the end of the namespace, `static_assert(kEmuThreadDefault == emuThreadDefaultFor(kBuildChannel), "VJ_EMU_THREAD_DEFAULT disagrees with VJ_BUILD_CHANNEL (app/CMakeLists.txt)");` so any CMake drift fails the compile. CMake target `vj_build_channel` (INTERFACE) carrying `VJ_BUILD_CHANNEL="<channel>"` and `VJ_EMU_THREAD_DEFAULT=<1|0>`; ctest `emu_thread_default_unit`.

- [ ] **Step 1: Write the failing test** `app/tests/emu_thread_default_test.cpp` (same `CHECK` / `FAIL ` style as `emu_commands_test.cpp`; no SDL, no core):

```cpp
// "pref 0 is Off and pref 1 is On in every channel; only pref -1 follows the channel"  (Review Focus 4)
for (const char* ch : {"release", "nightly", "dev"}) {
    const bool def = emuThreadDefaultFor(ch);
    CHECK(emuThreadEffective(0, def) == false);
    CHECK(emuThreadEffective(1, def) == true);
    CHECK(emuThreadEffective(-1, def) == def);
}
CHECK(emuThreadDefaultFor("nightly") && !emuThreadDefaultFor("release") && !emuThreadDefaultFor("dev"));
CHECK(!emuThreadDefaultFor("") && !emuThreadDefaultFor("Nightly") && !emuThreadDefaultFor("nightlies"));
CHECK(emuThreadEffective(-5, true) && !emuThreadEffective(-5, false));   // any negative is "never chosen"
CHECK(!emuThreadEffective(2, true));                                      // only 1 is On
const std::string_view c = kBuildChannel;                                 // what THIS build was configured with
CHECK(c == "release" || c == "nightly" || c == "dev");
CHECK(kEmuThreadDefault == (c == "nightly"));
```

Register (after `late_latch_unit`'s block; the target links `vj_build_channel` so it sees the same defines as the app):

```cmake
add_executable(vj_emu_thread_default_test tests/emu_thread_default_test.cpp)
target_include_directories(vj_emu_thread_default_test PRIVATE src)
target_link_libraries(vj_emu_thread_default_test PRIVATE vj_build_channel)
vj_emu_thread_warnings(vj_emu_thread_default_test)
add_test(NAME emu_thread_default_unit COMMAND vj_emu_thread_default_test)
set_tests_properties(emu_thread_default_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 2: Run it, expect FAIL.**

`cmake -S app -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3` (use the full configure line from the plan header). Expected: the configure still succeeds (CMake treats the unknown `vj_build_channel` as a plain library name), then `cmake --build build -j4 --target vj_emu_thread_default_test` fails with `'build_channel.h' file not found`.

- [ ] **Step 3: Implement.** In `app/CMakeLists.txt`:

```cmake
set(VJ_BUILD_CHANNEL "dev" CACHE STRING "release | nightly | dev: the build channel (nightlies default the emulation thread on, #967)")
set_property(CACHE VJ_BUILD_CHANNEL PROPERTY STRINGS release nightly dev)
if(NOT VJ_BUILD_CHANNEL MATCHES "^(release|nightly|dev)$")
    message(FATAL_ERROR "VJ_BUILD_CHANNEL must be release, nightly or dev (got '${VJ_BUILD_CHANNEL}')")
endif()
set(VJ_EMU_THREAD_DEFAULT_VALUE 0)
if(VJ_BUILD_CHANNEL STREQUAL "nightly")
    set(VJ_EMU_THREAD_DEFAULT_VALUE 1)
endif()
add_library(vj_build_channel INTERFACE)
target_compile_definitions(vj_build_channel INTERFACE VJ_BUILD_CHANNEL="${VJ_BUILD_CHANNEL}" VJ_EMU_THREAD_DEFAULT=${VJ_EMU_THREAD_DEFAULT_VALUE})
```

(the check and the cache variable go before the first fetched dependency, so a bad value fails in under a second), and `target_link_libraries(virtualjaguar PRIVATE vj_build_channel)` in the `virtualjaguar` block. Write `build_channel.h` per the Interfaces section. Then:

```cpp
// app.cpp:282, replacing the one-liner:
bool App::emuThreadWanted() const { return emuThreadEffective(emuThreadPref, kEmuThreadDefault); }
```

(`#include "build_channel.h"` in app.cpp; update the app.h:245-249 comment to "-1 = never chosen: follows the build channel (nightly on, release and dev off)"; delete the "Phase 3's default" remark on the `saveConfig` line at app.cpp:249).

- [ ] **Step 4: Run, expect PASS, for all three channels.**

```bash
cmake --build build -j4 --target vj_emu_thread_default_test virtualjaguar && ctest --test-dir build -R '^emu_thread_default_unit$' --output-on-failure
for c in release nightly; do
  env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build-$c -DCMAKE_BUILD_TYPE=Release -DVJ_BUILD_CHANNEL=$c >/dev/null \
    && cmake --build build-$c -j4 --target vj_emu_thread_default_test && ctest --test-dir build-$c -R '^emu_thread_default_unit$' --output-on-failure
done
cmake -S app -B build-bogus -DVJ_BUILD_CHANNEL=bogus 2>&1 | /usr/bin/grep 'VJ_BUILD_CHANNEL must be'; echo "rc=${PIPESTATUS[0]}"
```

Expected: `1/1 Test passed` three times; the bogus configure prints `VJ_BUILD_CHANNEL must be release, nightly or dev (got 'bogus')` and `rc=1`. Then `trash build-release build-nightly build-bogus`.

- [ ] **Step 5: Commit.**

```bash
git add app/src/build_channel.h app/CMakeLists.txt app/src/app.cpp app/src/app.h app/tests/emu_thread_default_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): build channel and the emulation-thread default rule (#967)

VJ_BUILD_CHANNEL (release | nightly | dev, default dev) sets VJ_EMU_THREAD_DEFAULT (1 for nightly). The default
applies only while ui.emu_thread is unwritten: an explicit Off stays Off in nightlies and an explicit On stays On in
releases. Nothing is built with the nightly channel yet.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task D2: Settings and About text, start-up log lines, `emu_thread_default_smoke`

**Files:**
- Modify: `app/src/ui.cpp:258-267` (Interface tab hint) and `:476` (About), `app/src/app.cpp:1938-1943` (log lines, after the TV override), `app/cmake/VJEmuThreadTests.cmake`, `app/tests/threaded_branch_allowlist.txt` (`app/src/app.cpp` count +1)
- No change to `app/src/settings_reset.cpp`: `ui.emu_thread` is already `KeyClass::Interface` (settings_reset.cpp:39) and `saveConfig` erases the key while the choice is -1, so Reset restores the channel default; `settings_reset_test.cpp` already pins the class.

**Interfaces:**
- Consumes: `kBuildChannel`, `kEmuThreadDefault`, `emuThreadPref`, `emuThreadWanted()`, `emuThreadThisRun()`.
- Produces: two stderr lines on every start (headless included), exactly:
  - `[emu] channel=<c>, emulation thread default <on|off>, this run <on|off>`
  - `[emu] ui.emu_thread <unset|on|off> -> wanted <on|off>`

  and ctests `emu_thread_default_smoke_unset`, `_false`, `_true`, `_flag`.

- [ ] **Step 1: Write the failing smokes** (`VJEmuThreadTests.cmake`, after the `emu_thread_default_unit` block). The PASS regexes are derived from the configured channel, so the same file pins every channel's behaviour:

```cmake
# The default rule through the real app (Review Focus 4): the data dir's ui.emu_thread decides "wanted"; only an unset
# key follows the configured channel.  Headless stays inline unless --emu-thread says otherwise, so "this run" is off.
set(_vj_chan_def off)
if(VJ_BUILD_CHANNEL STREQUAL "nightly")
    set(_vj_chan_def on)
endif()
foreach(_p unset false true)
    set(_d "${CMAKE_CURRENT_BINARY_DIR}/emu-default-${_p}")
    file(MAKE_DIRECTORY "${_d}")
    file(REMOVE "${_d}/config.ini")
    if(NOT _p STREQUAL "unset")
        file(WRITE "${_d}/config.ini" "ui.emu_thread=${_p}\n")
    endif()
    if(_p STREQUAL "unset")
        set(_w "${_vj_chan_def}")
        set(_s unset)
    elseif(_p STREQUAL "true")
        set(_w on)
        set(_s on)
    else()
        set(_w off)
        set(_s off)
    endif()
    add_test(NAME emu_thread_default_smoke_${_p}
        COMMAND virtualjaguar --headless --frames 5 --data-dir "${_d}" "${VJ_ROMS}/yarc.j64")
    set_tests_properties(emu_thread_default_smoke_${_p} PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "\\[emu\\] channel=${VJ_BUILD_CHANNEL}, emulation thread default ${_vj_chan_def}, this run off.*\\[emu\\] ui\\.emu_thread ${_s} -> wanted ${_w}"
        FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
endforeach()
# --emu-thread wins over the stored choice for one run
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/emu-default-flag")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/emu-default-flag/config.ini" "ui.emu_thread=false\n")   # its own dir: ctest -j runs the smokes in parallel
add_test(NAME emu_thread_default_smoke_flag
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 --data-dir "${CMAKE_CURRENT_BINARY_DIR}/emu-default-flag" "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_default_smoke_flag PROPERTIES TIMEOUT 120 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "this run on.*\\[emu\\] ui\\.emu_thread off -> wanted off"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
```

Run `cmake -S app -B build >/dev/null && ctest --test-dir build -R '^emu_thread_default_smoke' --output-on-failure`. Expected: 4 tests, all `Failed` (the lines do not exist yet; the flag test also), with the Test output showing no `[emu] channel=` line.

- [ ] **Step 2: Implement the log lines** at app.cpp right after the `--tv-test-script` override (~:1943), before `if (a.tv)`:

```cpp
std::fprintf(stderr, "[emu] channel=%s, emulation thread default %s, this run %s\n", kBuildChannel, kEmuThreadDefault ? "on" : "off", emuThreadThisRun_ ? "on" : "off");
std::fprintf(stderr, "[emu] ui.emu_thread %s -> wanted %s\n", emuThreadPref < 0 ? "unset" : emuThreadPref ? "on" : "off", emuThreadWanted() ? "on" : "off");
```

  Settings (ui.cpp:260-267): the checkbox already shows `emuThreadWanted()`, which now includes the channel default; replace the hint's last two sentences ("While it is on, this version turns off the Tools ... Off by default.") with a note chosen by `kEmuThreadDefault`:
  - on: `"This is a nightly build, where it is on unless you turn it off here. Release builds default to off."`
  - off: `"Off by default here; nightly builds default to on."`

  passed as a `%s` argument of the same `ui::hint(...)` (it takes printf arguments, like the resume hint at ui.cpp:271); keep the `if (app.emuThreadWanted() != app.emuThreadThisRun()) ImGui::TextDisabled("Restart the app to apply.");` line. About (ui.cpp:476): `ImGui::Text("Virtual Jaguar %s", VJ_APP_VERSION);` becomes `ImGui::Text("Virtual Jaguar %s (%s build)", VJ_APP_VERSION, kBuildChannel);`.

- [ ] **Step 3: Run, expect PASS.**

```bash
cmake --build build -j4 --target virtualjaguar && ctest --test-dir build -R '^emu_thread_default_(unit|smoke)' --output-on-failure
bash app/tests/no_threaded_limits.sh "$PWD"
```

Expected: 5 passed. The second command reports `app/src/app.cpp asks 'am I threaded?' <n+1> times, allowed <n>`: the new `this run` ternary is a report, not a gate. Raise the `app/src/app.cpp` count by one in the allowlist and re-run: `no_threaded_limits: OK`.

  Then the nightly configure end to end: `env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build-nightly -DCMAKE_BUILD_TYPE=Release -DVJ_BUILD_CHANNEL=nightly >/dev/null && cmake --build build-nightly -j4 && ctest --test-dir build-nightly -R '^emu_thread_default|^smoke_' --output-on-failure -E shaders_gl`. Expected: all pass; `emu_thread_default_smoke_unset` now requires `default on ... wanted on`, `_false` requires `wanted off`. Then `trash build-nightly`.

- [ ] **Step 4: Commit.**

```bash
git add app/src/ui.cpp app/src/app.cpp app/cmake/VJEmuThreadTests.cmake app/tests/threaded_branch_allowlist.txt
git commit -F - <<'EOF'
feat(app): show the build channel and the emulation-thread default (#967)

Settings > Interface says which way the default goes in this build, About names the channel, and the start-up log
reports channel, default, the stored choice and this run. The smokes derive their expected output from the
configured channel, so a nightly configure checks the On default and an explicit Off still winning.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task D3: workflows, flatpak, docs, the PR

**Files:**
- Modify: `.github/workflows/app-package.yml` (`meta` job 76-128; the three `cmake -S app` invocations ~:193, ~:370, ~:638; the three job `env:` blocks ~:173, ~:353, ~:610; a new step in the `flatpak` job before "Build the bundle" ~:549), `app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml:80-92`, `.github/workflows/app.yml` (matrix ~:115), `app/README.md:523-552`, `docs/agent/testing.md:309-340`, `docs/app-emulation-thread-design.md` (§8 and §10 step 3), `app/packaging/README.md`
- `.github/workflows/release.yml:596-598` needs no change: it already passes `release_tag` (`nightly` on develop pushes, `prerelease` on `release/**`, the `v*` tag on tags) and `app-package.yml` derives the channel from that.

**Interfaces:**
- Consumes: `release_tag` (`nightly` | `prerelease` | `v*` | `pr<N>` | `dev`), `needs.meta.outputs.label`.
- Produces: `meta` output `channel` (`nightly` -> `nightly`, `v[0-9]*` -> `release`, anything else -> `dev`; `prerelease` and PR builds are `dev`, which is Off like a release); `-DVJ_BUILD_CHANNEL=` on the macOS, Windows and Linux app configures; the flatpak manifest line `- -DVJ_BUILD_CHANNEL=release`, rewritten to `nightly` by the flatpak job when `label == nightly`; one `app.yml` row configured `-DVJ_BUILD_CHANNEL=nightly`.

- [ ] **Step 1: `meta` emits the channel.** In the `case "$TAG"`: the `v[0-9]*)` branch gains `channel=release`; the `*)` branch gains
`case "$TAG" in nightly) channel=nightly ;; *) channel=dev ;; esac`. Add `echo "channel=$channel"` to the `{ ...; } >> "$GITHUB_OUTPUT"` group, `channel` to the echo at the end, and `channel: ${{ steps.m.outputs.channel }}` under `outputs:`.

- [ ] **Step 2: Pass it to the three configures** the way `SHORT_VERSION` is passed (a job env var, never an inline `${{ }}` in the script). In each of the macOS (~:173), Windows (~:353) and Linux (~:610) job `env:` blocks add `CHANNEL: ${{ needs.meta.outputs.channel }}`; add `-DVJ_BUILD_CHANNEL="$CHANNEL"` on the line that carries `-DVJ_PACKAGE_VERSION="$SHORT_VERSION"` at ~:196, ~:371 and ~:638 (macOS builds both architectures in one loop: one edit covers both).

- [ ] **Step 3: Flatpak.** Manifest `config-opts` (app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml, after `- -DVJ_APP_BUILD_TESTS=OFF`):

```yaml
      # app-package.yml's flatpak job rewrites this value to "nightly" for nightly builds (the manifest is committed, so
      # it is the only per-channel knob); render.sh's Flathub variant keeps "release".
      - -DVJ_BUILD_CHANNEL=release
```

  and in the `flatpak` job, before "Build the bundle":

```yaml
      - name: Stamp the nightly channel into the manifest
        if: needs.meta.outputs.label == 'nightly'
        run: |
          set -eu
          m=app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml
          sed -i 's/^\(      - -DVJ_BUILD_CHANNEL=\)release$/\1nightly/' "$m"
          grep -q -- '- -DVJ_BUILD_CHANNEL=nightly' "$m"
```

  The `grep` fails the job when the sed matched nothing (a reindented manifest). The `cache-key` already hashes the manifest, evaluated after this step, so nightly and release get different flatpak-builder caches: expected. The `ruby -ryaml` lint at ~:159 runs on `render.sh`'s output from the committed file and is unaffected. Check locally: `bash app/packaging/manifests/render.sh --version 1.2.3 --sums <any SHA256SUMS.txt> --out "$(mktemp -d)"` still ends without error, and `ruby -ryaml -e 'YAML.safe_load_file("app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml")' && echo yaml-ok` prints `yaml-ok`.

- [ ] **Step 4: One CI configure with the nightly channel.** `app.yml` matrix, after the `Linux x86_64` row:

```yaml
          # The nightly channel's On default is built and tested here, so it is exercised without waiting for a
          # nightly (emu_thread_default_smoke_* derive their expectations from this configure; #967).
          - name: 'Linux x86_64 (nightly channel)'
            os: ubuntu-latest
            cmake_extra: '-DVJ_BUILD_CHANNEL=nightly'
```

  Verify the workflows parse: `ruby -ryaml -e 'ARGV.each { |f| YAML.safe_load_file(f, aliases: true); puts "ok #{f}" }' .github/workflows/app.yml .github/workflows/app-package.yml`.

- [ ] **Step 5: Docs.**
  - `app/README.md` "Emulation thread" (:523-552): replace "off by default (nightly builds will turn it on once nothing is switched off while it runs)" with "**on** by default in nightly builds and **off** in releases (the build channel; About shows it). A stored choice always wins: Reset in Settings > Interface goes back to the channel's default. `dev` builds (pull-request artifacts, local builds) default to off; `-DVJ_BUILD_CHANNEL=nightly` builds the nightly default from source."; delete the paragraph "While it is on, this version switches off the Tools (debugger) windows and the GDB stub option; they move to the emulation thread in the next phase." (3a-3c should already have replaced it; confirm with `no_threaded_limits`), and say what a halt does threaded in one line each for the Tools and GDB if 3b's docs left it out.
  - `docs/agent/testing.md` ("App emulation thread tests", :309-340): add bullets for `no_threaded_limits` (what each of its six checks forbids, how to raise the allowlist honestly), `emu_thread_default_unit` and `emu_thread_default_smoke_*` (why four: a headless run is inline, so the stored-choice line is what pins the rule), the nightly-channel `app.yml` row, and `emu_thread_gate_check.sh` (Studio only, `VJ_WINDOWED_OK=1`, `--show-window tools`, `--perf-halt`).
  - `docs/app-emulation-thread-design.md` §8: after the 2026-10-20 amendment add "*Shipped (phase 3d):* as above; the default is `nightly` only, `dev` is Off, and `prerelease` builds are `dev`. The flatpak job stamps the channel by sed because the manifest's `config-opts` are committed." §10 step 3: append "3d merged after a green Studio gate (docs: PR #<n>)".
  - `app/packaging/README.md`: a short "Build channel" note: the three channels, `-DVJ_BUILD_CHANNEL`, where the packaging workflow sets it.

- [ ] **Step 6: Local checks.**

```bash
bash app/tests/no_threaded_limits.sh "$PWD"
env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j4 && ctest --test-dir build -E shaders_gl -j4 --output-on-failure 2>&1 | tail -5
```

Expected: `no_threaded_limits: OK`; `100% tests passed`. (Under heavy load the real-time netplay tests may flake: re-run them alone first.)

- [ ] **Step 7: Commit, push, open the PR.**

```bash
git add .github/workflows/app-package.yml .github/workflows/app.yml app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml app/packaging/README.md app/README.md docs/agent/testing.md docs/app-emulation-thread-design.md
git commit -F - <<'EOF'
ci(app): nightly builds default the emulation thread on (#967)

app-package.yml derives the channel from release_tag (nightly, v* -> release, else dev) and passes
-DVJ_BUILD_CHANNEL to the macOS, Windows and Linux configures; the flatpak job rewrites the committed manifest's
channel to nightly for nightly builds. app.yml configures one Linux row with the nightly channel so the On default is
built and tested without waiting for a nightly. Docs updated.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
git push -u libretro feat/967-emu-thread-phase3d
```

  Write the body to `~/Workspace/Provenance/vj-eval/remote/emu-p3d/pr-body.md`, filling every cell from Task G1c (run values) and G1d (the table):

```markdown
Refs #967 (phase 3d of 5: docs/app-emulation-thread-plan-phase3.md, design: docs/app-emulation-thread-design.md)

Nightly builds now default Settings > Interface > Emulation thread **On** for users who never chose; releases and
`dev` builds (PR artifacts, local builds) stay Off, and a stored choice always wins in both directions. Phase 3a-3c
(debug views, the halt and the GDB stub, Neuro) are merged: nothing is switched off while the thread runs, which is
what this PR's gate checks. Reverting this PR alone restores the Off default.

- `VJ_BUILD_CHANNEL` (release | nightly | dev, default dev) sets `VJ_EMU_THREAD_DEFAULT`; `emuThreadEffective(pref, default)`.
- Settings says which way the default goes; About shows the channel; the log prints `[emu] channel=..., emulation thread default ..., this run ...` and the stored choice.
- `app-package.yml` derives the channel from `release_tag`; the flatpak job rewrites the manifest's channel for nightlies; one `app.yml` row configures `-DVJ_BUILD_CHANNEL=nightly`.
- `no_threaded_limits` keeps the bar in CI: no threaded-only gate, no "off while the emulation thread is on" text, no old limit test, no unlisted `threaded()` branch, no growth of `tsan.supp`.

## Gate G1 (Mac Studio M4 Max, 60 Hz, <date>, head <sha>)

| check | result |
|---|---|
| full suite, Release (`-E shaders_gl`) | |
| `ctest -L emu_thread`, Release | |
| `ctest -L emu_thread`, ThreadSanitizer | |
| windowed gate, Release: tools open, F9 halt, Neuro | |
| windowed gate, Release: gdb halt held 5 s with tools open | |
| UI while halted, F9: median / p99 iter_ms | |
| UI while halted, gdb: median / p99 iter_ms | |
| Neuro presses answered | |
| windowed gate, ThreadSanitizer (no report, no owner violation) | |
| yarc repeats + skips / min: inline, Memory window open [runs] | |
| yarc repeats + skips / min: threaded, Memory window open [runs] (#1018: 12 + 31.5) | |
| emulation-thread frame cost, window open vs not (ms) | |

## Whole-branch architecture review (Fable, `git diff <pre-3a>..HEAD -- app/`)

| finding | severity | disposition |
|---|---|---|
| | | |

(or: no findings; checked: <its list>)

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

```bash
gh pr create --repo libretro/virtualjaguar-libretro --base develop --head feat/967-emu-thread-phase3d \
  --title "feat(app): nightly builds default the emulation thread on (#967)" \
  --body-file ~/Workspace/Provenance/vj-eval/remote/emu-p3d/pr-body.md
```

  Expected: the PR URL; on its checks, the new `App Linux x86_64 (nightly channel)` row, the `tsan` job and the package jobs (`channel` visible in the `meta` log line) go green. After the first push to develop the nightly job's log shows `-DVJ_BUILD_CHANNEL=nightly` in its configure and the flatpak step "Stamp the nightly channel into the manifest" runs. Remove the worktree when the PR merges: `git worktree remove ~/Workspace/Provenance/vj-wt/emu-p3d`.

## Races and the tests that pin them

The named concurrency cases across all four sub-phases, with the mechanism that closes each and the test that fails
if it reopens. (3b's own table above lists the halt cases in more detail.)

| Case | Mechanism | Test |
|---|---|---|
| A view buffer torn between the UI's read and the copier's swap | one mutex; the fill copies the request table out, calls the iface with no lock, installs under the lock only if the slot's `rev` is unchanged | `debug_views_unit` testConcurrent, testFillHoldsNoLock (A2) |
| A buffer from the previous game drawn after a reload | `DebugViews::epoch()` bumped by `onCoreIface()` on the emulation thread; `read()` fails for an older epoch | `debug_views_unit` testEpoch (A2); `debug_views_threaded` case 4 (A9) |
| A UI-side `setIface` mutating `bps_` / firing `closeAllTools` on the emulation thread | split into `onCoreIface()` (emulation side, atomics + epoch) and `syncCore()` (UI side, once per frame) | `debug_session_unit` testCoreSyncSplit (A3) |
| A paused window never refilled (no frame runs) | `wakeViews()` posts one fill to the debug lane; a parked loop wakes on the post | `debug_session_unit` testViewsFill (A3); `debug_views_threaded` case 3 (A9); `emu_thread_unit` testDebugLane (A1) |
| The frame lane drained inside a halt, or the debug lane not drained outside one | two deques; `loop()` drains both at the boundary, the pump drains `Lane::Debug` only | `emu_commands_unit` testLanes (A1); `debug_e2e_threaded` section 3 (B5) |
| A `call()` queued behind a halting frame | the give-up predicate; a cancelled task is skipped when drained | `emu_thread_unit` testCallGivesUp (B3); `debug_e2e_threaded` section 4 (B5) |
| The pump outwaits `GDBHalt()`'s socket service or timeout | the wait is `waitVerdict`'s `kPumpWait` (4 ms); the pump returns NONE | `debug_session_unit` testPumpNeverOutwaitsHook, testBoundedPumpReturnsNone (B1) |
| gdb resumed; the app's mirror stays "halted" and F9 resumes the next halt | `frameEnded()` after every `retro_run`; a verdict for a dead halt dropped by the serial check | testFrameEndedClosesForeignResume (B1), testStaleVerdictDropped (B2) |
| Quit while halted (F9 or gdb) past the 3 s join | `quitting_` / `g_signalQuit` -> `abortHalt` -> DETACH within one pump wait; a lane notify first | testDetachEndsPumpWithinOneWait, testAbortHaltDetaches (B1); `debug_e2e_threaded` 7; `gdb_threaded_halt` step 5 (B5) |
| Hardcore turned on from the UI while halted calls `iface->detach` off-thread | the DETACH verdict when halted, a lane post otherwise | testHardcoreGoesThroughTheLane (B2); `debug_e2e_threaded` 6 (B5) |
| A Neuro program replaced mid-play replays or drops frames | `lineage` + `firstFrame`; the replica keeps `played` within a lineage | `neuro_unit` testAgentPlayer 4 (C1), testAgentProgram 5 (C2) |
| A Neuro press of N frames lasting N UI frames instead | the replica advances once per core poll | `neuro_core_e2e` sessions 2-4 (C2); `neuro_loopback_threaded` (C4) |
| Hardcore / a drop / `release_all` leaving a button held | `publishTo()` after every `tick()`, unconditionally; `enabled=false` empties the replica at the next poll | `neuro_unit` testAgentProgram 2-4 (C2); `loopback` `rightNow` / `upNow` (C2, C4) |
| Neuro watch bytes read off the emulation thread | a `DebugViews` request; `watchBytes` is a UI copy | `neuro_watch_inline` / `_threaded` (C3, C4); TSan on the label |
| The nightly default overriding a stored choice | `emuThreadEffective(pref, default)`: only `pref < 0` follows the channel | `emu_thread_default_unit` (D1); `emu_thread_default_smoke_*` (D2) |
| A gate coming back silently in a later change | `no_threaded_limits` (static, in CI) | G1a |

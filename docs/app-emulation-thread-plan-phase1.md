# Emulation thread, Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Run the desktop app's emulation on its own thread behind a Settings > Interface > "Emulation thread" switch (Off by default in every build in Phase 1; nightlies turn it on by default in Phase 3). The app must be fully usable with the switch On: load, boot BIOS, unload, reset, pause, fast-forward, mute, save and load states, options, discs and cheats all work, and the features that are not yet thread-safe are switched off cleanly.

**Architecture:** One owner thread (`EmuThread`) is the only caller of the core: at each frame boundary it drains an ordered command queue (`EmuCommands`), waits until `PacingClock` (mode A: a wall-clock grid trimmed by the SDL audio queue) says the next frame is due, runs it and publishes the result. The UI thread hands input over through `InputSnapshot`, takes frames from `FrameExchange` (a triple buffer) and reads options, AV info and discs from the per-frame `EmuSnapshot` that `CoreHost` publishes. `CoreHost` keeps its public API: off the owner thread its readers answer from the pinned snapshot, its writers queue through `post`, and any other call is refused and counted. With vsync, the UI loop's late latch (`LateLatch`) waits until just before each vblank, handing input over as it arrives, then takes the newest frame.

**Tech Stack:** C++17 (app/), SDL3 (audio stream, events), Dear ImGui, CMake/CTest, the statically linked C89 core (unchanged), ThreadSanitizer (clang), GitHub Actions.

**Spec:** docs/app-emulation-thread-design.md

## Global Constraints

- C++17 in `app/`; the core stays single-threaded C89 and is not touched (no file under `src/` or `libretro*.c` changes).
- Only the emulation thread calls into the core: every `retro_*` call, `vj_debug_iface`, and every read or write of core memory.
- App CMake builds must NOT set `DEVELOPER_DIR` (it breaks the C++ toolchain; the `DEVELOPER_DIR=/Library/Developer/CommandLineTools` prefix is for the core's Makefile build only).
- Never launch the app with a window on the maintainer's Mac. Windowed checks run on the Mac Studio only (`vjr.sh`, ssh alias `studio-runner`); headless runs are fine anywhere.
- The setting is Off by default in every build (release, nightly, dev, PR) in Phase 1, and Phase 1 adds no build-channel flag. Nightlies default it On from Phase 3, when nothing is switched off while threaded; that plan adds the `VJ_BUILD_CHANNEL` plumbing (decided 2026-10-16).
- Input latency: the emulation thread takes the newest input snapshot right before each frame, the UI thread publishes input as each event arrives while it waits, and the late latch takes the newest frame just before the vblank (Task 15). Target: event-to-present within ~2 ms of the single-thread loop at 60 Hz. Where it misses (heavy core frames, see "Decided: input latency in mode A"), the PR says so with numbers.
- True Jaguar rate in mode A: 60.05445 Hz NTSC / 50.08013 Hz PAL, paced by the audio clock. Never quantize to the display.
- Conventional commits (`feat(app):`, `test(app):`, `ci(app):`, `docs(app):`), each message ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- If commit signing fails with a 1Password error, wait and retry. Never disable signing.
- Branch off `libretro/develop` (never `master`). PRs target `develop` with `Refs #967` in the body.
- Delete with `trash`, or `command rm -f`; never a bare `rm`, `cp` or `mv` (the shell aliases them to `-i`, and with no TTY they hang).

## Review Focus

These are the six failure modes most likely to slip past the per-task tests. Each one has a pinning test in the task named.

1. **A content switch shows the previous game's frame or option list.** Pinned by: `frame_exchange_unit` "a frame begun before a switch is never shown" (Task 1); `core_host_thread` "after a reload the UI sees the new option push" (Task 7); `emu_thread_e2e` "load while running" (Task 8); on screen, the threaded loop drops the previous texture on a content switch (Task 13).
2. **Shutdown while a `call()` is pending, or while the core is blocked inside `retro_run`.** Pinned by: `emu_thread_unit` "calls during shutdown each run exactly once, no deadlock" and "a thread stuck in the core is abandoned after the join timeout, and call() then runs nothing" (Task 4).
3. **A save-state thumbnail taken from a torn or previous-game frame.** Pinned by: `emu_thread_e2e` "inside the load call the core's buffer is empty" (Task 8) and `smoke_state_threaded`, which saves a state with its thumbnail and requires identical roundtrip hashes (Task 11).
4. **Audio-device loss, or a paused stream, stalling mode-A pacing.** Pinned by: `pacing_clock_unit` "a device that stops draining falls back to the wall clock" and "no audio clock: the wall clock at the core's period" (Task 3). App passes `-1` for a paused or muted stream (Task 11).
5. **A UI path still calling CoreHost directly while threaded.** Pinned by: `core_host_thread` "core calls off the owner are refused and counted" (Task 7); every `smoke_*_threaded` test fails on `owner violation` (Task 11); and the windowed check script greps the log for it (Task 13).
6. **Input latency regressing against the single-thread loop.** Pinned by: `late_latch_unit`'s pipeline model (Task 15), which runs the real `LateLatch` against a fake clock; measured by Task 18 from the `input_lat_ms` and `present_lat_ms` columns (Tasks 10 and 14).

## Execution notes (pre-flight scan, 2026-10-16)

A scan of this plan against develop after TV mode (#995) found these corrections. They are applied during execution
(the implementing task in brackets), not by editing the task text below:

- `InputManager::poll` also ticks TV mode's `holds_.frame()` once per emulated frame, so it loops it `overlayTicks`
  times like `overlay.tick()` (Task 5).
- The replacement `openContent`, `openNoContent` and `closeContent` keep TV mode's `tvShell().closeMenu()` and
  `input.clearHolds()` lines (Task 11).
- `CoreHost::overrideOption` / `clearOverride` off the owner queue a core-only helper, so the UI mirror is written
  only on the UI thread (Task 7).
- The threaded loop naps when vsync is on but the swap did not block (minimized or occluded window), drops the
  previous game's texture on a content switch, and draws with the shown frame's own aspect (Task 13).
- `emu_thread_e2e`'s "load while running" polls against a deadline instead of asserting after fixed sleeps, so it
  holds under load and TSan (Task 8). A headless `--tv-test-script` run stays inline (Task 9).

## Resolved spec ambiguities

| Spec says | Phase 1 does | Why |
|---|---|---|
| "nightlies default On (nightly build flag)" | Phase 1 defaults it Off in every build and adds no build-channel flag. `--emu-thread on|off` and the Settings switch are how it is tested. | Decided 2026-10-16. With the switch On, Phase 1 turns off RetroAchievements, netplay, the debugger tools and the GDB stub, so nightly users would lose features. Nightlies default it On in Phase 3, and that plan adds `VJ_BUILD_CHANNEL` (set by `app-package.yml` from its `release_tag`). Until then this deviates from the approved spec (section 8, Rollout). |
| "No regression in input latency" (mode A) | A late latch in the UI loop (Task 15), and an event-to-present column to measure it (Task 14). | Decided 2026-10-16. See "Decided: input latency in mode A". |
| Mode A: "Below target: run the next frame. Above target: sleep for the time the queue takes to drain back to target." | Frames run on a wall-clock grid at the core's period. The period is trimmed by at most ±0.5%, from the queue level smoothed over 0.5 s. A nearly empty queue (below 25% of target) refills at up to 2x; an overfull queue (above 3x target, so the device is not draining) uses the plain wall clock. | SDL's device pulls 512-frame chunks (10.7 ms). Triggering frames off the raw queue level would jitter frame starts by up to one chunk, and a 60 Hz display would then repeat and skip frames constantly. The trimmed grid keeps the sound card as the master clock (long-run rate = device consumption rate), and frame starts stay even. |
| "a 60.05 Hz core on a 60 Hz panel: one repeat in about 20 s" | The test asserts one **skipped** frame every ~18.3 s (2 to 5 in 60 s) and zero repeats. | The core runs faster than the panel (60.0545 > 60), so a frame is overwritten before it is shown. A repeat would need the core to run slower. |
| Save/load state: "thumbnail from FrameExchange" | Inside the save `call()`, the emulation thread copies `host.frame()`, which is the frame the state was serialized on. The UI thread encodes the PNG afterwards. | At that boundary this is the same pixels FrameExchange last received. It also holds no frame at all right after a load (empty buffer, so no thumbnail rather than the previous game's). A UI-side FrameExchange read could be one frame newer than the state. |
| Reset is a **command** (post) | Reset is a `call()`. | `retro_reset` can re-stage the CD BIOS (`libretro.c` ~7981, "Console reset re-runs the CD BIOS boot"). That writes `CoreHost::staged_`, which the BIOS panel reads directly. A click can wait one frame. |
| Disc eject/index are commands | They are posts. `DiscInfo` reaches the UI through the snapshot, refreshed after each disc command and each load. | Matches the spec. The audit's per-frame `discInfo()` from the menus becomes a snapshot read. |
| Option set/override/clear/reset are commands | `CoreHost` keeps a UI-side mirror of the option values. A write updates the mirror and `config.ini` on the UI thread at once, and reaches the core's copy through `post`. | Settings must show the new value in the same frame (otherwise the combo flickers back for one frame), and the config may only be written on the UI thread (spec section 5). |
| `EmuCommands: post, call<T>(fn) -> T` | `EmuCommands` is the queue (`post`, `drain`, `waitUntil`, `closeAndDrain`). `call`, `callFor` and `advance` live on `EmuThread`. | `call()` must know whether the thread is running, stopping, abandoned or inline, and whether the caller *is* the emulation thread (run inline, never deadlock). |
| `EmuSnapshot` (options, messages, events, perf, scope, debug views) | Phase 1's subset is in place: `EmuSnapshot` in `core_host.h` (options table, push count, fps, sample rate, aspect, discs), published by `CoreHost::publishSnapshot()` and pinned by `pinSnapshot()`. Other parts: an OSD message mailbox (mutex), atomic counters (crash events, frame count, min audio latency), and perf counters on the App (frames run, retro time, input latency, underruns). | This is the minimum the app needs to be usable with the switch On. Phase 2 moves `EmuSnapshot` to its own file and adds events and achievements. |
| "Slow calls: after 250 ms the UI shows a spinner" | Not in Phase 1. A `call()` blocks the UI thread exactly as today's single-thread loop blocks during a load. | Today's behaviour, so no regression. Listed in follow-up plan 2. |
| "Headless runs and existing tests use inline mode" | Headless runs are inline unless `--emu-thread on` is given, whatever the setting says. Interactive runs use `--emu-thread on/off` if given, otherwise the setting. | Keeps every existing ctest byte-identical. The threaded smoke tests opt in explicitly. |
| Setting On/Off | Takes effect at the next launch. Settings shows "Restart the app to apply" while the choice and the running mode differ. | Switching threads live would mean moving the core between threads mid-session, for no user benefit. |
| Fast-forward: "unthrottled or a set multiple" | 4x the core's rate on the wall clock, with the audio dropped. | The same 4 frames per 60 Hz UI frame that today's loop runs. |

## Decided: input latency in mode A

The spec asks for no input-latency regression, and for mode A (a free-running grid on the audio clock). On a 60 Hz
vsync display those two conflict, because the emulation grid's phase drifts against the display's:

- **Inline** (today): events are polled at the start of a UI iteration (just after a vblank), the frame runs at once
  and polls them, and the result is swapped at the next vblank.
- **Threaded mode A, unlatched** (Task 13 alone): the UI publishes the input at the same point. Then the next emulated
  frame starts somewhere in the following period (on average half a period later), finishes, and waits for the next UI
  iteration (another half period on average) before it is swapped at the vblank after that.

**Decision (the user, 2026-10-16): fix it in Phase 1 with a late latch in the UI loop (Task 15).** After the swap, the
UI thread waits until shortly before the next vblank (minus the measured UI build time), then takes the newest frame,
builds the UI and swaps. While it waits it pumps events and publishes the input the moment each one arrives, so the
emulation thread's next poll sees it wherever in the period that frame starts. Publishing only once, at the latch, would
leave event -> poll at a whole period on average and miss the target by about 8 ms. Task 14 adds the event-to-present
column that measures it.

The fake-clock model in `late_latch_unit` (Task 15) runs the real `LateLatch`: a 60 Hz display, a 60.05445 Hz core,
UI builds of 2 ms +- 0.3 ms and events uniform in time, over 22 000 display periods. It gives:

| core frame | loop | event -> core poll | event -> present |
|---|---|---|---|
| 5 ms | inline (today) | 8.3 ms | 25.0 ms |
| 5 ms | threaded, unlatched | 16.7 ms | 46.4 ms |
| 5 ms | threaded + late latch | 8.7 ms | 25.3 ms |
| 14 ms | inline (today) | 8.3 ms | 25.0 ms |
| 14 ms | threaded, unlatched | 16.7 ms | 55.3 ms |
| 14 ms | threaded + late latch | 8.7 ms | 34.2 ms |

So the late latch meets the target (within ~2 ms of inline at 60 Hz) for light core frames, but not for heavy ones.
With the latch, event -> present is about poll + core frame + half a period + the UI budget; inline it is poll + one
period. The two meet only while the core frame is shorter than about half a period minus the UI budget (~5 ms at
60 Hz). A heavy title such as Cybermorph (14 ms+ frames) stays about 9 ms behind inline. Only mode B (Phase 4) closes
that gap, by starting the emulated frame on the display tick.

Task 18 measures inline, threaded + late latch and threaded unlatched (`--late-latch off`) on the Studio. If threaded +
late latch is within ~2 ms of inline event -> present on every measured title, the PR is opened for review. Otherwise
the PR is a draft that puts the measured numbers next to this model, states that the target is missed and by how much,
and leaves the next step to the user (mode B is Phase 4). Either way it is not merged by the agent.

## Phase 1 boundary: what is routed, snapshotted or switched off while threaded

| Kind | Touch points |
|---|---|
| `call()` (UI waits, at most one frame) | `openContent` (load), `openNoContent` (load with no content), `bootBios` (SRAM flush + boot), `closeContent` (unload), `reset`, disc eject / disc index / disc append (`App::addDisc`; a disk insert can re-stage the CD BIOS, which writes `staged_`, which the BIOS panel reads), `saveState` (serialize + thumbnail frame copy), `loadState` (unserialize + cheats re-applied), `handleDrop`'s SRAM flush, the final SRAM flush when the loop exits, shutdown's unload (`callFor`, 3 s), headless `--expect-ram` (`cheatsHeadlessCheck`), headless audio clear |
| `post()` (fire and forget, in order) | `CoreHost::setOptionValue` / `resetOptionToDefault` / `overrideOption` / `clearOverride`, `setControllerDevice`, `sendKeyEvent`; the 5-second SRAM autosave; a cheat batch from the Cheats window (reset + every set, as one command) |
| Atomic flags (UI writes, emulation reads) | pause, fast-forward, mute, audio target (`EmuThread::setPaused/setFastForward/setAudioTargetMs`, `App::emuFastForward_/emuMute_`); `AudioOut`'s paused, target, ratio and underrun count |
| Snapshot (read safely by the UI) | frames (`FrameExchange`); option definitions, categories, visibility, locks and push count (`EmuSnapshot`, copied only when they change); fps, sample rate, aspect, discs, and the content identity a disc swap moves (`contentPath`, `contentName`, `discSwaps`) (`EmuSnapshot`); OSD messages (mutex mailbox); crash-event count, frame count, min audio latency (atomics); frames run, retro time, input latency, underruns (App atomics) |
| Read directly (written only inside a `call()`, while the UI waits) | `isLoaded`, `loadedPath`, `loadedName`, `stagedBios`, `controllerPorts`, `coreName`, `coreVersion`, `eventsActive`, `debugIface` |
| Switched off while threaded (this version) | **RetroAchievements**: `Achievements::init` is skipped, so `available()` is false and the menu is hidden; `rc_client_do_frame` reads core RAM and the UI reads its state. **Netplay**: the menu item and window are disabled, `--netplay*` with `--emu-thread on` exits 2, and the log/event taps are not installed (they write netplay state from inside `retro_run`). **Debugger Tools**: Memory, Palette, Scanner, Object list (including its "step one frame"), Blitter, Textures, Audio scope and CPU, via `DebugSession::setThreadBlocked` + `announceDebug = false`; the halt pump would run `haltedFrame` (SDL, GL, ImGui) on the emulation thread, so `haltedFrame` also refuses to run there. **GDB stub**: `virtualjaguar_gdb_stub` is locked to `disabled`, because a socket halt would hang every `call()`. |

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `app/src/frame.h` | new | `struct Frame` (moved out of `core_host.h`), shared by CoreHost, FrameExchange and VideoOut |
| `app/src/frame_exchange.h` | new | Lock-free triple buffer of finished frames with content epochs (each slot carries the input publish its frame polled) |
| `app/src/emu_commands.h` | new | Ordered multi-producer command queue: post, drain, timed wait, close-and-drain |
| `app/src/pacing_clock.h` | new | Mode A pacing arithmetic (wall-clock grid trimmed by the audio queue), pure |
| `app/src/emu_thread.h`, `app/src/emu_thread.cpp` | new | The emulation thread: lifecycle, frame loop, `post`/`call`/`callFor`/`advance`, inline mode |
| `app/src/input_snapshot.h` | new | Newest input state from the UI thread to the core's poll; motion delivered exactly once; numbered publishes |
| `app/src/late_latch.h` | new | When the threaded UI loop stops waiting after a swap: the vblank grid from blocking swaps, the UI build budget, missed-vblank backoff; pure |
| `app/src/core_host.h`, `app/src/core_host.cpp` | modify | Owner thread, `EmuSnapshot` publish/pin, option mirror, marshalled writers, refused off-owner core calls, message mutex, atomics |
| `app/src/remap.h`, `app/src/remap.cpp` | modify | `CoreInput::carryMotion` / `clearMotion` |
| `app/src/input.h`, `app/src/input.cpp` | modify | `poll(captureKeyboard, overlayTicks)`, `coreInput()` |
| `app/src/audio.h`, `app/src/audio.cpp` | modify | Thread-safe fields (atomics), `update(adjustRatio)`, `paused()` |
| `app/src/perf.h` | modify | VJ_PERF_CSV columns `underruns`, `input_lat_ms`, `present_lat_ms` and their summary |
| `app/src/app.h`, `app/src/app.cpp` | modify | EmuThread wiring, threaded UI loop and its late latch, actions through `call()`, headless threaded path, feature limits, setting, latency measurement |
| `app/src/main.cpp` | modify | `--emu-thread on|off`, `--perf-input`, `--late-latch on|off` |
| `app/src/ui.cpp` | modify | Interface > Performance > "Emulation thread"; halt checks use `haltedNow()` |
| `app/src/settings_reset.cpp` | modify | `ui.emu_thread` belongs to the Interface group |
| `app/src/cheats.h`, `app/src/cheats.cpp`, `app/src/cheats_core.cpp` | modify | `doneSink`; threaded sinks batch and post |
| `app/src/debug/debug_session.h`, `app/src/debug/debug_session.cpp` | modify | `setThreadBlocked` |
| `app/src/netplay_ui.cpp` | modify | Netplay off while threaded |
| `app/src/debug/op_ui.cpp` | modify | Object list's "step one frame" refused while threaded |
| `docs/agent/testing.md` | modify | VJ_PERF_CSV columns, `--perf-input`, `--emu-thread` |
| `app/CMakeLists.txt` | modify | `VJ_APP_TSAN`, new sources, include `VJEmuThreadTests` |
| `app/cmake/VJEmuThreadTests.cmake` | new | Every new test (label `emu_thread`) |
| `app/tests/frame_exchange_test.cpp`, `emu_commands_test.cpp`, `pacing_clock_test.cpp`, `emu_thread_test.cpp`, `input_snapshot_test.cpp`, `audio_out_test.cpp`, `core_host_thread_test.cpp`, `emu_thread_e2e.cpp`, `perf_csv_test.cpp`, `late_latch_test.cpp` | new | Unit and end-to-end tests |
| `app/tests/emu_thread_windowed_check.sh` | new | Windowed check for the Mac Studio only |
| `app/tests/tsan.supp` | new | ThreadSanitizer suppressions (starts empty) |
| `app/tests/tsan_canary.cpp` | new | A deliberate data race that proves the TSan build really is instrumented |
| `app/tests/settings_reset_test.cpp`, `app/tests/cheats_test.cpp`, `app/tests/debug_session_test.cpp` | modify | New checks |
| `.github/workflows/app.yml` | modify | `tsan` job |
| `app/README.md` | modify | "Emulation thread" section, layout rows |

## Before Task 1: the worktree

- [ ] **Create the implementation worktree off `libretro/develop`**

```bash
cd ~/Workspace/Provenance/virtualjaguar-libretro
git fetch libretro
git worktree add ~/Workspace/Provenance/vj-wt/emu-thread-p1 -b feat/967-emu-thread-phase1 libretro/develop
cd ~/Workspace/Provenance/vj-wt/emu-thread-p1
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j8
ctest --test-dir app/build -C Release -E shaders_gl --output-on-failure -j4
```

Expected: the build succeeds and every test passes. This is the baseline; record any test that already fails here and leave it alone.

All later commands run from `~/Workspace/Provenance/vj-wt/emu-thread-p1`.

### Task 1: FrameExchange (and `frame.h`)

**Files:**
- Create: `app/src/frame.h`, `app/src/frame_exchange.h`, `app/tests/frame_exchange_test.cpp`, `app/cmake/VJEmuThreadTests.cmake`
- Modify: `app/src/core_host.h:30-36` (move `struct Frame` out), `app/CMakeLists.txt:454` (include the new test file after `include(VJThemeTests)`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct vj::Frame { unsigned width, height; std::vector<uint32_t> pixels; uint64_t serial; }` (unchanged fields, new header)
  - `class vj::FrameExchange { struct Slot { Frame frame; float aspect; uint64_t epoch; }; Slot& writeBuffer(); void publish(); const Slot* takeNewest(); const Slot* current() const; void clear(); uint64_t epoch() const; }`
  - CMake helper `vj_emu_thread_warnings(target)` and the `emu_thread` test label.

- [ ] **Step 1: Write the failing test** `app/tests/frame_exchange_test.cpp`

```cpp
// FrameExchange (frame_exchange.h): the triple buffer that hands finished frames from the emulation thread to the UI
// thread (#967).  No SDL, no GL, no core.
//
//   vj_frame_exchange_test
#include "frame_exchange.h"

#include <atomic>
#include <cstdio>
#include <thread>

using vj::FrameExchange;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

// One frame whose every pixel is its serial, so a torn read shows up as a mixed frame.
void put(FrameExchange& x, uint64_t serial, unsigned w = 4, unsigned h = 2) {
    FrameExchange::Slot& s = x.writeBuffer();
    s.frame.width = w;
    s.frame.height = h;
    s.frame.serial = serial;
    s.frame.pixels.assign((size_t)w * h, (uint32_t)serial);
    s.aspect = 4.0f / 3.0f;
    x.publish();
}

bool uniform(const vj::Frame& f) {
    for (uint32_t p : f.pixels)
        if (p != (uint32_t)f.serial) return false;
    return true;
}

void testBasics() {
    FrameExchange x;
    CHECK(!x.takeNewest() && !x.current(), "nothing published: nothing to take");
    put(x, 1);
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 1 && uniform(s->frame), "the published frame is taken");
    CHECK(!x.takeNewest(), "nothing new: null (the UI draws current() again)");
    CHECK(x.current() && x.current()->frame.serial == 1, "current() is the last frame taken");
}

void testNewestWins() {
    FrameExchange x;
    put(x, 2);
    put(x, 3);
    put(x, 4);
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 4, "three published, the newest is taken (got %llu)",
          s ? (unsigned long long)s->frame.serial : 0ull);
}

void testReaderSlotStable() {
    FrameExchange x;
    put(x, 10);
    const FrameExchange::Slot* held = x.takeNewest();
    for (uint64_t k = 11; k <= 30; k++) put(x, k);
    CHECK(held && held->frame.serial == 10 && uniform(held->frame), "the writer never writes the slot the reader holds");
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 30, "then the newest of the twenty");
}

void testContentSwitch() {
    FrameExchange x;
    put(x, 40);
    x.clear();
    CHECK(!x.takeNewest() && !x.current(), "a frame published before the switch is never shown");
    FrameExchange::Slot& begun = x.writeBuffer();   // the emulation thread had started this one...
    begun.frame.width = 4;
    begun.frame.height = 2;
    begun.frame.serial = 41;
    begun.frame.pixels.assign(8, 41u);
    x.clear();                                      // ...when the load call switched the content
    x.publish();
    CHECK(!x.takeNewest(), "a frame begun before the switch is never shown either");
    put(x, 42);
    const FrameExchange::Slot* s = x.takeNewest();
    CHECK(s && s->frame.serial == 42, "the new game's first frame is shown");
    CHECK(x.current() && x.current()->epoch == x.epoch(), "current() belongs to the new epoch");
}

void testNoTearing() {
    FrameExchange x;
    const uint64_t kFrames = 3000;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (uint64_t k = 1; k <= kFrames; k++) put(x, k, 160, 120);
        done = true;
    });
    uint64_t last = 0, taken = 0, torn = 0, backwards = 0;
    for (;;) {
        const bool finished = done.load();
        const FrameExchange::Slot* s = x.takeNewest();
        if (s) {
            taken++;
            if (!uniform(s->frame)) torn++;
            if (s->frame.serial <= last) backwards++;
            last = s->frame.serial;
        }
        if (finished && !s) break;
    }
    writer.join();
    CHECK(torn == 0, "%llu of %llu taken frames were torn", (unsigned long long)torn, (unsigned long long)taken);
    CHECK(backwards == 0, "serials went backwards %llu times", (unsigned long long)backwards);
    CHECK(last == kFrames, "the last frame taken is the last published (%llu)", (unsigned long long)last);
}

}  // namespace

int main() {
    testBasics();
    testNewestWins();
    testReaderSlotStable();
    testContentSwitch();
    testNoTearing();
    std::printf(g_fail ? "frame_exchange_test: %d FAILED\n" : "frame_exchange_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Create `app/cmake/VJEmuThreadTests.cmake`:

```cmake
# Emulation thread (#967, docs/app-emulation-thread-design.md, docs/app-emulation-thread-plan-phase1.md).
# Every test here carries the label `emu_thread`: the ThreadSanitizer job in .github/workflows/app.yml runs exactly
# these (ctest -L emu_thread).
#
#   frame_exchange_unit   FrameExchange: newest wins, the reader's slot is never written, no tearing, content epochs

function(vj_emu_thread_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

add_executable(vj_frame_exchange_test tests/frame_exchange_test.cpp)
target_include_directories(vj_frame_exchange_test PRIVATE src)
target_link_libraries(vj_frame_exchange_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_frame_exchange_test)
add_test(NAME frame_exchange_unit COMMAND vj_frame_exchange_test)
set_tests_properties(frame_exchange_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

In `app/CMakeLists.txt`, inside `if(VJ_APP_BUILD_TESTS)`, after `include(VJThemeTests)`, add:

```cmake
    include(VJEmuThreadTests)
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j8 --target vj_frame_exchange_test`
Expected: compile error `frame_exchange.h: No such file or directory`.

- [ ] **Step 4: Move `Frame` into `app/src/frame.h`**

```cpp
// One video frame as the app handles it: XRGB8888, tight pitch (width * 4 bytes).  Shared by CoreHost (the core's
// last frame), FrameExchange (frames handed to the UI thread) and VideoOut (the GL upload).
#pragma once

#include <cstdint>
#include <vector>

namespace vj {

struct Frame {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint32_t> pixels;
    uint64_t serial = 0;  // bumps every time new pixels arrive (a NULL "dupe" frame does not)
};

}  // namespace vj
```

In `app/src/core_host.h`, delete the block

```cpp
// One video frame, repacked to a tight pitch (width * 4 bytes), XRGB8888.
struct Frame {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint32_t> pixels;
    uint64_t serial = 0;  // bumps every time new pixels arrive (a NULL "dupe" frame does not)
};

```

and add `#include "frame.h"` after `#include "libretro.h"`.

- [ ] **Step 5: Implement `app/src/frame_exchange.h`**

```cpp
// Finished frames from the emulation thread to the UI thread (#967): a triple buffer.
//
// The writer (emulation thread) fills writeBuffer() and publish()es it; it never waits.  The reader (UI thread)
// takeNewest()s whatever was published last -- older unshown frames are skipped -- and never waits either.  Each side
// owns one of the three slots at any time and the third is the hand-over, so a slot is never written while it is read
// (no tearing).  The reader's slot stays valid, unchanged, until its next takeNewest().
//
// clear() starts a new epoch (a content switch: the load call runs it on the emulation thread).  A frame published
// before it, or begun before it and published after, is never handed out, so the previous game's last picture cannot
// reappear.  Serials come from CoreHost and stay monotonic across loads, which VideoOut::upload relies on.
#pragma once

#include "frame.h"

#include <atomic>
#include <cstdint>

namespace vj {

class FrameExchange {
public:
    struct Slot {
        Frame frame;
        float aspect = 4.0f / 3.0f;   // CoreHost::aspect() when the frame was made
        uint64_t epoch = 0;           // stamped by writeBuffer()
    };

    // ---- writer (the emulation thread) ----
    Slot& writeBuffer() {
        Slot& s = slots_[write_];
        s.epoch = epoch_.load(std::memory_order_acquire);
        return s;
    }
    void publish() {
        const unsigned prev = ready_.exchange(write_ | kFresh, std::memory_order_acq_rel);
        write_ = prev & kIndex;
    }

    // ---- reader (the UI thread) ----
    // The newest frame published since the last call; null when nothing new (or only a stale frame) arrived.
    const Slot* takeNewest() {
        if (!(ready_.load(std::memory_order_acquire) & kFresh)) return nullptr;
        const unsigned prev = ready_.exchange(read_, std::memory_order_acq_rel);
        read_ = prev & kIndex;
        hasRead_ = true;
        return fresh(slots_[read_]) ? &slots_[read_] : nullptr;
    }
    // The frame the last takeNewest() returned (to draw again), or null.
    const Slot* current() const { return hasRead_ && fresh(slots_[read_]) ? &slots_[read_] : nullptr; }

    // ---- either thread ----
    void clear() { epoch_.fetch_add(1, std::memory_order_acq_rel); }
    uint64_t epoch() const { return epoch_.load(std::memory_order_acquire); }

private:
    static constexpr unsigned kIndex = 3, kFresh = 4;
    bool fresh(const Slot& s) const { return s.epoch == epoch_.load(std::memory_order_acquire); }

    Slot slots_[3];
    std::atomic<unsigned> ready_{1};    // the hand-over slot's index, plus kFresh until the reader takes it
    unsigned write_ = 0;                // writer-owned
    unsigned read_ = 2;                 // reader-owned
    bool hasRead_ = false;              // reader-owned
    std::atomic<uint64_t> epoch_{0};
};

}  // namespace vj
```

- [ ] **Step 6: Run the test and the full build**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R frame_exchange_unit --output-on-failure`
Expected: `frame_exchange_test: OK`, 1/1 passed. The whole app builds, which proves the `Frame` move compiles everywhere.

- [ ] **Step 7: Commit**

```bash
git add app/src/frame.h app/src/frame_exchange.h app/src/core_host.h app/tests/frame_exchange_test.cpp \
        app/cmake/VJEmuThreadTests.cmake app/CMakeLists.txt
git commit -F - <<'EOF'
feat(app): FrameExchange triple buffer for the emulation thread (#967)

Frame moves to frame.h so CoreHost, FrameExchange and VideoOut share it.
Content epochs keep a previous game's frame from ever being shown.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 2: EmuCommands

**Files:**
- Create: `app/src/emu_commands.h`, `app/tests/emu_commands_test.cpp`
- Modify: `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: nothing.
- Produces: `class vj::EmuCommands { using Fn = std::function<void()>; using ErrorFn = std::function<void(const std::string&)>; using TimePoint = std::chrono::steady_clock::time_point; bool post(Fn&& fn); size_t drain(const ErrorFn& onError); void waitUntil(TimePoint deadline); void notify(); size_t closeAndDrain(const ErrorFn& onError); void reopen(); bool closed() const; }`. `post` moves from `fn` only when it accepts it (returns true).

- [ ] **Step 1: Write the failing test** `app/tests/emu_commands_test.cpp`

```cpp
// EmuCommands (emu_commands.h): the emulation thread's command queue (#967).  No SDL, no core.
//
//   vj_emu_commands_test
#include "emu_commands.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using vj::EmuCommands;
using Clock = std::chrono::steady_clock;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

double since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

void testFifo() {
    EmuCommands q;
    std::vector<int> v;
    for (int i = 0; i < 5; i++) CHECK(q.post([&v, i] { v.push_back(i); }), "post %d accepted", i);
    CHECK(q.drain(nullptr) == 5, "five ran");
    CHECK((v == std::vector<int>{0, 1, 2, 3, 4}), "in the order posted");
    CHECK(q.drain(nullptr) == 0, "nothing left");
}

void testWaits() {
    EmuCommands q;
    Clock::time_point t0 = Clock::now();
    q.waitUntil(t0 + std::chrono::milliseconds(50));
    CHECK(since(t0) >= 0.045, "nothing to do: waits until the deadline (%.3f s)", since(t0));

    t0 = Clock::now();
    std::thread poster([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.post([] {});
    });
    q.waitUntil(t0 + std::chrono::seconds(5));
    CHECK(since(t0) < 1.0, "a post wakes the wait early (%.3f s)", since(t0));
    poster.join();
    q.drain(nullptr);

    t0 = Clock::now();
    std::thread notifier([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.notify();
    });
    q.waitUntil(t0 + std::chrono::seconds(5));
    CHECK(since(t0) < 1.0, "notify() wakes it too (pause, stop, fast-forward) (%.3f s)", since(t0));
    notifier.join();
}

void testCloseAndDrain() {
    EmuCommands q;
    int ran = 0;
    for (int i = 0; i < 3; i++) q.post([&ran] { ran++; });
    CHECK(q.closeAndDrain(nullptr) == 3 && ran == 3, "what was queued still runs (%d)", ran);
    CHECK(q.closed(), "then the queue is closed");
    CHECK(!q.post([&ran] { ran++; }) && ran == 3, "and refuses new commands");
    q.reopen();
    CHECK(q.post([&ran] { ran++; }) && q.drain(nullptr) == 1 && ran == 4, "reopen() takes commands again");
}

void testErrors() {
    EmuCommands q;
    std::vector<std::string> errors;
    bool after = false;
    q.post([] { throw std::runtime_error("boom"); });
    q.post([&after] { after = true; });
    q.drain([&errors](const std::string& e) { errors.push_back(e); });
    CHECK(errors.size() == 1 && errors[0] == "boom", "a throwing command is reported");
    CHECK(after, "and the next command still runs");
}

void testProducers() {
    EmuCommands q;
    const int kProducers = 4, kEach = 2000;
    std::vector<std::pair<int, int>> got;   // (producer, sequence); appended by the drains on this thread
    std::atomic<int> finished{0};
    std::vector<std::thread> ps;
    for (int p = 0; p < kProducers; p++)
        ps.emplace_back([&q, &got, &finished, p] {
            for (int k = 0; k < kEach; k++) q.post([&got, p, k] { got.emplace_back(p, k); });
            finished++;
        });
    while (finished.load() < kProducers) q.drain(nullptr);
    for (std::thread& t : ps) t.join();
    q.drain(nullptr);
    std::vector<int> next(kProducers, 0);
    bool ordered = (int)got.size() == kProducers * kEach;
    for (const auto& g : got) {
        if (g.second != next[(size_t)g.first]) ordered = false;
        next[(size_t)g.first] = g.second + 1;
    }
    CHECK(ordered, "every producer's commands run once each, in its own order (%zu ran)", got.size());
}

}  // namespace

int main() {
    testFifo();
    testWaits();
    testCloseAndDrain();
    testErrors();
    testProducers();
    std::printf(g_fail ? "emu_commands_test: %d FAILED\n" : "emu_commands_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   emu_commands_unit     EmuCommands: FIFO per producer, timed waits, close-and-drain, errors` to the header list:

```cmake
add_executable(vj_emu_commands_test tests/emu_commands_test.cpp)
target_include_directories(vj_emu_commands_test PRIVATE src)
target_link_libraries(vj_emu_commands_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_emu_commands_test)
add_test(NAME emu_commands_unit COMMAND vj_emu_commands_test)
set_tests_properties(emu_commands_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_emu_commands_test`
Expected: compile error `emu_commands.h: No such file or directory`.

- [ ] **Step 4: Implement `app/src/emu_commands.h`**

```cpp
// The emulation thread's command queue (#967): any thread post()s, the emulation thread drain()s at a frame boundary,
// in the order posted (per producer).  The same mutex carries the thread's timed wait, so a post or notify() wakes a
// sleeping emulation thread at once.  closeAndDrain() runs whatever is still queued -- including what arrives while
// it runs -- and then refuses more, so no command posted before the stop is ever lost.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>

namespace vj {

class EmuCommands {
public:
    using Fn = std::function<void()>;
    using ErrorFn = std::function<void(const std::string&)>;
    using TimePoint = std::chrono::steady_clock::time_point;

    // False once closed; `fn` is then left untouched (the caller may run it itself).
    bool post(Fn&& fn) {
        {
            std::lock_guard<std::mutex> l(mu_);
            if (closed_) return false;
            q_.push_back(std::move(fn));
            wake_ = true;
        }
        cv_.notify_one();
        return true;
    }

    // Runs every queued command, oldest first.  A command that throws is reported and the rest still run.
    size_t drain(const ErrorFn& onError) {
        std::deque<Fn> batch;
        {
            std::lock_guard<std::mutex> l(mu_);
            batch.swap(q_);
        }
        for (Fn& f : batch) runOne(f, onError);
        return batch.size();
    }

    // Sleeps until `deadline`, or until a post() / notify() since the last wait.
    void waitUntil(TimePoint deadline) {
        std::unique_lock<std::mutex> l(mu_);
        cv_.wait_until(l, deadline, [this] { return wake_ || !q_.empty(); });
        wake_ = false;
    }

    void notify() {
        {
            std::lock_guard<std::mutex> l(mu_);
            wake_ = true;
        }
        cv_.notify_one();
    }

    size_t closeAndDrain(const ErrorFn& onError) {
        size_t n = 0;
        for (;;) {
            std::deque<Fn> batch;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (q_.empty()) {
                    closed_ = true;
                    return n;
                }
                batch.swap(q_);
            }
            for (Fn& f : batch) runOne(f, onError);
            n += batch.size();
        }
    }

    void reopen() {
        std::lock_guard<std::mutex> l(mu_);
        closed_ = false;
        wake_ = false;
    }

    bool closed() const {
        std::lock_guard<std::mutex> l(mu_);
        return closed_;
    }

private:
    static void runOne(Fn& f, const ErrorFn& onError) {
        try {
            f();
        } catch (const std::exception& e) {
            if (onError) onError(e.what());
        } catch (...) {
            if (onError) onError("unknown error");
        }
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Fn> q_;
    bool wake_ = false;
    bool closed_ = false;
};

}  // namespace vj
```

- [ ] **Step 5: Run it**

Run: `cmake --build app/build -j8 --target vj_emu_commands_test && ctest --test-dir app/build -C Release -R emu_commands_unit --output-on-failure`
Expected: `emu_commands_test: OK`.

- [ ] **Step 6: Commit**

```bash
git add app/src/emu_commands.h app/tests/emu_commands_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): EmuCommands, the emulation thread's ordered command queue (#967)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 3: PacingClock (mode A)

**Files:**
- Create: `app/src/pacing_clock.h`, `app/tests/pacing_clock_test.cpp`
- Modify: `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: nothing.
- Produces: `struct vj::PacingInput { double now, period, audioQueuedMs, targetMs; bool fastForward; }` and `class vj::PacingClock { static constexpr double kMaxTrim, kSmoothSec, kMaxLateFrames, kFastForward, kLowWater, kRefillRate, kHighWater; void reset(double now); double nextDue(const PacingInput&); void frameStarted(const PacingInput&); bool audioLocked() const; double trim() const; uint64_t droppedFrames() const; }`. `audioQueuedMs < 0` means there is no usable audio clock.

- [ ] **Step 1: Write the failing test** `app/tests/pacing_clock_test.cpp`

```cpp
// Mode A pacing (pacing_clock.h) against a fake clock and a fake SDL audio device: no thread, no sleeping, no core
// (#967).  The device pulls 512-frame chunks like SDL's (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, audio.cpp).
//
//   vj_pacing_clock_test
#include "pacing_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

using vj::PacingClock;
using vj::PacingInput;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

const double kCoreHz = 60.05445;     // NTSC field rate (libretro.c retro_get_system_av_info; JTRM)
const double kPeriod = 1.0 / kCoreHz;
const double kStreamHz = 48044.0;    // AudioOut::open: the stream runs at the rounded core rate
const double kBatch = 800.0;         // stereo frames per emulated frame (src/jerry/dac.c, NTSC)
const double kChunk = 512.0;         // frames per device pull
const double kTargetMs = 64.0;       // audio.target_ms default

struct Device {
    double rate = kStreamHz;   // stream frames this device consumes per second (its crystal)
    double queued = 0;         // stream frames queued
    double nextPull = 0;
    bool draining = true;
    double countFrom = 0;      // underruns before this time are not counted (start-up)
    uint64_t underruns = 0;
    void advanceTo(double t) {
        while (nextPull <= t) {
            if (draining) {
                if (queued < kChunk && nextPull >= countFrom) underruns++;
                queued = std::max(0.0, queued - kChunk);
            }
            nextPull += kChunk / rate;
        }
    }
    double ms() const { return 1000.0 * queued / kStreamHz; }
    void prime() { queued = kTargetMs / 1000.0 * kStreamHz; }
};

struct Run {
    std::vector<double> starts, ends;   // each emulated frame's start and end (= when it can be shown)
    uint64_t dropped = 0;
    bool lockedAtEnd = false;
};

// The emulation thread's loop on a fake clock: ask when the next frame is due, "sleep" until then, run a frame of
// cost(i) seconds, and queue its audio at its end (fast-forward drops it, as App does).
Run simulate(double secs, const std::function<double(size_t)>& cost, bool audio, Device& dev, bool ff = false) {
    PacingClock clock;
    Run r;
    double t = 0;
    clock.reset(t);
    size_t i = 0;
    while (t < secs) {
        dev.advanceTo(t);
        PacingInput in;
        in.now = t;
        in.period = kPeriod;
        in.audioQueuedMs = audio ? dev.ms() : -1.0;
        in.targetMs = kTargetMs;
        in.fastForward = ff;
        const double due = clock.nextDue(in);
        if (due > t + 1e-12) {
            t = due;
            continue;
        }
        clock.frameStarted(in);
        r.starts.push_back(t);
        t += cost(i++);
        dev.advanceTo(t);
        if (!ff) dev.queued += kBatch;
        r.ends.push_back(t);
    }
    r.dropped = clock.droppedFrames();
    r.lockedAtEnd = clock.audioLocked();
    return r;
}

struct Shown {
    int repeats = 0, skips = 0, vblanks = 0;
};

// A display refreshing at `hz` shows, at each vblank, the newest frame finished by then.
Shown present(const Run& r, double hz, double from, double to) {
    Shown s;
    size_t k = 0;
    long last = -1;
    for (long n = (long)std::ceil(from * hz); (double)n / hz < to; n++) {
        const double v = (double)n / hz;
        while (k < r.ends.size() && r.ends[k] <= v) k++;
        const long newest = (long)k - 1;
        if (last >= 0) {
            if (newest == last) s.repeats++;
            else if (newest > last + 1) s.skips += (int)(newest - last - 1);
        }
        last = newest;
        s.vblanks++;
    }
    return s;
}

// Frames per second between two times, from the first and last start inside them.
double rateBetween(const Run& r, double from, double to) {
    double first = -1, last = -1;
    size_t n = 0;
    for (double s : r.starts) {
        if (s < from || s >= to) continue;
        if (first < 0) first = s;
        last = s;
        n++;
    }
    return n > 1 ? (double)(n - 1) / (last - first) : 0.0;
}

// The largest distance of a frame-start interval from the mean interval, between two times (seconds).
double unevenness(const Run& r, double from, double to) {
    std::vector<double> d;
    for (size_t i = 1; i < r.starts.size(); i++)
        if (r.starts[i - 1] >= from && r.starts[i] < to) d.push_back(r.starts[i] - r.starts[i - 1]);
    if (d.empty()) return 1.0;
    double mean = 0;
    for (double x : d) mean += x;
    mean /= (double)d.size();
    double worst = 0;
    for (double x : d) worst = std::max(worst, std::fabs(x - mean));
    return worst;
}

double fiveMs(size_t) { return 0.005; }

void testTrueRateOn60Hz() {
    Device dev;
    dev.prime();
    Run r = simulate(70.0, fiveMs, true, dev);
    const double want = kStreamHz / kBatch;   // frames' worth of audio the device consumes per second
    const double got = rateBetween(r, 10.0, 70.0);
    const Shown s = present(r, 60.0, 10.0, 70.0);
    CHECK(std::fabs(got - want) / want < 1e-4, "frame rate %.5f Hz, the audio clock's %.5f Hz", got, want);
    CHECK(s.repeats == 0, "a 60.05 Hz core on a 60 Hz panel never repeats a frame (%d repeats)", s.repeats);
    CHECK(s.skips >= 2 && s.skips <= 5, "one skipped frame about every 18 s: %d in 60 s", s.skips);
    CHECK(unevenness(r, 10.0, 70.0) < 0.00005, "frame starts are even (worst %.4f ms off)",
          1000.0 * unevenness(r, 10.0, 70.0));
    CHECK(dev.underruns == 0, "no underrun (%llu)", (unsigned long long)dev.underruns);
    CHECK(r.dropped == 0 && r.lockedAtEnd, "nothing dropped, still following the audio clock");
}

void testFollowsTheCrystal() {
    for (double ppm : {300.0, -300.0}) {
        Device dev;
        dev.rate = kStreamHz * (1.0 + ppm * 1e-6);
        dev.prime();
        Run r = simulate(100.0, fiveMs, true, dev);
        const double want = dev.rate / kBatch;
        const double got = rateBetween(r, 50.0, 100.0);
        CHECK(std::fabs(got - want) / want < 3e-5, "a %+.0f ppm sound card: %.5f Hz, want %.5f", ppm, got, want);
        CHECK(dev.underruns == 0, "%+.0f ppm: no underrun (%llu)", ppm, (unsigned long long)dev.underruns);
    }
}

void testNoAudioClock() {
    Device dev;
    Run r = simulate(5.0, fiveMs, false, dev);
    const double got = rateBetween(r, 1.0, 5.0);
    CHECK(std::fabs(got - kCoreHz) / kCoreHz < 1e-6, "no audio clock (no device, a paused or muted stream): the wall clock at the core's rate (%.5f Hz)", got);
    CHECK(unevenness(r, 1.0, 5.0) < 1e-9, "exactly one period apart");
    CHECK(!r.lockedAtEnd, "not audio-locked");
}

void testDeviceStopsDraining() {
    Device dev;
    dev.prime();
    dev.draining = false;   // unplugged, or the stream stopped pulling
    Run r = simulate(5.0, fiveMs, true, dev);
    const double got = rateBetween(r, 1.0, 5.0);
    CHECK(std::fabs(got - kCoreHz) / kCoreHz < 1e-6, "the game keeps its own rate on the wall clock (%.5f Hz)", got);
    CHECK(!r.lockedAtEnd, "and stops following the dead audio clock");
}

void testRefillFromEmpty() {
    Device dev;   // a fresh stream: nothing queued
    dev.countFrom = 0.5;
    Run r = simulate(3.0, fiveMs, true, dev);
    double closest = 1.0;
    for (size_t i = 1; i < r.starts.size() && r.starts[i] < 0.5; i++) closest = std::min(closest, r.starts[i] - r.starts[i - 1]);
    CHECK(closest >= kPeriod / PacingClock::kRefillRate - 1e-9, "refilling runs at most %.0fx (closest starts %.2f ms)",
          PacingClock::kRefillRate, 1000.0 * closest);
    CHECK(dev.underruns == 0, "refilled within half a second: no underrun after it (%llu)", (unsigned long long)dev.underruns);
    CHECK(r.lockedAtEnd, "then it follows the audio clock");
}

void testSpikeCatchesUp() {
    Device dev;
    Run r = simulate(3.0, [](size_t i) { return i == 100 ? 0.030 : 0.005; }, false, dev);
    CHECK(r.dropped == 0, "one 30 ms frame (under two periods late) is caught up, not dropped");
    CHECK(std::fabs(r.starts[101] - r.ends[100]) < 1e-9, "the late frame runs at once");
    CHECK(std::fabs((r.starts[110] - r.starts[0]) - 110 * kPeriod) < 1e-9, "and the frames are back on their grid");
}

void testStallDropsOwedTime() {
    Device dev;
    Run r = simulate(3.0, [](size_t i) { return i == 100 ? 0.500 : 0.005; }, false, dev);
    CHECK(r.dropped >= 25, "a 500 ms stall drops the owed frames (%llu)", (unsigned long long)r.dropped);
    const double back = r.ends[100];
    int burst = 0;
    for (double s : r.starts)
        if (s >= back && s < back + 2.5 * kPeriod) burst++;
    CHECK(burst <= 3, "no burst after the stall: %d frames in the next 2.5 periods", burst);
}

void testFastForward() {
    Device dev;
    dev.prime();
    Run r = simulate(2.0, [](size_t) { return 0.001; }, true, dev, true);
    const double want = PacingClock::kFastForward * kCoreHz;
    const double got = rateBetween(r, 0.5, 2.0);
    CHECK(std::fabs(got - want) / want < 1e-6, "fast-forward: %.2f Hz, want %.2f", got, want);
    CHECK(!r.lockedAtEnd, "fast-forward ignores the audio clock");
}

}  // namespace

int main() {
    testTrueRateOn60Hz();
    testFollowsTheCrystal();
    testNoAudioClock();
    testDeviceStopsDraining();
    testRefillFromEmpty();
    testSpikeCatchesUp();
    testStallDropsOwedTime();
    testFastForward();
    std::printf(g_fail ? "pacing_clock_test: %d FAILED\n" : "pacing_clock_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   pacing_clock_unit     PacingClock mode A against a fake clock and a fake 512-frame-chunk audio device` to the header list:

```cmake
add_executable(vj_pacing_clock_test tests/pacing_clock_test.cpp)
target_include_directories(vj_pacing_clock_test PRIVATE src)
vj_emu_thread_warnings(vj_pacing_clock_test)
add_test(NAME pacing_clock_unit COMMAND vj_pacing_clock_test)
set_tests_properties(pacing_clock_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_pacing_clock_test`
Expected: compile error `pacing_clock.h: No such file or directory`.

- [ ] **Step 4: Implement `app/src/pacing_clock.h`**

```cpp
// Mode A pacing for the emulation thread (#967, design section 4): when is the next emulated frame due?
//
// The sound card is the master clock.  Frames are scheduled on a wall-clock grid at the core's period, and that period
// is trimmed by at most +/-0.5% from the SDL audio queue's level against its target (audio.target_ms), smoothed over
// half a second: more queued than the target -> frames come slightly slower, less -> slightly faster.  Over time the
// game runs at exactly the rate the audio device consumes samples (the true Jaguar rate on the sound card's crystal),
// and audio never drifts.  Not the raw "queue below target -> run now" rule: SDL's device pulls 512-frame chunks
// (10.7 ms), so frames triggered by those steps would start up to a chunk early or late, and a 60 Hz display would then
// repeat and skip frames all the time.
//
//   * no audio clock (no device, a paused or muted stream, fast-forward): the wall clock at the core's period;
//   * a device that is not draining (more than 3x the target queued): the wall clock until it drains again;
//   * nearly empty (under a quarter of the target): frames may start early, at most 2x the core's rate, until the
//     target is reached (a new stream, or after a stall);
//   * more than two frames late (a stall, a slow frame): the owed time is dropped, never run as a burst (#968);
//   * fast-forward: 4x the core's rate on the wall clock.
// Pure arithmetic on the times it is handed, so tests/pacing_clock_test.cpp drives it with a fake clock and device.
#pragma once

#include <algorithm>
#include <cstdint>

namespace vj {

struct PacingInput {
    double now = 0;               // seconds, any monotonic origin
    double period = 1.0 / 60.0;   // the core's frame period, seconds
    double audioQueuedMs = -1;    // queued audio; < 0 = no usable audio clock
    double targetMs = 64;         // audio.target_ms
    bool fastForward = false;
};

class PacingClock {
public:
    static constexpr double kMaxTrim = 0.005;       // the period moves at most +/-0.5% to follow the audio clock
    static constexpr double kSmoothSec = 0.5;       // queue-level smoothing (dozens of device pulls)
    static constexpr double kMaxLateFrames = 2.0;   // owed time beyond this many frames is dropped
    static constexpr double kFastForward = 4.0;     // fast-forward runs this many times the core's rate
    static constexpr double kLowWater = 0.25;       // below this fraction of the target: refill...
    static constexpr double kRefillRate = 2.0;      // ...at up to this multiple of the core's rate, up to the target
    static constexpr double kHighWater = 3.0;       // above this multiple of the target the device is not draining

    void reset(double now) {
        next_ = now;
        lastStart_ = now - 1.0;
        haveQ_ = overfull_ = refill_ = audio_ = false;
        trim_ = 1.0;
        init_ = true;
    }

    // When the next frame should start (absolute, on in.now's clock).  At or before in.now: start it now.
    double nextDue(const PacingInput& in) {
        if (!init_) reset(in.now);
        observe(in);
        const double base = basePeriod(in);
        if (in.now - next_ > kMaxLateFrames * base) {
            dropped_ += (uint64_t)((in.now - next_) / base);
            next_ = in.now;
        }
        if (refill_) return std::min(next_, std::max(in.now, lastStart_ + base / kRefillRate));
        return next_;
    }

    // The caller starts a frame now (in.now).
    void frameStarted(const PacingInput& in) {
        if (!init_) reset(in.now);
        const double base = basePeriod(in);
        if (in.now - next_ > kMaxLateFrames * base) next_ = in.now;
        lastStart_ = in.now;
        next_ += base * (audio_ ? trim_ : 1.0);
    }

    bool audioLocked() const { return audio_; }
    double trim() const { return audio_ ? trim_ : 1.0; }
    uint64_t droppedFrames() const { return dropped_; }

private:
    static double basePeriod(const PacingInput& in) { return in.fastForward ? in.period / kFastForward : in.period; }

    void observe(const PacingInput& in) {
        audio_ = false;
        if (in.fastForward || in.audioQueuedMs < 0 || in.targetMs <= 0) {
            haveQ_ = refill_ = overfull_ = false;
            trim_ = 1.0;
            return;
        }
        const double q = in.audioQueuedMs;
        if (!haveQ_) {
            filtQ_ = q;
            lastObs_ = in.now;
            haveQ_ = true;
        }
        filtQ_ += (q - filtQ_) * std::min(1.0, std::max(0.0, in.now - lastObs_) / kSmoothSec);
        lastObs_ = in.now;
        if (q > kHighWater * in.targetMs) overfull_ = true;
        else if (q < 2.0 * in.targetMs) overfull_ = false;
        if (overfull_) {
            refill_ = false;
            trim_ = 1.0;
            return;
        }
        if (q < kLowWater * in.targetMs) {
            refill_ = true;
        } else if (refill_ && q >= in.targetMs) {
            refill_ = false;
            filtQ_ = q;   // the filter lagged the refill; start the trim from where the queue really is
        }
        audio_ = true;
        const double e = std::max(-1.0, std::min(1.0, (filtQ_ - in.targetMs) / in.targetMs));
        trim_ = 1.0 + kMaxTrim * e;
    }

    double next_ = 0, lastStart_ = 0, filtQ_ = 0, lastObs_ = 0, trim_ = 1.0;
    bool init_ = false, haveQ_ = false, overfull_ = false, refill_ = false, audio_ = false;
    uint64_t dropped_ = 0;
};

}  // namespace vj
```

- [ ] **Step 5: Run it**

Run: `cmake --build app/build -j8 --target vj_pacing_clock_test && ctest --test-dir app/build -C Release -R pacing_clock_unit --output-on-failure`
Expected: `pacing_clock_test: OK`. If `testTrueRateOn60Hz` reports 6+ skips, the trim has not settled: the fault is in `observe()` (the sign of `e`, or the filter weight), not in the test's tolerance.

- [ ] **Step 6: Commit**

```bash
git add app/src/pacing_clock.h app/tests/pacing_clock_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): PacingClock mode A, the audio clock as master with an even frame grid (#967)

A wall-clock grid at the core's period, trimmed at most 0.5% by the smoothed
SDL queue level: true Jaguar rate on the sound card's crystal without the
10.7 ms device-pull jitter a raw queue trigger would add.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 4: EmuThread

**Files:**
- Create: `app/src/emu_thread.h`, `app/src/emu_thread.cpp`, `app/tests/emu_thread_test.cpp`
- Modify: `app/cmake/VJEmuThreadTests.cmake` (append), `app/CMakeLists.txt:280` (`VJ_APP_SOURCES`: the app links it from Task 11 on)

**Interfaces:**
- Consumes: `EmuCommands` (Task 2); `PacingClock`, `PacingInput` (Task 3).
- Produces:

```cpp
class vj::EmuThread {
public:
    struct Client {   // every method runs on the emulation thread (the caller, inline)
        virtual void threadStarted();  virtual void threadStopping();
        virtual bool canRun() = 0;     virtual double framePeriod() = 0;   virtual double audioQueuedMs() = 0;
        virtual void runFrame() = 0;   virtual void afterCommands();       virtual void commandFailed(const std::string&);
    };
    enum class Mode { Inline, Threaded };
    bool start(Mode, Client&);
    bool stop(std::chrono::milliseconds joinTimeout = std::chrono::milliseconds(3000));
    bool threaded() const;  bool abandoned() const;  bool onEmuThread() const;
    void post(std::function<void()> fn);
    template <class F> auto call(F&& fn) -> decltype(fn());                          // R must be default-constructible
    template <class F> bool callFor(std::chrono::milliseconds timeout, F&& fn);       // false = timed out / abandoned
    void inlineStep();  bool advance(unsigned n);
    void setPaused(bool);  bool paused() const;  void setFastForward(bool);  void setAudioTargetMs(double);
    uint64_t framesRun() const;  uint64_t droppedFrames() const;
};
```

- [ ] **Step 1: Write the failing test** `app/tests/emu_thread_test.cpp`

```cpp
// EmuThread (emu_thread.h): lifecycle, ordering, pause / advance, shutdown races, against a fake client -- no core,
// no SDL (#967).
//
//   vj_emu_thread_test
#include "emu_thread.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using vj::EmuThread;
using Clock = std::chrono::steady_clock;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

double since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct FakeClient : EmuThread::Client {
    std::atomic<int> frames{0};
    std::atomic<bool> loaded{true};
    std::atomic<bool> block{false};     // runFrame waits while set: a core stuck inside retro_run
    std::atomic<bool> blocked{false};
    std::atomic<int> started{0}, stopping{0}, afterCmds{0};
    std::thread::id startedOn;
    std::atomic<double> period{0.010};
    std::mutex errMu;
    std::vector<std::string> errors;

    void threadStarted() override {
        startedOn = std::this_thread::get_id();
        started++;
    }
    void threadStopping() override { stopping++; }
    bool canRun() override { return loaded.load(); }
    double framePeriod() override { return period.load(); }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {
        while (block.load()) {
            blocked = true;
            sleepMs(1);
        }
        frames++;
    }
    void afterCommands() override { afterCmds++; }
    void commandFailed(const std::string& what) override {
        std::lock_guard<std::mutex> l(errMu);
        errors.push_back(what);
    }
};

void testInline() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Inline, c), "inline start");
    CHECK(!e.threaded() && !e.onEmuThread(), "inline: no thread");
    int x = 0;
    e.post([&x] { x = 1; });
    CHECK(x == 1, "inline post runs at once");
    CHECK(e.call([&x] { return x + 1; }) == 2, "inline call runs at once");
    e.inlineStep();
    CHECK(c.frames == 1 && e.framesRun() == 1, "inlineStep runs one frame");
    CHECK(e.advance(3) && c.frames == 4, "advance(3) runs three more");
    CHECK(e.stop(), "stop is a no-op inline");
}

void testCallSeesPosts() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "threaded start");
    CHECK(e.threaded() && !e.onEmuThread(), "the caller is not the emulation thread");
    CHECK(c.started == 1 && c.startedOn != std::this_thread::get_id(), "threadStarted ran on the new thread");
    std::vector<int> seen;   // touched only on the emulation thread until the call returns
    for (int i = 0; i < 1000; i++) e.post([&seen, i] { seen.push_back(i); });
    const std::vector<int> got = e.call([&seen] { return seen; });
    bool inOrder = got.size() == 1000;
    for (size_t i = 0; inOrder && i < got.size(); i++) inOrder = got[i] == (int)i;
    CHECK(inOrder, "a call sees every earlier post applied, in order (%zu)", got.size());
    CHECK(e.call([&e] { return e.onEmuThread(); }), "a call runs on the emulation thread");
    CHECK(e.call([&e] { return e.call([] { return 7; }); }) == 7, "a call made on the emulation thread runs inline");
    CHECK(c.afterCmds.load() > 0, "afterCommands ran after a batch");
    CHECK(e.stop(), "stop");
    CHECK(c.stopping == 1, "threadStopping ran once");
}

void testProducers() {
    EmuThread e;
    FakeClient c;
    e.setPaused(true);
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    std::vector<std::pair<int, int>> got;   // emulation thread only, until the final call
    std::vector<std::thread> ps;
    for (int p = 0; p < 4; p++)
        ps.emplace_back([&e, &got, p] {
            for (int k = 0; k < 500; k++) e.post([&got, p, k] { got.emplace_back(p, k); });
        });
    for (std::thread& t : ps) t.join();
    const std::vector<std::pair<int, int>> all = e.call([&got] { return got; });
    std::vector<int> next(4, 0);
    bool ordered = all.size() == 2000;
    for (const auto& g : all) {
        if (g.second != next[(size_t)g.first]) ordered = false;
        next[(size_t)g.first] = g.second + 1;
    }
    CHECK(ordered, "four producers: each one's posts run in its order (%zu)", all.size());
    CHECK(e.stop(), "stop");
}

void testPauseAdvance() {
    EmuThread e;
    FakeClient c;
    c.period = 0.002;
    e.setPaused(true);
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start paused");
    sleepMs(50);
    const int f0 = c.frames;
    sleepMs(50);
    CHECK(c.frames == f0, "paused: no frames (%d -> %d)", f0, c.frames.load());
    CHECK(e.advance(5) && c.frames == f0 + 5, "advance(5) runs exactly five while paused (%d)", c.frames.load() - f0);
    e.setPaused(false);
    sleepMs(100);
    CHECK(c.frames > f0 + 15, "running again (%d)", c.frames.load() - f0);
    e.setPaused(true);
    c.loaded = false;
    CHECK(!e.advance(2), "advance() with nothing loaded gives up instead of waiting");
    CHECK(e.stop(), "stop");
}

void testPacingWallClock() {
    EmuThread e;
    FakeClient c;
    c.period = 0.010;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    sleepMs(300);
    const int n = c.frames;
    CHECK(n >= 10 && n <= 45, "about 30 frames at a 10 ms period in 0.3 s (got %d)", n);
    e.setFastForward(true);
    sleepMs(200);
    const int ff = c.frames - n;
    CHECK(ff > 30, "fast-forward runs %gx (got %d frames in 0.2 s)", vj::PacingClock::kFastForward, ff);
    CHECK(e.stop(), "stop");
}

void testStopJoin() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    const Clock::time_point t0 = Clock::now();
    CHECK(e.stop(), "stop joins");
    CHECK(since(t0) < 1.0, "promptly (%.3f s)", since(t0));
    CHECK(!e.threaded() && c.stopping == 1, "stopped");
    int x = 0;
    e.post([&x] { x = 5; });
    CHECK(x == 5, "after stop, post runs inline");
    CHECK(e.call([] { return 3; }) == 3, "after stop, call runs inline");
}

void testCallDuringShutdown() {
    EmuThread e;
    FakeClient c;
    c.period = 0.001;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    std::atomic<int> calls{0}, ran{0};
    std::atomic<bool> go{true};
    std::thread caller([&] {
        while (go.load()) {
            ran += e.call([] { return 1; });
            calls++;
        }
    });
    sleepMs(30);
    const Clock::time_point t0 = Clock::now();
    CHECK(e.stop(), "stop while another thread keeps calling");
    go = false;
    caller.join();
    CHECK(since(t0) < 2.0, "no deadlock (%.3f s)", since(t0));
    CHECK(ran.load() == calls.load() && calls.load() > 0, "every call ran exactly once (%d calls, %d ran)", calls.load(),
          ran.load());
}

void testCommandErrors() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    e.post([] { throw std::runtime_error("boom"); });
    CHECK(e.call([] { return 1; }) == 1, "the thread keeps running after a command throws");
    {
        std::lock_guard<std::mutex> l(c.errMu);
        CHECK(c.errors.size() == 1 && c.errors[0] == "boom", "the error reached commandFailed");
    }
    bool threw = false;
    try {
        e.call([]() -> int { throw std::runtime_error("bad call"); });
    } catch (const std::runtime_error& x) {
        threw = std::string(x.what()) == "bad call";
    }
    CHECK(threw, "a call's exception reaches its caller");
    CHECK(e.stop(), "stop");
}

// Last: leaves an abandoned thread behind.  Both objects are leaked on purpose -- the detached thread still uses them.
void testJoinTimeout() {
    EmuThread* e = new EmuThread;
    FakeClient* c = new FakeClient;
    c->block = true;
    CHECK(e->start(EmuThread::Mode::Threaded, *c), "start");
    for (int i = 0; i < 1000 && !c->blocked.load(); i++) sleepMs(1);
    CHECK(c->blocked.load(), "the fake core is stuck inside a frame");
    std::shared_ptr<std::atomic<bool>> late = std::make_shared<std::atomic<bool>>(false);
    CHECK(!e->callFor(std::chrono::milliseconds(50), [late] { *late = true; }), "callFor times out on a stuck thread");
    const Clock::time_point t0 = Clock::now();
    CHECK(!e->stop(std::chrono::milliseconds(100)), "stop gives up on a thread stuck in the core");
    CHECK(since(t0) < 1.0, "after the timeout (%.3f s)", since(t0));
    CHECK(e->abandoned() && !e->threaded(), "the thread is abandoned");
    bool ran = false;
    CHECK(e->call([&ran] {
        ran = true;
        return 1;
    }) == 0 && !ran, "after abandoning, call() runs nothing and returns a default value");
    CHECK(!e->callFor(std::chrono::milliseconds(10), [&ran] { ran = true; }) && !ran, "nor callFor()");
    c->block = false;   // let the abandoned thread finish its frame and exit; it runs the queued callFor on the way out
    for (int i = 0; i < 1000 && !late->load(); i++) sleepMs(1);
    CHECK(late->load(), "the command queued before the stop still ran, on the thread, once it got free");
}

}  // namespace

int main() {
    testInline();
    testCallSeesPosts();
    testProducers();
    testPauseAdvance();
    testPacingWallClock();
    testStopJoin();
    testCallDuringShutdown();
    testCommandErrors();
    testJoinTimeout();
    std::printf(g_fail ? "emu_thread_test: %d FAILED\n" : "emu_thread_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   emu_thread_unit       EmuThread: inline vs threaded, call/post ordering, pause/advance, stop/join timeout, shutdown races` to the header list:

```cmake
add_executable(vj_emu_thread_test tests/emu_thread_test.cpp src/emu_thread.cpp)
target_include_directories(vj_emu_thread_test PRIVATE src)
target_link_libraries(vj_emu_thread_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_emu_thread_test)
add_test(NAME emu_thread_unit COMMAND vj_emu_thread_test)
set_tests_properties(emu_thread_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_emu_thread_test`
Expected: CMake error `Cannot find source file: src/emu_thread.cpp`, or a compile error for `emu_thread.h`.

- [ ] **Step 4: Implement `app/src/emu_thread.h`**

```cpp
// The emulation thread (#967, docs/app-emulation-thread-design.md): the one thread that calls into the core.
//
// Threaded: a std::thread runs the frame loop: apply queued commands, wait until PacingClock says a frame is due (or
// for a command while paused / idle), run one frame through the Client, repeat.  The UI thread talks to it only
// through post() (fire and forget; applied at the next frame boundary, in order), call() (runs on the emulation
// thread and waits for the result: at most one frame) and the per-frame publishers (FrameExchange, CoreHost's
// EmuSnapshot, InputSnapshot).
//
// Inline: no thread.  post() and call() run on the caller at once and inlineStep() runs one frame -- the single-thread
// loop, headless runs and the existing tests, exactly as before.
//
// stop() gives up on a thread that does not finish within its timeout (stuck inside the core): the thread is detached
// and the object is "abandoned" -- call() returns a default value without running anything, and the owner must not
// touch the core or destroy this object (App exits the process).
#pragma once

#include "emu_commands.h"
#include "pacing_clock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace vj {

class EmuThread {
public:
    // What the loop drives.  Every method runs on the emulation thread (on the caller, inline).
    struct Client {
        virtual ~Client() = default;
        virtual void threadStarted() {}               // first thing on the new thread (App: CoreHost::setOwner)
        virtual void threadStopping() {}              // last thing on it
        virtual bool canRun() = 0;                    // content is loaded
        virtual double framePeriod() = 0;             // seconds per emulated frame (1 / the core's fps)
        virtual double audioQueuedMs() = 0;           // the audio clock; < 0 = none usable (wall clock)
        virtual void runFrame() = 0;                  // one emulated frame, then publish it
        virtual void afterCommands() {}               // a batch of commands ran (App: CoreHost::publishSnapshot)
        virtual void commandFailed(const std::string& what) { (void)what; }   // a post()ed command threw
    };
    enum class Mode { Inline, Threaded };

    EmuThread() = default;
    EmuThread(const EmuThread&) = delete;
    EmuThread& operator=(const EmuThread&) = delete;
    ~EmuThread();

    // Inline: remembers the client and never fails.  Threaded: starts the thread and returns once it runs; false when
    // no thread could be created (the caller stays inline).
    bool start(Mode mode, Client& client);
    // Runs what is queued, ends the loop, joins.  False when the thread did not finish within `joinTimeout`.
    bool stop(std::chrono::milliseconds joinTimeout = std::chrono::milliseconds(3000));

    bool threaded() const {
        const State s = state_.load();
        return s == State::Running || s == State::Stopping;
    }
    bool abandoned() const { return state_.load() == State::Abandoned; }
    bool onEmuThread() const { return std::this_thread::get_id() == threadId_.load(); }

    // Applied at the next frame boundary, in order.  Inline (or from the emulation thread itself): runs now.
    void post(std::function<void()> fn);
    // Runs `fn` on the emulation thread at the next frame boundary and returns its result (rethrowing what it threw).
    // Inline, or from the emulation thread itself: runs now.  Abandoned: returns R() and runs nothing.
    template <class F>
    auto call(F&& fn) -> decltype(fn());
    // call() for shutdown: gives up after `timeout`.  On false the command may still run later, so capture by value.
    template <class F>
    bool callFor(std::chrono::milliseconds timeout, F&& fn);

    void inlineStep();          // inline: one frame now
    bool advance(unsigned n);   // n frames now, even while paused, ignoring pacing; returns once they ran

    void setPaused(bool p) {
        if (paused_.exchange(p) != p) cmds_.notify();
    }
    bool paused() const { return paused_.load(); }
    void setFastForward(bool on) {
        if (ff_.exchange(on) != on) cmds_.notify();
    }
    void setAudioTargetMs(double ms) { targetMs_.store(ms); }
    uint64_t framesRun() const { return frames_.load(); }
    uint64_t droppedFrames() const { return dropped_.load(); }

private:
    enum class State { Inline, Starting, Running, Stopping, Stopped, Abandoned };
    void loop();
    void waitFinished();   // the thread has left its loop for good (or was abandoned)
    void notifyFrame();

    Client* client_ = nullptr;
    EmuCommands cmds_;
    std::thread thread_;
    std::atomic<State> state_{State::Inline};
    std::atomic<std::thread::id> threadId_{std::thread::id()};
    std::atomic<bool> stop_{false}, paused_{false}, ff_{false};
    std::atomic<double> targetMs_{64.0};
    std::atomic<unsigned> advanceLeft_{0};
    std::atomic<uint64_t> frames_{0}, dropped_{0};
    std::mutex mu_;
    std::condition_variable cv_;          // start and finish handshakes
    std::condition_variable advanceCv_;   // a frame ran
    bool ready_ = false, go_ = false, finished_ = false;   // under mu_
};

template <class F>
auto EmuThread::call(F&& fn) -> decltype(fn()) {
    using R = decltype(fn());
    if (abandoned()) return R();
    if (!threaded() || onEmuThread()) return fn();
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    std::future<R> done = task->get_future();
    if (!cmds_.post([task] { (*task)(); })) {   // the thread is finishing: run it here once it has gone
        waitFinished();
        if (abandoned()) return R();
        (*task)();
    }
    return done.get();
}

template <class F>
bool EmuThread::callFor(std::chrono::milliseconds timeout, F&& fn) {
    if (abandoned()) return false;
    if (!threaded() || onEmuThread()) {
        fn();
        return true;
    }
    auto task = std::make_shared<std::packaged_task<void()>>(std::forward<F>(fn));
    std::future<void> done = task->get_future();
    if (!cmds_.post([task] { (*task)(); })) {
        waitFinished();
        if (abandoned()) return false;
        (*task)();
        done.get();
        return true;
    }
    if (done.wait_for(timeout) != std::future_status::ready) return false;
    done.get();
    return true;
}

}  // namespace vj
```

- [ ] **Step 5: Implement `app/src/emu_thread.cpp`**

```cpp
#include "emu_thread.h"

#include <algorithm>
#include <exception>

namespace vj {

namespace {

using Steady = std::chrono::steady_clock;

double nowSec() { return std::chrono::duration<double>(Steady::now().time_since_epoch()).count(); }

Steady::time_point atSec(double s) {
    return Steady::time_point(std::chrono::duration_cast<Steady::duration>(std::chrono::duration<double>(s)));
}

}  // namespace

EmuThread::~EmuThread() {
    if (threaded()) stop();
}

bool EmuThread::start(Mode mode, Client& client) {
    const State s = state_.load();
    if (s == State::Running || s == State::Stopping || s == State::Abandoned) return false;
    client_ = &client;
    if (mode == Mode::Inline) {
        state_.store(State::Inline);
        return true;
    }
    cmds_.reopen();
    stop_.store(false);
    advanceLeft_.store(0);
    {
        std::lock_guard<std::mutex> l(mu_);
        ready_ = go_ = finished_ = false;
    }
    state_.store(State::Starting);
    try {
        thread_ = std::thread([this] { loop(); });
    } catch (const std::exception&) {   // std::system_error: the OS refused a thread
        state_.store(State::Inline);
        return false;
    }
    std::unique_lock<std::mutex> l(mu_);
    cv_.wait(l, [this] { return ready_; });
    state_.store(State::Running);
    go_ = true;
    l.unlock();
    cv_.notify_all();
    return true;
}

bool EmuThread::stop(std::chrono::milliseconds joinTimeout) {
    const State s = state_.load();
    if (s == State::Abandoned) return false;
    if (s != State::Running) return true;
    state_.store(State::Stopping);
    stop_.store(true);
    cmds_.notify();
    {
        std::unique_lock<std::mutex> l(mu_);
        if (!cv_.wait_for(l, joinTimeout, [this] { return finished_; })) {
            state_.store(State::Abandoned);
            l.unlock();
            cv_.notify_all();
            advanceCv_.notify_all();
            thread_.detach();
            return false;
        }
    }
    thread_.join();
    state_.store(State::Stopped);
    return true;
}

void EmuThread::post(std::function<void()> fn) {
    if (abandoned()) return;
    if (threaded() && !onEmuThread()) {
        if (cmds_.post(std::move(fn))) return;   // (post() leaves `fn` alone when it refuses it)
        waitFinished();
        if (abandoned()) return;
    }
    fn();
}

void EmuThread::waitFinished() {
    std::unique_lock<std::mutex> l(mu_);
    cv_.wait(l, [this] { return finished_ || state_.load() == State::Abandoned; });
}

void EmuThread::notifyFrame() {
    { std::lock_guard<std::mutex> l(mu_); }   // a waiter is either before its predicate check or asleep: no lost wake-up
    advanceCv_.notify_all();
}

void EmuThread::inlineStep() {
    if (threaded() || abandoned() || !client_ || !client_->canRun()) return;
    client_->runFrame();
    frames_.fetch_add(1);
}

bool EmuThread::advance(unsigned n) {
    if (n == 0) return true;
    if (abandoned() || !client_) return false;
    if (!threaded()) {
        if (!client_->canRun()) return false;
        for (unsigned i = 0; i < n; i++) inlineStep();
        return true;
    }
    if (onEmuThread()) return false;   // a command cannot wait for frames that only run after it
    std::unique_lock<std::mutex> l(mu_);
    const uint64_t target = frames_.load() + n;
    advanceLeft_.fetch_add(n);
    cmds_.notify();
    advanceCv_.wait(l, [&] {
        return frames_.load() >= target || advanceLeft_.load() == 0 || finished_ || state_.load() == State::Abandoned;
    });
    return frames_.load() >= target;
}

void EmuThread::loop() {
    threadId_.store(std::this_thread::get_id());
    client_->threadStarted();
    {
        std::unique_lock<std::mutex> l(mu_);
        ready_ = true;
        cv_.notify_all();
        cv_.wait(l, [this] { return go_; });
    }
    const auto onError = [this](const std::string& what) { client_->commandFailed(what); };
    PacingClock clock;
    bool pacing = false;   // the clock was reset since frames last stopped (a pause is never owed time)
    while (!stop_.load()) {
        if (cmds_.drain(onError) > 0) client_->afterCommands();
        if (stop_.load()) break;
        const bool budget = advanceLeft_.load() > 0;
        const bool runnable = client_->canRun();
        if (budget && !runnable) {   // advance() asked for frames nothing can run: release it
            advanceLeft_.store(0);
            notifyFrame();
        }
        if (!runnable || (paused_.load() && !budget)) {
            pacing = false;
            cmds_.waitUntil(Steady::now() + std::chrono::milliseconds(250));
            continue;
        }
        PacingInput in;
        in.now = nowSec();
        in.period = client_->framePeriod();
        in.audioQueuedMs = client_->audioQueuedMs();
        in.targetMs = targetMs_.load();
        in.fastForward = ff_.load();
        if (!pacing) {
            clock.reset(in.now);
            pacing = true;
        }
        if (!budget) {
            const double due = clock.nextDue(in);
            if (due > in.now + 0.0002) {
                cmds_.waitUntil(atSec(std::min(due, in.now + 0.1)));   // re-checked at least every 100 ms
                continue;
            }
        }
        clock.frameStarted(in);
        client_->runFrame();
        frames_.fetch_add(1);                    // before the budget: advance() reads frames_ when the budget hits 0
        if (budget) advanceLeft_.fetch_sub(1);
        dropped_.store(clock.droppedFrames());
        notifyFrame();
    }
    if (cmds_.closeAndDrain(onError) > 0) client_->afterCommands();
    client_->threadStopping();
    threadId_.store(std::thread::id());
    {
        std::lock_guard<std::mutex> l(mu_);
        finished_ = true;
    }
    cv_.notify_all();
    advanceCv_.notify_all();
}

}  // namespace vj
```

- [ ] **Step 6: Add it to the app and run the test**

In `app/CMakeLists.txt`, in `set(VJ_APP_SOURCES`, add `    src/emu_thread.cpp` after `    src/core_host.cpp` (the app calls it from Task 11 on; `VJ_APP_SOURCES` is an explicit list).

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R emu_thread_unit --output-on-failure`
Expected: the whole app builds, and `emu_thread_test: OK`.

- [ ] **Step 7: Commit**

```bash
git add app/src/emu_thread.h app/src/emu_thread.cpp app/tests/emu_thread_test.cpp app/cmake/VJEmuThreadTests.cmake \
        app/CMakeLists.txt
git commit -F - <<'EOF'
feat(app): EmuThread, the single owner thread of the core (#967)

post() at frame boundaries in order, call() that runs inline on the thread
itself and after a stop, callFor() and a join timeout for a core stuck in
retro_run, advance() for headless and tests, inline mode for everything else.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 5: InputSnapshot

**Files:**
- Create: `app/src/input_snapshot.h`, `app/tests/input_snapshot_test.cpp`
- Modify: `app/src/remap.h:220-226` (declare `carryMotion`, `clearMotion` after `setGun`), `app/src/remap.cpp:548-553` (define them after `CoreInput::addMouse`), `app/src/input.h:46` (`poll` signature) and after `:51` (`coreInput()`), `app/src/input.cpp:322-324` (`poll` body head), `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: `vj::CoreInput` (remap.h).
- Produces:
  - `void CoreInput::carryMotion(const CoreInput& older)`, `void CoreInput::clearMotion()`
  - `class vj::InputSnapshot { void publish(const CoreInput& in, uint64_t eventNs); uint64_t take(CoreInput& out); unsigned takenSince(); }`
  - `void InputManager::poll(bool captureKeyboard, unsigned overlayTicks = 1)`, `const CoreInput& InputManager::coreInput() const`

- [ ] **Step 1: Write the failing test** `app/tests/input_snapshot_test.cpp`

```cpp
// InputSnapshot (input_snapshot.h): the newest input from the UI thread to the core's input poll on the emulation
// thread (#967).  No SDL window, no core.
//
//   vj_input_snapshot_test
#include "input_snapshot.h"

#include "libretro.h"

#include <atomic>
#include <cstdio>
#include <thread>

using namespace vj;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

CoreInput holding(Ctl c) {
    CoreInput in;
    in.beginFrame();
    ControlSet s;
    s.set(c);
    in.addControls(0, s);
    return in;
}

CoreInput released() {
    CoreInput in;
    in.beginFrame();
    return in;
}

CoreInput moved(int dx) {
    CoreInput in;
    in.beginFrame();
    in.addMouse(0, dx, 0, 0);
    return in;
}

int mouseX(const CoreInput& in) { return in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X); }

void testNextFrameSeesIt() {
    InputSnapshot s;
    s.publish(holding(C_A), 1000);
    CoreInput out;
    CHECK(s.take(out) == 1000, "the event's timestamp comes with the first take");
    CHECK(out.pad(0) == holding(C_A).pad(0) && out.pad(0) != 0, "the next frame sees A held");
    CHECK(s.take(out) == 0, "a second take: no new event");
    CHECK(out.pad(0) == holding(C_A).pad(0), "and A is still held (state, not an edge)");
}

void testNewestWins() {
    InputSnapshot s;
    s.publish(holding(C_A), 0);
    s.publish(released(), 0);
    CoreInput out;
    s.take(out);
    CHECK(out.pad(0) == 0, "the newest state wins: released");
}

void testOldestEventKept() {
    InputSnapshot s;
    s.publish(holding(C_A), 100);
    s.publish(holding(C_B), 200);
    CoreInput out;
    CHECK(s.take(out) == 100, "the latency is measured from the oldest event not yet taken");
    s.publish(holding(C_B), 300);
    CHECK(s.take(out) == 300, "then the next one");
}

void testMotionOnce() {
    InputSnapshot s;
    CoreInput out;
    s.publish(moved(5), 0);
    s.publish(moved(3), 0);
    s.take(out);
    CHECK(mouseX(out) == 8, "two publishes before a frame: both movements arrive (%d)", mouseX(out));
    s.take(out);
    CHECK(mouseX(out) == 0, "a second frame with no new motion moves nothing (%d)", mouseX(out));
    s.publish(moved(2), 0);
    s.take(out);
    CHECK(mouseX(out) == 2, "then the next movement alone (%d)", mouseX(out));
}

void testTakenSince() {
    InputSnapshot s;
    CoreInput out;
    s.publish(released(), 0);
    s.take(out);
    s.take(out);
    CHECK(s.takenSince() == 2, "two frames took input since the UI last asked");
    CHECK(s.takenSince() == 0, "and none since");
}

void testConcurrentMotion() {
    InputSnapshot s;
    const int kMoves = 20000;
    std::atomic<bool> done{false};
    std::thread ui([&] {
        for (int i = 0; i < kMoves; i++) s.publish(moved(1), 0);
        done = true;
    });
    long total = 0;
    CoreInput out;
    while (!done.load()) {
        s.take(out);
        total += mouseX(out);
    }
    ui.join();
    s.take(out);
    total += mouseX(out);
    CHECK(total == kMoves, "every movement reaches the core exactly once (%ld of %d)", total, kMoves);
}

}  // namespace

int main() {
    testNextFrameSeesIt();
    testNewestWins();
    testOldestEventKept();
    testMotionOnce();
    testTakenSince();
    testConcurrentMotion();
    std::printf(g_fail ? "input_snapshot_test: %d FAILED\n" : "input_snapshot_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   input_snapshot_unit   InputSnapshot: newest state, pointer motion exactly once, event timestamps, overlay ticks` to the header list:

```cmake
add_executable(vj_input_snapshot_test tests/input_snapshot_test.cpp src/remap.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_input_snapshot_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_input_snapshot_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
vj_emu_thread_warnings(vj_input_snapshot_test)
add_test(NAME input_snapshot_unit COMMAND vj_input_snapshot_test)
set_tests_properties(input_snapshot_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_input_snapshot_test`
Expected: compile error `input_snapshot.h: No such file or directory`.

- [ ] **Step 4: Add the motion helpers to CoreInput.** In `app/src/remap.h`, after the `setGun` declaration, add:

```cpp
    // InputSnapshot (#967): pointer / rotary motion is a delta, not a state.  carryMotion() adds an older snapshot's
    // motion that the core has not taken yet; clearMotion() forgets it once taken.
    void carryMotion(const CoreInput& older);
    void clearMotion();
```

In `app/src/remap.cpp`, after `CoreInput::addMouse`, add:

```cpp
void CoreInput::carryMotion(const CoreInput& older) {
    for (size_t p = 0; p < extra_.size(); p++) {
        extra_[p].mouseDx += older.extra_[p].mouseDx;
        extra_[p].mouseDy += older.extra_[p].mouseDy;
    }
}

void CoreInput::clearMotion() {
    for (PortExtra& e : extra_) e.mouseDx = e.mouseDy = 0;
}
```

- [ ] **Step 5: Implement `app/src/input_snapshot.h`**

```cpp
// The newest input, from the UI thread to the core's input poll on the emulation thread (#967).
//
// The UI thread runs InputManager::poll() (SDL_GetMouseState is main-thread only, and the routes, the keypad overlay
// and bind capture all live there) and publish()es the resulting CoreInput after every batch of events.  The core's
// input-poll callback take()s the newest one right before each frame, so input is never older than the last event
// batch.  Buttons and axes are states (the newest wins); pointer / rotary motion is a delta and is summed until a frame
// takes it, then delivered exactly once, however many publishes or frames fall in between.
#pragma once

#include "remap.h"

#include <cstdint>
#include <mutex>

namespace vj {

class InputSnapshot {
public:
    // UI thread.  `eventNs`: SDL timestamp (SDL_GetTicksNS base) of the oldest input event this state includes that
    // was not published before, 0 = none.
    void publish(const CoreInput& in, uint64_t eventNs) {
        std::lock_guard<std::mutex> l(mu_);
        CoreInput next = in;
        next.carryMotion(latest_);   // motion the emulation has not taken yet is kept, not replaced
        latest_ = next;
        if (eventNs && !eventNs_) eventNs_ = eventNs;
    }

    // Emulation thread (the core's input-poll callback).  Returns the oldest untaken event's timestamp (0 = none),
    // for the input-latency measurement.
    uint64_t take(CoreInput& out) {
        std::lock_guard<std::mutex> l(mu_);
        out = latest_;
        latest_.clearMotion();
        taken_++;
        const uint64_t ns = eventNs_;
        eventNs_ = 0;
        return ns;
    }

    // UI thread: frames that took input since the last call (the keypad overlay ticks once per emulated frame).
    unsigned takenSince() {
        std::lock_guard<std::mutex> l(mu_);
        const unsigned n = taken_;
        taken_ = 0;
        return n;
    }

private:
    std::mutex mu_;
    CoreInput latest_;
    uint64_t eventNs_ = 0;
    unsigned taken_ = 0;
};

}  // namespace vj
```

- [ ] **Step 6: Let InputManager poll for the UI thread.** In `app/src/input.h`, replace

```cpp
    void poll(bool captureKeyboard);
```

with

```cpp
    // `overlayTicks`: emulated frames since the last poll.  Inline that is always 1 (the core polls); threaded, the UI
    // thread polls once per UI iteration and passes InputSnapshot::takenSince(), so the keypad overlay's press timing
    // stays per emulated frame.
    void poll(bool captureKeyboard, unsigned overlayTicks = 1);
    // The result of the last poll() (threaded: what the UI thread publishes to InputSnapshot).
    const CoreInput& coreInput() const { return core_; }
```

In `app/src/input.cpp`, replace

```cpp
void InputManager::poll(bool captureKeyboard) {
    core_.beginFrame();
    overlay.tick();
```

with

```cpp
void InputManager::poll(bool captureKeyboard, unsigned overlayTicks) {
    core_.beginFrame();
    for (unsigned i = 0; i < overlayTicks; i++) overlay.tick();
```

- [ ] **Step 7: Run it, plus the input tests that drive CoreInput and InputManager**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "input_snapshot_unit|remap_unit|input_e2e|devices_unit|devices_e2e" --output-on-failure`
Expected: all five pass.

- [ ] **Step 8: Commit**

```bash
git add app/src/input_snapshot.h app/src/remap.h app/src/remap.cpp app/src/input.h app/src/input.cpp \
        app/tests/input_snapshot_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): InputSnapshot, the newest input for the emulation thread's poll (#967)

States are newest-wins; pointer motion is summed until a frame takes it and is
delivered exactly once.  InputManager::poll can run on the UI thread with the
keypad overlay ticking per emulated frame.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 6: AudioOut for two threads

**Files:**
- Create: `app/tests/audio_out_test.cpp`
- Modify: `app/src/audio.h` (whole file below), `app/src/audio.cpp:40-43` (`open`'s resume), `:58-62` (`push`), `:63-72` (`update`), `:74-79` (`setPaused`), `:86-89` (`clear`), `:91-97` (`bufferStatus`), `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: SDL3 audio.
- Produces: `void AudioOut::update(bool adjustRatio = true)`, `bool AudioOut::paused() const`. `paused_`, `targetMs_`, `ratio_` and `underruns_` become atomics. Existing callers compile unchanged.

- [ ] **Step 1: Write the failing test** `app/tests/audio_out_test.cpp`

```cpp
// AudioOut (audio.h) under SDL's dummy audio driver (#967): the mode-A hooks, and the fields the emulation thread and
// the UI thread both touch (the ThreadSanitizer job runs this).
//
//   vj_audio_out_test
#include "audio.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

}  // namespace

int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::printf("SKIP no SDL audio: %s\n", SDL_GetError());
        return 77;
    }
    {
        vj::AudioOut a;
        CHECK(a.open(48043.6), "the dummy device opens");
        std::vector<int16_t> batch(800 * 2, 0);
        a.push(batch.data(), 800);
        CHECK(a.queuedMs() > 5.0f, "a frame's audio is queued (%.2f ms)", a.queuedMs());
        a.update(false);
        CHECK(a.ratio() == 1.0, "update(false): mode A leaves the playback ratio alone (%.6f)", a.ratio());
        a.update(true);
        CHECK(a.ratio() != 1.0, "update(true): the single-thread loop's controller still nudges it (%.6f)", a.ratio());
        a.clear();
        CHECK(a.ratio() == 1.0, "clear() resets the ratio");
        a.setPaused(true);
        CHECK(a.paused(), "paused() reports the pause");
        bool active = true, underrun = false;
        unsigned occ = 0;
        a.bufferStatus(active, occ, underrun);
        CHECK(!active, "a paused stream is not an active audio clock");
        a.setPaused(false);

        // The emulation thread pushes and measures while the UI thread pauses, re-targets and reads the counters.
        std::atomic<bool> stop{false};
        std::thread emu([&] {
            while (!stop.load()) {
                a.push(batch.data(), 800);
                a.update(false);
                bool ac = false, ur = false;
                unsigned oc = 0;
                a.bufferStatus(ac, oc, ur);
                if (a.queuedMs() > 200.0f) a.clear();
            }
        });
        for (int i = 0; i < 2000; i++) {
            a.setPaused((i & 1) != 0);
            a.setTargetMs(32.0f + (float)(i % 64));
            (void)a.ratio();
            (void)a.underruns();
        }
        stop = true;
        emu.join();
        a.setPaused(false);
        CHECK(a.isOpen(), "still open after the cross-thread run");
        a.close();
    }
    SDL_Quit();
    std::printf(g_fail ? "audio_out_test: %d FAILED\n" : "audio_out_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   audio_out_unit        AudioOut under the SDL dummy driver: the mode-A hooks and cross-thread use` to the header list:

```cmake
add_executable(vj_audio_out_test tests/audio_out_test.cpp src/audio.cpp)
target_include_directories(vj_audio_out_test PRIVATE src)
target_link_libraries(vj_audio_out_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
vj_emu_thread_warnings(vj_audio_out_test)
add_test(NAME audio_out_unit COMMAND vj_audio_out_test)
set_tests_properties(audio_out_unit PROPERTIES TIMEOUT 120 LABELS emu_thread SKIP_RETURN_CODE 77
                     FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_audio_out_test`
Expected: compile errors: `no member named 'paused' in 'vj::AudioOut'` and `too many arguments to function call` for `update(false)`.

- [ ] **Step 4: Replace `app/src/audio.h`** with

```cpp
// SDL3 audio output with dynamic rate control.
//
// The core hands over one fixed batch of stereo pairs per field; the true
// rate is pairs * field rate (48043.6 Hz NTSC), so the SDL stream is opened
// at the rounded core rate and SDL resamples to the device.  A small playback
// ratio correction keeps the queued amount near a target so audio follows the
// video pacing instead of drifting into under/overruns.
//
// Threads (#967): with the emulation thread on, push(), update() and bufferStatus() run on the emulation thread while
// the UI thread pauses, re-targets, sets the volume and reads the counters.  The fields both touch are atomics, and
// every SDL call used here is safe from any thread (SDL3 headers).  open() and close() only run while the other thread
// waits: inside an EmuThread::call(), or before the thread starts and after it stops.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

struct SDL_AudioStream;

namespace vj {

class AudioOut {
public:
    ~AudioOut();
    bool open(double coreRate);          // opens (or re-formats) the device stream
    void close();
    bool isOpen() const { return stream_ != nullptr; }

    void push(const int16_t* stereo, size_t frames);
    // Once per emulated frame, before the core runs: counts underruns and, when `adjustRatio` (the single-thread
    // loop), nudges the playback ratio toward the queue target.  Mode A (the emulation thread) passes false: there the
    // pacer follows the audio clock instead, and two controllers must not fight.
    void update(bool adjustRatio = true);
    void setPaused(bool p);
    bool paused() const { return paused_.load(); }
    void setVolume(float v);             // 0..1
    void setTargetMs(float ms) { targetMs_.store(ms); }
    void clear();
    // Occupancy for RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK.
    void bufferStatus(bool& active, unsigned& occupancy, bool& underrun) const;

    float queuedMs() const;
    double ratio() const { return ratio_.load(); }
    uint64_t underruns() const { return underruns_.load(); }

private:
    SDL_AudioStream* stream_ = nullptr;   // changed only by open() / close() (see above)
    int rate_ = 48000;
    std::atomic<float> targetMs_{64.0f};
    std::atomic<double> ratio_{1.0};
    float volume_ = 1.0f;                 // UI thread only
    std::atomic<bool> paused_{false};
    std::atomic<uint64_t> underruns_{0};
};

}  // namespace vj
```

- [ ] **Step 5: Update `app/src/audio.cpp`.** In `open`, replace `if (!paused_) SDL_ResumeAudioStreamDevice(stream_);` with `if (!paused_.load()) SDL_ResumeAudioStreamDevice(stream_);`. Replace `push`, `update`, `setPaused`, `clear` and `bufferStatus` with:

```cpp
void AudioOut::push(const int16_t* stereo, size_t frames) {
    if (!stream_ || paused_.load()) return;
    SDL_PutAudioStreamData(stream_, stereo, (int)(frames * 2 * sizeof(int16_t)));
}
```

```cpp
void AudioOut::update(bool adjustRatio) {
    if (!stream_) return;
    const float q = queuedMs();
    if (q <= 0.5f) underruns_.fetch_add(1);
    if (!adjustRatio) return;
    // Relative error against the target, mapped to a +/-0.5% playback-speed
    // nudge.  Smoothed so one burst does not wobble the pitch.
    const float target = targetMs_.load();
    float err = std::max(-1.0f, std::min(1.0f, (q - target) / target));
    double want = 1.0 + 0.005 * (double)err;
    double r = ratio_.load();
    r += (want - r) * 0.05;
    ratio_.store(r);
    SDL_SetAudioStreamFrequencyRatio(stream_, (float)r);
}

void AudioOut::setPaused(bool p) {
    paused_.store(p);
    if (!stream_) return;
    if (p) SDL_PauseAudioStreamDevice(stream_);
    else SDL_ResumeAudioStreamDevice(stream_);
}
```

```cpp
void AudioOut::clear() {
    if (stream_) SDL_ClearAudioStream(stream_);
    ratio_.store(1.0);
}

void AudioOut::bufferStatus(bool& active, unsigned& occupancy, bool& underrun) const {
    active = stream_ != nullptr && !paused_.load();
    const float target = targetMs_.load();
    float cap = std::max(target * 2.0f, 1.0f);
    float q = queuedMs();
    occupancy = (unsigned)std::max(0.0f, std::min(100.0f, 100.0f * q / cap));
    underrun = q < target * 0.25f;
}
```

- [ ] **Step 6: Run it and the audio smoke tests**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "audio_out_unit|smoke_yarc|smoke_state" --output-on-failure`
Expected: all pass. `smoke_yarc` still reports `audio_frames` > 0 and hash `f4c30ea3`.

- [ ] **Step 7: Commit**

```bash
git add app/src/audio.h app/src/audio.cpp app/tests/audio_out_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): AudioOut safe across the emulation and UI threads; mode-A update (#967)

update(false) counts underruns without nudging the playback ratio: in mode A
the pacer follows the audio clock and the two controllers must not fight.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 7: CoreHost owner thread and EmuSnapshot

**Files:**
- Create: `app/tests/core_host_thread_test.cpp`
- Modify: `app/src/core_host.h` (includes; new `OptionTable` and `EmuSnapshot` after `DiscInfo`; accessors at `:101`, `:144`, `:166-176`, `:185-205`, `:211`; a threading block before `private:`; private members at `:252-324`), `app/src/core_host.cpp` (each spot named in Steps 5-8), `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: `EmuThread` (Task 4, in the test only).
- Produces (CoreHost, all existing signatures unchanged):
  - `struct vj::OptionTable { std::vector<OptionCategory> categories; std::vector<OptionDef> defs; std::map<std::string,bool> visibility; std::map<std::string,std::string> locked; }`
  - `struct vj::EmuSnapshot { std::shared_ptr<const OptionTable> options; unsigned optionPushes; double fps, sampleRate; float aspect; DiscInfo disc; }`
  - `void setOwner(std::thread::id)`, `bool threaded() const`, `bool onOwner() const`, `std::function<void(std::function<void()>)> post`, `void publishSnapshot()`, `void pinSnapshot()`, `unsigned ownerViolations() const`
  - Off the owner: option, AV and disc readers answer from the pinned snapshot; option writers update the UI's mirror, call `onOptionChanged` on the caller's thread, and queue the core-side write through `post`; `setControllerDevice`, `discSetEject`, `discSetIndex` and `sendKeyEvent` are queued; `loadContent`, `loadNoContent`, `unloadContent`, `runFrame`, `reset`, `saveStateToFile`, `loadStateFromFile`, `flushSaveRam` and `memoryRegion` are refused and counted; `frame()`, `inFrame()` and `checkOverrides()` are counted.

- [ ] **Step 1: Write the failing test** `app/tests/core_host_thread_test.cpp`

```cpp
// CoreHost with an owner thread and the REAL core (#967): what the UI thread may read and write while the emulation
// thread owns the core, and what it may not.
//
// usage: vj_core_host_thread_test <rom.j64>
#include "core_host.h"
#include "emu_thread.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_fail++;                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

struct HostClient : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 60.0; }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {
        host.runFrame();
        host.publishSnapshot();
    }
    void afterCommands() override { host.publishSnapshot(); }
};

const OptionDef* findDef(CoreHost& host, const std::string& key) {
    for (const OptionDef& d : host.optionDefs())
        if (d.key == key) return &d;
    return nullptr;
}

std::string valueOf(CoreHost& host, const std::string& key) {
    const OptionDef* d = findDef(host, key);
    return d ? host.optionValue(*d) : std::string("<missing>");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_core_host_thread_test <rom.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-core-host-thread-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    CoreHost& host = CoreHost::instance();
    host.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    host.inputPoll = [] {};
    host.inputState = [](unsigned, unsigned, unsigned, unsigned) -> int16_t { return 0; };
    std::vector<std::string> changed;   // onOptionChanged calls (the app writes config.ini there)
    std::thread::id changedOn;
    host.onOptionChanged = [&](const std::string& k, const std::string& v, bool erased) {
        changed.push_back(k + "=" + (erased ? std::string("<erased>") : v));
        changedOn = std::this_thread::get_id();
    };
    const std::string rom = argv[1];
    const std::string key = "virtualjaguar_crash_detect";

    // ---- inline: no owner, everything as before ----------------------------------------------------
    CHECK(!host.threaded() && host.onOwner());
    std::string err;
    CHECK(host.loadContent(rom, err));
    host.runFrame();
    CHECK(host.frameCount() == 1);

    // ---- threaded --------------------------------------------------------------------------------
    EmuThread emu;
    HostClient client;
    host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setPaused(true);   // frames only through advance()
    CHECK(emu.start(EmuThread::Mode::Threaded, client));
    CHECK(host.threaded() && !host.onOwner());
    host.pinSnapshot();

    // Reads off the owner come from the pinned snapshot.
    const OptionDef* def = findDef(host, key);
    CHECK(def != nullptr);
    CHECK(host.fps() > 59.0 && host.fps() < 61.0);
    CHECK(host.sampleRate() > 40000.0);
    const std::vector<OptionDef>* pinned = &host.optionDefs();
    const size_t pinnedCount = pinned->size();
    CHECK(emu.advance(5));
    CHECK(pinned->size() == pinnedCount);   // the pinned table stays valid while frames run, until the next pin

    // A write: the UI sees it at once, the config is written on the UI thread, the core's copy has it after the boundary.
    std::string other;
    if (def)
        for (const OptionValue& v : def->values)
            if (v.value != host.optionValue(*def)) {
                other = v.value;
                break;
            }
    CHECK(!other.empty());
    host.setOptionValue(key, other);
    CHECK(valueOf(host, key) == other);
    CHECK(!changed.empty() && changed.back() == key + "=" + other && changedOn == std::this_thread::get_id());
    CHECK(emu.call([&] { return valueOf(host, key); }) == other);   // posted before the call, so applied first
    host.resetOptionToDefault(key);
    CHECK(changed.back() == key + "=<erased>");
    CHECK(emu.call([&] { return host.userOptions().count(key) == 0; }));

    // OSD messages pushed on the emulation thread are taken here.
    host.takeMessages();
    emu.call([&] { host.pushMessage("from the emulation thread"); });
    std::vector<OsdMessage> msgs = host.takeMessages();
    CHECK(msgs.size() == 1 && msgs[0].text == "from the emulation thread");

    // Queued writers: no violation.
    const unsigned v0 = host.ownerViolations();
    host.sendKeyEvent(false, 0, 0, 0);
    host.setControllerDevice(0, RETRO_DEVICE_JOYPAD);
    host.discSetEject(false);
    emu.call([] {});
    CHECK(host.ownerViolations() == v0);

    // Core calls off the owner are refused and counted.
    const uint64_t f0 = host.frameCount();
    host.runFrame();
    host.reset();
    CHECK(!host.saveStateToFile((tmp / "x.state").string(), err));
    (void)host.frame();
    CHECK(host.frameCount() == f0);
    CHECK(host.ownerViolations() >= v0 + 4);
    CHECK(emu.advance(2) && host.frameCount() == f0 + 2);   // the owner runs them

    // A reload in a call: after the next pin the UI sees the new option push, never the previous table.
    const unsigned pushes = host.optionPushes();
    CHECK(emu.call([&] { return host.loadContent(rom, err); }));
    host.pinSnapshot();
    CHECK(host.optionPushes() > pushes);
    CHECK(findDef(host, key) != nullptr);

    emu.call([&] { host.unloadContent(); });
    CHECK(emu.stop());
    host.post = nullptr;
    CHECK(!host.threaded() && host.onOwner());
    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("core_host_thread_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   core_host_thread      CoreHost with an owner thread and the REAL core: snapshot reads, queued writes, refused calls` to the header list:

```cmake
add_executable(vj_core_host_thread_test tests/core_host_thread_test.cpp src/core_host.cpp src/emu_thread.cpp src/paths.cpp)
target_include_directories(vj_core_host_thread_test PRIVATE src)
target_link_libraries(vj_core_host_thread_test PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_core_host_thread_test PRIVATE ws2_32 iphlpapi)
endif()
vj_emu_thread_warnings(vj_core_host_thread_test)
add_test(NAME core_host_thread COMMAND vj_core_host_thread_test "${VJ_ROMS}/yarc.j64")
set_tests_properties(core_host_thread PROPERTIES TIMEOUT 300 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_core_host_thread_test`
Expected: compile errors: `no member named 'setOwner'`, `'pinSnapshot'`, `'publishSnapshot'`, `'ownerViolations'`, `'post'` in `vj::CoreHost`.

- [ ] **Step 4: `app/src/core_host.h`: types, accessors and the threading API.**

Includes: after `#include <map>` add

```cpp
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
```

After `struct DiscInfo { ... };` add

```cpp
// What SET_CORE_OPTIONS_V2 / _DISPLAY last said, plus the app's locks: the part of the option state the core writes.
struct OptionTable {
    std::vector<OptionCategory> categories;
    std::vector<OptionDef> defs;
    std::map<std::string, bool> visibility;
    std::map<std::string, std::string> locked;
};

// The per-frame state the emulation thread publishes for the UI thread (#967, design section 3).  Phase 1 holds what
// the UI reads every frame; phase 2 adds events, achievements and debug views and moves it to its own file.
struct EmuSnapshot {
    std::shared_ptr<const OptionTable> options;   // replaced only when the table changed
    unsigned optionPushes = 0;
    double fps = 0, sampleRate = 0;
    float aspect = 4.0f / 3.0f;
    DiscInfo disc;
};
```

Accessor replacements (old line, then new line):

```cpp
    unsigned crashEvents() const { return crashEvents_; }   // crash-watchdog events since the content loaded
```
```cpp
    unsigned crashEvents() const { return crashEvents_.load(); }   // crash-watchdog events since the content loaded
```

```cpp
    bool inFrame() const { return inFrame_; }
```
```cpp
    bool inFrame() const {
        noteOffOwner("inFrame");   // the UI asks DebugSession::halted() instead (App::haltedNow)
        return inFrame_;
    }
```

```cpp
    double fps() const { return avInfo_.timing.fps; }
    double sampleRate() const { return avInfo_.timing.sample_rate; }
    float aspect() const;
    const Frame& frame() const { return frame_; }
    uint64_t frameCount() const { return frameCount_; }
```
```cpp
    double fps() const { return onOwner() ? avInfo_.timing.fps : ui_.fps; }
    double sampleRate() const { return onOwner() ? avInfo_.timing.sample_rate : ui_.sampleRate; }
    float aspect() const { return onOwner() ? aspectLive() : ui_.aspect; }
    // The core's last frame.  Emulation thread only when threaded: the UI reads frames from FrameExchange.
    const Frame& frame() const {
        noteOffOwner("frame");
        return frame_;
    }
    uint64_t frameCount() const { return frameCount_.load(); }
```

```cpp
    unsigned optionPushes() const { return optionPushes_; }
    unsigned minAudioLatencyMs() const { return minAudioLatencyMs_; }
```
```cpp
    unsigned optionPushes() const { return onOwner() ? optionPushes_ : ui_.optionPushes; }
    unsigned minAudioLatencyMs() const { return minAudioLatencyMs_.load(); }
```

```cpp
    const std::vector<OptionCategory>& optionCategories() const { return categories_; }
    const std::vector<OptionDef>& optionDefs() const { return defs_; }
```
```cpp
    const std::vector<OptionCategory>& optionCategories() const { return onOwner() ? categories_ : uiTable().categories; }
    const std::vector<OptionDef>& optionDefs() const { return onOwner() ? defs_ : uiTable().defs; }
```

```cpp
    bool optionLocked(const std::string& key) const { return locked_.count(key) != 0; }
```
```cpp
    bool optionLocked(const std::string& key) const { return (onOwner() ? locked_ : uiTable().locked).count(key) != 0; }
```

```cpp
    bool optionOverridden(const std::string& key) const { return sessionValues_.count(key) != 0; }
```
```cpp
    bool optionOverridden(const std::string& key) const { return (onOwner() ? sessionValues_ : uiSession_).count(key) != 0; }
```

In `optionValueFor`, replace the body with

```cpp
        const std::map<std::string, std::string>& session = onOwner() ? sessionValues_ : uiSession_;
        const std::map<std::string, std::string>& user = onOwner() ? userValues_ : uiUser_;
        auto s = session.find(key);
        if (s != session.end()) return s->second;
        auto u = user.find(key);
        return u != user.end() ? u->second : fallback;
```

```cpp
    const std::map<std::string, std::string>& userOptions() const { return userValues_; }
```
```cpp
    const std::map<std::string, std::string>& userOptions() const { return onOwner() ? userValues_ : uiUser_; }
```

Before `private:` add the threading block:

```cpp
    // ---- threading (#967, docs/app-emulation-thread-design.md) -------------------------------------
    // Inline (no owner, the default): every method runs on its caller's thread, exactly as before.
    // Threaded: setOwner() names the emulation thread, the only thread that calls into the core.  From any other
    // thread (the UI):
    //   * the option, AV and disc readers answer from the snapshot pinned by pinSnapshot() (the emulation thread
    //     publishSnapshot()s after each frame and each command batch);
    //   * option values are mirrored: a write updates the UI's copy and calls onOptionChanged (config.ini) right
    //     there, and reaches the core's copy through `post` at the next frame boundary;
    //   * setControllerDevice, discSetEject / discSetIndex and sendKeyEvent are queued through `post`;
    //   * the fields only load / unload write (isLoaded, contentPath / contentName, stagedBios, controllerPorts,
    //     coreName / coreVersion, eventsActive, debugIface) are read directly: they change only inside an
    //     EmuThread::call(), while the UI thread waits;
    //   * anything else (runFrame, loads, states, SRAM, memory, frame(), inFrame()) is a bug: it is counted in
    //     ownerViolations() and logged once per method, and the calls into the core are refused.
    void setOwner(std::thread::id owner);
    bool threaded() const { return owner_.load() != std::thread::id(); }
    bool onOwner() const {
        const std::thread::id o = owner_.load();
        return o == std::thread::id() || o == std::this_thread::get_id();
    }
    std::function<void(std::function<void()>)> post;   // the emulation thread's queue (App: EmuThread::post)
    void publishSnapshot();   // emulation thread: after each frame and each command batch
    void pinSnapshot();       // UI thread, once per UI iteration; what the readers return stays valid until the next pin
    unsigned ownerViolations() const { return ownerViolations_.load(); }
```

In the private section: change `unsigned crashEvents_ = 0;` to `std::atomic<unsigned> crashEvents_{0};`, `uint64_t frameCount_ = 0;` to `std::atomic<uint64_t> frameCount_{0};`, `unsigned minAudioLatencyMs_ = 0;` to `std::atomic<unsigned> minAudioLatencyMs_{0};`, and add at the end of the private section:

```cpp
    // threading (#967)
    float aspectLive() const;
    DiscInfo discInfoLive();
    const OptionTable& uiTable() const;
    bool noteOffOwner(const char* what) const;   // true (counted, logged once) when called off the owner
    void queue(std::function<void()> fn);         // through `post`, or now when there is none
    void applyUserValue(const std::string& key, const std::string* value);   // null = erase
    std::atomic<std::thread::id> owner_{std::thread::id()};
    mutable std::atomic<unsigned> ownerViolations_{0};
    mutable std::mutex violationMu_;
    mutable std::set<std::string> violationSeen_;
    std::mutex snapMu_;
    EmuSnapshot pub_;   // under snapMu_: the emulation thread's latest
    EmuSnapshot ui_;    // the UI thread's pin
    std::map<std::string, std::string> uiUser_, uiSession_;   // threaded: the UI's option values
    unsigned optSerial_ = 1, pubOptSerial_ = 0;   // bumped on every change to the option table
    bool discDirty_ = true;
    std::mutex msgMu_;   // messages_
```

- [ ] **Step 5: `app/src/core_host.cpp`: options.**

Rename `float CoreHost::aspect() const {` to `float CoreHost::aspectLive() const {` (body unchanged).

At the end of `captureOptions`, replace `    optionPushes_++;` with

```cpp
    optionPushes_++;
    optSerial_++;
```

Replace `optionVisible` and `optionValue` with

```cpp
bool CoreHost::optionVisible(const std::string& key) const {
    const bool own = onOwner();
    const std::map<std::string, std::string>& locked = own ? locked_ : uiTable().locked;
    const std::map<std::string, bool>& vis = own ? visibility_ : uiTable().visibility;
    if (locked.count(key)) return false;
    auto it = vis.find(key);
    return it == vis.end() ? true : it->second;
}
```

```cpp
std::string CoreHost::optionValue(const OptionDef& d) const {
    const bool own = onOwner();
    const std::map<std::string, std::string>& locked = own ? locked_ : uiTable().locked;
    const std::map<std::string, std::string>& session = own ? sessionValues_ : uiSession_;
    const std::map<std::string, std::string>& user = own ? userValues_ : uiUser_;
    auto l = locked.find(d.key);
    if (l != locked.end()) return l->second;
    auto s = session.find(d.key);
    if (s != session.end() && validFor(d, s->second)) return s->second;
    auto u = user.find(d.key);
    if (u != user.end() && validFor(d, u->second)) return u->second;
    return d.defaultValue;
}
```

Replace `overrideOption`, `setOptionValue`, `clearOverride` and `resetOptionToDefault` with

```cpp
void CoreHost::overrideOption(const std::string& key, const std::string& value) {
    if (!onOwner()) {
        uiSession_[key] = value;
        queue([this, key, value] { overrideOption(key, value); });
        return;
    }
    if (inFrame_) return;   // the debugger has the core halted inside retro_run(): no option changes
    sessionValues_[key] = value;
    if (threaded()) uiSession_[key] = value;   // (threaded, on the owner: only inside a call(), while the UI waits)
    variableUpdate_ = true;
}
```

```cpp
void CoreHost::setOptionValue(const std::string& key, const std::string& value) {
    if (!onOwner()) {
        uiUser_[key] = value;
        if (onOptionChanged) onOptionChanged(key, value, false);   // config.ini is only ever written on the UI thread
        queue([this, key, value] { applyUserValue(key, &value); });
        return;
    }
    if (inFrame_) return;
    if (onOptionChanged) onOptionChanged(key, value, false);
    if (threaded()) uiUser_[key] = value;
    applyUserValue(key, &value);
}

void CoreHost::clearOverride(const std::string& key) {
    if (!onOwner()) {
        uiSession_.erase(key);
        queue([this, key] { clearOverride(key); });
        return;
    }
    if (inFrame_) return;
    if (threaded()) uiSession_.erase(key);
    if (sessionValues_.erase(key)) variableUpdate_ = true;
}

void CoreHost::resetOptionToDefault(const std::string& key) {
    if (!onOwner()) {
        uiUser_.erase(key);
        if (onOptionChanged) onOptionChanged(key, "", true);
        queue([this, key] { applyUserValue(key, nullptr); });
        return;
    }
    if (inFrame_) return;
    if (onOptionChanged) onOptionChanged(key, "", true);
    if (threaded()) uiUser_.erase(key);
    applyUserValue(key, nullptr);
}

// The core's copy of a saved value (the owner side of setOptionValue / resetOptionToDefault).
void CoreHost::applyUserValue(const std::string& key, const std::string* value) {
    if (inFrame_) return;
    if (value) userValues_[key] = *value;
    else userValues_.erase(key);
    variableUpdate_ = true;
    if (updateDisplayCb_) updateDisplayCb_();
}
```

At the top of `checkOverrides`, add `noteOffOwner("checkOverrides");`.

Replace `takeMessages` and the `messages_.push_back(std::move(m));` line of `pushMessage` with

```cpp
std::vector<OsdMessage> CoreHost::takeMessages() {
    std::vector<OsdMessage> out;
    std::lock_guard<std::mutex> l(msgMu_);
    out.swap(messages_);
    return out;
}
```

```cpp
    std::lock_guard<std::mutex> l(msgMu_);
    messages_.push_back(std::move(m));
```

- [ ] **Step 6: `app/src/core_host.cpp`: the environment and the session.**

In `env`, `RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY`, replace `if (d && d->key) visibility_[d->key] = d->visible;` with

```cpp
        if (d && d->key) {
            auto it = visibility_.find(d->key);
            if (it == visibility_.end() || it->second != d->visible) {
                visibility_[d->key] = d->visible;
                optSerial_++;   // the UI's snapshot of the table is out of date
            }
        }
```

In `RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY`, replace `if (data) minAudioLatencyMs_ = *(const unsigned*)data;` with `if (data) minAudioLatencyMs_.store(*(const unsigned*)data);`. In `VJ_ENV_EVENT`, replace `crashEvents_++;` with `crashEvents_.fetch_add(1);`.

In `startSession`, after `visibility_.clear();` add `optSerial_++;`; replace `minAudioLatencyMs_ = 0;` with `minAudioLatencyMs_.store(0);` and `frameCount_ = 0;` with `frameCount_.store(0);`.

In `finishLoad`, replace `crashEvents_ = 0;` with

```cpp
    crashEvents_.store(0);
    discDirty_ = true;
```

At the end of `stopSession` (after `memMap_ = retro_memory_map{};`) add `discDirty_ = true;`.

At the top of `loadContent` and `loadNoContent` (before the `inFrame_` check) add respectively

```cpp
    if (noteOffOwner("loadContent")) { err = "called off the emulation thread"; return false; }
```

```cpp
    if (noteOffOwner("loadNoContent")) { err = "called off the emulation thread"; return false; }
```

- [ ] **Step 7: `app/src/core_host.cpp`: the other core calls.**

```cpp
bool CoreHost::memoryRegion(unsigned id, uint8_t*& data, size_t& size) const {
    data = nullptr;
    size = 0;
    if (noteOffOwner("memoryRegion")) return false;
    if (!loaded_) return false;
    data = (uint8_t*)retro_get_memory_data(id);
    size = data ? retro_get_memory_size(id) : 0;
    return data && size;
}

void CoreHost::unloadContent() {
    if (noteOffOwner("unloadContent")) return;
    if (inFrame_) return;   // never tear the core down from inside its own retro_run()
    stopSession();
}

void CoreHost::runFrame() {
    if (noteOffOwner("runFrame")) return;
    if (!loaded_ || inFrame_) return;
    if (g_audioBufferCb && audioBufferStatus) {
        bool active = false, underrun = false;
        unsigned occ = 0;
        audioBufferStatus(active, occ, underrun);
        g_audioBufferCb(active, occ, underrun);
    }
    inFrame_ = true;
    retro_run();   // a debugger halt parks the core in here and runs the UI (see debug_session.h)
    inFrame_ = false;
    frameCount_.fetch_add(1);
}

void CoreHost::reset() {
    if (noteOffOwner("reset")) return;
    if (loaded_ && !inFrame_) retro_reset();
}

void CoreHost::sendKeyEvent(bool down, unsigned retroKey, uint32_t character, uint16_t mods) {
    if (!onOwner()) {
        queue([this, down, retroKey, character, mods] { sendKeyEvent(down, retroKey, character, mods); });
        return;
    }
    if (keyboardCb_) keyboardCb_(down, retroKey, character, mods);
}
```

At the top of `saveStateToFile` and `loadStateFromFile` add respectively

```cpp
    if (noteOffOwner("saveStateToFile")) { err = "called off the emulation thread"; return false; }
```

```cpp
    if (noteOffOwner("loadStateFromFile")) { err = "called off the emulation thread"; return false; }
```

At the top of `flushSaveRam` add `if (noteOffOwner("flushSaveRam")) return false;`.

Replace `setControllerDevice`, `discInfo`, `discSetEject` and `discSetIndex` with

```cpp
void CoreHost::setControllerDevice(unsigned port, unsigned device) {
    if (!onOwner()) {
        queue([this, port, device] { setControllerDevice(port, device); });
        return;
    }
    if (loaded_ && !inFrame_) retro_set_controller_port_device(port, device);
}

DiscInfo CoreHost::discInfo() {
    if (!onOwner()) return ui_.disc;
    return discInfoLive();
}

DiscInfo CoreHost::discInfoLive() {
    DiscInfo d;
    if (!loaded_ || !diskRegistered_ || !disk_.get_num_images) return d;
    d.count = disk_.get_num_images();
    d.available = d.count > 0;
    d.ejected = disk_.get_eject_state ? disk_.get_eject_state() : false;
    d.index = disk_.get_image_index ? disk_.get_image_index() : 0;
    for (unsigned i = 0; i < d.count; i++) {
        char label[256] = {0};
        std::string name;
        if (disk_.get_image_label && disk_.get_image_label(i, label, sizeof label) && label[0])
            name = label;
        else if (disk_.get_image_path && disk_.get_image_path(i, label, sizeof label) && label[0])
            name = pathStem(label);
        else
            name = "Disc " + std::to_string(i + 1);
        d.labels.push_back(name);
    }
    return d;
}

void CoreHost::discSetEject(bool eject) {
    if (!onOwner()) {
        queue([this, eject] { discSetEject(eject); });
        return;
    }
    if (loaded_ && !inFrame_ && diskRegistered_ && disk_.set_eject_state) {
        disk_.set_eject_state(eject);
        discDirty_ = true;
    }
}

void CoreHost::discSetIndex(unsigned idx) {
    if (!onOwner()) {
        queue([this, idx] { discSetIndex(idx); });
        return;
    }
    if (loaded_ && !inFrame_ && diskRegistered_ && disk_.set_image_index) {
        disk_.set_image_index(idx);
        discDirty_ = true;
    }
}
```

- [ ] **Step 8: `app/src/core_host.cpp`: the threading functions.** Before the closing `}  // namespace vj`, add

```cpp
// ---- threading (#967) -------------------------------------------------------------
void CoreHost::setOwner(std::thread::id owner) {
    if (owner != std::thread::id()) {
        // Runs on the new emulation thread while the UI thread waits in EmuThread::start(): the copies start equal.
        uiUser_ = userValues_;
        uiSession_ = sessionValues_;
        pubOptSerial_ = optSerial_ - 1;   // publish the table at once
        discDirty_ = true;
    }
    owner_.store(owner);
    if (owner != std::thread::id()) {
        publishSnapshot();
        std::lock_guard<std::mutex> l(snapMu_);
        ui_ = pub_;
    }
}

bool CoreHost::noteOffOwner(const char* what) const {
    if (onOwner()) return false;
    ownerViolations_.fetch_add(1);
    std::lock_guard<std::mutex> l(violationMu_);
    if (violationSeen_.insert(what).second)
        std::fprintf(stderr, "[core_host] owner violation: %s called off the emulation thread\n", what);
    return true;
}

void CoreHost::queue(std::function<void()> fn) {
    if (post) post(std::move(fn));
    else fn();
}

const OptionTable& CoreHost::uiTable() const {
    static const OptionTable kNone;
    return ui_.options ? *ui_.options : kNone;
}

void CoreHost::publishSnapshot() {
    if (!threaded() || !onOwner()) return;
    std::shared_ptr<const OptionTable> table;
    if (optSerial_ != pubOptSerial_) {
        std::shared_ptr<OptionTable> t = std::make_shared<OptionTable>();
        t->categories = categories_;
        t->defs = defs_;
        t->visibility = visibility_;
        t->locked = locked_;
        table = t;
        pubOptSerial_ = optSerial_;
    }
    const bool discChanged = discDirty_;
    DiscInfo disc;
    if (discChanged) {
        disc = discInfoLive();
        discDirty_ = false;
    }
    std::lock_guard<std::mutex> l(snapMu_);
    if (table) pub_.options = table;
    pub_.optionPushes = optionPushes_;
    pub_.fps = avInfo_.timing.fps;
    pub_.sampleRate = avInfo_.timing.sample_rate;
    pub_.aspect = aspectLive();
    if (discChanged) pub_.disc = disc;
}

void CoreHost::pinSnapshot() {
    if (onOwner()) return;
    std::lock_guard<std::mutex> l(snapMu_);
    ui_ = pub_;
}
```

- [ ] **Step 9: Run the new test and every test that drives CoreHost inline**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "core_host_thread|settings_reset|network_options|typed_options|netplay_unit|bios_e2e|input_e2e|devices_e2e|debug_e2e|debug_cpu_e2e|debug_op_e2e|debug_scan_e2e|smoke_" --output-on-failure`
Expected: all pass. `core_host_thread` prints `core_host_thread_test: N checks, 0 failed`. The inline tests are unchanged because `onOwner()` is always true without an owner.

- [ ] **Step 10: Commit**

```bash
git add app/src/core_host.h app/src/core_host.cpp app/tests/core_host_thread_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): CoreHost owner thread, EmuSnapshot and queued writes (#967)

Inline nothing changes.  With an owner, the UI reads options, AV info and
discs from a pinned EmuSnapshot, option values through a UI-side mirror (the
config is still written on the UI thread), and its writes reach the core at
the next frame boundary.  Any other core call off the owner is refused,
counted and logged once.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 8: Real-core determinism, inline vs threaded

This is an integration test over Tasks 1-7, so it should pass on its first run. If it fails, the defect is in one of those tasks: fix it there, never in this test's expectations.

**Files:**
- Create: `app/tests/emu_thread_e2e.cpp`
- Modify: `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: `EmuThread`, `FrameExchange`, `InputSnapshot`, `CoreHost::setOwner/publishSnapshot/ownerViolations`, `CoreInput`.
- Produces: the `emu_thread_e2e` ctest.

- [ ] **Step 1: Write the test** `app/tests/emu_thread_e2e.cpp`

```cpp
// The REAL core inline vs on the emulation thread (#967): the same per-frame input script must give the same frames
// and the same Jaguar joypad matrix, frame for frame -- with the script applied on the emulation side, and through
// InputSnapshot in lock-step with this (UI) thread.  A negative control proves the script reaches the core.  Then the
// plan's review-focus pins: a load while frames run, and the owner rule.
//
// usage: vj_emu_thread_e2e <rom.j64>
#include "core_host.h"
#include "emu_thread.h"
#include "frame_exchange.h"
#include "input_snapshot.h"
#include "remap.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

extern "C" {
extern uint8_t joypad0Buttons[];   // src/jerry/joystick.h: what the 68000 reads for port 1
}

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_fail++;                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

uint32_t fnv(const void* p, size_t n, uint32_t h = 2166136261u) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

// What frame `i` holds on port 1: A for frames 60-119, keypad 5 + B for 180-239.  `press` false = nothing at all.
CoreInput script(unsigned i, bool press) {
    CoreInput c;
    c.beginFrame();
    if (!press) return c;
    ControlSet s;
    if (i >= 60 && i < 120) s.set(C_A);
    if (i >= 180 && i < 240) {
        s.set(C_K5);
        s.set(C_B);
    }
    c.addControls(0, s);
    return c;
}

enum class Feed { Script, Snapshot };   // the script on the emulation side, or through InputSnapshot

struct Rig : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    FrameExchange frames;
    InputSnapshot snap;
    CoreInput cur;
    Feed feed = Feed::Script;
    bool press = true;
    unsigned index = 0;              // emulated frames in this session (emulation thread)
    std::vector<uint32_t> record;    // per frame: pixels + joypad matrix, hashed (emulation thread)
    uint64_t lastSerial = 0;

    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 600.0; }   // free-running parts: ten times real speed
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {
        if (feed == Feed::Script) cur = script(index, press);
        host.runFrame();
        const Frame& f = host.frame();
        uint32_t h = fnv(f.pixels.data(), f.pixels.size() * sizeof(uint32_t));
        h = fnv(joypad0Buttons, 21, h);
        record.push_back(h);
        if (f.serial != lastSerial) {
            FrameExchange::Slot& s = frames.writeBuffer();
            s.frame = f;
            frames.publish();
            lastSerial = f.serial;
        }
        index++;
        host.publishSnapshot();
    }
    void afterCommands() override { host.publishSnapshot(); }

    void begin(Feed fd, bool pr) {
        feed = fd;
        press = pr;
        index = 0;
        record.clear();
        lastSerial = 0;
    }
};

// Loads the ROM, runs `n` frames, unloads; returns the per-frame record.
std::vector<uint32_t> session(Rig& rig, const std::string& rom, unsigned n, bool threaded, Feed feed, bool press) {
    rig.begin(feed, press);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setPaused(true);   // frames only through advance()
    CHECK(emu.start(threaded ? EmuThread::Mode::Threaded : EmuThread::Mode::Inline, rig));
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    if (feed == Feed::Snapshot) {
        for (unsigned i = 0; i < n; i++) {
            rig.snap.publish(script(i, press), 0);   // lock-step: input i is published, then frame i runs and takes it
            emu.advance(1);
        }
    } else {
        emu.advance(n);
    }
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
    return rig.record;   // the thread has been joined: its writes are visible
}

void reportDivergence(const char* what, const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) {
            std::fprintf(stderr, "  %s: first divergence at frame %zu (%08x vs %08x)\n", what, i, a[i], b[i]);
            return;
        }
    if (a.size() != b.size()) std::fprintf(stderr, "  %s: %zu vs %zu frames\n", what, a.size(), b.size());
}

// A load while frames free-run: served at a frame boundary (never refused as "halted"), the core's buffer is empty
// inside the load call (a thumbnail taken then would be blank, never the previous game's), and FrameExchange never
// hands out a frame from before the switch.
void loadWhileRunning(Rig& rig, const std::string& rom) {
    rig.begin(Feed::Script, false);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    CHECK(emu.start(EmuThread::Mode::Threaded, rig));   // not paused: frames run at 600 Hz
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(emu.framesRun() > 20);
    uint64_t lastSeen = 0;
    while (const FrameExchange::Slot* s = rig.frames.takeNewest()) lastSeen = s->frame.serial;
    unsigned widthInsideLoad = 1;
    const bool loaded = emu.call([&] {
        rig.frames.clear();
        const bool ok = rig.host.loadContent(rom, err);
        widthInsideLoad = rig.host.frame().width;
        return ok;
    });
    CHECK(loaded);
    CHECK(widthInsideLoad == 0);
    const uint64_t epoch = rig.frames.epoch();
    int newFrames = 0;
    bool monotonic = true, sameEpoch = true;
    for (int i = 0; i < 100; i++) {
        if (const FrameExchange::Slot* s = rig.frames.takeNewest()) {
            newFrames++;
            if (s->epoch != epoch) sameEpoch = false;
            if (s->frame.serial <= lastSeen) monotonic = false;
            lastSeen = s->frame.serial;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(newFrames > 0);
    CHECK(sameEpoch);
    CHECK(monotonic);   // VideoOut::upload skips a repeated serial: serials must keep rising across the load
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
}

// Off the emulation thread a core call is refused and counted; the emulation thread runs it.
void ownerRule(Rig& rig, const std::string& rom, const std::string& stateFile) {
    rig.begin(Feed::Script, false);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setPaused(true);
    CHECK(emu.start(EmuThread::Mode::Threaded, rig));
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    const unsigned v0 = rig.host.ownerViolations();
    const uint64_t f0 = rig.host.frameCount();
    rig.host.runFrame();
    CHECK(!rig.host.saveStateToFile(stateFile, err));
    CHECK(rig.host.frameCount() == f0);
    CHECK(rig.host.ownerViolations() >= v0 + 2);
    CHECK(emu.advance(2) && rig.host.frameCount() == f0 + 2);
    CHECK(emu.call([&] { return rig.host.saveStateToFile(stateFile, err); }));
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_emu_thread_e2e <rom.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-emu-thread-e2e-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    Rig rig;
    rig.host.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    rig.host.inputPoll = [&rig] {
        if (rig.feed == Feed::Snapshot) rig.snap.take(rig.cur);
    };
    rig.host.inputState = [&rig](unsigned p, unsigned d, unsigned i, unsigned id) { return rig.cur.state(p, d, i, id); };
    const std::string rom = argv[1];
    const unsigned N = 300;

    const std::vector<uint32_t> inl = session(rig, rom, N, false, Feed::Script, true);
    const std::vector<uint32_t> thr = session(rig, rom, N, true, Feed::Script, true);
    CHECK(inl.size() == N);
    CHECK(thr == inl);
    if (thr != inl) reportDivergence("threaded vs inline", thr, inl);

    const std::vector<uint32_t> viaSnapshot = session(rig, rom, N, true, Feed::Snapshot, true);
    CHECK(viaSnapshot == inl);
    if (viaSnapshot != inl) reportDivergence("InputSnapshot vs inline", viaSnapshot, inl);

    const std::vector<uint32_t> none = session(rig, rom, N, true, Feed::Script, false);
    CHECK(none.size() == N && none[59] == inl[59]);   // identical until the first press...
    bool differs = false;
    for (unsigned i = 60; i < 126 && i < none.size(); i++) differs = differs || none[i] != inl[i];
    CHECK(differs);                                   // ...then the script is visible in the record

    loadWhileRunning(rig, rom);
    ownerRule(rig, rom, (tmp / "owner.state").string());

    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("emu_thread_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   emu_thread_e2e        the REAL core inline vs threaded: identical frames and joypad matrix for one input script` to the header list:

```cmake
add_executable(vj_emu_thread_e2e tests/emu_thread_e2e.cpp src/core_host.cpp src/emu_thread.cpp src/remap.cpp
               src/config.cpp src/paths.cpp)
target_include_directories(vj_emu_thread_e2e PRIVATE src)
target_link_libraries(vj_emu_thread_e2e PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_emu_thread_e2e PRIVATE ws2_32 iphlpapi)
endif()
vj_emu_thread_warnings(vj_emu_thread_e2e)
add_test(NAME emu_thread_e2e COMMAND vj_emu_thread_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_e2e PROPERTIES TIMEOUT 600 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it**

Run: `cmake --build app/build -j8 --target vj_emu_thread_e2e && ctest --test-dir app/build -C Release -R emu_thread_e2e --output-on-failure`
Expected: `emu_thread_e2e: N checks, 0 failed`. If the negative control fails (`differs` is false), check that the `joypad0Buttons` hash covers the 21 port-1 slots (`src/jerry/joystick.h`) before suspecting the input path.

- [ ] **Step 4: Commit**

```bash
git add app/tests/emu_thread_e2e.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
test(app): the real core inline vs on the emulation thread, frame for frame (#967)

Also pins a load while frames run (no stale frame, empty buffer inside the
load call, rising serials) and the owner rule.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 9: The "Emulation thread" setting and `--emu-thread`

Phase 1 defaults the setting Off in every build (decided 2026-10-16): with it On, this phase switches off RetroAchievements, netplay, the debugger tools and the GDB stub, so nightly users would lose features. Nightlies turn it on by default in Phase 3, and that plan adds the build-channel plumbing. Until then, `--emu-thread on` and the Settings switch are how it gets tested.

**Files:**
- Modify: `app/src/app.h` (`CliArgs` after `perfPause` at `:74`; `App` public after `keepAwake` at `:184`; private after `inputCaptured_` at `:291`), `app/src/app.cpp` (`applySettingsFromConfig` `:102-124`, `saveConfig` `:126-155`, a definition after `toast` `:165-174`, `run` after `loadConfig();` at `:1158`), `app/src/main.cpp` (usage text before `"dev: --perf-frames ..."` at `:38`; parsing after `--perf-pause` at `:178`), `app/src/ui.cpp` (`drawInterfaceTab` `:248-274`), `app/src/settings_reset.cpp:34`, `app/tests/settings_reset_test.cpp` (`:67`, `:98`, `:203`), `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: nothing new.
- Produces: `CliArgs::emuThread` (-1/0/1); `int App::emuThreadPref` (-1 never chosen, 0, 1); `bool App::emuThreadWanted() const`; `bool App::emuThreadThisRun() const`; config key `ui.emu_thread` (written only once chosen, so Phase 3 can change the default for users who never chose).

- [ ] **Step 1: Write the failing tests.** In `app/tests/settings_reset_test.cpp`, in `fill()`'s key list, after `"ui.tray", "ui.keep_awake", "ui.viewports", "ui.menubar_in_window",` add `"ui.emu_thread",`. After the line `CHECK(classifyKey("ui.tray") == KeyClass::Interface && classifyKey("ui.keep_awake") == KeyClass::Interface);` add

```cpp
    CHECK(classifyKey("ui.emu_thread") == KeyClass::Interface);   // #967: Settings > Interface > Emulation thread
```

In the "Every app setting is gone" list, after `"ui.tray", "ui.keep_awake", "ui.viewports", "ui.menubar_in_window",` add `"ui.emu_thread",`.

Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   emu_thread_flag_rejects  --emu-thread takes on or off only` to the header list:

```cmake
add_test(NAME emu_thread_flag_rejects COMMAND virtualjaguar --headless --emu-thread maybe "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_flag_rejects PROPERTIES LABELS emu_thread PASS_REGULAR_EXPRESSION "--emu-thread wants on or off")
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "settings_reset|emu_thread_flag_rejects" --output-on-failure`
Expected: `settings_reset` fails on `classifyKey("ui.emu_thread") == KeyClass::Interface` (the key is unknown); `emu_thread_flag_rejects` fails because `--emu-thread` is an unknown option.

- [ ] **Step 3: The setting in App.** In `app/src/app.h`, in `CliArgs` after `bool perfPause = false;` add

```cpp
    int emuThread = -1;             // --emu-thread on|off: the emulation on its own thread for this run (-1 = the setting; headless: off)
```

In `App`, after `bool keepAwake = true;` add

```cpp
    // Settings > Interface > Emulation thread (ui.emu_thread, #967): -1 = never chosen, 0 off, 1 on.  Never chosen is
    // off in Phase 1 on every build; Phase 3 turns it on by default in nightlies.
    int emuThreadPref = -1;
    bool emuThreadWanted() const;                                 // the choice (never chosen = off)
    bool emuThreadThisRun() const { return emuThreadThisRun_; }   // decided once at start-up
```

and in the private section, after `bool inputCaptured_ = false;` add

```cpp
    bool emuThreadThisRun_ = false;   // run(): --emu-thread, else the setting (headless: off unless asked)
```

In `app/src/app.cpp`, at the end of `applySettingsFromConfig()` add

```cpp
    emuThreadPref = cfg.has("ui.emu_thread") ? (cfg.getBool("ui.emu_thread", false) ? 1 : 0) : -1;
```

In `saveConfig()`, after `cfg.setBool("ui.menubar_in_window", showInWindowMenu);` add

```cpp
    if (emuThreadPref >= 0) cfg.setBool("ui.emu_thread", emuThreadPref == 1);   // unwritten until chosen (Phase 3's default)
    else cfg.erase("ui.emu_thread");
```

After `App::toast`, add

```cpp
bool App::emuThreadWanted() const { return emuThreadPref == 1; }   // never chosen: off in Phase 1 (#967)
```

In `App::run`, right after `loadConfig();`, add

```cpp
    // The emulation thread (#967) is decided once per run: switching it live would move the core between threads.
    // Headless runs stay inline unless asked (the smoke tests' hashes are inline hashes), whatever the setting says.
    emuThreadThisRun_ = a.emuThread >= 0 ? a.emuThread == 1 : (!a.headless && emuThreadWanted());
```

- [ ] **Step 4: The command line.** In `app/src/main.cpp`, in the usage text, right before the `"dev: --perf-frames N ..."` line, add

```cpp
        "  --emu-thread on|off   the emulation on its own thread for this run (default: Settings > Interface; headless: off)\n"
```

and after `else if (!std::strcmp(s, "--perf-pause")) a.perfPause = true;` add

```cpp
        else if (!std::strcmp(s, "--emu-thread")) {
            const std::string v = need(s);
            if (v != "on" && v != "off") {
                std::fprintf(stderr, "--emu-thread wants on or off, got '%s'\n", v.c_str());
                return 2;
            }
            a.emuThread = v == "on" ? 1 : 0;
        }
```

- [ ] **Step 5: Settings > Interface.** In `app/src/ui.cpp`, in `drawInterfaceTab`, right before `ImGui::SeparatorText("Save states");`, add

```cpp
    ImGui::SeparatorText("Performance");
    bool emuThread = app.emuThreadWanted();
    if (ImGui::Checkbox("Emulation thread", &emuThread)) {
        app.emuThreadPref = emuThread ? 1 : 0;
        app.saveConfig();
    }
    ui::hint("Runs the emulation on its own thread, so a slow frame or a busy window does not stall the picture, and "
             "the game keeps its true speed (60.05 Hz, 50.08 Hz in PAL) on any display. Takes effect the next time the "
             "app starts. While it is on, this version turns off RetroAchievements, netplay and the Tools (debugger) "
             "menu; they come back in a later update. Off by default.");
    if (app.emuThreadWanted() != app.emuThreadThisRun()) ImGui::TextDisabled("Restart the app to apply.");
```

In `app/src/settings_reset.cpp`, after the line `{"ui.viewports", KeyClass::Interface},    {"ui.menubar_in_window", KeyClass::Interface},` add

```cpp
    {"ui.emu_thread", KeyClass::Interface},   // Interface > Performance > Emulation thread (#967)
```

- [ ] **Step 6: Run the tests**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "settings_reset|emu_thread_flag_rejects|smoke_yarc" --output-on-failure`
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add app/src/app.h app/src/app.cpp app/src/main.cpp app/src/ui.cpp app/src/settings_reset.cpp \
        app/tests/settings_reset_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): Settings > Interface > Emulation thread and --emu-thread (#967)

Off by default in every build for now (nightlies default it on in phase 3,
once nothing is switched off while threaded).  Decided once per run;
headless runs stay inline unless --emu-thread on.  Nothing starts a thread
yet.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 10: Perf: input latency and underrun columns

**Files:**
- Create: `app/tests/perf_csv_test.cpp`
- Modify: `app/src/perf.h` (fields after `curSync` at `:47`; `frameBegin` `:77-92`; `report` `:115-147`), `app/src/app.h` (`CliArgs` after `emuThread`; App private after `emuThreadThisRun_`), `app/src/app.cpp` (`wireHost` `:337`, `handleEvent` after `:985`, the `runInteractive` loop before `perf.frameEnd();`, new functions after `syncDebug`), `app/src/main.cpp` (after the `--emu-thread` parsing), `docs/agent/testing.md:304-305`, `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: `AudioOut::underruns()`.
- Produces: `Perf::curUnder`, `Perf::curLat`, CSV header `iter_ms,emu_frames,swap_sync,underruns,input_lat_ms` (the first three columns are unchanged, so `pacing_trace` still reads the file); `App::noteInputLatency(uint64_t eventNs)`, `App::perfSample()`; members `pendingEventNs_`, `latSumNs_`, `latCount_`, `underrunsSeen_`; `CliArgs::perfInput` (`--perf-input`).

- [ ] **Step 1: Write the failing test** `app/tests/perf_csv_test.cpp`

```cpp
// VJ_PERF_CSV (perf.h): one row per measured UI iteration, with the audio-underrun and input-latency columns the #967
// measurement uses.  No window: Perf is driven by hand.
//
//   vj_perf_csv_test <scratch-dir>
#include "perf.h"

#include <stdlib.h>   // setenv / _putenv_s

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_perf_csv_test <scratch-dir>\n");
        return 2;
    }
    const std::string path = std::string(argv[1]) + "/vj_perf_csv_test.csv";
#ifdef _WIN32
    _putenv_s("VJ_PERF_CSV", path.c_str());
#else
    setenv("VJ_PERF_CSV", path.c_str(), 1);
#endif
    vj::Perf p;
    p.init(3, false);
    p.warmup = 0;
    for (int i = 0; i < 4; i++) {   // iteration i's values are recorded by the frameBegin() of iteration i + 1
        p.frameBegin();
        p.emulated();
        p.curUnder = i == 2 ? 1u : 0u;
        p.curLat = i == 1 ? 4.5f : -1.0f;
        p.frameEnd();
    }
    p.report("perf_csv_test");
    std::ifstream f(path);
    std::vector<std::string> lines;
    for (std::string l; std::getline(f, l);) lines.push_back(l);
    CHECK(lines.size() == 4, "a header and three rows (%zu lines)", lines.size());
    if (lines.size() == 4) {
        CHECK(lines[0] == "iter_ms,emu_frames,swap_sync,underruns,input_lat_ms", "header: %s", lines[0].c_str());
        CHECK(lines[1].find(",1,1,0,-1.000") != std::string::npos, "row 1, one frame and no input: %s", lines[1].c_str());
        CHECK(lines[2].find(",0,4.500") != std::string::npos, "row 2, a 4.5 ms input latency: %s", lines[2].c_str());
        CHECK(lines[3].find(",1,-1.000") != std::string::npos, "row 3, one underrun: %s", lines[3].c_str());
    }
    std::printf(g_fail ? "perf_csv_test: %d FAILED\n" : "perf_csv_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   perf_csv_unit         VJ_PERF_CSV columns: underruns and input latency` to the header list:

```cmake
add_executable(vj_perf_csv_test tests/perf_csv_test.cpp)
target_include_directories(vj_perf_csv_test PRIVATE src)
target_link_libraries(vj_perf_csv_test PRIVATE ${VJ_SDL3_TARGET})
vj_emu_thread_warnings(vj_perf_csv_test)
add_test(NAME perf_csv_unit COMMAND vj_perf_csv_test "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(perf_csv_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_perf_csv_test`
Expected: compile errors: `no member named 'curUnder'` and `'curLat'` in `vj::Perf`.

- [ ] **Step 4: Perf.** In `app/src/perf.h`, after `bool curSync = true;` add

```cpp
    std::vector<unsigned short> underPer;   // audio underruns counted during that iteration (VJ_PERF_CSV)
    std::vector<float> latPer;              // input event -> core poll latency measured in it, ms; -1 = none
    unsigned curUnder = 0;                  // set by the loop for the current iteration (App::perfSample)
    float curLat = -1.0f;
```

In `frameBegin`, after `syncPer.push_back(curSync ? 1 : 0);` add

```cpp
            underPer.push_back((unsigned short)std::min(curUnder, 65535u));
            latPer.push_back(curLat);
```

and after `emuThis = 0;` add

```cpp
        curUnder = 0;
        curLat = -1.0f;
```

In `report`, before the `if (const char* csv = std::getenv("VJ_PERF_CSV"))` block add

```cpp
        {
            double lsum = 0, lmax = 0;
            size_t ln = 0;
            unsigned long long under = 0;
            for (float l : latPer)
                if (l >= 0) {
                    lsum += l;
                    lmax = std::max(lmax, (double)l);
                    ln++;
                }
            for (unsigned short u : underPer) under += u;
            if (ln)
                std::printf("PERF   input latency (event -> core poll): avg %.3f ms  max %.3f ms  (%zu samples)\n",
                            lsum / (double)ln, lmax, ln);
            std::printf("PERF   audio underruns: %llu\n", under);
        }
```

and replace the CSV header and row lines with

```cpp
                std::fprintf(f, "iter_ms,emu_frames,swap_sync,underruns,input_lat_ms\n");
                for (size_t i = 0; i < n && i < emuPer.size(); i++)
                    std::fprintf(f, "%.3f,%d,%d,%u,%.3f\n", frameMs[i], (int)emuPer[i], (int)syncPer[i],
                                 i < underPer.size() ? (unsigned)underPer[i] : 0u,
                                 i < latPer.size() ? (double)latPer[i] : -1.0);
```

Update the comment above it to `// VJ_PERF_CSV=path: one row per measured loop iteration (iteration ms, emulated frames run in it, swap interval 1?, audio underruns, input latency ms or -1) -- the raw material for "how evenly were new images presented" (tests/pacing_trace.cpp replays the first three from a core cost trace).`

- [ ] **Step 5: Run the unit test**

Run: `cmake --build app/build -j8 --target vj_perf_csv_test && ctest --test-dir app/build -C Release -R perf_csv_unit --output-on-failure`
Expected: `perf_csv_test: OK`.

- [ ] **Step 6: Feed the columns from the app (inline loop; Task 11 adds the threaded side).** In `app/src/app.h`, `CliArgs`, after `emuThread`, add

```cpp
    bool perfInput = false;         // dev: with --perf-frames, inject a key event every 30 iterations (input-latency column)
```

and in the App private section, after `emuThreadThisRun_`, add

```cpp
    // VJ_PERF_CSV input latency and underruns (#967 measurement)
    uint64_t pendingEventNs_ = 0;             // UI thread: SDL timestamp of the oldest input event not yet polled
    std::atomic<uint64_t> latSumNs_{0}, latCount_{0};   // written where the core polls, read per UI iteration
    uint64_t underrunsSeen_ = 0;
    void noteInputLatency(uint64_t eventNs);
    void perfSample();
```

Add `#include <atomic>` to `app/src/app.h` after `#include <cstdint>`. In `app/src/app.cpp`, at the top of the anonymous namespace add

```cpp
bool isInputEvent(Uint32 type) {
    switch (type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: return true;
    default: return false;
    }
}
```

In `wireHost`, replace `host.inputPoll = [this]() { input.poll(!inputCaptured_); };` with

```cpp
    host.inputPoll = [this]() {
        if (pendingEventNs_) {   // VJ_PERF_CSV: from the oldest unhandled input event to this poll
            noteInputLatency(pendingEventNs_);
            pendingEventNs_ = 0;
        }
        input.poll(!inputCaptured_);
    };
```

In `handleEvent`, after `if (fromExtraWindow(e)) return;` add

```cpp
    if (isInputEvent(e.type) && !pendingEventNs_) pendingEventNs_ = e.common.timestamp;
```

After `App::syncDebug`, add

```cpp
void App::noteInputLatency(uint64_t eventNs) {
    const uint64_t t = SDL_GetTicksNS();
    // An event older than 250 ms waited out a pause or a load, not the pacing: it would only pollute the average.
    if (!eventNs || t <= eventNs || t - eventNs > 250000000ull) return;
    latSumNs_.fetch_add(t - eventNs, std::memory_order_relaxed);
    latCount_.fetch_add(1, std::memory_order_relaxed);
}

// Once per UI iteration (VJ_PERF_CSV): the audio underruns and the input latency measured since the last one.
void App::perfSample() {
    if (!perf.on) return;
    const uint64_t u = audio.underruns();
    perf.curUnder = (unsigned)(u - underrunsSeen_);
    underrunsSeen_ = u;
    const uint64_t n = latCount_.exchange(0, std::memory_order_relaxed);
    const uint64_t s = latSumNs_.exchange(0, std::memory_order_relaxed);
    perf.curLat = n ? (float)((double)s / (double)n / 1e6) : -1.0f;
}
```

In `runInteractive`'s loop, replace

```cpp
        perf.curSync = pacer_.swapSync;
        perf.frameEnd();
```

with

```cpp
        perf.curSync = pacer_.swapSync;
        perfSample();
        if (a.perfInput && perf.on && perf.seen % 30 == 29) {
            // A synthetic key (F24: bound to nothing) so the input-latency column has samples in an unattended run.
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.windowID = SDL_GetWindowID(window_);
            ev.key.scancode = SDL_SCANCODE_F24;
            ev.key.down = true;
            ev.key.timestamp = SDL_GetTicksNS();
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_KEY_UP;
            ev.key.down = false;
            SDL_PushEvent(&ev);
        }
        perf.frameEnd();
```

In `app/src/main.cpp`, after the `--emu-thread` block add `else if (!std::strcmp(s, "--perf-input")) a.perfInput = true;`, and extend the `dev: --perf-frames` usage line to `"dev: --perf-frames N [--perf-gpu] [--perf-pause] [--perf-no-reuse] [--perf-shader-out WxH] [--perf-input]\n"`.

In `docs/agent/testing.md`, replace

```text
vsync release/restore cycles); `virtualjaguar --perf-frames N` with `VJ_PERF_CSV=f.csv` records the
real thing.
```

with

```text
vsync release/restore cycles); `virtualjaguar --perf-frames N` with `VJ_PERF_CSV=f.csv` records the
real thing (`iter_ms,emu_frames,swap_sync,underruns,input_lat_ms`; `--perf-input` injects a key every 30
iterations so the latency column has samples; `--emu-thread on|off` picks the loop, #967).
```

- [ ] **Step 7: Build and run the regression set**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "perf_csv_unit|smoke_|input_e2e|remap_unit" --output-on-failure`
Expected: all pass.

- [ ] **Step 8: Commit**

```bash
git add app/src/perf.h app/src/app.h app/src/app.cpp app/src/main.cpp app/tests/perf_csv_test.cpp \
        app/cmake/VJEmuThreadTests.cmake docs/agent/testing.md
git commit -F - <<'EOF'
feat(app): VJ_PERF_CSV input-latency and underrun columns, --perf-input (#967)

Latency is the oldest input event's SDL timestamp to the core's poll; the
first three columns are unchanged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 11: App actions through `call()`, and the headless threaded run

**Files:**
- Modify: `app/src/app.h` (includes; public services after `DebugSession debug;` at `:126`; private section), `app/src/app.cpp` (new `App::EmuClient` before `App::App()` at `:70`; `wireHost` `:332-349`; `openContent` `:534-587` + new `openContentOnCore`; `openNoContent` `:589-622` + new `openNoContentOnCore`; `bootBios` `:624-634`; `closeContent` `:667-683`; `reset` `:685-695`; `togglePause` `:697-709`; `autoSave` `:725-730`; `offerResume` `:736-739`; `openAndLoadState` `:778-779`; `handleDrop` `:798`; `saveState` `:822-854`; `loadState` `:856-880`; `stepEmulation` `:1100`; `frameHash` `:1128-1138`; `shutdown` `:302-306`; `runInteractive` (`syncEmuFlags()` before `stepEmulation()`, the final SRAM flush); `runHeadless` `:1404-1592`; new functions after `perfSample`), `app/src/ui.cpp:537`, `:543`, `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: `EmuThread`, `FrameExchange`, `InputSnapshot`, `CoreHost::setOwner/publishSnapshot/pinSnapshot`, `AudioOut::update(bool)/paused()`, `App::noteInputLatency`, `App::emuThreadThisRun_`.
- Produces: `App::emu`, `App::frames`, `App::inputSnap`, `bool App::threaded() const`, `bool App::haltedNow() const`; private `startEmuThread()`, `stopEmuThread()`, `emuRunOneFrame()`, `emuAudioQueuedMs()`, `syncEmuFlags()`, `shownFrame()`, `runHeadlessFrames(unsigned)`, `openContentOnCore(...)`, `openNoContentOnCore(...)`, `runSeconds()`, `frameHash(const Frame&)`; members `runFrames_`, `retroTicks_`, `emuFastForward_`, `emuMute_`, `emuInput_`, `lastSentSerial_`, `emuFramesSeen_`, `pendingStateAt_`. Inline behaviour is unchanged: `emu.call()` runs inline.

- [ ] **Step 1: Write the failing tests.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   smoke_*_threaded      the headless smoke tests with --emu-thread on: the inline hashes, and no owner violation` to the header list:

```cmake
# The headless smoke tests (VJSmokeTests.cmake) again with --emu-thread on: the same hashes as inline, nothing on the
# UI thread calling into the core, and the run really threaded (the OK line says so only then).
set(_vj_threaded_ok "headless: OK \\([0-9]+ load\\(s\\), emulation thread\\)")
add_test(NAME smoke_yarc_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames ${VJ_SMOKE_FRAMES} ${_yarc_hash_args}
            "${VJ_ROMS}/yarc.j64")
add_test(NAME smoke_reload_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames ${VJ_SMOKE_FRAMES}
            "${VJ_ROMS}/yarc.j64" "${VJ_ROMS}/jagniccc.j64" "${VJ_ROMS}/yarc.j64")
add_test(NAME smoke_state_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 240 --state-roundtrip "${VJ_ROMS}/jagniccc.j64")
add_test(NAME smoke_boot_cd_bios_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 400 --boot-bios cd --expect-bios cd:builtin
            --expect-hash b83cb3ee)
add_test(NAME cheat_apply_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 60 --cheat 001F0000:AB --expect-ram 1F0000=AB
            "${VJ_ROMS}/yarc.j64")
set_tests_properties(smoke_yarc_threaded smoke_reload_threaded smoke_state_threaded smoke_boot_cd_bios_threaded
                     cheat_apply_threaded PROPERTIES
    TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "${_vj_threaded_ok}"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "_threaded$" --output-on-failure`
Expected: all five FAIL. The runs are inline, so the output ends `headless: OK (1 load(s))` without `, emulation thread`, and the pass expression does not match.

- [ ] **Step 3: App members.** In `app/src/app.h`, add `#include <memory>` after `#include <map>`, and `#include "emu_thread.h"`, `#include "frame_exchange.h"`, `#include "input_snapshot.h"` after `#include "discord_presence.h"`. After `DebugSession debug;` add

```cpp
    // ---- the emulation thread (#967; docs/app-emulation-thread-design.md) ------------------------------------
    EmuThread emu;              // inline (no thread) unless this run turned it on (emuThreadThisRun)
    FrameExchange frames;       // finished frames, emulation thread -> UI thread
    InputSnapshot inputSnap;    // the newest input, UI thread -> the core's poll
    bool threaded() const { return emu.threaded(); }
    // The core is parked at a debugger breakpoint (it then runs the UI from inside retro_run; inline mode only).  This
    // is what every "may the UI re-enter the core?" check asks: CoreHost::inFrame() is the core's own view, and when
    // threaded it is true for the whole of every frame.
    bool haltedNow() const { return debug.halted(); }
```

In the private section replace `uint32_t frameHash() const;` with `uint32_t frameHash(const Frame& f) const;`, replace `double runSeconds_ = 0;           // emulated seconds of the loaded game (auto-save needs a few)` with

```cpp
    std::atomic<uint64_t> runFrames_{0};   // emulated frames of the loaded game (auto-save needs a few seconds)
    double runSeconds() const;
```

replace `size_t audioFramesSeen_ = 0;` with `std::atomic<size_t> audioFramesSeen_{0};`, and after `uint64_t perfFreq_ = 1;` add

```cpp
    // the emulation thread (#967)
    struct EmuClient;                          // EmuThread::Client -> App (app.cpp)
    std::unique_ptr<EmuClient> emuClient_;
    void startEmuThread();                     // after the window and the audio device; stays inline if it cannot
    void stopEmuThread();                      // exits the process if the thread is stuck in the core
    void emuRunOneFrame();                     // emulation thread: one frame, then publish it
    double emuAudioQueuedMs() const;           // mode A's audio clock, or -1 for none
    void syncEmuFlags();                       // UI thread: pause / fast-forward / mute / audio target -> emulation side
    const Frame& shownFrame();                 // the newest finished frame (FrameExchange when threaded)
    void runHeadlessFrames(unsigned n);
    bool openContentOnCore(const std::string& path, std::string& err, std::vector<std::string>& overrideErrors);
    bool openNoContentOnCore(std::string& err);
    std::atomic<bool> emuFastForward_{false}, emuMute_{false};   // the UI's fastForward / mute, for the emulation side
    std::atomic<uint64_t> retroTicks_{0};      // performance-counter ticks inside retro_run, since the UI last read them
    CoreInput emuInput_;                       // emulation thread: what the core reads this frame
    uint64_t lastSentSerial_ = 0;              // emulation thread: the last frame handed to FrameExchange
    uint64_t emuFramesSeen_ = 0;               // UI thread: EmuThread::framesRun() at the previous iteration
    uint64_t pendingStateAt_ = 0;              // threaded: run the pending state load once framesRun() reaches this
```

- [ ] **Step 4: The client and the per-frame work.** In `app/src/app.cpp` add `#include <chrono>`, `#include <cstdlib>` (for `std::_Exit`) and `#include <thread>` with the standard includes. Right before `App::App() : host(CoreHost::instance()) {}` add

```cpp
// EmuThread's view of the app.  Every method runs on the emulation thread (#967).
struct App::EmuClient : EmuThread::Client {
    App& app;
    explicit EmuClient(App& a) : app(a) {}
    void threadStarted() override { app.host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { app.host.setOwner(std::thread::id()); }
    bool canRun() override { return app.host.isLoaded(); }
    double framePeriod() override { return 1.0 / std::max(1.0, app.host.fps()); }
    double audioQueuedMs() override { return app.emuAudioQueuedMs(); }
    void runFrame() override { app.emuRunOneFrame(); }
    void afterCommands() override { app.host.publishSnapshot(); }
    void commandFailed(const std::string& what) override {
        app.host.pushMessage("Emulation command failed: " + what, 5000, RETRO_LOG_ERROR);
    }
};
```

After `App::perfSample` add

```cpp
// ---- the emulation thread (#967) ------------------------------------------------------------------------
void App::startEmuThread() {
    if (!emuClient_) emuClient_.reset(new EmuClient(*this));
    host.post = [this](std::function<void()> fn) { emu.post(std::move(fn)); };
    if (!emu.start(EmuThread::Mode::Threaded, *emuClient_)) {
        host.post = nullptr;
        std::fprintf(stderr, "[emu] could not start the emulation thread; running emulation on the UI thread\n");
        return;
    }
    syncEmuFlags();
    std::fprintf(stderr, "[emu] emulation thread on\n");
}

void App::stopEmuThread() {
    if (!emu.threaded()) return;
    if (!emu.stop(std::chrono::milliseconds(3000))) {
        std::fprintf(stderr, "[emu] the emulation thread did not stop within 3 s (the core is blocked); exiting without unloading\n");
        std::fflush(stderr);
        std::_Exit(0);   // the core still belongs to that thread: nothing here may touch it, nor run destructors over it
    }
    host.post = nullptr;
}

// One emulated frame on the emulation thread.  Inline mode keeps stepEmulation().
void App::emuRunOneFrame() {
    if (!emuFastForward_.load(std::memory_order_relaxed)) audio.update(false);   // underruns only: mode A follows the audio clock
    host.wantVideo = true;
    const uint64_t r0 = SDL_GetPerformanceCounter();
    host.runFrame();
    retroTicks_.fetch_add(SDL_GetPerformanceCounter() - r0, std::memory_order_relaxed);
    runFrames_.fetch_add(1, std::memory_order_relaxed);
    if (audio.queuedMs() > 1000.0f) audio.clear();   // a device that stopped pulling: keep the queue (and memory) bounded
    const Frame& f = host.frame();
    if (f.serial != lastSentSerial_ && f.width && f.height) {
        FrameExchange::Slot& s = frames.writeBuffer();
        s.frame.width = f.width;
        s.frame.height = f.height;
        s.frame.serial = f.serial;
        s.frame.pixels.assign(f.pixels.begin(), f.pixels.end());
        s.aspect = host.aspect();
        frames.publish();
        lastSentSerial_ = f.serial;
    }
    host.publishSnapshot();
}

// The audio clock for mode A, or -1 when there is none to follow: no device, a paused stream, mute (nothing is
// queued) or fast-forward (the audio is dropped).  PacingClock then uses the wall clock.
double App::emuAudioQueuedMs() const {
    if (!audio.isOpen() || audio.paused() || emuMute_.load(std::memory_order_relaxed) ||
        emuFastForward_.load(std::memory_order_relaxed))
        return -1.0;
    return audio.queuedMs();
}

// UI thread, every iteration and after a pause or a load: the switches the emulation side reads.
void App::syncEmuFlags() {
    emuFastForward_.store(fastForward, std::memory_order_relaxed);
    emuMute_.store(mute, std::memory_order_relaxed);
    emu.setFastForward(fastForward);
    emu.setPaused(paused || headless_);   // headless: frames only through advance(), so a run is exactly --frames long
    emu.setAudioTargetMs(audioTargetMs);
}

// The newest finished frame: the core's own buffer inline, FrameExchange's newest when threaded (the UI thread then
// never reads the core's buffer).
const Frame& App::shownFrame() {
    static const Frame kNone;
    if (!threaded()) return host.frame();
    if (const FrameExchange::Slot* s = frames.takeNewest()) return s->frame;
    const FrameExchange::Slot* c = frames.current();
    return c ? c->frame : kNone;
}

void App::runHeadlessFrames(unsigned n) {
    if (threaded()) {
        emu.advance(n);
        return;
    }
    for (unsigned i = 0; i < n; i++) {
        audio.update();
        host.runFrame();
    }
}

double App::runSeconds() const { return (double)runFrames_.load() / std::max(1.0, host.fps()); }
```

- [ ] **Step 5: `wireHost`.** Replace the `host.onAudio`, `host.inputPoll` and `host.inputState` assignments with

```cpp
    host.onAudio = [this](const int16_t* d, size_t n) {
        audioFramesSeen_ += n;
        if (!emuFastForward_.load(std::memory_order_relaxed) && !emuMute_.load(std::memory_order_relaxed)) audio.push(d, n);
    };
    host.inputPoll = [this]() {
        if (host.threaded()) {   // the emulation thread: the newest input the UI thread published (input_snapshot.h)
            const uint64_t ns = inputSnap.take(emuInput_);
            if (ns) noteInputLatency(ns);
            return;
        }
        if (pendingEventNs_) {   // VJ_PERF_CSV: from the oldest unhandled input event to this poll
            noteInputLatency(pendingEventNs_);
            pendingEventNs_ = 0;
        }
        input.poll(!inputCaptured_);
    };
    host.inputState = [this](unsigned p, unsigned d, unsigned i, unsigned id) {
        return host.threaded() ? emuInput_.state(p, d, i, id) : input.state(p, d, i, id);
    };
```

(`input.notify` and `host.optionLock` stay between them as before.)

- [ ] **Step 6: Content actions.** Replace `openContent` with the two functions below, and `openNoContent` likewise:

```cpp
bool App::openContent(const std::string& pathIn) {
    if (haltedNow()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return false;
    }
    const std::string path = absolutePath(pathIn);
    pendingStateSlot_ = kNoPendingSlot;   // a state load queued for the previous game does not follow to this one
    resumeOffer.active = false;
    const bool noResume = suppressResume_ || reloading_;
    if (!reloading_) autoSave();          // leaving a running game for another: keep where it was
    for (std::string& f : biosOnce_) f.clear();   // a game is not a BIOS boot
    bootTarget_ = 0;
    std::string err;
    std::vector<std::string> overrideErrors;
    // Everything that touches the core runs where the core lives: on the emulation thread at a frame boundary when
    // threaded (this thread waits), right here otherwise.
    const bool ok = emu.call([&] { return openContentOnCore(path, err, overrideErrors); });
    host.pinSnapshot();
    if (!ok) {
        toast("Could not open " + pathStem(path) + ": " + err, 5.0, 3);
        std::fprintf(stderr, "open failed: %s\n", err.c_str());
        return false;
    }
    for (const std::string& p : overrideErrors) toast("--option: " + p, 8.0, 3);
    paused = false;
    syncEmuFlags();
    pacer_.reset();
    lastTick_ = now();
    controlsOverlayOnLoad(*this);
    addRecent(path);
    library.noteLaunched(path);   // the library's recently played (library_lists.h)
    invalidateThumbs();
    debug.requestRefresh();
    discordNoteGameStarted(*this);
    runFrames_.store(0);
    toast("Loaded " + host.contentName());
    if (!noResume) offerResume();
    saveConfig();
    return true;
}

// The part of openContent that touches the core, and what feeds it (#967: the emulation thread runs it).
bool App::openContentOnCore(const std::string& path, std::string& err, std::vector<std::string>& overrideErrors) {
    host.setDirectories(paths.system, paths.saves);
    applyBios();
    frames.clear();   // no frame of the previous game is shown after this
    if (!host.loadContent(path, err)) {
        ach.onContentClosed();
        cheatsOnClose(*this);
        return false;
    }
    // An option override the core does not accept would silently run the default.
    badOverride_ = false;
    for (const std::string& p : host.checkOverrides(true)) {
        badOverride_ = true;
        std::fprintf(stderr, "option error: %s\n", p.c_str());
        overrideErrors.push_back(p);
    }
    for (const std::string& p : host.checkOverrides(false))
        std::fprintf(stderr, "option warning (saved setting ignored): %s\n", p.c_str());
    audio.open(host.sampleRate());   // (re)opens, re-formatted for this game's region
    applyAudioSettings();
    audio.clear();
    audio.setPaused(false);
    input.setContent(contentIdentity(path));   // selects the per-game control overrides
    applyPortDevices(*this);                   // before the first frame: the controller types chosen in Settings
    ach.onContentLoaded();                     // identify the game, look up its achievements
    cheatsAfterLoad(*this);                    // the core drops its cheat list on every unload
    host.publishSnapshot();
    return true;
}
```

```cpp
bool App::openNoContent() {
    if (haltedNow()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return false;
    }
    pendingStateSlot_ = kNoPendingSlot;   // booting a BIOS replaces the running game like opening another one does
    resumeOffer.active = false;
    autoSave();
    std::string err;
    const bool ok = emu.call([&] { return openNoContentOnCore(err); });
    host.pinSnapshot();
    if (!ok) {
        toast("Could not boot: " + err, 5.0, 3);
        return false;
    }
    paused = false;
    syncEmuFlags();
    pacer_.reset();
    lastTick_ = now();
    invalidateThumbs();
    discordNoteGameStarted(*this);
    const CoreHost::StagedBios& sb = host.stagedBios(bootTarget_ == VJ_BOOT_CART_BOOT_ROM ? bios::CartBoot : bios::Cd);
    toast(std::string(bootTarget_ == VJ_BOOT_CART_BOOT_ROM ? "Booted the cartridge boot ROM with an empty slot"
                                                           : "Booted the CD BIOS with no disc") +
          (sb.seen ? ": " + bios::describeStaged(sb.source, sb.where, sb.crc) : std::string()));
    return true;
}

bool App::openNoContentOnCore(std::string& err) {
    host.setDirectories(paths.system, paths.saves);
    applyBios();
    frames.clear();
    if (!host.loadNoContent(err)) {
        ach.onContentClosed();
        return false;
    }
    audio.open(host.sampleRate());
    applyAudioSettings();
    audio.clear();
    ach.onContentClosed();   // no game, so nothing to look up
    cheatsOnClose(*this);
    input.setContent("");
    applyPortDevices(*this);
    host.publishSnapshot();
    return true;
}
```

In `bootBios`, replace `if (host.inFrame()) {` with `if (haltedNow()) {` and `if (host.isLoaded()) host.flushSaveRam();` with `if (host.isLoaded()) emu.call([this] { host.flushSaveRam(); });`. Replace `closeContent` and `reset` with

```cpp
void App::closeContent() {
    if (!host.isLoaded()) return;
    if (haltedNow()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return;
    }
    pendingStateSlot_ = kNoPendingSlot;
    resumeOffer.active = false;
    autoSave();
    emu.call([this] {
        host.unloadContent();
        ach.onContentClosed();
        cheatsOnClose(*this);
        audio.clear();
        input.setContent("");
        frames.clear();
        host.publishSnapshot();
    });
    host.pinSnapshot();
    debug.requestRefresh();
    invalidateThumbs();
}

void App::reset() {
    if (!host.isLoaded()) return;
    if (haltedNow()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return;
    }
    // A call, not a post: retro_reset can re-stage the CD BIOS, which the BIOS panel reads.
    emu.call([this] { host.reset(); });
    debug.requestRefresh();
    ach.onReset();
    toast("Reset");
}
```

In `togglePause`, after `audio.setPaused(paused);` add `syncEmuFlags();`. In `autoSave`, replace `runSeconds_` with `runSeconds()`. In `offerResume`, case `Load`, after `pendingStateFrames_ = 2;` add `pendingStateAt_ = emu.framesRun() + 2;`. In `openAndLoadState`, after the final `pendingStateFrames_ = 2;` add `pendingStateAt_ = emu.framesRun() + 2;`. In `handleDrop`, replace `if (host.isLoaded()) host.flushSaveRam();` with `if (host.isLoaded()) emu.call([this] { host.flushSaveRam(); });`. In `stepEmulation`, replace `runSeconds_ += 1.0 / std::max(1.0, host.fps());   // emulated time of this game (paused time does not count)` with `runFrames_.fetch_add(1, std::memory_order_relaxed);   // emulated time of this game (paused time does not count)`.

In `app/src/ui.cpp`, replace `host.isLoaded() && !host.inFrame()` with `host.isLoaded() && !app.haltedNow()` on the "Close game" line and on the `bool loaded = ...` line of the Emulation menu.

- [ ] **Step 7: Save and load states.** Replace `saveState` and `loadState` with

```cpp
bool App::saveState(int slot, bool quiet) {
    if (!host.isLoaded()) {
        if (!quiet) toast("No game loaded");
        return false;
    }
    if (slot == lib::kAutoSlot) SDL_CreateDirectory(lib::autoStatesDir(paths.states, paths.instance).c_str());
    const std::string path = statePath(slot);
    std::string err;
    Frame thumb;
    // On the emulation thread: the state and the frame it was taken on.  Right after a load that frame is empty, so
    // there is no thumbnail -- never the previous game's picture.
    const bool ok = emu.call([&] {
        if (!host.saveStateToFile(path, err)) return false;   // written atomically (temp file + rename)
        thumb = host.frame();
        ach.onStateSaved(path);
        return true;
    });
    if (!ok) {
        if (!quiet) toast("Save state failed: " + err, 4.0, 3);
        return false;
    }
    if (thumb.width && thumb.height) {
        SDL_Surface* s = SDL_CreateSurfaceFrom((int)thumb.width, (int)thumb.height, SDL_PIXELFORMAT_XRGB8888,
                                               (void*)thumb.pixels.data(), (int)thumb.width * 4);
        if (s) {
            // the screenshot is replaced atomically too: the library tab may be reading it
            const std::string dst = thumbPath(slot), tmp = dst + "." + std::to_string(processId()) + ".tmp";
            if (SDL_SavePNG(s, tmp.c_str())) {
                if (!SDL_RenamePath(tmp.c_str(), dst.c_str())) SDL_RemovePath(tmp.c_str());
            }
            SDL_DestroySurface(s);
        }
    }
    auto it = thumbs_.find(slot);
    if (it != thumbs_.end()) {
        VideoOut::destroyTexture(it->second.tex);
        thumbs_.erase(it);
    }
    if (!quiet) toast("State saved to " + lib::stateLabel(slot));
    return true;
}

bool App::loadState(int slot) {
    if (!host.isLoaded()) {
        toast("No game loaded");
        return false;
    }
    if (!ach.allowStateLoad()) {
        toast("Hardcore mode: loading save states is disabled", 3.5, 2);
        return false;
    }
    if (!slotExists(slot)) {
        toast(lib::stateLabel(slot) + " is empty");
        return false;
    }
    const std::string path = statePath(slot);
    std::string err;
    const bool ok = emu.call([&] {
        if (!host.loadStateFromFile(path, err)) return false;
        audio.clear();
        cheatsAfterStateLoad(*this);
        ach.onStateLoaded(path);
        return true;
    });
    if (!ok) {
        toast("Load state failed: " + err, 4.0, 3);
        return false;
    }
    debug.requestRefresh();
    toast("State loaded from " + lib::stateLabel(slot));
    return true;
}
```

Replace `frameHash` with

```cpp
uint32_t App::frameHash(const Frame& f) const {
    uint32_t h = 2166136261u;  // FNV-1a, same as test/harness/harness.c
    const unsigned char* p = (const unsigned char*)f.pixels.data();
    size_t n = (size_t)f.width * f.height * 4;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}
```

- [ ] **Step 8: Shutdown and the loop's last flush.** In `shutdown`, replace `if (host.isLoaded()) host.unloadContent();` with

```cpp
    // Unload (it flushes SRAM) where the core lives, then stop the thread.  A core stuck inside retro_run (it should
    // not happen in phase 1: the GDB stub is locked off) must not hang the quit: give up after 3 s and exit.
    if (host.isLoaded() && !emu.callFor(std::chrono::milliseconds(3000), [this] { host.unloadContent(); })) {
        std::fprintf(stderr, "[emu] the emulation thread did not answer within 3 s (the core is blocked); exiting without unloading\n");
        std::fflush(stderr);
        std::_Exit(0);
    }
    stopEmuThread();
```

In `runInteractive`, add `syncEmuFlags();` on its own line right before `stepEmulation();` (the inline loop's `onAudio` reads the mirrored mute / fast-forward), and replace `if (host.isLoaded()) host.flushSaveRam(true);` after the loop with `if (host.isLoaded()) emu.call([this] { host.flushSaveRam(true); });`.

- [ ] **Step 9: The headless threaded run.** In `runHeadless`, after `applyAudioSettings();` (the second line) add

```cpp
    syncEmuFlags();
    if (emuThreadThisRun_) startEmuThread();   // --emu-thread on: the same run through the emulation thread
```

Replace the frame loop (from `for (unsigned i = 0; i < a.frames; i++) {` through its closing `}`) with

```cpp
        if (threaded()) {
            retroTicks_.store(0);
            for (unsigned i = 0; i < a.frames;) {
                const unsigned n = std::min(16u, a.frames - i);   // the dummy device drains in real time: clear every 16
                emu.advance(n);
                i += n;
                emu.call([this] { audio.clear(); });
                for (const OsdMessage& m : host.takeMessages()) std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
            }
            hlRun = retroTicks_.exchange(0);
            host.pinSnapshot();
        } else {
            for (unsigned i = 0; i < a.frames; i++) {
                ach.headlessFrame(i);
                audio.update();
                uint64_t hr0 = SDL_GetPerformanceCounter();
                host.runFrame();
                hlRun += SDL_GetPerformanceCounter() - hr0;
                ach.afterFrame();
                ach.pump();
                if ((i & 15) == 15) audio.clear();   // the dummy device drains in real time
                for (const OsdMessage& m : host.takeMessages())
                    std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
            }
        }
```

In the `TIMING` block replace `int nr = netplayHeadlessCheck(*this, a);` with `int nr = threaded() ? 0 : netplayHeadlessCheck(*this, a);`. Replace both `if (!cheatsHeadlessCheck(*this, a)) rc = 1;` lines with `if (!emu.call([&] { return cheatsHeadlessCheck(*this, a); })) rc = 1;`. Replace `const Frame& f = host.frame();` with `const Frame& f = shownFrame();` and `r.hash = frameHash();` with `r.hash = frameHash(f);`. In the `stateRoundtrip` block replace both `for (int i = 0; i < 60; i++) { audio.update(); host.runFrame(); }` with `runHeadlessFrames(60);` and both `frameHash()` calls with `frameHash(shownFrame())`. Replace the final `if (rc == 0) std::printf("headless: OK (%zu load(s))\n", results_.size());` with

```cpp
    if (rc == 0) std::printf("headless: OK (%zu load(s)%s)\n", results_.size(), threaded() ? ", emulation thread" : "");
```

- [ ] **Step 10: Run the threaded smoke tests and the whole suite**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "_threaded$" --output-on-failure`
Expected: all five pass. `smoke_yarc_threaded` prints `HASH f4c30ea3`; `smoke_state_threaded` prints `STATE roundtrip X X` with equal hashes.

Run: `ctest --test-dir app/build -C Release -E shaders_gl --output-on-failure -j4`
Expected: everything passes, including every inline smoke, `ra_*`, `cheat_*`, `netplay_*` and `debug_*` test (inline behaviour is unchanged).

- [ ] **Step 11: Commit**

```bash
git add app/src/app.h app/src/app.cpp app/src/ui.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): content and state actions through EmuThread::call; headless --emu-thread on (#967)

Load, boot BIOS, unload, reset, save and load state, SRAM flushes and the
shutdown unload run where the core lives; inline they run in place as before.
The save-state thumbnail is the frame the state was taken on.  UI checks ask
"halted?" instead of CoreHost::inFrame().  The headless smoke tests pass with
--emu-thread on with the inline hashes.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 12: Phase-1 feature limits and the threaded cheat batch

**Files:**
- Modify: `app/src/debug/debug_session.h:78-81` and private members at `:164`, `app/src/debug/debug_session.cpp:62-66`, `app/tests/debug_session_test.cpp` (new test + `main`), `app/src/debug/op_ui.cpp:483` (the Object list builds its context, including `host.inFrame()`, every UI frame while paused, even with its window closed), `app/src/cheats.h:105-106`, `app/src/cheats.cpp:653-675`, `app/src/cheats_core.cpp:73-77`, `app/tests/cheats_test.cpp` (new test + `main`), `app/src/netplay_ui.cpp` (`drawNetplayMenu` `:325`, `drawNetplay` `:339`, `netplayInit` `:411-415`), `app/src/app.h` (private), `app/src/app.cpp` (`run` after `wireHost();`, `haltedFrame` `:387-391`, `runInteractive`'s `openDebugWindow` line), `app/cmake/VJEmuThreadTests.cmake` (append)

**Interfaces:**
- Consumes: `App::emuThreadThisRun()`, `EmuThread::threaded()/onEmuThread()/post()`.
- Produces: `void DebugSession::setThreadBlocked(bool)`, `bool DebugSession::threadBlocked() const`; `std::function<void()> CheatManager::doneSink` (called once at the end of every `apply()`); `void App::limitFeaturesForEmuThread()`.

- [ ] **Step 1: Write the failing tests.** In `app/tests/debug_session_test.cpp`, before `int main()`, add

```cpp
static void testThreadBlocked() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.enabled());
    s.setThreadBlocked(true);   // #967 phase 1: the emulation thread is on
    CHECK(!s.enabled() && !s.attach());
    CHECK(s.disabledReason().find("Emulation thread") != std::string::npos);
    s.setThreadBlocked(false);
    CHECK(s.enabled());
}
```

and call `testThreadBlocked();` in `main()` after `testAvailability();`. In `app/tests/cheats_test.cpp`, before `int main(`, add

```cpp
// #967: the threaded app collects the sinks' calls and posts them as one command when apply() ends.
static void testDoneSink() {
    CheatManager m;
    Recorder r;
    r.attach(m);
    int done = 0;
    size_t logAtDone = 0;
    m.doneSink = [&] {
        done++;
        logAtDone = r.log.size();
    };
    m.apply();
    CHECK(done == 1 && logAtDone == r.log.size() && !r.log.empty() && r.log[0] == "reset");
    m.blocked = [] { return true; };
    r.log.clear();
    done = 0;
    m.apply();
    CHECK(done == 1 && r.log.size() == 1 && r.log[0] == "reset");   // blocked: the reset still goes, and the batch ends
}
```

and call `testDoneSink();` in `main()` after `testPersistence();`. Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   *_threaded limits     RetroAchievements, netplay, the debugger and the GDB stub are off while threaded` to the header list:

```cmake
add_test(NAME emu_thread_limits
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_limits PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "RetroAchievements, netplay and the debugger tools are off while the emulation thread is on")
add_test(NAME netplay_refused_threaded
    COMMAND virtualjaguar --headless --emu-thread on --netplay host --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(netplay_refused_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "netplay: not available while the emulation thread is on")
add_test(NAME gdb_locked_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 --option virtualjaguar_gdb_stub=enabled
            "${VJ_ROMS}/yarc.j64")
set_tests_properties(gdb_locked_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "managed by the app.*rejected by the core")
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -R "debug_session_unit|cheats_unit|emu_thread_limits|netplay_refused_threaded|gdb_locked_threaded" --output-on-failure`
Expected: `vj_debug_session_test` and `vj_cheats_test` fail to compile (`setThreadBlocked`, `doneSink`). The three CLI tests fail: no limits line, netplay starts, and the gdb option is accepted.

- [ ] **Step 3: DebugSession.** In `app/src/debug/debug_session.h`, replace

```cpp
    bool enabled() const { return iface_ != nullptr && loaded_ && !hardcore_; }
```

with

```cpp
    bool enabled() const { return iface_ != nullptr && loaded_ && !hardcore_ && !threadBlocked_; }
    // The emulation runs on its own thread (#967 phase 1): until the halt pump and the tool windows move there
    // (phase 3) the tools are off.  Turning it on detaches and closes the windows, like hardcore does.
    void setThreadBlocked(bool on);
    bool threadBlocked() const { return threadBlocked_; }
```

and after `bool hardcore_ = false;` add `bool threadBlocked_ = false;`. In `app/src/debug/debug_session.cpp`, after `setHardcore` add

```cpp
void DebugSession::setThreadBlocked(bool on) {
    if (on == threadBlocked_) return;
    threadBlocked_ = on;
    if (on) {
        detach();
        closeAllTools("thread");
    }
    serial_++;
}
```

and make the first line of `disabledReason()`:

```cpp
    if (threadBlocked_) return "Not available while Settings > Interface > Emulation thread is on (this version)";
```

- [ ] **Step 4: The cheat batch.** In `app/src/cheats.h`, after the `setSink` declaration add

```cpp
    // Called once at the end of every apply(), after the reset and the sets.  The threaded app (#967) collects the
    // sinks' calls on the UI thread and posts them here as one command, so the core's list never changes mid-frame.
    std::function<void()> doneSink;
```

In `app/src/cheats.cpp`, `CheatManager::apply`, replace `if (blockedNow()) return;   // hardcore: the core's list stays empty` with

```cpp
    if (blockedNow()) {   // hardcore: the core's list stays empty
        if (doneSink) doneSink();
        return;
    }
```

and right after the user-cheat loop (before `if (appliedSegments_ > kCoreLimit && warn)`) add `if (doneSink) doneSink();`.

In `app/src/cheats_core.cpp`, add `#include <string>` and `#include <vector>` with the includes. In the anonymous namespace add

```cpp
// A cheat-list change made on the UI thread while the emulation thread runs (#967): collected here, posted whole.
struct CheatOp {
    bool reset = false;
    unsigned index = 0;
    bool enabled = false;
    std::string code;
};
std::vector<CheatOp> g_batch;
```

and replace the two sink assignments with

```cpp
    // Inline, or on the emulation thread (inside a load call): straight into the core.  From the UI thread while
    // threaded: batched, and posted as one command by doneSink, so a frame never sees half a cheat list.
    cm.resetSink = [&app] {
        if (app.emu.threaded() && !app.emu.onEmuThread()) {
            g_batch.clear();
            g_batch.push_back(CheatOp{true, 0, false, std::string()});
            return;
        }
        retro_cheat_reset();
    };
    cm.setSink = [&app](unsigned index, bool enabled, const char* code) {
        if (app.emu.threaded() && !app.emu.onEmuThread()) {
            g_batch.push_back(CheatOp{false, index, enabled, code ? code : ""});
            return;
        }
        retro_cheat_set(index, enabled, code);
    };
    cm.doneSink = [&app] {
        if (g_batch.empty()) return;
        std::vector<CheatOp> ops;
        ops.swap(g_batch);
        app.emu.post([ops] {
            for (const CheatOp& o : ops) {
                if (o.reset) retro_cheat_reset();
                else retro_cheat_set(o.index, o.enabled, o.code.c_str());
            }
        });
    };
```

- [ ] **Step 5: Netplay off while threaded.** In `app/src/netplay_ui.cpp`, at the top of `netplayInit`, before `app.host.onLog = ...`, add

```cpp
    if (app.emuThreadThisRun()) {
        // #967 phase 1: the core's link events and [NETLINK] log lines arrive on the emulation thread and Netplay is not
        // thread-safe yet, so netplay is off for this run: no taps, no window, and the command line says so.
        app.netplay.setSystemDir(app.paths.system);
        if (!a.netplay.role.empty()) {
            std::fprintf(stderr, "netplay: not available while the emulation thread is on (this version; use --emu-thread off)\n");
            return false;
        }
        return true;
    }
```

At the top of `drawNetplayMenu`, after `if (!ImGui::BeginMenu("Netplay")) return;`, add

```cpp
    if (app.emuThreadThisRun()) {
        ImGui::MenuItem("Netplay window...", nullptr, false, false);
        ImGui::TextDisabled("Not available while Settings > Interface > Emulation thread is on");
        ImGui::EndMenu();
        return;
    }
```

At the top of `drawNetplay`, before `initUi(app);`, add

```cpp
    if (app.emuThreadThisRun()) {   // #967 phase 1: netplay is off for this run
        app.showNetplay = false;
        return;
    }
```

- [ ] **Step 6: The rest of the limits in App.** In `app/src/app.h`, private section, add `void limitFeaturesForEmuThread();`. In `app/src/app.cpp`, in `run`, replace

```cpp
    wireHost();
    ach.init(*this, a);
```

with

```cpp
    wireHost();
    if (emuThreadThisRun_) limitFeaturesForEmuThread();
    else ach.init(*this, a);   // with the emulation thread on, RetroAchievements waits for #967 phase 2
```

and after `App::syncDebug` add

```cpp
// #967 phase 1 runs the emulation on its own thread before these features are thread-safe: they are switched off
// for the run rather than racing the core (docs/app-emulation-thread-plan-phase1.md, "Phase 1 boundary").
//   * RetroAchievements: ach.init() is skipped, so available() is false and the menu is hidden.
//   * Netplay: netplayInit / the Netplay menu (netplay_ui.cpp).
//   * The Tools windows: they read core memory from the UI thread, and a halt would run the UI on the emulation thread.
//   * The GDB stub: a socket halt would park the emulation thread with every call() waiting behind it.
void App::limitFeaturesForEmuThread() {
    debug.setThreadBlocked(true);
    host.announceDebug = false;   // the core never hands over its debug interface, so it never halts for the app
    host.optionLock = [](const std::string& key, std::string& value) {
        if (key == "virtualjaguar_gdb_stub") {
            value = "disabled";
            return true;
        }
        return lockedCoreOption(key, value);
    };
    std::fprintf(stderr, "[emu] RetroAchievements, netplay and the debugger tools are off while the emulation thread is on\n");
}
```

In `haltedFrame`, replace `if (!imguiUp_ || !window_) {` with

```cpp
    if (!imguiUp_ || !window_ || emu.onEmuThread()) {   // never run the UI on the emulation thread (#967 phase 3 moves the pump)
```

In `runInteractive`, replace `else if (openDebugWindow(debug, a.showWindow)) {}` with `else if (!emuThreadThisRun_ && openDebugWindow(debug, a.showWindow)) {}`.

In `app/src/debug/op_ui.cpp`, `drawObjectsTool` evaluates its "step one frame" condition every UI frame while paused, window open or not, and that condition calls `host.inFrame()` (an owner violation when threaded). Replace

```cpp
    if (app.paused && !app.debug.attached() && !app.debug.halted() && !app.host.inFrame() && app.host.isLoaded() &&
```

with

```cpp
    if (!app.threaded() && app.paused && !app.debug.attached() && !app.debug.halted() && !app.host.inFrame() &&
        app.host.isLoaded() &&
```

- [ ] **Step 7: Run them, then the whole suite**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -R "debug_session_unit|cheats_unit|emu_thread_limits|netplay_refused_threaded|gdb_locked_threaded|_threaded$" --output-on-failure`
Expected: all pass.

Run: `ctest --test-dir app/build -C Release -E shaders_gl --output-on-failure -j4`
Expected: everything passes.

- [ ] **Step 8: Commit**

```bash
git add app/src/debug/debug_session.h app/src/debug/debug_session.cpp app/src/debug/op_ui.cpp app/tests/debug_session_test.cpp \
        app/src/cheats.h app/src/cheats.cpp app/src/cheats_core.cpp app/tests/cheats_test.cpp app/src/netplay_ui.cpp \
        app/src/app.h app/src/app.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): switch off what is not thread-safe yet while the emulation thread is on (#967)

RetroAchievements, netplay, the Tools windows and the GDB stub option are off
for a threaded run (phases 2 and 3 bring them over).  Cheat changes from the
Cheats window reach the core as one command, never half a list.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 13: The threaded interactive loop

The windowed loop cannot run in CI (no display) or on the maintainer's Mac (no windows there). Its test is a windowed check script that runs on the Mac Studio only.

**Files:**
- Create: `app/tests/emu_thread_windowed_check.sh`
- Modify: `app/src/app.h` (private: `threadedFrame()`), `app/src/app.cpp` (`runInteractive`: thread start after `audio.open(48043.6);`, the perf note, the step/upload lines, the 5-second SRAM flush, the sleep block, `perf.curSync`; new `threadedFrame`)

**Interfaces:**
- Consumes: everything from Tasks 1-12.
- Produces: `void App::threadedFrame()`; stderr line `[emu] emulation thread on` for interactive runs too.

- [ ] **Step 1: Write the windowed check** `app/tests/emu_thread_windowed_check.sh`

```bash
#!/usr/bin/env bash
# Windowed check of the emulation thread (#967).  It OPENS A WINDOW: run it on the Mac Studio (vjr.sh run), never on
# a machine someone is using.  Interactive runs with --emu-thread on and off: each must measure 600 UI frames, show a
# new frame in nearly every one, log no owner violation, and (on) really start the thread.  Then a paused threaded run
# (the UI draws every tool provider while the emulation is parked) and a screenshot with the Settings window open (the
# option table read every frame) must log no owner violation either.
#
# usage: VJ_WINDOWED_OK=1 emu_thread_windowed_check.sh <virtualjaguar> <rom> <out-dir>
set -uo pipefail
[ "${VJ_WINDOWED_OK:-}" = 1 ] || { echo "refusing: this opens a window; set VJ_WINDOWED_OK=1 on the measurement machine"; exit 2; }
exe="$1"; rom="$2"; out="$3"
mkdir -p "$out"
fail=0
for mode in on off; do
  log="$out/windowed-$mode.log"
  csv="$out/windowed-$mode.csv"
  VJ_PERF_CSV="$csv" "$exe" --emu-thread "$mode" --perf-frames 600 --perf-input --show-window settings-interface "$rom" \
    >"$log" 2>&1 || { echo "FAIL $mode: exit status $?"; fail=1; }
  if grep -q "owner violation" "$log"; then echo "FAIL $mode: owner violation"; grep "owner violation" "$log"; fail=1; fi
  if [ "$mode" = on ] && ! grep -q "\[emu\] emulation thread on" "$log"; then
    echo "FAIL on: the emulation thread did not start"; fail=1
  fi
  rows=$(( $(wc -l < "$csv" 2>/dev/null || echo 1) - 1 ))
  [ "$rows" -ge 590 ] || { echo "FAIL $mode: $rows CSV rows, expected 600"; fail=1; }
  awk -F, 'NR > 1 && $2 > 0 { n++ } END { exit !(n > 500) }' "$csv" 2>/dev/null \
    || { echo "FAIL $mode: fewer than 500 of 600 UI frames showed a new emulated frame"; fail=1; }
  grep -E "^PERF|\[emu\]" "$log"
done
# Paused (at UI frame 30): the UI keeps drawing while the emulation thread is parked; nothing may reach into the core.
"$exe" --emu-thread on --perf-frames 300 --perf-pause --show-window settings "$rom" >"$out/windowed-paused.log" 2>&1 \
  || { echo "FAIL paused run: exit status $?"; fail=1; }
if grep -q "owner violation" "$out/windowed-paused.log"; then echo "FAIL paused run: owner violation"; fail=1; fi
"$exe" --emu-thread on --show-window settings --screenshot "$out/windowed-on.png" --screenshot-frame 240 "$rom" \
  >"$out/windowed-shot.log" 2>&1 || { echo "FAIL screenshot run: exit status $?"; fail=1; }
[ -s "$out/windowed-on.png" ] || { echo "FAIL: no screenshot written"; fail=1; }
if grep -q "owner violation" "$out/windowed-shot.log"; then echo "FAIL screenshot run: owner violation"; fail=1; fi
[ "$fail" = 0 ] && echo "emu_thread_windowed_check: OK"
exit "$fail"
```

Make it executable: `chmod +x app/tests/emu_thread_windowed_check.sh`.

- [ ] **Step 2: Run it on the Mac Studio and watch it fail.** Commit the script first (`git add app/tests/emu_thread_windowed_check.sh && git commit -m "test(app): windowed emulation-thread check for the Mac Studio (#967)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`), then:

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p1 HEAD
bash $VJR run emu-p1 'cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build app/build -j14 >/dev/null && VJ_WINDOWED_OK=1 bash app/tests/emu_thread_windowed_check.sh app/build/virtualjaguar test/roms/yarc.j64 ~/vj-remote/out/emu-p1-t13'
```

Expected: `FAIL on: the emulation thread did not start` (interactive runs never start it yet). The `off` half passes.

- [ ] **Step 3: Start the thread in interactive runs.** In `runInteractive`, right after `audio.open(48043.6);`, add

```cpp
    if (emuThreadThisRun_) startEmuThread();   // after the window and the audio device (design section 7)
```

and add `perf.note += std::string(", emulation thread ") + (threaded() ? "on" : "off");` after the `perf.note += ", chain runs " ...` statement.

- [ ] **Step 4: The UI side of an iteration.** In `app/src/app.h`, private section, add `void threadedFrame();   // UI thread, threaded: input out, frames / counters / messages in`. In `app/src/app.cpp`, after `App::syncEmuFlags`, add

```cpp
// Threaded mode, once per UI iteration (#967): hand the newest input over and adopt what the emulation thread
// published.  Never waits for the emulation.
void App::threadedFrame() {
    input.poll(!inputCaptured_, inputSnap.takenSince());   // on this thread: SDL_GetMouseState is main-thread only
    inputSnap.publish(input.coreInput(), pendingEventNs_);
    pendingEventNs_ = 0;
    host.pinSnapshot();
    const uint64_t fr = emu.framesRun();
    for (uint64_t k = emuFramesSeen_; k < fr; k++) perf.emulated();
    fpsWindowFrames_ += fr - emuFramesSeen_;
    emuFramesSeen_ = fr;
    const uint64_t rt = retroTicks_.exchange(0, std::memory_order_relaxed);
    if (rt) perf.add(Perf::Retro, 1000.0 * (double)rt / (double)perfFreq_);
    if (pendingStateSlot_ != kNoPendingSlot && fr >= pendingStateAt_) {   // openAndLoadState / resume: after 2 frames
        const int slot = pendingStateSlot_;
        pendingStateSlot_ = kNoPendingSlot;
        loadState(slot);
    }
    for (const OsdMessage& m : host.takeMessages()) toast(m.text, m.durationMs / 1000.0, m.level);
}
```

In the loop, replace

```cpp
        syncEmuFlags();
        stepEmulation();
        perf.lap(Perf::Step);
        video.setSourceFps((float)host.fps());
        video.upload(host.frame());
```

with

```cpp
        syncEmuFlags();
        if (threaded()) threadedFrame();
        else stepEmulation();
        perf.lap(Perf::Step);
        video.setSourceFps((float)host.fps());
        if (!threaded()) video.upload(host.frame());
        else if (const FrameExchange::Slot* s = frames.takeNewest()) video.upload(s->frame);   // nothing new: redraw
```

In the 5-second SRAM block, replace `host.flushSaveRam();` with

```cpp
            if (threaded()) emu.post([this] { host.flushSaveRam(); });   // on the emulation thread, never waited for
            else host.flushSaveRam();
```

Replace

```cpp
        syncSwapInterval();
        if (!pacer_.swapSync) {
            // No swap-interval pacing: sleep until the next emulated frame is due -- never when the core is behind.
            double period = 1.0 / std::max(1.0, host.fps());
            double wait = (host.isLoaded() && !paused) ? pacer_.sleepFor(t - lastTick_, period) : 0.008;
            if (wait > 0) SDL_DelayNS((Uint64)(wait * 1e9));
        }
        perf.lap(Perf::Sleep);
        perf.curSync = pacer_.swapSync;
```

with

```cpp
        if (threaded()) {
            // The emulation keeps its own time on its own thread; this loop only presents.  Vsync on: the swap paces
            // it.  Off: a short nap, so a new frame still shows within a few ms without spinning a core.
            if (!vsync) SDL_DelayNS(4000000);
        } else {
            syncSwapInterval();
            if (!pacer_.swapSync) {
                // No swap-interval pacing: sleep until the next emulated frame is due -- never when the core is behind.
                double period = 1.0 / std::max(1.0, host.fps());
                double wait = (host.isLoaded() && !paused) ? pacer_.sleepFor(t - lastTick_, period) : 0.008;
                if (wait > 0) SDL_DelayNS((Uint64)(wait * 1e9));
            }
        }
        perf.lap(Perf::Sleep);
        perf.curSync = threaded() ? vsync : pacer_.swapSync;
```

- [ ] **Step 5: Build here (headless tests only) and run the windowed check on the Studio**

Run locally: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -E shaders_gl --output-on-failure -j4`
Expected: everything passes.

Commit (Step 6), then on the Studio:

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p1 HEAD
bash $VJR run emu-p1 'cmake --build app/build -j14 >/dev/null && VJ_WINDOWED_OK=1 bash app/tests/emu_thread_windowed_check.sh app/build/virtualjaguar test/roms/yarc.j64 ~/vj-remote/out/emu-p1-t13'
```

Expected: `emu_thread_windowed_check: OK`, `[emu] emulation thread on` in the `on` log, no `owner violation`, and PERF lines for both modes. Pull the screenshot with `bash $VJR pull emu-p1` and look at `~/Workspace/Provenance/vj-eval/remote/emu-p1/windowed-on.png`: the game picture under an open Settings window.

- [ ] **Step 6: Commit**

```bash
git add app/src/app.h app/src/app.cpp
git commit -F - <<'EOF'
feat(app): the interactive loop with the emulation on its own thread (#967)

The UI thread polls and publishes input, takes the newest frame, drains
messages and counters and presents; it never waits for the emulation.  The
5 s SRAM flush is posted.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 14: Event-to-present latency column

The late-latch target (Task 15) is event to photon, but Task 10 only measures event to core poll. This task adds the second half: from the input event to the swap that first shows a frame that polled it. With vsync on, that swap returns at the vblank, so its return time is the photon proxy in both loops. It also moves `--perf-input`'s synthetic key to an SDL timer with a swept delay. Pushed from the loop (Task 10), the key always lands just after a swap: the single-thread loop then polls it at once, while the threaded loop waits longest, which would bias the comparison.

**Files:**
- Modify: `app/src/frame_exchange.h` (`Slot`), `app/src/input_snapshot.h` (`publish`, `take`), `app/tests/input_snapshot_test.cpp`, `app/src/perf.h` (Task 10's fields, `frameBegin`, `report`), `app/tests/perf_csv_test.cpp`, `app/src/app.h` (includes; private, after Task 10's latency members), `app/src/app.cpp` (anonymous namespace; `wireHost`'s `inputPoll`; `emuRunOneFrame`; `threadedFrame`; the `runInteractive` loop: the frame take, the swap and the `--perf-input` injection; `perfSample`; new functions after `threadedFrame`), `app/tests/emu_thread_windowed_check.sh`, `docs/agent/testing.md`

**Interfaces:**
- Consumes: `InputSnapshot` (Task 5); `Perf::curLat`, `App::noteInputLatency`, `App::perfSample`, `--perf-input` (Task 10); `App::emuRunOneFrame`, `wireHost` (Task 11); `App::threadedFrame` (Task 13).
- Produces: `FrameExchange::Slot::inputSeq`; `uint64_t InputSnapshot::publish(const CoreInput&, uint64_t eventNs)` (returns the publish's sequence number, from 1); `uint64_t InputSnapshot::take(CoreInput& out, uint64_t* seq = nullptr)` (`*seq` = the newest publish taken, 0 = none yet); `Perf::presPer`, `Perf::curPres`; CSV header `iter_ms,emu_frames,swap_sync,underruns,input_lat_ms,present_lat_ms`; `void App::publishInput()`; `void App::notePresented(uint64_t swapEndNs)`; members `presentEventNs_`, `shownSeq_`, `presentQueue_`, `presSumNs_`, `presCount_`, `emuInputSeq_`, `perfInputK_`.

- [ ] **Step 1: Write the failing tests.** In `app/tests/input_snapshot_test.cpp`, after `testTakenSince`, add

```cpp
void testSequence() {
    InputSnapshot s;
    CoreInput out;
    uint64_t seq = 99;
    s.take(out, &seq);
    CHECK(seq == 0, "nothing published yet: sequence 0 (%llu)", (unsigned long long)seq);
    const uint64_t a = s.publish(released(), 5);
    const uint64_t b = s.publish(released(), 0);
    CHECK(a == 1 && b == 2, "publishes are numbered from 1 (%llu, %llu)", (unsigned long long)a, (unsigned long long)b);
    s.take(out, &seq);
    CHECK(seq == 2, "a take reports the newest publish it handed out (%llu)", (unsigned long long)seq);
    s.take(out, &seq);
    CHECK(seq == 2, "and again with nothing new (%llu)", (unsigned long long)seq);
}
```

and call `testSequence();` in `main` after `testTakenSince();`.

In `app/tests/perf_csv_test.cpp`, in the loop, after `p.curLat = i == 1 ? 4.5f : -1.0f;` add `p.curPres = i == 1 ? 21.25f : -1.0f;`, and replace the four header and row checks with

```cpp
        CHECK(lines[0] == "iter_ms,emu_frames,swap_sync,underruns,input_lat_ms,present_lat_ms", "header: %s",
              lines[0].c_str());
        CHECK(lines[1].find(",1,1,0,-1.000,-1.000") != std::string::npos, "row 1, one frame and no input: %s",
              lines[1].c_str());
        CHECK(lines[2].find(",0,4.500,21.250") != std::string::npos, "row 2, 4.5 ms to the poll, 21.25 to the swap: %s",
              lines[2].c_str());
        CHECK(lines[3].find(",1,-1.000,-1.000") != std::string::npos, "row 3, one underrun: %s", lines[3].c_str());
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build app/build -j8 --target vj_input_snapshot_test vj_perf_csv_test`
Expected: compile errors: `take` does not accept two arguments, `publish` returns `void`, and `vj::Perf` has no member `curPres`.

- [ ] **Step 3: Number the publishes.** In `app/src/input_snapshot.h`, replace `publish` and `take` with

```cpp
    // UI thread.  `eventNs`: SDL timestamp (SDL_GetTicksNS base) of the oldest input event this state includes that
    // was not published before, 0 = none.  Returns this publish's sequence number (from 1), which the frame that
    // polls it carries to the UI (FrameExchange::Slot::inputSeq) for the event -> present measurement.
    uint64_t publish(const CoreInput& in, uint64_t eventNs) {
        std::lock_guard<std::mutex> l(mu_);
        CoreInput next = in;
        next.carryMotion(latest_);   // motion the emulation has not taken yet is kept, not replaced
        latest_ = next;
        if (eventNs && !eventNs_) eventNs_ = eventNs;
        return ++seq_;
    }

    // Emulation thread (the core's input-poll callback).  Returns the oldest untaken event's timestamp (0 = none),
    // for the input-latency measurement; `seq` (optional) gets the newest publish handed out (0 = none yet).
    uint64_t take(CoreInput& out, uint64_t* seq = nullptr) {
        std::lock_guard<std::mutex> l(mu_);
        out = latest_;
        latest_.clearMotion();
        taken_++;
        if (seq) *seq = seq_;
        const uint64_t ns = eventNs_;
        eventNs_ = 0;
        return ns;
    }
```

and add `uint64_t seq_ = 0;   // publishes so far` after `unsigned taken_ = 0;`.

In `app/src/frame_exchange.h`, in `Slot`, after `uint64_t epoch = 0;` add

```cpp
        uint64_t inputSeq = 0;        // the InputSnapshot publish this frame's poll took (event -> present, VJ_PERF_CSV)
```

- [ ] **Step 4: Perf.** In `app/src/perf.h`, after Task 10's `float curLat = -1.0f;` add

```cpp
    std::vector<float> presPer;             // input event -> the swap that showed its frame, ms; -1 = none
    float curPres = -1.0f;                  // set by the loop for the current iteration (App::perfSample)
```

In `frameBegin`, after `latPer.push_back(curLat);` add `presPer.push_back(curPres);`, and after `curLat = -1.0f;` add `curPres = -1.0f;`.

In `report`, in Task 10's block, replace

```cpp
            if (ln)
                std::printf("PERF   input latency (event -> core poll): avg %.3f ms  max %.3f ms  (%zu samples)\n",
                            lsum / (double)ln, lmax, ln);
```

with

```cpp
            if (ln)
                std::printf("PERF   input latency (event -> core poll): avg %.3f ms  max %.3f ms  (%zu samples)\n",
                            lsum / (double)ln, lmax, ln);
            double psum = 0, pmax = 0;
            size_t pn = 0;
            for (float l : presPer)
                if (l >= 0) {
                    psum += l;
                    pmax = std::max(pmax, (double)l);
                    pn++;
                }
            if (pn)
                std::printf("PERF   input latency (event -> present): avg %.3f ms  max %.3f ms  (%zu samples)\n",
                            psum / (double)pn, pmax, pn);
```

and replace the CSV header and row lines with

```cpp
                std::fprintf(f, "iter_ms,emu_frames,swap_sync,underruns,input_lat_ms,present_lat_ms\n");
                for (size_t i = 0; i < n && i < emuPer.size(); i++)
                    std::fprintf(f, "%.3f,%d,%d,%u,%.3f,%.3f\n", frameMs[i], (int)emuPer[i], (int)syncPer[i],
                                 i < underPer.size() ? (unsigned)underPer[i] : 0u,
                                 i < latPer.size() ? (double)latPer[i] : -1.0,
                                 i < presPer.size() ? (double)presPer[i] : -1.0);
```

Extend the comment above it: `..., input latency ms or -1, event -> present ms or -1) ...`.

- [ ] **Step 5: Run the unit tests**

Run: `cmake --build app/build -j8 --target vj_input_snapshot_test vj_perf_csv_test && ctest --test-dir app/build -C Release -R "input_snapshot_unit|perf_csv_unit" --output-on-failure`
Expected: `input_snapshot_test: OK` and `perf_csv_test: OK`.

- [ ] **Step 6: Feed the column from the app.** In `app/src/app.h`, add `#include <deque>` and `#include <utility>` with the other standard includes, and in the private section, after Task 10's `void perfSample();`, add

```cpp
    // VJ_PERF_CSV event -> present (#967: the late latch's measurement).  The swap's return stands in for the photon.
    uint64_t presentEventNs_ = 0;   // inline: the oldest input event the frames of this iteration polled
    uint64_t shownSeq_ = 0;         // threaded: the InputSnapshot publish the frame on screen polled
    std::deque<std::pair<uint64_t, uint64_t>> presentQueue_;   // threaded: (publish seq, oldest event ns), oldest first
    uint64_t presSumNs_ = 0, presCount_ = 0;
    uint64_t emuInputSeq_ = 0;      // emulation thread: the publish the last core poll took
    unsigned perfInputK_ = 0;       // --perf-input: the next injected key's delay step
    void publishInput();
    void notePresented(uint64_t swapEndNs);
```

In `app/src/app.cpp`, in `wireHost`, replace the `host.inputPoll` assignment (Task 11's) with

```cpp
    host.inputPoll = [this]() {
        if (host.threaded()) {   // the emulation thread: the newest input the UI thread published (input_snapshot.h)
            uint64_t seq = 0;
            const uint64_t ns = inputSnap.take(emuInput_, &seq);
            emuInputSeq_ = seq;   // stamped on the next frame this thread publishes
            if (ns) noteInputLatency(ns);
            return;
        }
        if (pendingEventNs_) {   // VJ_PERF_CSV: from the oldest unhandled input event to this poll
            noteInputLatency(pendingEventNs_);
            if (!presentEventNs_) presentEventNs_ = pendingEventNs_;   // and to this iteration's swap
            pendingEventNs_ = 0;
        }
        input.poll(!inputCaptured_);
    };
```

In `emuRunOneFrame`, after `s.aspect = host.aspect();` add `s.inputSeq = emuInputSeq_;`.

After `App::threadedFrame`, add

```cpp
// Threaded: hand the input the UI thread sees now to the emulation thread, and remember which publish carried an input
// event, so the swap that first shows a frame which polled it can time event -> present (VJ_PERF_CSV).
void App::publishInput() {
    input.poll(!inputCaptured_, inputSnap.takenSince());   // on this thread: SDL_GetMouseState is main-thread only
    const uint64_t seq = inputSnap.publish(input.coreInput(), pendingEventNs_);
    if (pendingEventNs_ && perf.on) {
        if (presentQueue_.size() >= 64) presentQueue_.pop_front();
        presentQueue_.push_back({seq, pendingEventNs_});
    }
    pendingEventNs_ = 0;
}

// Right after the swap (VJ_PERF_CSV): time the input events whose frame this swap shows.  With vsync the swap returns
// at the vblank in both loops, so its return is the same photon proxy for both.
void App::notePresented(uint64_t swapEndNs) {
    if (!perf.on) return;
    auto note = [&](uint64_t eventNs) {
        if (!eventNs || swapEndNs <= eventNs || swapEndNs - eventNs > 250000000ull) return;   // as noteInputLatency
        presSumNs_ += swapEndNs - eventNs;
        presCount_++;
    };
    if (!threaded()) {
        note(presentEventNs_);
        presentEventNs_ = 0;
        return;
    }
    while (!presentQueue_.empty() && presentQueue_.front().first <= shownSeq_) {
        note(presentQueue_.front().second);
        presentQueue_.pop_front();
    }
}
```

In `threadedFrame`, replace its first three lines (`input.poll(...)`, `inputSnap.publish(...)`, `pendingEventNs_ = 0;`) with `publishInput();`.

In `perfSample`, at the end, add

```cpp
    perf.curPres = presCount_ ? (float)((double)presSumNs_ / (double)presCount_ / 1e6) : -1.0f;
    presSumNs_ = presCount_ = 0;
```

In the `runInteractive` loop, replace Task 13's

```cpp
        else if (const FrameExchange::Slot* s = frames.takeNewest()) video.upload(s->frame);   // nothing new: redraw
```

with

```cpp
        else if (const FrameExchange::Slot* s = frames.takeNewest()) {   // nothing new: redraw
            video.upload(s->frame);
            shownSeq_ = s->inputSeq;
        }
```

and replace `SDL_GL_SwapWindow(window_);` with

```cpp
        SDL_GL_SwapWindow(window_);
        notePresented(SDL_GetTicksNS());
```

In the anonymous namespace, after `isInputEvent`, add

```cpp
// --perf-input: a synthetic key (F24: bound to nothing), pushed from an SDL timer thread so the event lands at a swept
// point of the display period.  Pushed from the loop it would always arrive just after a swap, where the single-thread
// loop polls at once and the threaded loop waits longest, biasing the comparison (#967).
Uint64 SDLCALL perfInputKey(void* window, SDL_TimerID, Uint64) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.windowID = (SDL_WindowID)(uintptr_t)window;
    ev.key.scancode = SDL_SCANCODE_F24;
    ev.key.down = true;
    ev.key.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&ev);
    ev.type = SDL_EVENT_KEY_UP;
    ev.key.down = false;
    SDL_PushEvent(&ev);
    return 0;   // once
}
```

and in the loop replace Task 10's whole `if (a.perfInput && perf.on && perf.seen % 30 == 29) { ... }` block with

```cpp
        if (a.perfInput && perf.on && perf.seen % 30 == 29)   // delays 0.5 .. 16.7 ms in 37 steps: every phase at 60 Hz
            SDL_AddTimerNS(500000ull + (uint64_t)(perfInputK_++ % 37) * 450000ull, perfInputKey,
                           (void*)(uintptr_t)SDL_GetWindowID(window_));
```

In `app/tests/emu_thread_windowed_check.sh`, after the `fewer than 500 of 600 UI frames` check, add

```bash
  awk -F, 'NR > 1 && $6 >= 0 { n++ } END { exit !(n >= 15) }' "$csv" 2>/dev/null \
    || { echo "FAIL $mode: fewer than 15 event -> present samples (--perf-input injects one every 30 frames)"; fail=1; }
```

In `docs/agent/testing.md`, in Task 10's text, replace `` (`iter_ms,emu_frames,swap_sync,underruns,input_lat_ms`; `` with `` (`iter_ms,emu_frames,swap_sync,underruns,input_lat_ms,present_lat_ms`; present = the swap that first shows a frame which polled the event; ``.

- [ ] **Step 7: Build, run the headless suite, then the windowed check on the Studio**

Run: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -E shaders_gl --output-on-failure -j4`
Expected: everything passes.

Commit (Step 8), then:

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p1 HEAD
bash $VJR run emu-p1 'cmake --build app/build -j14 >/dev/null && VJ_WINDOWED_OK=1 bash app/tests/emu_thread_windowed_check.sh app/build/virtualjaguar test/roms/yarc.j64 ~/vj-remote/out/emu-p1-t14'
```

Expected: `emu_thread_windowed_check: OK`, and both PERF blocks print an `input latency (event -> present)` line.

- [ ] **Step 8: Commit**

```bash
git add app/src/frame_exchange.h app/src/input_snapshot.h app/tests/input_snapshot_test.cpp app/src/perf.h \
        app/tests/perf_csv_test.cpp app/src/app.h app/src/app.cpp app/tests/emu_thread_windowed_check.sh \
        docs/agent/testing.md
git commit -F - <<'EOF'
feat(app): VJ_PERF_CSV event-to-present column (#967)

The swap that first shows a frame which polled an input event, timed from
the event: inline from the iteration's oldest polled event, threaded through
the InputSnapshot publish number each frame carries.  --perf-input now
injects its key from an SDL timer at a swept delay, so events land evenly
over the display period in both loops.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 15: The late latch

Decided on 2026-10-16 (see "Decided: input latency in mode A"). After the swap, the threaded loop waits until shortly before the next vblank, minus the measured UI build time. While it waits, it hands every input event to the emulation thread the moment the event arrives. Then it takes the newest frame, builds the UI and swaps. The fake-clock model in this task's test puts event-to-present at 25.3 ms for 5 ms core frames (single-thread loop: 25.0 ms; threaded without the latch: 46.4 ms). For 14 ms core frames it gives 34.2 ms against 25.0, because mode A cannot hide a heavy frame; mode B (Phase 4) is the fix for that.

**Files:**
- Create: `app/src/late_latch.h`, `app/tests/late_latch_test.cpp`
- Modify: `app/src/app.h` (includes; `CliArgs` after `perfInput`; private, after Task 14's members), `app/src/app.cpp` (`runInteractive`: after `startEmuThread`, the perf note, the swap, the threaded sleep block; new functions after `notePresented`), `app/src/main.cpp` (usage text after the `--emu-thread` line; parsing after the `--perf-input` parsing), `app/cmake/VJEmuThreadTests.cmake` (append), `app/tests/emu_thread_windowed_check.sh`

**Interfaces:**
- Consumes: `App::publishInput`, `App::notePresented` (Task 14); `App::threadedFrame`, the threaded sleep block (Task 13); `App::handleEvent`.
- Produces: `class vj::LateLatch { void setPeriodNs(uint64_t); uint64_t periodNs() const; void noteBuild(uint64_t); void onSwap(uint64_t beginNs, uint64_t endNs); uint64_t wakeAt(uint64_t nowNs); bool locked() const; uint64_t padNs() const; unsigned misses() const; uint64_t budgetNs() const; }`; `CliArgs::lateLatch` (-1/0/1, `--late-latch on|off`); `App::latchWait()`, `App::latchUpdatePeriod()`; members `latch_`, `lateLatchOn_`, `latchedAtNs_`, `latchChecks_`, `latchLogged_`; stderr lines `[emu] late latch locked: ...` and `[emu] late latch released: ...`.

- [ ] **Step 1: Write the failing test** `app/tests/late_latch_test.cpp`

```cpp
// LateLatch (late_latch.h) against a fake clock: no thread, no sleeping, no display.  The first cases drive the vblank
// tracker directly; the last one replays the whole pipeline (display, UI loop, mode-A emulation grid, input events) with
// the real LateLatch deciding the waits, and compares event -> present with the single-thread loop (#967, plan section
// "Decided: input latency in mode A").
#include "late_latch.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

using vj::LateLatch;

constexpr uint64_t kMs = 1000000;
constexpr uint64_t kP60 = 16666667;   // a 60 Hz display
constexpr uint64_t kV0 = 1000000000;  // the first vblank

uint32_t g_rng = 12345;
double unit() {   // deterministic uniform [0, 1)
    g_rng = g_rng * 1664525u + 1013904223u;
    return (double)(g_rng >> 8) / 16777216.0;
}

// Swaps that block until a vblank of an exact grid, 2 ms builds.  Returns the latch after `n` of them.
void runGrid(LateLatch& l, uint64_t period, unsigned n, uint64_t& lastEnd) {
    for (unsigned k = 1; k <= n; k++) {
        const uint64_t v = kV0 + k * period;
        l.noteBuild(2 * kMs);
        l.onSwap(v - 3 * kMs, v + 50000);
        lastEnd = v + 50000;
    }
}

void testLocks() {
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 10, end);
    CHECK(!l.locked(), "not locked after 10 swaps");
    CHECK(l.wakeAt(end + 200000) == end + 200000, "no wait before the lock");
    runGrid(l, kP60, 40, end);
    CHECK(l.locked(), "locked after 40 blocking swaps on an exact grid");
    const uint64_t now = end + 200000;
    const uint64_t nextV = end - 50000 + kP60;
    const uint64_t w = l.wakeAt(now);
    CHECK(l.budgetNs() == 3 * kMs, "budget = the slowest build (2 ms) + the 1 ms pad: %llu", (unsigned long long)l.budgetNs());
    const int64_t err = (int64_t)w - (int64_t)(nextV - 3 * kMs);
    CHECK(err > -200000 && err < 200000, "wakes 3 ms before the next vblank (off by %lld ns)", (long long)err);
}

void testNonBlocking() {
    LateLatch l;
    l.setPeriodNs(kP60);
    for (unsigned k = 1; k <= 200; k++) {
        const uint64_t t = kV0 + k * kP60 + 2 * kMs;
        l.noteBuild(2 * kMs);
        l.onSwap(t, t + 20000);   // returns at once: vsync off, or a queued swap
    }
    CHECK(!l.locked(), "swaps that never block never lock");
    CHECK(l.wakeAt(kV0 + 300 * kP60) == kV0 + 300 * kP60, "and never wait");
}

void testJitter() {
    LateLatch l;
    l.setPeriodNs(kP60);
    for (unsigned k = 1; k <= 600; k++) {
        const uint64_t v = kV0 + k * kP60 + (uint64_t)(unit() * 6 * kMs);   // returns anywhere in a 6 ms window
        l.noteBuild(2 * kMs);
        l.onSwap(v - 3 * kMs, v);
    }
    CHECK(!l.locked(), "swap returns spread over 6 ms do not lock");
}

void testUnknownPeriod() {
    LateLatch l;
    uint64_t end = 0;
    runGrid(l, kP60, 100, end);
    CHECK(!l.locked() && l.wakeAt(end) == end, "no display period: off");
}

void testNoRoom() {
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 60, end);
    l.noteBuild(16 * kMs);   // one build nearly a whole period long
    CHECK(l.wakeAt(end + 200000) == end + 200000, "a 16 ms build leaves no room to wait");
}

void testMissGrowsPad() {
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 60, end);
    const uint64_t w = l.wakeAt(end + 200000);
    const uint64_t target = end - 50000 + kP60;
    CHECK(w < target, "waits");
    l.noteBuild(4 * kMs);                                   // the build ran long...
    l.onSwap(w + 4 * kMs, target + kP60 + 50000);          // ...and the swap landed one vblank late
    CHECK(l.misses() == 1, "a late swap counts as a miss (%u)", l.misses());
    CHECK(l.padNs() == LateLatch::kPadMinNs + LateLatch::kPadUpNs, "the pad grows by 0.5 ms");
    CHECK(l.locked(), "a single miss keeps the lock");
    uint64_t v = target + kP60;
    for (unsigned k = 0; k < LateLatch::kCalmRun * 20; k++) {   // calm again: the pad comes back down to the floor
        const uint64_t ww = l.wakeAt(v + 250000);
        l.noteBuild(2 * kMs);
        v += kP60;
        l.onSwap(ww + 2 * kMs, v + 50000);
    }
    CHECK(l.misses() == 1, "no further misses (%u)", l.misses());
    CHECK(l.padNs() == LateLatch::kPadMinNs, "the pad shrinks back to 1 ms (%llu)", (unsigned long long)l.padNs());
}

void testOffNominal() {
    // A display that really runs at 59.94 Hz while the mode says 60: the tracker follows the drift.
    LateLatch l;
    l.setPeriodNs(kP60);
    const uint64_t real = 16683350;
    uint64_t v = kV0, worst = 0;
    for (unsigned k = 0; k < 10000; k++) {
        const uint64_t w = l.wakeAt(v + 250000);
        if (l.locked()) {
            const int64_t err = (int64_t)(v + real) - (int64_t)l.budgetNs() - (int64_t)w;
            const uint64_t a = (uint64_t)(err < 0 ? -err : err);
            if (k > 100 && a > worst) worst = a;
        }
        l.noteBuild(2 * kMs);
        v += real;
        l.onSwap(v - 2 * kMs, v + 50000);
    }
    CHECK(l.locked(), "stays locked on a 59.94 Hz display announced as 60 Hz");
    CHECK(worst < 400000, "wake error at most 0.4 ms (%llu ns)", (unsigned long long)worst);
    CHECK(l.misses() == 0, "no misses (%u)", l.misses());
}

// ---- the pipeline model ---------------------------------------------------------------------------------------------

struct Iter {
    uint64_t now;    // the wait starts (after the swap and the end-of-iteration work)
    uint64_t take;   // the wait ends: pump the rest, publish, take the newest frame
    uint64_t present;
};

struct Result {
    double inlineMs, unlatchedMs, latchMs, latchPollMs;
    unsigned misses, iters;
};

// Display at 60 Hz, emulation on the mode-A grid at 60.05445 Hz (its phase sweeps the display's every ~18 s), core
// frames of `frameNs`, UI builds of 2 ms +- 0.3 ms, one input event per display period at a uniform time.
Result model(uint64_t frameNs) {
    const double E = 1e9 / 60.05445;
    const double T0 = (double)kV0 + 0.37 * (double)kP60;
    const uint64_t kEnd = 50000, kWork = 200000, kPump = 50000;
    const unsigned K = 22000;   // ~6 minutes: the drift sweeps every phase many times
    Result r = {};
    for (int latched = 0; latched < 2; latched++) {
        LateLatch l;
        l.setPeriodNs(kP60);
        g_rng = 777;
        std::vector<Iter> it;
        it.reserve(K);
        uint64_t m = 0;   // the vblank index the last swap returned at
        for (unsigned k = 0; k < K; k++) {
            const uint64_t now = kV0 + m * kP60 + kEnd + kWork;
            uint64_t w = latched ? l.wakeAt(now) : now;
            if (w < now) w = now;
            const uint64_t take = w + kPump;
            const uint64_t build = (uint64_t)(1.7e6 + unit() * 0.6e6);
            const uint64_t begin = take + build;
            const uint64_t n = (begin - kV0 + kP60 - 1) / kP60;   // the first vblank at or after the swap call
            l.noteBuild(begin - w);
            l.onSwap(begin, kV0 + n * kP60 + kEnd);
            it.push_back({now, take, kV0 + n * kP60});
            m = n;
        }
        if (latched) {
            r.misses = l.misses();
            r.iters = K;
        }
        // Events from 2 s in (after the lock) to 2 s before the end.
        double sum = 0, pollSum = 0, inl = 0;
        unsigned cnt = 0;
        size_t k = 0;
        g_rng = 4242;
        for (uint64_t e0 = kV0 + 120 * kP60; e0 + 120 * kP60 < it.back().present; e0 += kP60) {
            const uint64_t e = e0 + (uint64_t)(unit() * (double)kP60);
            while (k + 1 < it.size() && it[k + 1].now <= e) k++;
            // Published at once while the UI waits; otherwise when the next wait starts (latched) or the next
            // iteration starts (unlatched, where take == now + pump).
            const uint64_t pub = (latched && e >= it[k].now && e < it[k].take) ? e : it[k + 1].now;
            const double jf = (double)pub - T0 > 0 ? ((double)pub - T0) / E : 0;
            uint64_t j = (uint64_t)jf;
            if ((double)j < jf) j++;
            const uint64_t poll = (uint64_t)(T0 + (double)j * E);
            const uint64_t fin = poll + frameNs;
            size_t q = k;
            while (q < it.size() && it[q].take < fin) q++;
            if (q >= it.size()) break;
            sum += (double)(it[q].present - e);
            pollSum += (double)(poll - e);
            // The single-thread loop: polled when the iteration after the event starts (just after a vblank),
            // presented one vblank later (the frame and the UI fit in a period).
            const uint64_t vi = (e - kV0 + kP60 - 1) / kP60;
            inl += (double)(kV0 + (vi + 1) * kP60 - e);
            cnt++;
        }
        if (latched) {
            r.latchMs = sum / cnt / 1e6;
            r.latchPollMs = pollSum / cnt / 1e6;
            r.inlineMs = inl / cnt / 1e6;
        } else {
            r.unlatchedMs = sum / cnt / 1e6;
        }
    }
    return r;
}

void testModel() {
    const Result a = model(5 * kMs);
    std::printf("model, 5 ms frames:  inline %.1f ms, threaded %.1f ms, threaded + late latch %.1f ms (poll %.1f ms), "
                "misses %u of %u\n",
                a.inlineMs, a.unlatchedMs, a.latchMs, a.latchPollMs, a.misses, a.iters);
    CHECK(a.latchMs <= a.inlineMs + 2.0, "5 ms frames: the late latch is within 2 ms of inline (%.2f vs %.2f)", a.latchMs,
          a.inlineMs);
    CHECK(a.latchMs + 15.0 < a.unlatchedMs, "and far below the unlatched loop (%.2f vs %.2f)", a.latchMs, a.unlatchedMs);
    CHECK(a.misses * 100 <= a.iters, "at most 1%% missed vblanks (%u of %u)", a.misses, a.iters);
    const Result b = model(14 * kMs);
    std::printf("model, 14 ms frames: inline %.1f ms, threaded %.1f ms, threaded + late latch %.1f ms (poll %.1f ms), "
                "misses %u of %u\n",
                b.inlineMs, b.unlatchedMs, b.latchMs, b.latchPollMs, b.misses, b.iters);
    // Mode A cannot hide a heavy frame: event -> present is about poll + frame + half a period + the budget, against
    // inline's poll + one period.  Mode B (Phase 4) is the fix.  Pinned so a change to the model shows up here.
    CHECK(b.latchMs + 15.0 < b.unlatchedMs, "14 ms frames: still far below the unlatched loop (%.2f vs %.2f)", b.latchMs,
          b.unlatchedMs);
    CHECK(b.misses * 100 <= b.iters, "at most 1%% missed vblanks (%u of %u)", b.misses, b.iters);
}

}  // namespace

int main() {
    testLocks();
    testNonBlocking();
    testJitter();
    testUnknownPeriod();
    testNoRoom();
    testMissGrowsPad();
    testOffNominal();
    testModel();
    std::printf(g_fail ? "late_latch_test: %d FAILED\n" : "late_latch_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Register it.** Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   late_latch_unit       LateLatch: vblank tracking, budget, misses, and the event -> present model against a fake clock` to the header list:

```cmake
add_executable(vj_late_latch_test tests/late_latch_test.cpp)
target_include_directories(vj_late_latch_test PRIVATE src)
vj_emu_thread_warnings(vj_late_latch_test)
add_test(NAME late_latch_unit COMMAND vj_late_latch_test)
set_tests_properties(late_latch_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build -j8 --target vj_late_latch_test`
Expected: compile error `late_latch.h: No such file or directory`.

- [ ] **Step 4: Implement `app/src/late_latch.h`**

```cpp
// The late latch (#967): how long the UI thread may wait after a swap so the frame it builds next still makes the
// next vblank.  Threaded mode with vsync only.  App::latchWait sleeps until wakeAt(), handing every input event to the
// emulation thread the moment it arrives; then the iteration takes the newest finished frame, builds the UI and swaps.
// That moves the frame take from just after a vblank to just before the next one, which brings event -> present back
// to the single-thread loop's ~25 ms at 60 Hz for a light core frame (plan: "Decided: input latency in mode A").
//
// Pure arithmetic on the times it is handed (SDL_GetTicksNS), so tests/late_latch_test.cpp drives it with a fake clock.
// It can only shorten the wait: whenever it is unsure where the vblanks are, wakeAt() returns `now`, which is the
// unlatched loop of Task 13.
#pragma once

#include <algorithm>
#include <cstdint>

namespace vj {

class LateLatch {
public:
    static constexpr uint64_t kBlockedNs = 250000;      // a swap that took this long waited for a vblank
    static constexpr uint64_t kTrackNs = 1500000;       // a vblank observation within this of the grid is tracked
    static constexpr uint64_t kJitterMaxNs = 1000000;   // locked only while the smoothed |residual| stays under this
    static constexpr unsigned kLockAfter = 30;          // consecutive tracked observations before the first wait
    static constexpr unsigned kUnlockAfter = 4;         // consecutive outliers (or non-blocking swaps x 8) to let go
    static constexpr uint64_t kPadMinNs = 1000000, kPadMaxNs = 4000000;
    static constexpr uint64_t kPadUpNs = 500000, kPadDownNs = 100000;
    static constexpr unsigned kCalmRun = 120;           // on-time swaps before the pad shrinks one step
    static constexpr uint64_t kMinRoomNs = 1000000;     // a budget within this of a whole period: no wait
    static constexpr unsigned kBuilds = 60;             // build times remembered (one second at 60 Hz)

    // The display's refresh period (SDL_DisplayMode), 0 = unknown: the latch stays off.
    void setPeriodNs(uint64_t ns) {
        if (ns == periodNs_) return;
        periodNs_ = ns;
        havePhase_ = locked_ = false;
        tracked_ = outliers_ = nonBlocking_ = 0;
        jitterNs_ = 0;
        targetNs_ = 0;
    }
    uint64_t periodNs() const { return periodNs_; }

    // The UI's own work in one iteration: from the end of the wait to the start of the swap.
    void noteBuild(uint64_t ns) {
        builds_[buildAt_ % kBuilds] = ns;
        buildAt_++;
    }

    // Right after SDL_GL_SwapWindow: when the call began and when it returned.  A swap that blocked returned at a
    // vblank; one that did not says nothing about where the vblanks are.
    void onSwap(uint64_t beginNs, uint64_t endNs) {
        if (!periodNs_) return;
        const int64_t P = (int64_t)periodNs_;
        if (targetNs_) {   // this iteration waited: did its swap make the vblank it aimed at?
            if ((int64_t)(endNs - targetNs_) > P / 2) {
                misses_++;
                padNs_ = std::min(kPadMaxNs, padNs_ + kPadUpNs);
                calm_ = 0;
            } else if (++calm_ >= kCalmRun) {
                padNs_ = std::max(kPadMinNs, padNs_ - kPadDownNs);
                calm_ = 0;
            }
            targetNs_ = 0;
        }
        if (endNs - beginNs < kBlockedNs) {   // vsync off, or a driver that queues the swap: nothing learned
            if (++nonBlocking_ >= kUnlockAfter * 8) unlock();
            return;
        }
        nonBlocking_ = 0;
        if (!havePhase_) {
            phaseNs_ = endNs;
            havePhase_ = true;
            return;
        }
        int64_t r = (int64_t)(endNs - phaseNs_) % P;   // signed distance to the nearest grid point
        if (r < 0) r += P;
        if (r > P / 2) r -= P;
        const uint64_t ar = (uint64_t)(r < 0 ? -r : r);
        jitterNs_ = (jitterNs_ * 7 + ar) / 8;
        if (ar < kTrackNs) {
            phaseNs_ = endNs - (uint64_t)(r - r / 4);   // move a quarter of the way toward the observation
            outliers_ = 0;
            if (++tracked_ >= kLockAfter && jitterNs_ < kJitterMaxNs) locked_ = true;
        } else if (++outliers_ >= kUnlockAfter) {   // the grid moved (a new display mode, a compositor hiccup)
            unlock();
            phaseNs_ = endNs;
        }
        if (jitterNs_ >= kJitterMaxNs) locked_ = false;
    }

    // When to stop waiting, given the time now (after the swap and the end-of-iteration work).  <= now: no wait.
    uint64_t wakeAt(uint64_t nowNs) {
        targetNs_ = 0;
        if (!locked_ || !periodNs_) return nowNs;
        const uint64_t b = budgetNs();
        if (b + kMinRoomNs >= periodNs_) return nowNs;   // the UI needs nearly a whole period: nothing to gain
        const int64_t d = (int64_t)(nowNs + b - phaseNs_);
        const uint64_t v = d <= 0 ? phaseNs_ : phaseNs_ + ((uint64_t)d + periodNs_ - 1) / periodNs_ * periodNs_;
        targetNs_ = v;
        return v - b;
    }

    bool locked() const { return locked_; }
    uint64_t padNs() const { return padNs_; }
    unsigned misses() const { return misses_; }
    uint64_t budgetNs() const {   // the slowest recent build, plus the pad
        uint64_t mx = 0;
        for (unsigned i = 0; i < std::min(buildAt_, kBuilds); i++) mx = std::max(mx, builds_[i]);
        return mx + padNs_;
    }

private:
    void unlock() {
        locked_ = false;
        tracked_ = outliers_ = nonBlocking_ = 0;
    }

    uint64_t periodNs_ = 0;
    uint64_t phaseNs_ = 0;   // a vblank time
    bool havePhase_ = false;
    bool locked_ = false;
    unsigned tracked_ = 0, outliers_ = 0, nonBlocking_ = 0;
    uint64_t jitterNs_ = 0;
    uint64_t padNs_ = kPadMinNs;
    unsigned calm_ = 0;
    unsigned misses_ = 0;
    uint64_t targetNs_ = 0;   // the vblank the current wait aims at (0 = no wait this iteration)
    uint64_t builds_[kBuilds] = {};
    unsigned buildAt_ = 0;
};

}  // namespace vj
```

- [ ] **Step 5: Run the test**

Run: `cmake --build app/build -j8 --target vj_late_latch_test && ctest --test-dir app/build -C Release -R late_latch_unit --output-on-failure -V | grep -E "model|late_latch_test"`
Expected: `late_latch_test: OK` and the two model lines, close to

```text
model, 5 ms frames:  inline 25.0 ms, threaded 46.4 ms, threaded + late latch 25.3 ms (poll 8.7 ms), misses 0 of 22000
model, 14 ms frames: inline 25.0 ms, threaded 55.3 ms, threaded + late latch 34.2 ms (poll 8.7 ms), misses 0 of 22000
```

- [ ] **Step 6: Wire it into the loop.** In `app/src/app.h`, add `#include "late_latch.h"` with the app's own includes. In `CliArgs`, after `perfInput`, add

```cpp
    int lateLatch = -1;             // dev: --late-latch on|off, threaded + vsync only (default on; #967 measurement)
```

In the private section, after Task 14's `void notePresented(uint64_t swapEndNs);`, add

```cpp
    // The late latch (#967, late_latch.h): threaded + vsync, wait until shortly before the next vblank, handing input
    // to the emulation thread as it arrives, then take the newest frame.
    LateLatch latch_;
    bool lateLatchOn_ = true;
    uint64_t latchedAtNs_ = 0;   // when this iteration's wait ended: its UI work starts there
    unsigned latchChecks_ = 0;
    bool latchLogged_ = false;
    void latchWait();
    void latchUpdatePeriod();
```

In `app/src/app.cpp`, in `runInteractive`, after Task 13's `if (emuThreadThisRun_) startEmuThread();` add

```cpp
    lateLatchOn_ = a.lateLatch != 0;
```

and after Task 13's `perf.note += std::string(", emulation thread ") + ...;` add

```cpp
        perf.note += std::string(", late latch ") +
                     (threaded() && vsync && lateLatchOn_ ? (latch_.locked() ? "locked" : "not locked") : "off");
```

Replace Task 14's

```cpp
        SDL_GL_SwapWindow(window_);
        notePresented(SDL_GetTicksNS());
```

with

```cpp
        const uint64_t swapBeginNs = SDL_GetTicksNS();
        SDL_GL_SwapWindow(window_);
        const uint64_t swapEndNs = SDL_GetTicksNS();
        notePresented(swapEndNs);
        if (threaded()) {
            if (latchedAtNs_) latch_.noteBuild(swapBeginNs - latchedAtNs_);
            latch_.onSwap(swapBeginNs, swapEndNs);
        }
```

Replace Task 13's threaded sleep block

```cpp
        if (threaded()) {
            // The emulation keeps its own time on its own thread; this loop only presents.  Vsync on: the swap paces
            // it.  Off: a short nap, so a new frame still shows within a few ms without spinning a core.
            if (!vsync) SDL_DelayNS(4000000);
        } else {
```

with

```cpp
        if (threaded()) {
            // The emulation keeps its own time on its own thread; this loop only presents.  Vsync on: the late latch
            // waits until just before the next vblank, publishing input as it comes, and the swap paces the rest.
            // Off: a short nap, so a new frame still shows within a few ms without spinning a core.
            if (vsync && lateLatchOn_) latchWait();
            else if (!vsync) SDL_DelayNS(4000000);
            latchedAtNs_ = SDL_GetTicksNS();
        } else {
```

After `App::notePresented`, add

```cpp
// Threaded + vsync (#967, the late latch): wait until shortly before the next vblank and hand every input event to the
// emulation thread the moment it arrives, so the next emulated frame polls it wherever in the period that frame starts.
// The iteration then takes the newest finished frame, builds the UI and swaps just in time.  While the latch does not
// know where the vblanks are (late_latch.h), it returns at once: the unlatched loop.
void App::latchWait() {
    if (latchChecks_++ % 60 == 0) latchUpdatePeriod();   // the window may have moved to another display
    const uint64_t wake = latch_.wakeAt(SDL_GetTicksNS());
    if (latch_.locked() != latchLogged_) {
        latchLogged_ = latch_.locked();
        std::fprintf(stderr, "[emu] late latch %s: period %.3f ms, budget %.2f ms\n", latchLogged_ ? "locked" : "released",
                     (double)latch_.periodNs() / 1e6, (double)latch_.budgetNs() / 1e6);
    }
    for (;;) {
        const uint64_t t = SDL_GetTicksNS();
        if (t >= wake || quit) return;
        const uint64_t left = wake - t;
        if (left < 2000000) {   // SDL_WaitEventTimeout counts whole ms: the last stretch is a precise sleep
            SDL_DelayPrecise(left);
            return;
        }
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, (Sint32)((left - 1000000) / 1000000))) continue;
        do handleEvent(e, false);
        while (!quit && SDL_PollEvent(&e));
        publishInput();
    }
}

// The display's refresh period for the latch (0 = unknown: it stays off).
void App::latchUpdatePeriod() {
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    uint64_t ns = 0;
    if (m && m->refresh_rate_numerator > 0 && m->refresh_rate_denominator > 0)
        ns = (uint64_t)(1e9 * (double)m->refresh_rate_denominator / (double)m->refresh_rate_numerator);
    else if (m && m->refresh_rate > 0.0f)
        ns = (uint64_t)(1e9 / (double)m->refresh_rate);
    latch_.setPeriodNs(ns);
}
```

In `app/src/main.cpp`, after the `--emu-thread on|off` usage line, add

```cpp
        "  --late-latch on|off   dev: with the emulation thread and vsync, take each frame just before the vblank (default on)\n"
```

and after `else if (!std::strcmp(s, "--perf-input")) a.perfInput = true;` add

```cpp
        else if (!std::strcmp(s, "--late-latch")) {
            const std::string v = need(s);
            if (v != "on" && v != "off") {
                std::fprintf(stderr, "--late-latch wants on or off, got '%s'\n", v.c_str());
                return 2;
            }
            a.lateLatch = v == "on" ? 1 : 0;
        }
```

In `app/tests/emu_thread_windowed_check.sh`, after the `the emulation thread did not start` check, add

```bash
  if [ "$mode" = on ] && ! grep -q "\[emu\] late latch locked" "$log"; then
    echo "FAIL on: the late latch never locked (do swaps block at the vblank on this machine?)"; fail=1
  fi
```

- [ ] **Step 7: Build here (headless tests only), then the windowed check on the Studio**

Run locally: `cmake --build app/build -j8 && ctest --test-dir app/build -C Release -E shaders_gl --output-on-failure -j4`
Expected: everything passes.

Commit (Step 8), then:

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p1 HEAD
bash $VJR run emu-p1 'cmake --build app/build -j14 >/dev/null && VJ_WINDOWED_OK=1 bash app/tests/emu_thread_windowed_check.sh app/build/virtualjaguar test/roms/yarc.j64 ~/vj-remote/out/emu-p1-t15'
```

Expected: `emu_thread_windowed_check: OK`, with `[emu] late latch locked: period 16.667 ms, budget ...` in the `on` log. If the latch never locks there, the Studio's GL swaps do not block at the vblank, and the latch cannot work on that machine as designed. Do not tune the constants to force a lock: stop and report it with the swap timings. The PR must then say so (Task 18).

- [ ] **Step 8: Commit**

```bash
git add app/src/late_latch.h app/tests/late_latch_test.cpp app/src/app.h app/src/app.cpp app/src/main.cpp \
        app/cmake/VJEmuThreadTests.cmake app/tests/emu_thread_windowed_check.sh
git commit -F - <<'EOF'
feat(app): late latch for the threaded loop (#967)

With the emulation thread and vsync on, the UI thread waits until just
before the next vblank (the slowest recent UI build plus a pad that grows on
a missed vblank), publishing input as each event arrives, then takes the
newest frame.  It tracks the vblank grid from swaps that block and stays off
while it cannot.  The fake-clock model: event -> present 25.3 ms for 5 ms
core frames (inline 25.0, unlatched 46.4); 34.2 ms for 14 ms frames, which
only mode B can fix.  --late-latch off for A/B measurement.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 16: ThreadSanitizer: CMake option and CI job

**Files:**
- Create: `app/tests/tsan_canary.cpp`, `app/tests/tsan.supp`
- Modify: `app/CMakeLists.txt` (after the `set(VJ_APP_LIBRETRO_DB_DIR "" CACHE PATH ...)` statement, before SDL and the core are fetched), `app/cmake/VJEmuThreadTests.cmake` (append), `.github/workflows/app.yml` (new job after `app`)

**Interfaces:**
- Consumes: the `emu_thread` test label.
- Produces: CMake option `VJ_APP_TSAN`; ctest `tsan_canary` (TSan builds only); CI job `tsan`.

- [ ] **Step 1: Write the failing test.** `app/tests/tsan_canary.cpp`:

```cpp
// Built only with -DVJ_APP_TSAN=ON: a deliberate data race.  Registered WILL_FAIL, so the ThreadSanitizer job proves
// its sanitizer is really on -- a build without it exits 0 here and that fails the test.
#include <cstdio>
#include <thread>

static int g_shared = 0;

int main() {
    std::thread a([] {
        for (int i = 0; i < 100000; i++) g_shared++;
    });
    std::thread b([] {
        for (int i = 0; i < 100000; i++) g_shared++;
    });
    a.join();
    b.join();
    std::printf("tsan_canary: %d\n", g_shared);
    return 0;
}
```

`app/tests/tsan.supp`:

```text
# ThreadSanitizer suppressions for the emulation-thread tests (#967).  Empty on purpose: add an entry only for a race
# inside a third-party library (SDL, ...), with the report and the reason, never for the app's own code.
```

Append to `app/cmake/VJEmuThreadTests.cmake`, and add `#   tsan_canary           (TSan builds only) a deliberate race the sanitizer must catch` to the header list:

```cmake
if(VJ_APP_TSAN)
    add_executable(vj_tsan_canary tests/tsan_canary.cpp)
    target_link_libraries(vj_tsan_canary PRIVATE Threads::Threads)
    add_test(NAME tsan_canary COMMAND vj_tsan_canary)
    set_tests_properties(tsan_canary PROPERTIES WILL_FAIL TRUE LABELS emu_thread TIMEOUT 60)
endif()
```

- [ ] **Step 2: Run it and watch it fail** (on the Studio: a second full build with TSan is heavy)

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p1 HEAD
bash $VJR run emu-p1 'cmake -S app -B app/build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVJ_APP_TSAN=ON >/dev/null && cmake --build app/build-tsan -j14 --target vj_tsan_canary >/dev/null && ctest --test-dir app/build-tsan -R tsan_canary --output-on-failure'
```

Expected: `tsan_canary ... Failed`. CMake warns that `VJ_APP_TSAN` is unused, the canary is built without the sanitizer, and it exits 0.

- [ ] **Step 3: The option.** In `app/CMakeLists.txt`, after the `set(VJ_APP_LIBRETRO_DB_DIR "" CACHE PATH ...)` statement, add

```cmake
# ThreadSanitizer over the app, the core and the tests (#967; the `tsan` job in .github/workflows/app.yml).  It must
# come before any target (SDL, the core) is defined so everything is instrumented.  Clang or GCC only.
option(VJ_APP_TSAN "Build with ThreadSanitizer (-fsanitize=thread)" OFF)
if(VJ_APP_TSAN)
    if(MSVC)
        message(FATAL_ERROR "VJ_APP_TSAN needs Clang or GCC")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()
```

- [ ] **Step 4: Run the canary and the emulation-thread tests under TSan on the Studio**

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p1 HEAD
bash $VJR run emu-p1 'cmake -S app -B app/build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVJ_APP_TSAN=ON >/dev/null && cmake --build app/build-tsan -j14 >/dev/null && TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1 suppressions=$PWD/app/tests/tsan.supp" ctest --test-dir app/build-tsan -C RelWithDebInfo -L emu_thread --output-on-failure'
```

Expected: every `emu_thread` test passes, `tsan_canary` included (it is expected to fail, and does). If a test reports a race in the app's code, fix the race. Never suppress it.

- [ ] **Step 5: The CI job.** In `.github/workflows/app.yml`, after the `app` job (before the `# Installable packages` comment block), add

```yaml
  # ------------------------------------------------------------------------------------------
  # ThreadSanitizer over the emulation-thread tests (#967): FrameExchange, EmuCommands, EmuThread,
  # InputSnapshot, AudioOut, CoreHost's owner model, the real core inline vs threaded, and the
  # headless smoke tests with --emu-thread on.  Any race report fails the job; tsan_canary proves
  # the sanitizer is really on.
  # ------------------------------------------------------------------------------------------
  tsan:
    name: ThreadSanitizer (emulation thread)
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v7

    - name: Install Linux build dependencies (SDL3 needs the desktop dev packages)
      run: |
        sudo apt-get update -qq
        sudo apt-get install -y -qq cmake pkg-config clang \
          libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev \
          libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
          libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev \
          libdbus-1-dev libudev-dev libwayland-dev wayland-protocols libdecor-0-dev \
          libcurl4-openssl-dev

    # Newer runner kernels randomise mmap with more bits than TSan's shadow layout allows
    # ("FATAL: ThreadSanitizer: unexpected memory mapping"); 28 is what it supports.
    - name: Let TSan map its shadow memory
      run: sudo sysctl vm.mmap_rnd_bits=28

    - name: Configure (TSan, RelWithDebInfo, no LTO)
      run: >-
        cmake -S app -B app/build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVJ_APP_TSAN=ON -DVJ_APP_LTO=OFF
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

    - name: Build
      run: cmake --build app/build-tsan --config RelWithDebInfo -j4

    - name: Emulation-thread tests under ThreadSanitizer
      env:
        TSAN_OPTIONS: "halt_on_error=1 second_deadlock_stack=1 suppressions=${{ github.workspace }}/app/tests/tsan.supp"
      run: ctest --test-dir app/build-tsan -C RelWithDebInfo -L emu_thread --output-on-failure
```

Check the YAML parses: `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/app.yml')); print('ok')"`. Expected: `ok`.

- [ ] **Step 6: Commit**

```bash
git add app/CMakeLists.txt app/cmake/VJEmuThreadTests.cmake app/tests/tsan_canary.cpp app/tests/tsan.supp .github/workflows/app.yml
git commit -F - <<'EOF'
ci(app): ThreadSanitizer job over the emulation-thread tests (#967)

VJ_APP_TSAN instruments the app, the core, SDL and the tests; the job runs
ctest -L emu_thread and fails on any race report.  tsan_canary proves the
sanitizer is really on.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 17: Documentation

**Files:**
- Modify: `app/README.md` (a new `## Emulation thread` section before `## Running several instances` at `:486`; rows in the `## Layout` table at `:922-964`; one sentence in `## Smoke tests` at `:809`)

**Interfaces:** none.

- [ ] **Step 1: Write the section.** Before `## Running several instances`, add

```markdown
## Emulation thread

**Settings > Interface > Performance > Emulation thread** (`ui.emu_thread`) runs the emulation on its own thread
(#967; design in `docs/app-emulation-thread-design.md`). The window then only presents. A slow emulated frame or a
busy UI no longer stalls the picture. The game runs at the true Jaguar rate (60.05 Hz, PAL 50.08 Hz), paced by the
sound card's clock, and the display shows the newest frame (on a 60 Hz panel one frame is skipped about every 18 s).
The setting takes effect at the next start and is **off** by default (nightly builds will turn it on once nothing is
switched off while it runs). `--emu-thread on|off` decides for one run; headless runs stay single-threaded unless it
says `on`. With vsync, the window waits until just before each vblank, passing input to the emulation as it arrives,
and then shows the newest frame (the late latch, `src/late_latch.h`; `--late-latch off` turns it off for a
measurement).

While it is on, this version switches off RetroAchievements, netplay, the Tools (debugger) windows and the GDB stub
option. They move to the emulation thread in later phases. A log line `[core_host] owner violation: ...` means a UI
path still called into the core directly: that is a bug, please report it.
```

In `## Smoke tests`, append: ``The tests labelled `emu_thread` (`ctest -L emu_thread`) cover the emulation thread, including every `smoke_*_threaded` run with `--emu-thread on`; CI also runs them under ThreadSanitizer.``

In the `## Layout` table add these rows, in the table's format:

```markdown
| `src/emu_thread.*`, `src/emu_commands.h` | the emulation thread (#967): its loop, `post` / `call` / `advance`, the ordered command queue |
| `src/pacing_clock.h` | mode A pacing: the core's period trimmed by the audio queue (the sound card as master clock) |
| `src/frame_exchange.h`, `src/frame.h` | finished frames from the emulation thread to the UI (triple buffer, content epochs) |
| `src/input_snapshot.h` | the newest input from the UI thread to the core's poll (pointer motion exactly once) |
| `src/late_latch.h` | the late latch: when the threaded window loop stops waiting before a vblank (vblank tracking, UI build budget) |
```

- [ ] **Step 2: Check the links and the wording**

Run: `grep -n "Emulation thread\|emu_thread" app/README.md`
Expected: the new section, the Smoke tests sentence and the layout rows.

- [ ] **Step 3: Commit**

```bash
git add app/README.md
git commit -F - <<'EOF'
docs(app): the Emulation thread setting (#967)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 18: Measurement on the Mac Studio, written into the PR

Spec section 9: threaded vs single-thread, at 60 Hz and 120 Hz. Measure presentation evenness, input latency (event to poll and event to present), audio underruns and CPU, using `VJ_PERF_CSV` (#966). The latency question is decided (see "Decided: input latency in mode A"): the comparison that counts is threaded + late latch against single-thread, with threaded unlatched (`--late-latch off`) alongside as the reference. Windowed runs happen on the Studio only.

**Files:** none in the repository; the results go into the PR description.

**Interfaces:**
- Consumes: `--perf-frames`, `--perf-input`, `--emu-thread`, `--late-latch`, `VJ_PERF_CSV` (Tasks 9, 10, 13, 14, 15).
- Produces: the PR's measurement table.

- [ ] **Step 1: Ship the branch and check the machine**

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p1 HEAD
bash $VJR status
ssh -o BatchMode=yes studio-runner 'uptime; system_profiler SPDisplaysDataType | grep -iE "Resolution|Refresh|Display Type|UI Looks like"'
bash $VJR run emu-p1 'cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build app/build -j14 >/dev/null && find -L test/roms/private -iname "*cybermorph*" | head -3'
```

Expected: load average under 2 (if it is higher, wait: measurements under load lie), the display's refresh rate, and the Cybermorph path (the heavy title from the spec: 14 ms+ frames). Use `test/roms/yarc.j64` as the light title. If the display cannot do 120 Hz, record "120 Hz: not available on this display" in the table instead of guessing.

- [ ] **Step 2: Run A/B/C/C/B/A for each title at 60 Hz** (an interleaved order cancels host drift; never A,A,B,B). A = single-thread, B = threaded + late latch, C = threaded unlatched. The whole loop runs on the Studio in one command, and the Cybermorph path is found there:

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR run emu-p1 'HZ=60
cyb=$(find -L test/roms/private -iname "*cybermorph*" -type f | head -1)
[ -n "$cyb" ] || { echo "no Cybermorph ROM under test/roms/private"; exit 1; }
for run in 1-off 2-latch 3-nolatch 4-nolatch 5-latch 6-off; do
  case ${run#*-} in
    off) flags="--emu-thread off" ;;
    latch) flags="--emu-thread on --late-latch on" ;;
    nolatch) flags="--emu-thread on --late-latch off" ;;
  esac
  for rom in test/roms/yarc.j64 "$cyb"; do
    tag=$(basename "$rom" | tr -c "A-Za-z0-9\n" "_")
    echo "== $HZ Hz  $tag  run $run ($flags)"
    VJ_PERF_CSV=$HOME/vj-remote/out/emu-p1-${HZ}hz-$tag-$run.csv app/build/virtualjaguar $flags \
      --perf-frames 3600 --perf-input "$rom" 2>&1 | grep -E "^PERF|\[emu\]|owner violation"
  done
done'
bash $VJR pull emu-p1
```

Expected: each run prints its `PERF` block (`process CPU`, both `input latency` lines, `audio underruns`) and no `owner violation`. The threaded runs print `[emu] emulation thread on`; the `latch` runs also print `[emu] late latch locked: ...`, and their `PERF` note says `late latch locked`.

- [ ] **Step 3: Summarize each CSV**

```bash
for f in ~/Workspace/Provenance/vj-eval/remote/emu-p1/emu-p1-60hz-*.csv; do
  awk -F, -v f="$(basename "$f")" 'NR > 1 {
      n++; e[$2 > 2 ? 3 : $2]++; u += $4
      if ($5 >= 0) { l += $5; ln++; if ($5 > lm) lm = $5 }
      if ($6 >= 0) { p += $6; pn++; if ($6 > pm) pm = $6 }
      ms += $1; if ($1 > mx) mx = $1
    } END {
      printf "%-48s iters %d  repeats(0) %d  single(1) %d  skips(2) %d  skips(3+) %d  underruns %d  poll avg %.2f max %.2f ms (n=%d)  present avg %.2f max %.2f ms (n=%d)  iter avg %.2f max %.2f ms\n",
        f, n, e[0], e[1], e[2], e[3], u, ln ? l / ln : -1, lm, ln, pn ? p / pn : -1, pm, pn, ms / n, mx
    }' "$f"
done
```

Expected: one line per run. "Repeats" are UI frames that showed no new emulated frame; "skips" are UI frames where 2 or more frames ran, so one was never shown. At 60 Hz the threaded runs should show about one skip per 18 s on yarc, and far fewer repeats and skips on Cybermorph than the single-thread runs. The model predicts event -> present of about 25 ms for single-thread on both titles, about 25 ms for threaded + late latch on a light title and about 34 ms on a 14 ms one, and 46-55 ms unlatched.

- [ ] **Step 4: 120 Hz.** Only if Step 1 showed the display can do 120 Hz: ask the user to switch the Studio's display to 120 Hz (a system setting the agent must not change), then repeat Step 2 with `HZ=120` on its first line, and Step 3 over `emu-p1-120hz-*.csv`. Otherwise, skip.

- [ ] **Step 5: Write the table into the PR description.** Write the body to `~/Workspace/Provenance/vj-eval/remote/emu-p1/pr-body.md` (content below, every cell filled from Steps 2-4: the mean of the two runs of each arm, with both values in brackets). The latency target is met when threaded + late latch is within ~2 ms of single-thread event -> present on every measured title at 60 Hz. If it is met, open the PR for review. If it is not (the model predicts this for Cybermorph), open it as a draft (`--draft`), fill in the "Latency target" paragraph with the miss and its size, and leave the next step to the user (mode B is Phase 4). Either way, do not merge it.

```bash
git push -u libretro feat/967-emu-thread-phase1
gh pr create [--draft] --repo libretro/virtualjaguar-libretro --base develop --head feat/967-emu-thread-phase1 \
  --title "feat(app): emulation on its own thread, phase 1 (#967)" \
  --body-file ~/Workspace/Provenance/vj-eval/remote/emu-p1/pr-body.md
```

`pr-body.md`:

```markdown
Refs #967 (phase 1 of 5: docs/app-emulation-thread-plan-phase1.md)

Settings > Interface > Emulation thread, off by default in every build for now (nightlies turn it on in phase 3).
With it on, RetroAchievements, netplay, the debugger Tools and the GDB stub are off for the run (phases 2-3). With
vsync, the window loop uses a late latch: it waits until just before each vblank, passing input on as it arrives,
then shows the newest frame.

## Measurement (Mac Studio M4 Max, VJ_PERF_CSV, 3600 UI frames, A/B/C/C/B/A)

| title | display | loop | repeats / min | skips / min | event -> poll avg (ms) | event -> present avg / max (ms) | underruns | CPU % | UI iteration avg / max (ms) |
|---|---|---|---|---|---|---|---|---|---|
| yarc | 60 Hz | single-thread | | | | | | | |
| yarc | 60 Hz | emulation thread + late latch | | | | | | | |
| yarc | 60 Hz | emulation thread, unlatched | | | | | | | |
| Cybermorph | 60 Hz | single-thread | | | | | | | |
| Cybermorph | 60 Hz | emulation thread + late latch | | | | | | | |
| Cybermorph | 60 Hz | emulation thread, unlatched | | | | | | | |
| yarc | 120 Hz | single-thread | | | | | | | |
| yarc | 120 Hz | emulation thread + late latch | | | | | | | |

Model (`late_latch_unit`, event -> present): 5 ms core frames: single-thread 25.0, late latch 25.3, unlatched
46.4 ms. 14 ms core frames: 25.0, 34.2 and 55.3 ms.

Latency target (threaded + late latch within ~2 ms of single-thread event -> present at 60 Hz): met / missed on
<titles> by <ms>.

Windowed check: `app/tests/emu_thread_windowed_check.sh` OK on the Studio. TSan job: green.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

Expected: the PR's CI (`app`, `tsan`, `pr-issue-link`) goes green.

## Follow-up plans

**Phase 2: snapshots for options, messages, events; achievements; cheats; netplay `afterFrame`.** `EmuSnapshot` moves to its own file and gains the core-event stream (crash watchdog, BIOS staged/rejected, link up/down, `[NETLINK]` log lines). Netplay then reads its link state from the snapshot and `Netplay::afterFrame` runs per emulated frame on the emulation thread, so netplay comes back on with the switch On. RetroAchievements also returns: `rc_client_do_frame` runs right after each frame on the emulation thread, and unlock / progress / leaderboard events reach the UI through the snapshot while the HTTP worker stays as it is. The cheat batch from Phase 1 becomes a regular command. Two more items: `onOptionChanged`'s config write is replaced by snapshot events, and `call()` gets the 250 ms spinner from spec section 7. Tests: the `ra_*` and `netplay_pair*` suites run with `--emu-thread on`.

**Phase 3: the debugger halt, debug-view requests, the GDB stub.** A new `halt_pump` runs on the emulation thread. It publishes a "halted" snapshot, pauses audio, serves the mid-frame-safe debug commands (peek/poke, registers, disassembly, breakpoints, object-list and blitter-log snapshots, texture reload), and waits for Continue / Step / Detach from the UI. `App::haltedFrame` and its nested event loop go away; held commands show "applies after Continue". Each tool window registers a per-frame copy request (memory range, registers, object list, blitter log head, textures, audio scope ring) filled into its double buffer after every frame. The GDB socket loop then blocks only the emulation thread, which fixes #936's frozen window, and `virtualjaguar_gdb_stub` is unlocked. `debug_e2e` is rewritten so the UI keeps drawing while halted, and `call()`'s shutdown path learns to detach a halt first. With that, nothing is switched off while threaded any more, so Phase 3 also turns the setting on by default in nightlies: a `VJ_BUILD_CHANNEL` CMake cache string (`release`, `nightly` or `dev`) that `app-package.yml` sets from its `release_tag` (and the flatpak job stamps into its manifest), and a default rule that applies only while the user never chose (`ui.emu_thread` unwritten).

**Phase 4: mode B (display lock).** The UI thread sends a tick after each swap and the emulation thread runs one frame per tick, or one per k ticks when the display is near a whole multiple of the core rate (120/144 Hz). The audio rate is nudged ±0.5% with today's `AudioOut::update` controller, which mode A switches off. When the display rate is not close to a whole multiple, a notice says so and mode A takes over. `PacingClock` gets `nextDue` for vsync ticks plus fake-vsync tests (60, 120 and 144 Hz panels, and a 75 Hz panel that must fall back), and Settings gains the A/B choice.

**Phase 5: default On, measure, retire the old loop.** Nightlies default On from Phase 3, so this phase collects the nightly feedback and repeats Task 18's measurements on more titles and displays. Once a release has shipped with the switch defaulting On and no regressions, the next release removes `stepEmulation`, `Pacer` and the inline branch of `runInteractive`. Inline mode stays only for headless runs and tests, and the setting and `VJ_BUILD_CHANNEL`'s default rule are retired.


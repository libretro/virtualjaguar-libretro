# Desktop app: emulation on its own thread (design)

Issue: https://github.com/libretro/virtualjaguar-libretro/issues/967 (milestone v4.0.0).
Status: design approved in conversation 2026-10-15; this document is the spec for the implementation plan.

## 1. Goal and success criteria

The app runs `retro_run()` on the UI thread, so the core's cost per frame and the display's refresh are coupled.
Two things follow:
- a core frame that crosses the budget becomes a visible stall (blitter-heavy frames spike +1-7 ms; Cybermorph runs
  14 ms+);
- a debugger or gdb halt freezes the whole window.

The #956/#968 pacer work showed how fragile pacing is when emulation and presentation share one loop.

Success means:
- No visible stutter whenever the core's **average** cost fits the frame budget, even when single frames spike.
- The UI and every debug window stay responsive while the emulator is halted (app debugger or gdb stub).
- No regression in input latency, audio (underruns), save states, link play, achievements, cheats or determinism.
- True Jaguar speed by default (60.05 / 50.08 Hz).

Decisions taken during design:

| Question | Decision |
|---|---|
| Rate conflict (Jaguar 60.05 Hz vs display) | A setting. Default **A**: true Jaguar rate, paced by the audio clock, display shows the newest frame. **B**: lock to the display, nudging the audio rate. |
| Rollout | A setting, **off in releases, on by default in nightlies**; the old loop is removed after a release ships it on by default without regressions. *Amended 2026-10-16: off in every build through phase 2; nightlies default it on from phase 3, once nothing is switched off while it runs.* |
| Debug windows while running | **Live, copied once per emulated frame** from the emulation thread (consistent end-of-frame state). |
| Input latency | **No regression**: the emulation thread takes the newest input snapshot right before each frame. *Amended 2026-10-16: in mode A that alone costs ~22 ms event to present at 60 Hz, so phase 1 adds a late latch in the UI loop (input published as it arrives, the newest frame taken just before the vblank). It matches the old loop for light core frames; heavy frames (~14 ms) stay ~9 ms behind until mode B. Plan: "Decided: input latency in mode A".* |
| Architecture | **One owner thread** with commands, synchronous calls and per-frame snapshots (not a global lock, not a separate process). |

## 2. Threading rule

Only the emulation thread calls into the core: every `retro_*` function, the private debug interface
(`vj_debug_iface`), and anything that reads or writes core memory. This matches existing contracts:
`app/src/core_host.h` ("every method must be called from one thread") and `src/debug/vj_debug_iface.h` (calls on the
thread that runs `retro_run()`). The core itself is single-threaded C89 with global state and is not changed.

The main thread owns SDL events, the window, GL, ImGui, the shader chain and file dialogs. Existing background
workers (achievements HTTP, game hashing, RA images, library service, Discord IPC, font scan) are unchanged.

## 3. Components

Each unit lives in its own file under `app/src/` and can be unit-tested without SDL, GL or the core.

| Unit | Purpose | Interface (sketch) |
|---|---|---|
| `EmuThread` | Owns `CoreHost`. Frame loop: wait until due, run commands, run one frame, publish. **Inline mode** (no thread) for headless runs and existing tests. | `start(mode)`, `stop()`, `post(Command)`, `call(fn)`, `inlineStep()` |
| `EmuCommands` | Multi-producer queue of commands, applied in order at frame boundaries; `call(fn)` runs `fn` on the emulation thread and blocks the caller until done. | `post`, `call<T>(fn) -> T` |
| `FrameExchange` | Triple buffer of finished frames: pixels, width, height, pitch, aspect, frame serial. Writer never blocks; reader takes the newest without blocking. | `writeBuffer()`, `publish()`, `takeNewest() -> const Frame*` |
| `EmuSnapshot` | Per-frame published state: perf counters, option definitions/categories/visibility/locks (copied only when they change), OSD/log messages, core events, achievement events, audio-scope samples, and debug-view results. | `publish(...)` (emulation), `drain(...)` (main) |
| `InputSnapshot` | Latest input state (pads, keyboard, mouse, "input captured"). Main publishes after every input event; the core's input-poll callback copies the newest one. | `publish(state)`, `latest()` |
| `PacingClock` | Decides when the next frame is due (mode A or B); testable with a fake clock. | `nextDue(now, audioQueued, vsyncTicks) -> time` |

`CoreHost` keeps its current API; the emulation thread is its only caller. Most existing code moves rather than gets
rewritten.

## 4. Data flow

### Main thread (one pass per display refresh)
1. Poll SDL events; publish an `InputSnapshot` after every input event.
2. Take the newest frame from `FrameExchange` (redraw the previous one if nothing is new).
3. Drain `EmuSnapshot` (messages, events, option changes, debug results).
4. Draw ImGui and present with the user's vsync setting.

The UI never waits on emulation.

### Emulation thread (one emulated frame per iteration)
1. Wait until `PacingClock` says the next frame is due (or for a command while paused or halted).
2. Apply queued commands.
3. Run one frame. The core's input-poll callback copies the newest input snapshot. Audio goes straight into the SDL
   audio stream (`SDL_PutAudioStreamData` is safe from any thread).
4. Publish the frame and the snapshot; run the per-frame achievements check (it reads game memory).

### Mode A: audio clock (default)
- Target the SDL audio queue at `audio.target_ms` (64 ms today). Below target: run the next frame. Above target: sleep
  for the time the queue takes to drain back to target.
- The sound card's clock is the master, so the game runs at true speed and audio never drifts. On a 60 Hz panel one
  frame repeats about every 20 s.
- No audio device, muted output, or a paused stream: fall back to the wall clock at the core's period.
- *Amended 2026-10-17 (#967 evenness): phase steering.* A frame ends at its due time plus its wake-up lateness and
  cost, so the end times spread over a window (a few ms for Cybermorph). While the slowly drifting grid carries the UI's
  take point through that window, each frame's own cost decides whether it makes the take. The result is a run of random
  repeat + skip pairs at every crossing (60-160 a minute) instead of the one skip the beat owes. While the UI has the
  vblank grid locked, it publishes its take time and the display period. When the next frame's end window would contain
  a take point, `PacingClock` steps the grid's phase once across it, in the direction the grid is drifting (its trimmed
  period, so the queue error a step leaves can never turn the steps against the drift). Only the phase moves: at most
  the window's width, once per crossing, with at least 30 frames between steps. The rate is still the trimmed audio
  rate, and the display never holds the drift back, so this is not mode B. Steering runs only where the grid creeps
  along the take grid (under 0.2 ms a frame: 60.05 Hz on 60, 120 or 59.94 Hz panels). At ratios like 50.08 Hz on 60 Hz,
  every few frames straddle a take point by construction (5:6 judder), and only mode B can change that.
  `--pace-steer off` turns steering off for A/B runs.
- *Amended 2026-10-20 (#1026): the UI thread's wake-ups.* The late latch's wait must end on time, or the build starts
  late and the swap misses its vblank. On macOS two things made it end late. First, a launch from a terminal (and every
  ssh-run benchmark) starts the main thread at the default QoS rather than user-interactive, so any CPU load leaves it
  runnable but not running. Second, `SDL_WaitEventTimeout`'s timeout is the Cocoa run loop's own timeout, which Core
  Foundation delivers from a dispatch worker at `qos_class_main()`, the main thread's QoS at launch. Raising the thread
  later does not change that worker, which runs 3-20 ms late under load while the UI thread sleeps past its deadline. So
  the UI thread now runs at user-interactive QoS however it was launched (`platform_macos.mm`). Its timed event waits
  (the latch's and the threaded nap's) end on a wake event that `LatchWaker` (`app/src/latch_waker.h`) pushes from its
  own user-interactive thread, with SDL's timeout kept as a 5 ms backstop. A `CFRunLoopTimer` on the main run loop was
  tried instead, and from a terminal launch it fired ~3 ms late every time. `--latch-wake plain` restores the old wait
  for A/B runs (the QoS raise stays), and with `VJ_PERF_TRACE` the stall probe (`app/src/stall_probe.h`) writes every
  wait that ends over 3 ms late to `<prefix>-stall.txt`, with the run state, CPU time, run-loop transitions and sampled
  stacks.

### Mode B: display lock
- The main thread sends a tick after each swap; the emulation thread runs one frame per tick (one per k ticks when the
  display is close to a whole multiple of the core rate, e.g. 120/144 Hz). The audio rate is nudged ±0.5% to match
  (today's `AudioOut::update` controller).
- If the display rate is not close to a whole multiple of the core rate, show a notice and fall back to mode A.

### Shared rules
- **Fast-forward:** unthrottled or a set multiple, audio dropped (as today).
- **Pause:** the emulation thread parks on a condition variable; the UI keeps running.
- **Falling behind:** never run more than 2 frames late. Beyond that, drop owed time rather than burst (the #968
  lesson). The audio queue absorbs short spikes.
- **Readers of the frame** (state thumbnails, screenshots, frame hashes, the OP viewer) read from `FrameExchange`, never
  from the core's buffer.

## 5. The command / call / snapshot boundary

Classification follows the read-only audit of every UI-to-core touch point (2026-10-15).

| Kind | Touch points |
|---|---|
| **Command** (`post`, applied at the next frame boundary) | option set/override/clear/reset; controller device per port; cheats set/reset; reset; pause/resume; fast-forward; mute; frame advance; disc eject/insert/index; BIOS path and boot target; netplay option changes; hardcore toggle |
| **Call** (`call`, caller waits at most one frame) | load content; load with no content; boot BIOS; unload; save/load state (thumbnail from `FrameExchange`); SRAM flush (`retro_get_memory_*`); memory-map queries; debug poke/set register/breakpoints/step; texture-replacement reload |
| **Snapshot** (published per frame, drained by the UI) | option definitions/categories/visibility/locks (on change: the core can redefine options inside `retro_run`); OSD and log messages; core events (crash watchdog, BIOS staged/rejected, link events); perf counters; audio-scope samples (single-producer ring); debug-view results |

Callbacks that write app state today change to:
- `onOptionChanged` (writes the config) and netplay's `onCoreLog`/`onEvent` become snapshot events applied on the main
  thread. The config is only ever written on the main thread.
  *Amended 2026-10-18 (phase 2): the core-event stream, `app/src/emu_snapshot.h`.* Log lines, `vj_event`s and the
  option writes made on the owner side travel as `CoreEvent`s in one ordered queue; the UI thread reads them when it
  pins the newest snapshot, and `CoreHost::onUiEvent` is the UI-side channel they arrive on. `onLog` and `onEvent` stay
  synchronous on the core's thread (the headless `--disc-insert` check relies on that), so they are not the UI's tap.
- `fastForward` and `mute` become atomics; "input captured" becomes part of `InputSnapshot`.
- `audioBufferStatus` reads the SDL stream's queued size (thread-safe) on the emulation thread.

**Debug views.** Each open debug window registers a request: a memory range, registers, the object list, the blitter
log head, textures, the scope. After each frame the emulation thread copies just those into that tool's double buffer.
Closed windows cost nothing. The `g` singletons in `app/src/debug/*_ui.cpp` read only their published buffers.
*Amended 2026-10-20 (phase 3): the debug lane and `DebugViews`.* The command queue gets a second lane,
`EmuCommands::Lane::Debug`, that the emulation thread drains at every frame boundary and from inside a halt; the
frame lane is never drained while halted. `EmuThread::debugCall` / `debugPost` carry the one-shot actions (poke,
set_reg, breakpoints, OP arm, blit-log enable, pack reload, attach/detach). The periodic reads are `ViewRequest`s
(memory range, DRAM, registers, a disassembly window, the OP snapshot, the blit-log tail) in `DebugViews`
(`app/src/debug/debug_views.h`): `fill()` runs after each frame at `DebugSession::kRefreshHz` while running, on every
pump iteration whose serial changed while halted, and on the same thread inline, so the windows have one code path. A
content switch bumps the views' epoch and every buffer is unreadable until refilled. `DebugCore`
(`app/src/debug/debug_core.h`) is the emulation-thread owner of the interface; `DebugSession` stays the UI's object and
reaches it through `coreCall` / `corePost`.

**Achievements.** `rc_client_do_frame` runs on the emulation thread after each frame. Unlock, progress and leaderboard
events go to the main thread through the snapshot. The HTTP worker is unchanged.
*Amended 2026-10-18 (phase 2): idle and rich presence run on the emulation thread too.* `read_memory` called off it
(rc_client validates addresses while the game load completes on the UI thread) answers from the core's memory map,
never from RAM. rc_client events are copied into `AchEvent`s (`app/src/ach_event.h`), queued, and applied in
`Achievements::pump()` on the UI thread.

**Guards.** The `inFrame_` refusals in `CoreHost` and `App` mostly disappear, because commands wait for the boundary.
While halted, see section 6.

## 6. Debugger halt, gdb stub, link play, run-ahead

**Halt.** The core halts inside `retro_run` (on the emulation thread). The new `halt_pump`:
1. publishes a "halted" snapshot (target CPU, PC, reason) and pauses the audio stream;
2. blocks on a condition variable for Continue / Step / Detach from the UI;
3. meanwhile serves debug commands that are safe mid-frame: peek/poke, registers, disassembly, breakpoints,
   object-list and blitter-log snapshots, texture reload.

`App::haltedFrame` and its nested event loop are removed; the UI runs normally. Commands that cannot run mid-frame
(load, unload, reset, save/load state, option changes) are held until Continue, and the UI says "applies after
Continue". Hardcore mode still detaches the debugger.
*Amended 2026-10-20 (phase 3): the pump is bounded.* `GDBHalt()` (`src/debug/gdbtarget.c`) keeps its own loop:
socket pump, the app's `halt_pump`, the halt timeout, a 2 ms sleep. The core is not changed, so the app's pump
(`DebugCore::pump`) serves the debug lane, waits at most `kPumpWait` (4 ms) for a verdict or a debug post, and returns
`VJ_DBG_RESUME_NONE`; the core calls it again. Verdicts are an atomic the UI writes, honoured only for the halt they
were written against (a serial). `onHalt` / `onResume` fire on the emulation thread for emulation-side effects (audio
pause); the UI mirrors the `HaltState` once per frame (`DebugSession::syncHalt`) and fires its own `onHaltUi` for the
toast and the halt bar. `DebugCore::frameEnded()`, after every `retro_run`, closes a halt the core left without one of
the app's verdicts (gdb continued, the socket dropped, the halt timeout), which today leaves the app "halted" for good. `App::haltedFrame` runs only
inline, until phase 5 retires the old loop. Held commands: frame-lane posts wait in the queue by construction; save /
load state, boot and frame advance refuse with a toast while halted; open / close / reset / disc insert / drop / quit
detach first (a detach clears every breakpoint, so the next frame cannot re-halt) and then proceed. `EmuThread::call`
takes a give-up predicate so a call posted an instant before a halt returns instead of hanging the UI behind the spinner.

**GDB TCP stub.** `GDBHalt`'s socket loop runs on the emulation thread, so a gdb session that holds the target halted no
longer freezes the window (fixes the freeze found in #936). If both gdb and the app debugger are attached, the first
verdict wins, as today. The halt timeout is unchanged.
*Amended 2026-10-20 (phase 3): a host behind every halt.* Without an app host the app cannot see a gdb halt: a
`call()` would block until gdb continues and quit would wait out the join. So the app attaches a silent host whenever
`virtualjaguar_gdb_stub` is `enabled` and content is loaded (one stored pointer), the halt bar says "HALTED by gdb",
and quit detaches (which clears gdb's breakpoints, as the inline app already does). `attach()` needs the tools enabled,
so RetroAchievements hardcore locks the stub to `disabled`, as it does cheats. Both are proposed decisions (phase 3
plan, "Decisions to confirm").

**Link play.** The core's peer wait inside `retro_run` (`src/jerry/jlink.c`) now blocks only the emulation thread. Its
frame budget assumes one `retro_run` per frontend period, which mode A provides. Its 3 ms frontend margin becomes
conservative. `Netplay::afterFrame` runs per emulated frame on the emulation thread; its UI-visible state goes through
the snapshot.
*Amended 2026-10-18 (phase 2): `Netplay` stays a UI-thread object.* It reads what the core says from the core-event
stream (`onUiEvent`, with an epoch filter that drops what came before its last start, stop or bounce), and its settings
reach the core as commands. The restart bounce that a mode change needs is counted in emulated frames
(`CoreHost::frameCount()`), not in UI-loop iterations, so it lasts as long as the core sees it.

**Run-ahead (if added later).** Lives entirely on the emulation thread.

## 7. Lifecycle and failure handling

- **Startup:** the emulation thread starts after the window and audio are up. If it cannot start, log and fall back to
  the single-thread loop.
- **Shutdown:** detach any debugger halt, wake link play's wait, unload content through a call (flushes SRAM on the
  emulation thread), join. If the join takes more than 3 s, log and exit anyway.
- **Command errors:** caught on the emulation thread and shown as a toast; the thread keeps running.
- **Slow calls:** after 250 ms the UI shows a spinner instead of freezing.
  *Amended 2026-10-18 (phase 2): the hook.* `EmuThread::setWaitHook(fn)` (set before `start()`) is called on the thread
  that waits in `call()` once it has waited `kBusyAfter` (250 ms), then about every 16 ms until the call returns; the
  first call has tick 0. It may pump OS events and swap a frame it draws with raw GL; it must never open an ImGui
  frame, call `call()` or `post()`, or read a field that only a `call()` writes. It is never called inline, on the
  emulation thread, or re-entered. `App::busyFrame` draws `VideoOut::drawBusy`, which replaces the picture with the
  spinner while the call lasts.
- **Content switch:** unload and load are calls; `FrameExchange` and `EmuSnapshot` are cleared on load, so no frame or
  option list from the previous game is shown.

## 8. Rollout

- Settings > Interface > "Emulation thread" (On/Off). Releases default Off, nightlies default On (nightly build flag).
  Amended 2026-10-16: every build defaults Off through phase 2; the nightly default and its build flag arrive in
  phase 3.
  *Amended 2026-10-20 (phase 3): the flag.* `VJ_BUILD_CHANNEL` (CMake cache string: `release` | `nightly` | `dev`,
  default `dev`) sets `VJ_EMU_THREAD_DEFAULT` (1 for `nightly`). `app-package.yml` passes the channel from its
  `release_tag` (`nightly` -> nightly, `v*` -> release, else dev); the flatpak job stamps it into the manifest's
  `config-opts`. The default applies only while `ui.emu_thread` is unwritten (`emuThreadEffective(pref, default)`):
  an explicit Off stays Off in nightlies and an explicit On stays On in releases. Headless runs stay inline whatever
  the default. It ships last, in its own PR, after nothing is switched off while threaded.
- The old loop stays selectable until a release ships with the setting defaulting On and no regressions; then it is
  removed.

## 9. Testing

- **Unit tests:**
  - `FrameExchange`: never tears, always the newest frame.
  - Commands: posts run in order; a call sees earlier posts applied.
  - `PacingClock`: fake audio clock (mode A), fake vsync ticks (mode B), and a 60.05 Hz core on a 60 Hz panel giving one
    repeat in about 20 s.
  - `InputSnapshot`: an input event is seen by the next frame.
- **Determinism:** headless runs and the existing core-host tests use inline mode. A new headless test runs threaded and
  must produce the same frame hashes as inline mode for the same scripted input.
- **ThreadSanitizer:** a CI job runs the app tests in threaded mode; any race report fails it.
- **Existing tests:**
  - `debug_e2e`: updated so the UI keeps drawing while halted.
  - `netplay_pair*`: unchanged, must pass.
  - Achievements and cheats tests: must pass.
- **Measurement** (Mac Studio, threaded vs single-thread, 60 Hz and 120 Hz, using `VJ_PERF_CSV` from #966):
  - presentation evenness;
  - input latency (SDL event timestamp to the core's poll);
  - audio underruns;
  - CPU use.

## 10. Build order

Each step is its own PR, with the setting Off by default:

1. `EmuThread`, `EmuCommands`, `FrameExchange`, `InputSnapshot`, `PacingClock` mode A; basic commands (load, unload,
   reset, pause, fast-forward, save/load state).
2. Option/message/event snapshots; achievements; cheats; netplay `afterFrame`.
3. Debugger halt redesign; debug-view requests; the gdb stub path; default On in nightlies (amended 2026-10-16).
   *Amended 2026-10-20: four PRs.* 3a debug views on the emulation thread (the debug lane, `DebugViews`, every
   Tools window; halts still off); 3b the bounded halt pump, calls while halted, the gdb stub unlocked; 3c Neuro
   through the input snapshot's agent program and `DebugViews` watches; 3d `VJ_BUILD_CHANNEL` and the nightly
   default. 3b and 3c run in parallel after 3a; 3d merges last, after a Studio gate with every tool, gdb and Neuro on.
4. Mode B.
5. Measure the nightly feedback; then default On in a release and remove the old loop.

## 11. Out of scope

- Variable-refresh presentation (present-on-new-frame): a later improvement on top of mode A.
- Moving any work inside the core onto more threads (the core stays single-threaded).
- Run-ahead and rollback netplay (#864): they build on this design but are separate work.

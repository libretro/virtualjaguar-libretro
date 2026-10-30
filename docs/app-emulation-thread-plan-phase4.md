# Emulation thread, Phase 4 Implementation Plan (mode B: display lock)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

Issue: #1038 (part of #967 / #254). Design of record: [app-emulation-thread-design.md](app-emulation-thread-design.md), section "Mode B: display lock".

**Goal:** With the emulation thread on, Settings > Video > Frame pacing offers **Jaguar rate** (mode A, the default: audio clock, true 60.05 / 50.08 Hz) and **Match display** (mode B). In mode B every emulated frame starts on a display tick, the audio rate follows the display, and the UI swaps as soon as the tick's frame is ready. That removes mode A's periodic repeat (one about every 20 s on a 60 Hz panel) and closes the ~9 ms event-to-present gap phase 1 left for heavy titles such as Cybermorph. Wherever a lock is not sound, mode B says why and runs mode A.

**Architecture:** two PRs.
- **4a, the pure parts** (no SDL, no GL, no app): `DisplayLock` decides whether and how to lock; `PacingClock` gains a tick-driven path; `AudioOut` gains a base ratio for its existing ±0.5% controller. All unit-tested with fake clocks and fake vsync ticks.
- **4b, the wiring:** the setting, the UI thread's tick and lock verdict, the fallback notice, link play forcing mode A, the latch waking on frame publish, perf-trace columns, docs, and the Studio measurement.

The old inline loop is untouched and ignores the setting (it is already display-paced and is removed in phase 5).

## Decisions (agreed 2026-10-30)

| Question | Decision |
|---|---|
| Which panels lock | k = round(display / core). Lock when \|display/k − core\| / core < 0.3%, measured from the vblank period, not SDL's nominal `refresh_rate`. 0.3% leaves headroom inside the audio controller's ±0.5%. |
| 144 Hz | **Falls back** (correction to the design doc and the phase 1 follow-up text, which named it a lock case): 144 / 60.05 = 2.40. |
| 120 Hz validation | Fake-tick unit tests; real windowed runs only on the Studio's 60 Hz panel. A real 120 Hz check comes from nightly feedback. |
| Link play / netplay | Mode A is forced while `Netplay::link()` is not `Off`, with a notice; mode B resumes when the link is off. |
| Latch wake | In mode B the UI's late-latch wait also ends when the frame for the current tick is published; the latch deadline stays as the backstop. |
| Setting | Settings > Video > "Frame pacing": Jaguar rate (default) / Match display. Key `video.pacing` = `jaguar` \| `display`. Active only with the emulation thread on. |

Lock table (core 60.0544 / 50.0801 Hz, tolerance 0.3%):

| Panel | NTSC | PAL |
|---|---|---|
| 60.00 / 59.94 Hz | k=1, −0.09% / −0.19%: **lock** | fall back |
| 120.00 / 119.88 Hz | k=2, −0.09% / −0.19%: **lock** | fall back |
| 144 Hz | fall back (+19.9%) | fall back (−4.2%) |
| 75 Hz | fall back | fall back |
| 50 / 100 Hz | fall back | k=1 / k=2, −0.16%: **lock** |

## Behavior

**Lock verdict (UI thread, every iteration).** `DisplayLock::evaluate` takes the core period, the measured vblank period, and the conditions below. It returns `{mode, k, baseRatio, reason}`. Mode B needs **all** of:
1. Setting = Match display, emulation thread on.
2. Vsync wanted and allowed (`wantSync()`; `guardSwap` has not forced interval 0 because the display sleeps, #1081).
3. `LateLatch::locked()`. A swap grid that won't lock (ProMotion/VRR jitter, a compositor dropping frames) is not a clock to follow. VRR presentation stays out of scope (design section 11).
4. The ratio test above, with the latch's measured period.
5. Not fast-forwarding, and `Netplay::link() == Off`.

Hysteresis: enter B only after conditions 2–4 have held for 120 consecutive swaps (~2 s). Leave B on the first swap they fail. Re-evaluate at once on `SDL_EVENT_WINDOW_DISPLAY_CHANGED`, `SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED` and fullscreen enter/leave, which today only invalidate the cached period.

**Notice.** On each change of `reason` while the setting is Match display, show one OSD message, e.g. "Match display: using Jaguar rate (144 Hz is not a multiple of 60.05 Hz)", "(vsync is off)", "(link play)". Don't repeat it while the reason is unchanged. Settings shows the live state next to the choice.

**Ticks.** After `latch_.onSwap(swap0, swap1)` the UI publishes `tick{seq, swap1}` to the emulation thread, along with the verdict (`EmuThread::displayTick`: atomics plus `cmds_.notify()`). This period is published whatever `--pace-steer` says. Today's `takePeriodNs_` is 0 unless steering is on, so mode B can't reuse it.

**Emulation loop in mode B.** A frame is due when `seq − lastStartSeq ≥ k`, and it starts at once. If more than 2 frames' worth of ticks are owed (`2k`), the owed ticks are dropped and counted, never run as a burst (the #968 rule, as in mode A). Pause, halt, `advance(n)` budgets and the command lanes are unchanged. Mode switches happen only at a frame boundary:
- A→B: `PacingClock` leaves the audio grid and its trim and steering stop.
- B→A: `PacingClock::reset(now)`; the audio ratio returns to 1 and mode A's trim takes over.

Exactly one audio controller runs at a time.

**Audio in mode B.** The core produces samples at the display-derived rate, so the stream's frequency ratio is fed forward as `baseRatio = (display/k) / core` (0.9991 on a 60 Hz panel). `AudioOut::update(true)` then trims ±0.5% around that base to hold `audio.target_ms`. Mode A keeps `update(false)`. The pitch change (≤0.3%) is inaudible.

**Latch wake on publish.** In mode B, `FrameExchange::publish` wakes the UI's latch wait through `LatchWaker` once the published frame belongs to the latest tick. The UI then builds and swaps at once, like the inline loop, so a frame that fits `period − UI budget` shows at the next vblank. The latch deadline still ends the wait if the frame is late; the UI never waits on emulation.

## Files

| File | Change |
|---|---|
| `app/src/display_lock.h` (new, header-only) | `DisplayLock` (verdict, hysteresis, reasons) |
| `app/src/pacing_clock.h` | tick-driven path: `tickDue(seq, k)`, owed-tick drop, mode switch with `reset` |
| `app/src/audio.h`, `audio.cpp` | `setBaseRatio(r)`; `update(true)` trims around it |
| `app/src/emu_thread.h`, `.cpp` | `displayTick`, verdict atomics, mode-B wait in the loop, publish-wake hook |
| `app/src/app.cpp`, `app.h` | publish ticks + verdict after `onSwap`; display/fullscreen events re-evaluate; notice; link → A; wake-on-publish in `latchWait` |
| `app/src/ui.cpp`, `settings_reset.cpp`, TV settings rows | Frame pacing choice + live state |
| `app/src/perf_trace.h`, `app/tests/perf_trace_summary.py` | columns `mode`, `tick_seq`, `ticks_dropped`, `lock_reason` |
| tests: `display_lock_test.cpp` (new), `pacing_clock_test.cpp`, `audio_out_test.cpp`, `emu_thread_test.cpp` | below |
| docs: design doc (144 Hz correction, mode B as built), README pacing section, settings guide | |

## Tasks

### PR 4a: pure parts

- [ ] **Task 1: `DisplayLock`.** Write tests first (`display_lock_unit`):
  - Lock: 60.00 / 59.94 / 120 / 119.88 Hz NTSC; 50 / 100 Hz PAL.
  - Fall back: 144 Hz and 75 Hz both regions, and PAL on 60 / 120 Hz.
  - Each condition 2–5 alone forces A with its own reason.
  - Hysteresis: 119 good swaps stay A, 120 enter B, one bad swap leaves.
  - k and `baseRatio` values.
  Then implement.
- [ ] **Task 2: `PacingClock` tick path.** Tests with fake ticks:
  - k=1 runs one frame per tick.
  - k=2 runs one frame per two ticks.
  - A 3-period stall drops owed ticks and never bursts more than one catch-up frame.
  - A↔B switches reset correctly, with no owed time carried across.
  - A 60.00 Hz panel in mode B shows **no** periodic repeat over a simulated 60 s, where mode A shows the expected ~3.
  Keep every existing mode A test green.
- [ ] **Task 3: `AudioOut` base ratio.** `audio_out_unit` (dummy driver): the base ratio is applied; `update(true)` trims within ±0.5% of base; `update(false)` leaves it untouched; leaving B restores 1.0.
- [ ] Gate: `ctest -L emu_thread` and the TSan job green; PR 4a opened (Refs #1038).

### PR 4b: wiring

- [ ] **Task 4: setting.** `video.pacing`, Video settings choice with live state, TV settings row, reset class, settings guide.
- [ ] **Task 5: ticks and verdict.** `App` evaluates `DisplayLock` each iteration, publishes ticks after `onSwap`, and re-evaluates on display/mode/fullscreen events; `EmuThread` loop runs the mode-B path. `emu_thread_unit` gets a fake-tick client: frames run only on ticks; pause/halt/advance unaffected; commands still served while waiting for ticks.
- [ ] **Task 6: fallback notice + link play.** One OSD per reason change. While `Netplay::link() != Off` force A. `netplay_pair*` run with `video.pacing=display` and must match today's results.
- [ ] **Task 7: wake on publish.** `latchWait` ends on the tick's frame publish in mode B; perf-trace columns; `late_latch_unit` model case: with wake-on-publish a 14 ms frame at 60 Hz presents one period after its tick.
- [ ] **Task 8: Studio measurement** (windowed, 60 Hz Dell, launched at user-interactive QoS per #1026, A/B/B/A order, exact build IDs, quiet-host gate, raw traces kept):
  - Arms: inline, threaded mode A, threaded mode B.
  - Titles: a light NTSC title, Cybermorph (heavy NTSC), and one PAL title (must report the fallback and behave like mode A).
  - Report per arm: event → poll and event → present latency; repeats/skips split into true-rate beat vs missed deadline; audio underruns/overruns; CPU use; stalls from the stall probe kept apart from pacing misses.
  - Pass bar:
    - mode B event → present within ~2 ms of inline on every title, Cybermorph included;
    - no periodic repeat in mode B;
    - no new underruns;
    - CPU within +5% of mode A.
- [ ] **Task 9: transitions.** On the Studio:
  - fullscreen enter/leave, display sleep/wake (#1081 path), vsync toggle and fast-forward toggle each switch mode with the right notice and recover;
  - debugger halt / F10 step, GDB attach, RetroAchievements and a link-play pair still pass their existing checks with Match display selected.
- [ ] Docs: design doc "as built" amendments (144 Hz correction, wake-on-publish, link play forcing A), README pacing section. PR 4b opened (Closes #1038).

## Out of scope

- Variable-refresh presentation (design section 11).
- Changing the default from Jaguar rate (phase 5 decides with nightly feedback).
- Retiring the inline loop (phase 5).
- Rare Cocoa/host-runloop outliers (#1026).

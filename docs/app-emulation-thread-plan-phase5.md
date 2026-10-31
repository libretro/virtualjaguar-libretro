# Emulation thread, Phase 5 plan (qualify the stable default)

Issue: #1039 (part of #967 / #254). Design of record: [app-emulation-thread-design.md](app-emulation-thread-design.md), section 8 "Rollout". Earlier phases: [1](app-emulation-thread-plan-phase1.md), [2](app-emulation-thread-plan-phase2.md), [3](app-emulation-thread-plan-phase3.md), [4](app-emulation-thread-plan-phase4.md).

**Goal:** the 4.0 release turns Settings > Interface > Emulation thread **On by default**, on evidence, with the inline loop still selectable. This phase is mostly qualification. The only behaviour change is the last PR, which flips the release default.

**Not in this phase:**
- **Retiring the inline loop** (`stepEmulation`, `Pacer`, the inline branch of `runInteractive`). #1039's rule replaces the phase 4 plan's "Retiring the inline loop (phase 5)": remove it only after a stable release has shipped default-on without regressions. That follow-up issue is filed when the evidence exists, not before.
- **Changing Frame pacing's default.** It stays Jaguar rate unless decision 1 below says otherwise.

## Where it stands (2026-10-31, develop @ fe80b36a)

- **Phases 1–4 are merged.** Nothing is switched off while threaded: `no_threaded_limits` enforces this statically in CI. Mode B is in (#1125, #1128).
- **Nightlies default On since phase 3d** (about 2026-10-24); releases and `dev` builds default Off. No threading issue has been filed since. That is a week with no reports, not qualification evidence.
- **Headless runs stay inline whatever the default** (`app.cpp`, the `emuThreadThisRun_` decision). Flipping a channel's default therefore changes no existing test's mode, and the inline tests keep covering the fallback.
- **CI coverage today:**
  - 82 tests carry `LABELS emu_thread`. They run in all seven `app.yml` configurations: macOS arm64, Linux x86_64 (dev and nightly channel), Linux aarch64, Windows x86_64 MinGW, Windows ARM64, and MSVC (experimental).
  - The TSan job runs `ctest -L emu_thread` on Linux. On the merged mode B code, 82 of 82 pass, re-run on the Studio.
- **Windowed coverage today:** the Studio's 60 Hz panel (macOS) only. Headless CI never has vsync, so the late latch and mode B are exercised on Windows and Linux only by unit tests with fake clocks.

## Gates

Each gate lists its evidence. Gates 1–3 are work in this repo; gates 4 and 5 are evidence that has to come in. The flip (PR 5c) opens only when every gate is met or the maintainer waives it in writing on #1039.

| Gate | Met when | Evidence goes to |
|---|---|---|
| **G1 coverage** | Every headless test that runs the app with content has a `_threaded` twin, or a line here saying why it is mode-specific | PR 5a |
| **G2 platforms** | G1's twins pass on every `app.yml` configuration: the six required ones, plus MSVC, which may stay experimental. TSan stays green | PR 5a CI |
| **G3 Studio, 60 Hz** | The phase 4 measurement passes its bar (below) and the transition runs recover. The phase 3 Studio gate (`emu_thread_gate_check.sh`) passes on the 4.0 candidate | #1128 comment, then the evidence table here |
| **G4 other displays** | At least one windowed report each for Windows, Linux and a 120 Hz panel, threaded, with no regression against inline | #1039 comments |
| **G5 nightly window** | Two weeks of nightlies with no open threading regression; any report is fixed or shown not to be a threading issue | #1039 |

**G3 bar** (phase 4, Task 8):
- Mode B event→present within ~2 ms of inline on every title, Cybermorph included.
- No periodic repeat in mode B.
- No new underruns.
- CPU within +5% of mode A.
- PAL reports the `ratio` fallback.

Mode A must not regress against the phase 1 numbers.

### G1: tests without a threaded twin

Taken from `ctest --show-only=json-v1` at fe80b36a. These are tests whose command runs `virtualjaguar` with content and passes no `--emu-thread`, and that have no `_threaded` twin.

| Area | Tests | Twin |
|---|---|---|
| Smoke | `smoke_cd`, `smoke_boot_cart_rom`, `smoke_disc_insert_cart_hidden`, `smoke_bad_option`, `smoke_locked_option` | add |
| Cheats | `cheat_state`, `cheat_apply_control`, `cheat_persist_{save,restart,control}`, `cheat_tempest_{lives,control}`, `cheat_community_install` | add. The persist chain needs its own data directory and fixtures |
| Overlay | `controls_overlay_{default,keyboard,dismissed,rebound}` | add |
| Netplay | `netplay_pair_shared` | add (the other pair tests already have one) |

Mode-specific by design, so no twin:
- `gdb_inline_halt*`, `neuro_app_inline`, `netplay_pair_mixed` (one peer each way), `emu_thread_default_smoke_*`.
- `tv_menu_headless` and `tv_script_windowed_only`: a headless TV script always runs inline (`app.cpp`).
- `native_menu_dump`: no content.
- `netplay_discovery`: skipped in CI.

The `*_e2e` tests (`input_e2e`, `devices_e2e`, `bios_e2e`, `debug_scan_e2e`, `debug_op_e2e`) drive `CoreHost` directly, not the app loop. Their threaded counterparts are `core_host_thread` and `emu_thread_e2e`.

**Shutdown.** These already cover it:
- `emu_thread_unit` covers shutdown races.
- `gdb_threaded_halt` covers SIGTERM mid-halt.
- `debug_e2e_threaded` covers the 3 s join while halted.

5a adds one headless case: quit while a slow load `call()` is in flight (`VJ_TEST_SLOW_CALL_MS`, as `smoke_busy_threaded` uses). It must exit 0 within the join timeout with no owner violation.

## PRs

- **5a `test(app)`: threaded twins (G1, G2).**
  - The twins listed above go through the registration each area already uses: the `_threaded` loop in `VJEmuThreadTests.cmake`, the twin helper in `VJAchTests.cmake`, or the same pattern added to `VJCheatTests.cmake` and `VJSmokeTests.cmake`.
  - Each twin carries `LABELS emu_thread`, so the TSan job runs it.
  - Each twin must reach the same pass regex as its inline original. A twin that needs a different expectation is a finding, not a test tweak.
  - Fix any real failure in its own PR, outside 5a.
  - Refs #1039.
- **5b `docs(app)`: qualification record.**
  - The evidence table below, filled from G3–G5.
  - A short "Reporting a threading problem" note in the README's "Emulation thread" section: how to report (the About window's version and channel line, the perf note, `--emu-thread off` to compare).
  - One pinned comment on #1039 asking nightly users for G4 reports.
  - Refs #1039.
- **5c `feat(app)`: releases default the emulation thread On (the flip, last).**
  - `emuThreadDefaultFor("release")` returns true. `VJ_EMU_THREAD_DEFAULT_VALUE` follows in `app/CMakeLists.txt`, and the `static_assert` in `build_channel.h` and the `emu_thread_default_unit` / `emu_thread_default_smoke_*` expectations change with it.
  - Unchanged:
    - a stored `ui.emu_thread` choice still wins;
    - `--emu-thread off` still runs inline for one run;
    - a failed thread start still falls back inline.
  - Docs: design doc section 8 "Shipped (phase 5)" and the README.
  - The 4.0 release notes say how to turn the thread off.
  - Closes #1039 (and #967 if the maintainer agrees that the retirement follow-up is filed separately).

## Decisions for the maintainer

| | Question | Recommendation |
|---|---|---|
| 1 | **Frame pacing default:** keep Jaguar rate for 4.0, or default nightlies to Match display now so feedback accrues? | Default **nightlies** to Match display after G3 passes; keep Jaguar rate for the 4.0 release. Mode B's only windowed evidence is one 60 Hz macOS panel. |
| 2 | **`dev` channel after the flip:** Off (today) or On? | **On**, with 5c. Phase 3 kept `dev` Off so PR artifacts behave like releases; after the flip that same reason means On. |
| 3 | **G4 without volunteers:** if no Windows, Linux or 120 Hz report arrives, does the flip wait? | Wait for **Windows** (the largest user base), or waive in writing on #1039. Linux and 120 Hz can ship on unit-test coverage plus the inline fallback. |
| 4 | **G5 length:** two weeks of nightlies? | Two weeks, starting when 5a merges, so the twins have run on every PR in that time. |

## Evidence

Filled in by 5b, then kept up to date as G3–G5 evidence arrives.

Studio = the Mac Studio (M4 Max, 16 cores) on its 60 Hz panel, every windowed run launched through LaunchServices (#1026), 60 s per run with scripted input, started only at a 1-minute load under 4 (the host's iCloud daemons hold about 2.7 cores). Arms: off = inline; A = threaded, Jaguar rate; B = threaded, Match display. Event→present is the exact figure, about 11,100 samples per run. Traces are in `vj-eval/1039-g3/`.

| Date | Build | Host / display | What | Result |
|---|---|---|---|---|
| 2026-10-31 | 06fa8a71 (#1130) | GitHub CI, every `app.yml` configuration | **G1, G2:** the threaded twins | **Met.** 21 twins; `emu_thread` label 82 → 102 tests. All seven app configurations pass, MSVC included, and so does the ThreadSanitizer job |
| 2026-10-31 | a661028c (fe80b36a's code) | Studio | **G3 bar:** off / A / B / B / A / off on yarc, Cybermorph, yarc PAL (`mb-m3-*`) | **Met.** B − off event→present: yarc −0.18, Cybermorph +0.16, PAL +0.99 ms. 0 underruns in 18 runs. CPU against A: −9%, −6%, +1.5%. PAL reports `ratio`. Full tables: [#1128 comment](https://github.com/libretro/virtualjaguar-libretro/pull/1128#issuecomment-6545542038) |
| 2026-10-31 | a661028c | Studio | **G3 transitions,** Match display, yarc (`mbt-t3*`) | **Met.** sleep: B → A (no_vsync) → B, 0 underruns. fs: locks to B. link: both copies `mode A (link)`. halt (5 s, then Continue): ends B locked, ticks dropped 0, **1 underrun** across the halt |
| 2026-10-31 | 1b18d491 (phase 1) vs 43383a05 (develop) | Studio | **G3 "mode A vs phase 1":** A and off, yarc and Cybermorph, builds in order p1 dv dv p1 (`mb-p5-*`) | **Met: no mode A regression.** A − off event→present, p1 → dv: yarc +1.96 → −1.04 ms, Cybermorph +1.94 → +2.47 ms (dv's two runs: +1.5, +3.4). Mode A repeats in 60 s: yarc 26, 16 → 3, 1; Cybermorph 25, 19 → 64, 58. Inline Cybermorph rose as much (21, 15 → 46, 51), with the same change in frame cost in both arms (p1 4.8 → 3.0 ms, end-due spread 5.3 → 7.5 ms): the core or content changed between the builds, not the thread. A ÷ off repeats is about 1.2 on both builds. Both builds' traces read by the same summary script |
| 2026-10-31 | 43383a05 | Studio | **G3 halt, inline arm** | **Not measurable.** Inline, a halt runs in the core's nested `haltedFrame` loop; `--perf-halt`'s Continue is in the outer loop, so it never comes. That nested loop still runs the UI, so the debugger's Continue button works. Run stopped after 1 h 40 min |
| | 43383a05 | Studio | **G3 halt,** A vs B (`mbt-p5-{A,B}-halt`) | Pending |
| | 43383a05 | Studio | **G3 phase 3 gate** (`emu_thread_gate_check.sh`) | Pending here. Run again on the 4.0 candidate |
| | | Windows, Linux, 120 Hz | **G4** | Pending: request on #1039 |
| 2026-10-31 → 2026-11-14 | nightlies | | **G5** | Window open from the 5a merge |

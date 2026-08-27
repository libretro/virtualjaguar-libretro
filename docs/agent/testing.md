# Testing detail

Detail for [`CLAUDE.md`](../../CLAUDE.md). Local-only RetroAchievements validation — no RA
account/API/server. `test/tools/test_rcheevos_e2e.sh` downloads pinned `RCHEEVOS_REF` and
verifies `rc_libretro` mapping (`RC_CONSOLE_ATARI_JAGUAR`) matches host RAM.

## Shared test harness (`test/harness/`)

New tests should use `test/harness/harness.h` — shared lib eliminating dlopen/init/run
boilerplate (see header's AGENT QUICK-START). Features:

- Common CLI: `--json`, `--frames N`, `--bios`, `--option K=V`, `--quiet`.
- Scripted input: `--press FRAME:BUTTON[:HOLD]` (repeatable; buttons `up down left right a b c
  pause option 0-6`) — enough to navigate menus into gameplay headlessly. Programmatic:
  `harness_press()` / `harness_input_cb`.
- Log verbosity env: `VJ_HARNESS_LOG_INFO=1` passes core INFO (CD trace dumps),
  `VJ_HARNESS_LOG_DEBUG=1` adds DEBUG (per-call CD HLE trace).
- Automatic audio/video stats; `harness_dlsym()` probes internal state; `--json` machine output.
- Probe modules: `dsp_probe.h` (DSP registers, PC escape, LTXD ratio, RAM dumps),
  `timing_probe.h` (per-frame halflines, cycles, wall time, speed ratio).

Build: `cc -O2 -Wall -std=c99 $(INCFLAGS) -o test_foo test_foo.c test/harness/harness.c
[probe.c...] -ldl -lm`. New probe: create `test/harness/foo_probe.h` + `.c`, resolve via
`harness_dlsym()`.

## Key harnesses

- `test/regression_test.sh` — screenshot regression vs `test/baselines/` via miniretro (built
  from source first run; `MINIRETRO_BIN` env to skip). Baselines **not committed** — no-baseline
  ROM reports `NEW` and prints the `cp` command; first run on fresh clone establishes them.
- `test/tools/test_dsp_audio_diag.c` — DSP audio diagnostic (`make dsp-diag DSP_DIAG_ROM=path`);
  detects PC escape, bank init failures, silent LTXD.
- `test/tools/test_frame_timing.c` — per-frame timing (`make frame-timing FRAME_TIMING_ROM=path`);
  halflines/cycles/VBlanks per frame, wall-clock speed ratio, anomaly detection. `--csv`, `--json`.
- `test/test_audio_clipping.c` — detects loud-broken audio (saturation density, run length,
  sustained loud RMS). Catches Skyhammer/IS2 "saturated square wave".
- `test/test_audio_presence.c` — counterpart: asserts audio present in a known-good envelope
  (RMS in `[floor, ceiling]`, onset reached, no long zero runs). **Required to catch the
  silencing-regression class** where a "fix" drops RMS to zero (clipping passes, game silent).
  Iron Soldier 1 baseline: RMS ~1175 on develop.
- `test/tools/hires_box_check.c` — internal-res (hi-res Stage 1) inertness gate: every 2x frame
  must be exact 2×2 box replication of a 1x reference (`frame_hash_ab` CSV). Frames adjacent to a
  presented-dimension change excluded.
- `test/tools/hires_state_digest.c` — savestate FNV digests at frames 300/600/900; diff a 1x run
  vs `virtualjaguar_internal_resolution=2x` to prove the machine can't see the option.
- `test/tools/test_memory_map.c` — asserts `SET_MEMORY_MAPS`,
  `SET_SUPPORT_ACHIEVEMENTS=true`, descriptor layout.
- `test/tools/blit_memo_verify` + `test/tools/blit_memo_sweep.sh` — soundness gate for blit
  memoization (`virtualjaguar_blit_memo`, #411, `docs/blit-memo.md`). Verify mode never skips:
  runs every would-be skip live and compares write log + post-launch state. **Run before adding
  `virtualjaguar_blit_memo` to any titledb row.** Exit 3 = "thin" (too few checks) — a title
  that never repeats a blit stream is unverified, NOT clean; drive into gameplay with `--press`.
  No public ROM repeats a stream → no CI gate; private-corpus sweep is the gate.
- `test/tools/test_blitter_compare` — fast vs accurate blitter diff. Not in default `make`:
  `cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/test_blitter_compare
  test/tools/test_blitter_compare.c -ldl`.
  Usage: `<core.so|.dylib> <rom> [frames] --load-state <file> [--frame-window F L]
  [--cmd-filter MASK VAL] [--verbose-dump]` (note `--load-state`, not `--savestate`).
- `test/test_dsp_mac40.c` — DSP 40-bit MAC accumulator (`dsp_acc40.h`).
- `test/tools/dram_scale_sweep.sh` — **gate for any GPU/68K timing-model change** (#406). Sweeps
  `VJ_DRAM_SCALE` 1..16 with `dram_timing` + `gpu_pipeline_timing` on; asserts a per-window
  *liveness floor* on Doom's demo (default 30 flips per 300 fields; healthy 90-150), not merely
  "flips != 0". Exit 77 = private ROM absent (skip, never silent pass). Timing wedges relocate
  rather than disappear — a single-scale run proves nothing (#406 wedged at scales 4,8 before
  Verilator constants, at 3 after). Env: `VJ_SCALES VJ_WINDOW VJ_MIN_FLIPS VJ_WARMUP_W`.
- `test/tools/corpus_ab_sweep.sh` — A/B two cores across a ROM corpus, comparing the **full**
  framebuffer hash stream: `corpus_ab_sweep.sh <base-core> <new-core> [rom-dir]`
  (`FRAMES`, `TMO`, `VJ_ROMS`). Use it before merging anything that touches rendering. Two guards,
  both earned: **every run is under a timeout** (Chroma-Luma Color Pick hangs `retro_run` forever,
  #659 — the first sweep of this kind wedged on it for 2h23m emitting nothing, which is how #641's
  regression reached develop), and it compares the hash stream rather than the transition **count**
  (Super Burnout reads 143 transitions on both sides of #632 while rendering different pixels, so a
  count comparison calls a real change "no change"). Exit 1 on any SKIP: an unchecked ROM must not
  look like a clean sweep.
- `test/sram_test.sh` — SRAM round-trip.
- `test/tools/cd_boot_matrix.sh` — per-title CD boot-stage matrix (HLE + BIOS) vs
  `docs/cd-boot-matrix.md`. Env: `CD_MATRIX_FRAMES CD_MATRIX_TIMEOUT CD_MATRIX_MAX_RUNS
  CD_MATRIX_LOGDIR CD_MATRIX_OUT CD_MATRIX_ROMS_ROOT`; chunked/resumable. Rows stamped with build
  id (`<!-- build:<rev> -->`); resume skips only same-build rows, re-runs others. (Resuming from
  an older build used to resurrect ancient rows as "fresh" — the phantom Battle Morph bios
  `? (pc_escape)` `final_pc=$8FBFB758` was such a stale row.)
- CD trace ring: option `virtualjaguar_cd_trace` (or env `VJ_CD_TRACE=1`) records
  `DSA_TX/DSA_RX/SEEK_START/SEEK_DONE/FIFO_FILL/FIFO_DRAIN/STOP/HLE_READ`; dumped on
  `cd_seek_wedge` or on request.
- `test/tools/cd_wedge_probe.c` — catches intermittent CD lockups: detects a frozen framebuffer
  (`--arm N --freeze-frames N`, script repro with `--press`), dumps 68K regs, pcQueue traceback
  ring, CD counters + trace ring, RAM hexdump around stuck PC; exits 42 when caught. `--ram-dump
  BASE` writes full 2MB main-RAM + GPU-RAM at wedge; `--snap FRAME` (repeatable, `--snap-prefix
  BASE`) for clean-vs-wedged diffing. Found Hover Strike instant-CD_read code-stomp and B-skip
  stale-GPU-IRQ-latch corruption. Caveat: a static loading screen can false-positive short freeze
  windows — confirm with `--freeze-frames 900`.
- `test/tools/cd_visual_verify.c` — automated visual+audio verify for CD titles (`make cd-visual
  CD_VISUAL_DISC=<image.cue>`): per-second frame-motion timeline, non-black coverage, audio RMS,
  periodic PPM screenshots (`sips -s format png` to view). Replaces most "boot it and look"
  checks. Headless read-path caveat still applies for final sign-off.
- `test/tools/netlink_discover_probe.c` (`--selftest`) — validates LAN link-discovery beacon
  (`src/jerry/jlink_discover.c`). Lives inside the core-adjacent binary on purpose: macOS grants
  Local Network permission **per binary**, so a probe under `python3`'s identity can pass while
  the shipped binary is denied (bit the #500 CI fix once). `netlink_discover_pair.sh` drives two
  instances, PID-spreading the discovery port (`VJ_DISC_PORT`, `JLinkDiscPort()`) so parallel CI
  runs don't collide on `JLINK_DISC_PORT` (42170). `--selftest` exits 77 when same-host broadcast
  isn't available in the sandbox (some macOS CI runners) — a skip, not a failure.
  `netlink_rebuild_witness.c` proves the `SET_CORE_OPTIONS_V2` rebuild path (option-visibility
  re-push on link-mode change) executes.

## vjtrace flight recorder (dev-build-only)

Event ring + memory watch (`src/core/vjtrace.h`/`.c`): `make TEST_EXPORTS=1` defines
`VJ_TRACE`; plain `make` compiles every `VJT_*` to nothing and exports zero vjtrace symbols
(costs nothing shipped). Every harness inherits tracing via `trace_probe_attach()`
(`test/harness/trace_probe.h`; `test/tools/vjtrace_smoke.c` = generic driver). Flags:
`--trace-out FILE` (binary ring at exit), `--watch A[:LEN][:r|w|rw]` (memory watch, max 16),
`--field-csv FILE` (one row/frame: IRQ/GPU/OP/blitter counters, pad state, FB hash),
`--snap FRAME` (VJSN snapshot), `--mark FRAME:TAG` (host marker event).

Analyzer CLIs (offline, no core/harness dep): `trace_dump` (print/filter a ring by
`--type/--who/--frame`), `trace_diff` (structural diff of two rings, tolerant of frame offset),
`trace_memdiff` (diff two `--snap` VJSN files by section), `field_diff` (row/col diff of two
`--field-csv`). `test/tools/vjtrace_selftest.sh` is part of `make test`.

**Ring sizing:** default `VJ_TRACE_RING` = 1<<20 records; fills fast (OP_OBJECT/OP_LIST_START
emit unconditionally, ~2,578 events/frame on `test/roms/yarc.j64`, ROM-dependent). A 1800-frame
run needs ~4.6M records → set `VJ_TRACE_RING=6000000`. Both `trace_dump`/`trace_diff` print
`WARNING: ring wrapped` to stderr when a dump lost events — don't treat a clean-looking dump as
complete without checking.

**Coverage caveats** (authoritative in vjtrace.h WATCH COVERAGE block): GPU/DSP writes to their
own local RAM (`$F03000-$F03FFF` / `$F1B000-$F1CFFF`) never route through watched dispatch —
invisible to `--watch`; only accesses through `JaguarRead/WriteByte/Word/Long` and the 68K bus
fast path are covered. A 32-bit 68K access = ONE record for main RAM/cart ROM but TWO (at `addr`
and `addr+2`) for TOM/JERRY/CDROM/unknown → always round a watch's low bound down to a multiple
of 4.

## Audio / DSP work — required tests

**Any change to `src/jerry/dac.c`, `src/jerry/dsp.c`, the HLE BIOS DSP/audio path in
`src/core/jaguar.c`, or the DSP IRQ return-address logic MUST clear BOTH audio tests.** A
clipping check alone is insufficient: PR #170 (closed) took Iron Soldier 2 from 17% saturated to
RMS=521 (silent) and the clipping test passed because silence has 0% saturation.

Required before declaring an audio change done:

1. `make TEST_EXPORTS=1 test` exits 0. Both `test_audio_clipping` and `test_audio_presence` are
   in the suite. Presence on IS1 uses develop's envelope (`--rms-floor 200 --rms-ceiling 25000`);
   if your change moves IS1's RMS outside that band you've changed audio behavior — verify intent.
2. Sanity-check that previously-clipping titles (Skyhammer, IS2) didn't go loud-broken → silent-
   broken. Both fixed as of 2026-08-05 by the MMULT secondary-bank fix; both assert clean.
   Skyhammer `(World).j64`: 0.000% saturated, window RMS 3079.8, first audio frame 171.
3. **Verify in RetroArch on a real game.** Headless can't tell music from structured noise at the
   right RMS, nor catch BIOS-mode crashes (PR #170's BIOS crash + HLE silence were invisible to
   the suite).

Do not relax thresholds in `test_audio_clipping.c` / `test_audio_presence.c` to pass. A real fix
that legitimately quiets a known-broken title is a separate deliberate baseline update — call it
out in the commit.

## Runtime crash watchdog (`src/core/crash_detect.c`)

Runs once/frame from `retro_run`, logs to RetroArch log on these signatures. Option
`virtualjaguar_crash_detect = enabled` (default) / `disabled` / `verbose` (adds a state
heartbeat every 600 frames). Cost enabled: one indirect call + ~256-px hash/frame.

- `gpu_pc_escape` — GPU PC outside `[$F03000,$F03FFF] ∪ [$0,$E3FFFF]` (matches JaguarReadX
  decoding: main RAM mirrors bottom 8MB, cart ROM, boot ROM).
- `dsp_pc_escape` — DSP PC outside `[$F1B000,$F1CFFF] ∪ [$0,$E3FFFF]`.
- `gpu_wedge` / `dsp_wedge` — flagged running but **zero opcodes** for ≥180 / 600 frames
  (`gpu_exec_opcode_count` / `dsp_exec_opcode_count`). A stable sampled PC alone is NOT a wedge:
  deterministic slice budgets land the per-frame PC on the same instruction of a healthy
  wait/spin loop (Super Burnout spins ~446k GPU ops/frame at one sampled PC — the #378 false
  positive; regression `test/tools/test_wedge_spin`).
- `video_stall` — FB hash unchanged 300 frames while a processor runs.
- `cd_seek_wedge` — a CD seek started but FIFO drain frozen 300 frames while a processor runs;
  dumps the CD trace ring. **Known benign:** a title going CD-idle >5s after a transfer fires
  too — Myst (bios) fires during the intro movie's ~6s all-black pause (drain parked at payload
  end LBA 21189, movie from RAM, clock ticking); HLE shows the same black window (fires
  `video_stall`). Corroborate before treating a lone line as a wedge.

Triaging "X crashes/hangs/black screen": the RetroArch log shows the signature — no save state
or input recording needed. **Add new signatures here when you find a recurring failure mode not
already covered**; don't sprinkle one-off `LOG_ERR` across subsystem files.

## Acid suite CI gating

"Acid suite (linux x86_64)" can show `conclusion: failure` for two unrelated reasons — read the
summary first:

- `make -C test/acid test` exits non-zero by design (returns FAIL count). Job uses `set +e` and
  gates on `check-baseline.py`. Real regression = `Regressions: N` (N>0) in `acid-summary.txt`.
- Conclusion `failure` with `OK: no regressions` in step output = the artifact-upload step timed
  out (`Operation could not be completed within the specified time`). Re-run the job; no code action.

## Performance / profiling

`make benchmark` runs `test/tools/test_benchmark` headless vs a fixed ROM (default
`test/roms/yarc.j64`, 600 frames), prints FPS / ms-per-frame. Use as a same-host
commit-to-commit delta — don't compare across machines. Full guide: `docs/profiling.md`
(Instruments / `perf` / flame graphs, SIMD A/B knob).

**Measurement hazards:** concurrent `make` swaps the dylib mid-sweep; sequential A/B measures
host drift (an -O3 "+12.5%" flipped to -11.7% when interleaved) — always A/B/B/A, check `uptime`
first.

## Headless framebuffer caveat

The miniretro harness in `test/regression_test.sh` doesn't expose the same composited
framebuffer RetroArch reads. Symptom: `jag_240p_test_suite` main menu shows ~1k non-black pixels
via miniretro vs tens of thousands via RetroArch. Treat as a **headless read-path / presentation
bug** (OP+blitter output vs what the host reads), not a 240p timing or `__muldi3` perf bug.
Verify against RetroArch before treating a regression as real.

---
name: vj-debug
description: Use when debugging Virtual Jaguar emulation bugs — wrong game speed, input repeating/dropping, hangs, black screens, audio that's silent or clipped, wrong pixels, or two configs/builds behaving differently — to pick the right existing probe/harness/analyzer instead of writing a new one from scratch. Also use when a session doesn't know what debugging tooling exists in test/tools/ and test/harness/.
---

# Virtual Jaguar debug toolbox

This repo has dozens of purpose-built headless probes under `test/tools/` (built on
`test/harness/`) plus a flight-recorder (vjtrace) for "who touched this
address" questions. Reach for one of these before writing a new probe or
reasoning from source code alone — most emulation-accuracy bugs here were
root-caused by one of the tools below, not by re-reading `src/`.

## Preamble (every invocation needs this)

```bash
export DEVELOPER_DIR=/Library/Developer/CommandLineTools   # avoid the App Management prompt
make TEST_EXPORTS=1 -j$(getconf _NPROCESSORS_ONLN)          # wide ABI + VJ_TRACE, needed by every dlopen-based tool
export VJ_EXPECT_BUILD=$(./scripts/build-id.sh)              # fail loud on a stale/rebuilt-out-from-under-you dylib
```

Most tools in `test/tools/` are NOT built by plain `make`/`make test` — build
them by hand with the `cc` line in their own header comment (a few, like
`i2s_lag_probe` and the audio tests, do have Makefile rules; check `grep
<toolname> Makefile` if unsure). All of them accept `<core> <rom>` as the
first two positional args and default to `./virtualjaguar_libretro.dylib` in
the current directory when omitted.

## By symptom

### Runs at the wrong speed / input repeats or drops

- `test/tools/present_rate_probe` — fields-per-flip: how often the image the
  host is actually shown changes, independent of any internal game counter.
  ```
  cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/present_rate_probe \
     test/tools/present_rate_probe.c test/harness/harness.c -ldl -lm
  ./test/tools/present_rate_probe ./virtualjaguar_libretro.dylib rom.jag --window 300
  ```
  Reach for this when a game "feels" too fast/slow and you need a frame-rate-agnostic number (Doom's demo measured 2.00 fields/flip vs ~4 on hardware, #401).
- `test/tools/menu_step_probe --loop-rate` — how many menu items ONE input
  tap moves, and the underlying loop's passes-per-field.
  ```
  cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/menu_step_probe \
     test/tools/menu_step_probe.c test/harness/harness.c test/harness/trace_probe.c -ldl -lm
  ./test/tools/menu_step_probe ./virtualjaguar_libretro.dylib rom.jag --addr 05170C --hold 4 --loop-rate
  ```
  Reach for this when the bug is specifically in a MENU (not gameplay) — menu loops often have no tick gate and pace at renderer speed, which a gameplay-timing fix will not touch (#399). `--scan` (no `--addr`) locates the cursor variable empirically if you don't know it yet.
  **Items-per-tap is CONFOUNDED, not a pacing measure by itself** (established 2026-08-12). Two independent effects inflate it on Doom's main menu: (1) the menu is circular and the probe only presses DOWN, so a wrap (e.g. item 2 -> item 0) is one physical move — the probe now takes `--menu-items N` (default 3, Doom's `NUMMENUITEMS`) and computes the forward-circular distance `(v - prev_val + N) % N` instead of `|v - prev_val|`, so this part is fixed; (2) Doom's `m_main.c:141-142` has an INTENTIONAL fast level-select (`if (cursorpos == level && movecount == 3) movecount = 0;`) that legitimately moves the cursor twice for any tap that transits the `level` item, at ANY loop pass rate — confirmed via frames 467/470 (3 fields apart) for such a tap vs. one move otherwise. So even after the wrap fix, items-per-tap > 1.0 does NOT by itself mean the menu loop runs too fast. The valid pacing measure is loop passes per field: watch gamevbls at $040850 (written once per pass at pc=$9B76) via `--field-csv`/`--watch`, or `--loop-rate`. Measured: 1.00 pass/field under held input — no pass-rate bug found; the *hardware* pass rate is not established by this tool.
- vjtrace `--field-csv` (any harness tool via `trace_probe_attach()`, or the
  generic driver `test/tools/vjtrace_smoke.c`) — one CSV row per frame with
  IRQ/GPU/OP/blitter counts, pad bitmask, and framebuffer hash.
  ```
  cc -O2 -Wall -std=c99 -I. -I./libretro-common/include -o /tmp/vjt_smoke \
     test/tools/vjtrace_smoke.c test/harness/harness.c test/harness/trace_probe.c -ldl -lm
  /tmp/vjt_smoke ./virtualjaguar_libretro.dylib rom.jag --frames 600 --field-csv /tmp/f.csv
  ```
  Reach for this when you need per-frame counts of IRQs/OP objects/blits to spot something firing 2x per field instead of once (the exact shape of the Doom menu 2x bug).

### Who wrote this address, when, from where

- vjtrace `--watch A[:LEN][:r|w|rw]` + `test/tools/trace_dump`.
  ```
  cc -O2 -Wall -std=c99 -I. -I./libretro-common/include -o /tmp/vjt_smoke \
     test/tools/vjtrace_smoke.c test/harness/harness.c test/harness/trace_probe.c -ldl -lm
  cc -O2 -Wall -std=c99 -I. -o /tmp/trace_dump test/tools/trace_dump.c

  VJ_TRACE_RING=6000000 /tmp/vjt_smoke ./virtualjaguar_libretro.dylib rom.jag --frames 1800 \
     --watch 0x0A0000:4:w --trace-out /tmp/watch.vjtr
  /tmp/trace_dump /tmp/watch.vjtr --type WATCH_WR    # add --who NAME or --frame A:B to narrow
  ```
  The default 1M-record ring wraps well before 1800 frames — measured
  ~2,578 events/frame on `test/roms/yarc.j64` (ROM-dependent; scale from
  your own measurement), so 1800 frames needs ~4.6M records; the
  `VJ_TRACE_RING=6000000` above gives headroom. If a dump you're reading
  DID wrap, `trace_dump`/`trace_diff` print `WARNING: ring wrapped` to
  stderr — never ignore it.
  Each record shows `pc` (writer's instruction-start PC — 0 for OP/BLITTER, which never resolve one), `who` (M68K/GPU/DSP/OP/BLITTER/...), `f`/`hl` (frame/halfline), `val`.
  **Coverage caveat**: GPU/DSP writes to their OWN local RAM ($F03000-$F03FFF / $F1B000-$F1CFFF) never route through the watched dispatch and are invisible here — only accesses through the Jaguar memory dispatch and the 68K's own bus fast path are covered. A 32-bit 68K access produces ONE record for main RAM/cart ROM but TWO for TOM/JERRY/CDROM/unknown, so always round a watch's low bound DOWN to a multiple of 4 or you can silently miss the RAM case. Full details: the WATCH COVERAGE block in `src/core/vjtrace.h` and the WORKED EXAMPLE in `test/harness/trace_probe.h`.
  Reach for this when you need to name the exact instruction that last touched a byte, not just "something changed it".

### Two configs/builds behave differently

- Same input on both arms, THEN diff — never diff a single divergent run
  against nothing: if the two baseline runs of the SAME config aren't
  identical, cross-arm divergence is noise.
  ```
  cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include -o test/tools/frame_hash_ab \
     test/tools/frame_hash_ab.c test/harness/harness.c -ldl -lm
  ./test/tools/frame_hash_ab ./virtualjaguar_libretro.dylib rom.jag --csv a.csv --frames 1800 --option virtualjaguar_dsp=enabled
  ./test/tools/frame_hash_ab ./virtualjaguar_libretro.dylib rom.jag --csv b.csv --frames 1800 --option virtualjaguar_dsp=disabled
  ```
  Then diff whichever CSV/dump pair applies:
  - `field_diff` — generic row/column CSV diff (works on `frame_hash_ab`'s
    `--csv` output above, or on a `--field-csv` from `vjt_smoke`/
    `trace_probe_attach`; it only requires both files to share a header):
    ```
    cc -O2 -Wall -std=c99 -I. -o test/tools/field_diff test/tools/field_diff.c
    ./test/tools/field_diff a.csv b.csv
    ```
  - `trace_diff` — structural diff of two `--trace-out` event rings by
    `(type, who, addr)`, tolerant of a frame offset between the two runs:
    ```
    cc -O2 -Wall -std=c99 -I. -o test/tools/trace_diff test/tools/trace_diff.c
    ./test/tools/trace_diff A.vjtr B.vjtr [--types LIST]
    ```
  - `trace_memdiff` — diff two `--snap` VJSN state dumps section by section
    (MAINRAM/GPURAM/DSPRAM/TOMREG/JERRYREG/REGS68K/REGSGPU/REGSDSP):
    ```
    cc -O2 -Wall -std=c99 -I. -o test/tools/trace_memdiff test/tools/trace_memdiff.c
    ./test/tools/trace_memdiff A.vjsn B.vjsn [--section NAME]
    ```
  Reach for this when a core option, clock scale, or platform (iOS vs macOS) changes behavior and you need to name WHICH subsystem's events first diverge, not just that the output differs.

### What was the CPU/GPU/DSP doing when X happened

- `vjtrace_backtrace(who, out, maxn, count)` — a C API (`src/core/vjtrace.h`,
  confirmed exported: `nm -gU virtualjaguar_libretro.dylib | grep
  vjtrace_backtrace`), not a standalone binary: resolve it with
  `harness_dlsym(&cfg, "vjtrace_backtrace")` inside a purpose-built tool to
  pull the last up-to-1024 PCs a processor executed (`who` ∈ M68K/GPU/DSP).
  GPU/DSP are fill-tracked (never zero-filled or stale before the ring has
  real entries); the M68K case reads the existing `pcQueue` ring, which has
  no fill counter of its own, so it can return zero/stale placeholder
  entries for roughly the first 1024 68K instructions of emulation — a
  window that closes within a few dozen microseconds of boot and doesn't
  recur. Reach for this when writing a NEW probe that needs "how did we get
  here" for any of the three processors.
- `test/tools/m68k_pc_histogram` — hottest 68K PCs over a window; a spin loop
  names exactly what the CPU is waiting on.
  ```
  cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/m68k_pc_histogram \
     test/tools/m68k_pc_histogram.c test/harness/harness.c -ldl -lm
  ./test/tools/m68k_pc_histogram ./virtualjaguar_libretro.dylib rom.jag --frames 600
  ```
  Reach for this when a frame loop seems to be waiting on something and you need to know what (GPU completion, DSP handshake, a tick gate) rather than guessing from the source.
- `test/tools/gpu_disasm_dump` — dump + disassemble a span of GPU local RAM
  plus report GPU/68K PC, to identify which mailbox flag a spinning GPU
  kernel is polling.
  ```
  cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/gpu_disasm_dump \
     test/tools/gpu_disasm_dump.c test/harness/harness.c -ldl -lm
  ./test/tools/gpu_disasm_dump ./virtualjaguar_libretro.dylib rom.jag --at F03410 --words 16
  ```
  Reach for this when the GPU is stuck (or you suspect it is) and you need to read its actual instructions, not just its PC.

### Hang / black screen

- `crash_detect` signatures in the RetroArch log (core option
  `virtualjaguar_crash_detect`, default enabled) — `gpu_pc_escape`,
  `dsp_pc_escape`, `gpu_wedge`/`dsp_wedge`, `video_stall`, `cd_seek_wedge`.
  No save state or input recording needed; the log line at the moment of
  failure names which subsystem broke. Full signature list: CLAUDE.md
  "Runtime crash watchdog".
  Reach for this FIRST on any "X hangs / crashes / goes black" report — it's
  a passive log read, cheaper than any probe below.
- `test/tools/cd_wedge_probe` — for CD titles specifically: detects a frozen
  framebuffer, dumps 68K registers, the pcQueue traceback, CD counters + the
  CD trace ring, and a RAM hexdump around the stuck PC. `--freeze-frames 900`
  to rule out a false-positive on a static loading screen.
  ```
  cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/cd_wedge_probe \
     test/tools/cd_wedge_probe.c test/harness/harness.c -ldl -lm
  ./test/tools/cd_wedge_probe ./virtualjaguar_libretro.dylib disc.cue --arm 300 --freeze-frames 900
  ```
  Reach for this when crash_detect's `cd_seek_wedge`/`video_stall` fires on a CD title and you need the CD trace ring + register dump, not just the log line.

### Audio wrong

- `./test/test_audio_clipping` + `./test/test_audio_presence` — REQUIRED
  PAIR for any `src/jerry/dac.c`/`dsp.c` change; clipping alone misses the
  silencing-regression class (a "fix" that drops RMS to zero still passes
  clipping). Built by `make TEST_EXPORTS=1 test`; run individually:
  ```
  ./test/test_audio_clipping ./virtualjaguar_libretro.dylib rom.jag --label "My Title" --quiet
  ./test/test_audio_presence ./virtualjaguar_libretro.dylib rom.jag --rms-floor 200 --rms-ceiling 25000 --quiet
  ```
- `dsp_probe` (`test/harness/dsp_probe.h`, a harness module not a standalone
  binary) — DSP PC/FLAGS/CONTROL/register banks/RAM/LTXD. Consumed by:
  ```
  make dsp-diag DSP_DIAG_ROM=path/to/rom.jag
  ```
  Reach for this when clipping/presence says "wrong" and you need to see WHY: PC escape, bank init failure, silent LTXD.
- `test/tools/i2s_lag_probe` — I2S resample cursor drift/resync detector
  (periodic audible skip, #393).
  ```
  make TEST_EXPORTS=1 -j$(getconf _NPROCESSORS_ONLN)
  cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/i2s_lag_probe \
     test/tools/i2s_lag_probe.c test/harness/harness.c -ldl -lm
  VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/i2s_lag_probe \
     ./virtualjaguar_libretro.dylib rom.jag --frames 3600 --window 60
  ```
  Reach for this when audio has a periodic click/skip rather than being silent or clipped.

### Pixels wrong

- **What is the OP actually drawing?** `test/tools/op_list_dump` — decodes the
  live Object Processor display list into JTRM fields (TYPE, YPOS, HEIGHT,
  XPOS, IWIDTH, DWIDTH, DEPTH, PITCH, HSCALE, VSCALE, REMAINDER, DATA + raw
  phrases + BRANCH link/cc).
  ```
  cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include -o test/tools/op_list_dump \
     test/tools/op_list_dump.c test/harness/harness.c -ldl -lm
  # find the list base first: OP_OBJECT events carry the object phrase address
  /tmp/vjt_smoke ./virtualjaguar_libretro.dylib rom.jag --frames 900 --trace-out /tmp/t.vjtr
  ./test/tools/trace_dump /tmp/t.vjtr --type OP_OBJECT --frame 880:881
  OPLIST_BASE=13BA00 OPLIST_COUNT=26 ./test/tools/op_list_dump ./virtualjaguar_libretro.dylib rom.jag --frames 900 --quiet
  ```
  Reach for this **first** on any "wrong size / wrong position / missing plane"
  bug, before reading `src/tom/op.c`. It separates "the OP is misbehaving" from
  "the OP is faithfully drawing bad data the game wrote" — which are opposite
  fixes. On issue #354 it showed the suspect object's own HSCALE=$10 fully
  explains the render, exonerating the OP.
  **Do not read HSCALE/VSCALE backwards**: JTRM Rev 8 defines them as
  *destination per source*, so below $20 SHRINKS. Two separate sessions have now
  lost time to the inverted reading; `docs/jtrm-object-processor.md` carries the
  verbatim wording.
- `test/tools/frame_hash_ab` — per-frame framebuffer-hash CSV; compares WHEN
  a distinct image state first appears across two arms (see "Two
  configs/builds behave differently" above for the build/run lines).
  Reach for this for "does timing/an option change WHEN things render", not exact pixel correctness.
- `test/tools/hires_box_check` — Stage-1 hi-res inertness gate: every 2x
  frame must be an exact 2x2 box replication of the 1x reference.
  ```
  cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include -o test/tools/hires_box_check \
     test/tools/hires_box_check.c test/harness/harness.c -ldl -lm
  ./test/tools/frame_hash_ab ./virtualjaguar_libretro.dylib rom.jag --csv ref.csv --frames 600
  ./test/tools/hires_box_check ./virtualjaguar_libretro.dylib rom.jag --ref ref.csv --frames 600
  ```
  Reach for this specifically for a hi-res/internal-resolution regression, not general pixel bugs.
- `make cd-visual CD_VISUAL_DISC=<image.cue>` (`test/tools/cd_visual_verify`)
  — per-second motion timeline, non-black coverage, audio RMS, periodic PNG
  screenshots you can Read directly. Replaces most "boot it and look" checks
  for CD titles; the headless read-path caveat below still applies for final
  sign-off.

## Traps (issue #408 and prior sessions' mistakes — read before trusting a measurement)

- **Fixed-frame scripted input (`--press F:BTN`) is INVALID for timing
  measurement.** A hardcoded frame number silently measures nothing once
  the timing you're testing changes (the press lands on a different game
  state, or misses the input-accepting window entirely) — this produced a
  convincing FAKE fix during the #399 investigation. Drive input from
  observed game state instead: poll a RAM address / menu cursor and act
  when it reaches the expected value (`menu_step_probe`'s `--scan` mode is
  the pattern — locate the state variable first, then act on it).
- **The demo and the menu are different code paths.** In Doom, `M_Drawer`
  never calls `I_Update()`, so measuring the attract demo says nothing
  about menu behavior, and vice versa. Don't generalize a fix or a
  measurement across the two without re-testing both.
- **Stale iOS `.o` files break the macOS link.** Cross-platform object
  files left in the tree from a previous iOS build can get linked into a
  macOS build; a complete iOS-built `.dylib` is fine for macOS harnesses
  (same arm64), but mixed `.o` linking is not.
- **Concurrent agents must use separate worktrees.** A shared checkout's
  `.dylib` can be rebuilt/swapped out from under a running measurement by
  another agent's `make` — always measure from a private worktree + binary,
  and use `VJ_EXPECT_BUILD` to catch a swap that did happen.
- **`test/roms/private` is a symlink to an irreplaceable ROM collection
  OUTSIDE the repo.** Never `git clean -xfd` (or any recursive delete) at
  the repo root — the collection was destroyed this way once already.

## Headless framebuffer caveat

The miniretro harness used by `test/regression_test.sh` doesn't expose the
same composited framebuffer RetroArch reads — a headless pixel-count
mismatch can be a read-path bug, not a real rendering regression. Verify
against RetroArch before treating it as one.

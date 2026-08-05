# CLAUDE.md

Guidance for Claude Code working in this repository.

## Project

Virtual Jaguar libretro core — Atari Jaguar emulator on the libretro API. C, GPLv3. Upstream: `http://shamusworld.gotdns.org/git/virtualjaguar`.

## Branching

GitFlow: branch new work off **`develop`** (the integration branch); `master` is release-only (tagged commits, hotfix merges, release-branch merges). PRs targeting `master` get auto-warned by `.github/workflows/warn-pr-base.yml` — retarget to `develop` unless the source branch is `hotfix/*` or `release/*`. Full flow in [`docs/release-process.md`](docs/release-process.md).

## Build

```bash
make -j$(getconf _NPROCESSORS_ONLN)          # Build (auto-detects platform)
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1  # Debug (-O0 -g)
make clean
make platform=ios-arm64                       # Cross-compile target
```

Output: `virtualjaguar_libretro.{dylib,so,dll}`. CI: `make -j4` on Ubuntu (GCC) and macOS (Clang) plus `test/regression_test.sh` screenshots.

## C89 / GNU89 — strict

The libretro buildbot uses MSVC on Windows. CI has a `c89-lint` job. Run `bash scripts/c89-lint.sh src/YOURFILE.c` before pushing.

- **No mid-block declarations.** All vars at top of block, before any statement. Most common violation.
- `//` comments allowed (GNU89), but prefer `/* */` for new code.
- No C99: no `for (int i…)`, no compound literals, no designated initializers, no VLAs.
- Exempt (see `scripts/c89-lint.sh::skip_file`): `src/m68000/cpu*.c` and `src/m68000/read*.c` (UAE 68K), `src/bios/jag*bios*.c` (bin2c hex tables), `src/tom/blitter_simd_{sse2,neon}.c` (platform intrinsics), `test/tools/test_rcheevos_e2e.c` (rcheevos-dependent), `test/tools/flicker_detect.c` (diagnostic).

## Hardware model

Four processors, unified memory map, big-endian. `GET16/GET32/SET16/SET32` macros byte-swap on LE hosts. Address-range map is documented in `src/core/vjag_memory.c` (header comment); the dispatch logic lives in `src/core/jaguar.c`. RAM 0x000000 (2 MB), cart 0x800000, TOM regs 0xF00000, JERRY regs 0xF10000.

**Authoritative hardware reference:** The Jaguar Technical Reference Manual (JTRM) is the ground-truth spec for all emulation decisions. If you have a local copy, place it in `docs/atari-jaguar-1999/` (gitignored — copyrighted, not distributed). Key sections: Software Reference (register maps, PIT formulas, ISA), Technical Reference (clock hierarchy, bus timing), Hardware Bugs & Warnings (silicon errata). **Always verify clock rates and register behavior against the JTRM** — do not trust comments in the source code, which have historically been wrong (e.g. PIT clock was incorrectly halved). System clock: 26.590906 MHz NTSC / 26.593900 MHz PAL. 68K runs at half (~13.3 MHz). GPU/DSP/PIT run at the full system clock rate.

- **68000** (13.3 MHz, `src/m68000/`) — main CPU. UAE-derived. `cpuemu.c` is **machine-generated, ~1.8 MB** — never read whole; grep first, then `Read` with offset/limit only on matched ranges.
- **GPU** (26.6 MHz RISC, `src/tom/gpu.c`) — graphics coprocessor.
- **DSP** (`src/jerry/dsp.c`) — same ISA as GPU; audio.
- **Object Processor** (`src/tom/op.c`) — sprite/bitmap rendering.
- **TOM** (`src/tom/tom.c`) — video, GPU, OP, Blitter (`src/tom/blitter.c`).
- **JERRY** (`src/jerry/jerry.c`) — audio DAC, DSP, timers, EEPROM.

Frame loop is event-driven (not cycle-accurate): `JaguarExecuteNew()` in `src/core/jaguar.c` runs 68K to next event, then GPU, then fires callbacks (half-line render, timers).

## Libretro layer

`libretro.c` (top-level) implements the API. Video XRGB8888 dynamic res (320×240 NTSC / 320×256 PAL). Audio 48 kHz 16-bit stereo. Core options in `libretro_core_options.h` (blitter mode, BIOS, NTSC/PAL, DSP, input).

## Layout

- `src/core/` — orchestration, memory map, events, settings, files, cheats
- `src/tom/` — video, GPU, OP, blitter (+ SIMD)
- `src/jerry/` — audio, DSP, DAC, EEPROM, input, wavetable, UART/netlink (`uart.c` + `jlink.c`, see `docs/netlink-design.md`)
- `src/cd/` — Jaguar CD: BUTCH/FIFO/DSA in `cdrom.c`, image loading (CUE/BIN, CHD, CDI) in `cdintf.c`; BIOS auth bypass + boot stub in `src/core/jaguar.c`
- `src/bios/` — embedded BIOS / boot stubs
- `src/m68000/` — UAE 68K (machine-generated; treat as opaque)
- `libretro-common/` — shared utility lib
- `test/tools/` — test harnesses; `test/roms/private/` — commercial ROMs/BIOSes (gitignored)

## Build system

`Makefile` covers 30+ targets, auto-detected via `uname` or `platform=`. `Makefile.common` lists sources. Flags: `-D__LIBRETRO__`, `-DMSB_FIRST` for big-endian.

## Testing

Local-only RetroAchievements validation — no RA account/API/server. `test/tools/test_rcheevos_e2e.sh` downloads pinned `RCHEEVOS_REF` and verifies `rc_libretro` mapping (`RC_CONSOLE_ATARI_JAGUAR`) matches host RAM.

### Shared test harness (`test/harness/`)

New tests should use `test/harness/harness.h` — a shared library that eliminates dlopen/init/run boilerplate. See the header's AGENT QUICK-START comment for a full example. Key features:
- Common CLI (`--json`, `--frames N`, `--bios`, `--option K=V`, `--quiet`)
- Scripted input: `--press FRAME:BUTTON[:HOLD]` (repeatable; buttons `up down left right a b c pause option 0-6`) — enough to navigate menus into gameplay headlessly; programmatic tests use `harness_press()` or a `harness_input_cb`
- Log verbosity env: `VJ_HARNESS_LOG_INFO=1` passes core INFO logs (CD trace dumps), `VJ_HARNESS_LOG_DEBUG=1` additionally passes DEBUG (per-call CD HLE trace)
- Automatic audio/video stats collection
- `harness_dlsym()` for probing internal core state
- JSON output mode for machine-parseable results
- Probe modules: `dsp_probe.h` (DSP registers, PC escape, LTXD ratio, RAM dumps), `timing_probe.h` (per-frame halflines, cycles, wall time, speed ratio)

Build: `cc -O2 -Wall -std=c99 $(INCFLAGS) -o test_foo test_foo.c test/harness/harness.c [probe.c...] -ldl -lm`

To add a new probe: create `test/harness/foo_probe.h` + `.c`, resolve symbols via `harness_dlsym()`.

### Key harnesses

- `test/regression_test.sh` — screenshot regression vs `test/baselines/` via miniretro (built from source on first run; `MINIRETRO_BIN` env to skip the build). Baselines are **not committed** — a ROM with no baseline reports `NEW` and prints the `cp` command to create one, so the first run on a fresh clone establishes them locally rather than failing.
- `test/tools/test_dsp_audio_diag.c` — DSP audio diagnostic (`make dsp-diag DSP_DIAG_ROM=path`); detects PC escape, bank init failures, silent LTXD
- `test/tools/test_frame_timing.c` — per-frame timing diagnostic (`make frame-timing FRAME_TIMING_ROM=path`); reports halflines/cycles/VBlanks per frame, wall-clock speed ratio, anomaly detection. Use `--csv` for per-frame data, `--json` for machine output
- `test/test_audio_clipping.c` — detects loud-broken audio (saturation density, run length, sustained loud RMS). Catches the Skyhammer / IS2 "saturated square wave" failure mode.
- `test/test_audio_presence.c` — counterpart to clipping: asserts audio is present in a known-good envelope (RMS within `[floor, ceiling]`, onset reached, no long zero runs). **Required to catch the silencing-regression class** where a "fix" drops RMS to zero — clipping passes but the game has no audio. Iron Soldier 1 baseline: RMS ~1175 on develop.
- `test/tools/test_memory_map.c` — asserts `SET_MEMORY_MAPS`, `SET_SUPPORT_ACHIEVEMENTS=true`, descriptor layout
- `test/tools/test_blitter_compare` — fast vs accurate blitter diff. Not in default `make`; build manually:
  `cc -O2 -Wall -std=c99 -I./libretro-common/include -o test/tools/test_blitter_compare test/tools/test_blitter_compare.c -ldl`
  Usage: `<core.so|.dylib> <rom> [frames] --load-state <file> [--frame-window F L] [--cmd-filter MASK VAL] [--verbose-dump]` (note: `--load-state`, not `--savestate`).
- `test/test_dsp_mac40.c` — DSP 40-bit MAC accumulator (`dsp_acc40.h`)
- `test/sram_test.sh` — SRAM round-trip
- `test/tools/cd_boot_matrix.sh` — per-title CD boot-stage matrix (HLE + BIOS mode) vs `docs/cd-boot-matrix.md`; env knobs `CD_MATRIX_FRAMES`, `CD_MATRIX_TIMEOUT`, `CD_MATRIX_MAX_RUNS`, `CD_MATRIX_LOGDIR`, `CD_MATRIX_OUT`, `CD_MATRIX_ROMS_ROOT`; chunked/resumable across invocations. Rows are stamped with the core build id (`<!-- build:<rev> -->`); resume skips only same-build rows and re-runs/replaces rows recorded by any other build. This exists because resuming into an OUT file from an older build used to resurrect ancient rows as "fresh" results — the phantom intermittent Battle Morph bios `? (pc_escape)` (`final_pc=$8FBFB758`) was exactly such a stale row, not a real run.
- CD trace ring: core option `virtualjaguar_cd_trace` (or env `VJ_CD_TRACE=1` for headless use) records `DSA_TX`/`DSA_RX`, `SEEK_START`/`SEEK_DONE`, `FIFO_FILL`/`FIFO_DRAIN`, `STOP`, `HLE_READ` events; dumped to the log on `cd_seek_wedge` or on request
- `test/tools/cd_wedge_probe.c` — catches intermittent CD lockups: detects a frozen framebuffer (`--arm N --freeze-frames N`, script the repro with `--press`), then dumps 68K registers, the pcQueue traceback ring, CD counters + trace ring, and a RAM hexdump around the stuck PC; exits 42 when caught so a retry loop can distinguish "caught" from "ran clean". `--ram-dump BASE` writes full 2MB main-RAM + GPU-RAM snapshots at wedge time; `--snap FRAME` (repeatable, with `--snap-prefix BASE`) snapshots mid-run for clean-vs-wedged state diffing. Found the Hover Strike instant-CD_read code-stomp and the B-skip stale-GPU-IRQ-latch corruption. Caveat: a static loading screen can false-positive short freeze windows — confirm real wedges with `--freeze-frames 900`.
- `test/tools/cd_visual_verify.c` — automated visual+audio verification for CD titles (`make cd-visual CD_VISUAL_DISC=<image.cue>`): per-second frame-motion timeline, non-black coverage, audio RMS, periodic screenshots (PPM; `sips -s format png` to view). Replaces most "boot it on a device and look" checks — an agent can Read the PNGs directly. Headless read-path caveat still applies for final "looks right" sign-off.

### Build-identity guard (stale-binary protection)

Every harness that dlopens the core prints the binary's embedded version (`vX.Y.Z <gitrev>[-dirty]`, also logged by the core at `retro_init`). If `VJ_EXPECT_BUILD` is set (as `make test` and `cd_boot_matrix.sh` do automatically, via `scripts/build-id.sh`), a core whose version doesn't token-match fails the load loudly instead of silently testing stale code. When running harnesses by hand after edits, use `VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/...`. Note `make` can skip rebuilds when file mtimes are second-identical — the guard catches that class; `nm -gU <dylib> | grep <newsymbol>` is the manual fallback.

### Test ABI and re-linking

`make` links the production-slim ABI (`retro_*` only); `make test` needs the
wide test ABI so harnesses can `dlsym` internals. Switching between them
re-links automatically — the Makefile deletes the library when the mode
changes, so `make` followed by `make TEST_EXPORTS=1 test` works. (It did not
before v2.3.2: the library was newer than every object, nothing relinked, and
the suite failed with `Missing: m68k_execute`.) After `make test`, the library
in the tree carries the wide exports; plain `make` restores the shipped ABI.

### Build-identity guard (stale-binary protection)

Every harness that dlopens the core prints the binary's embedded version
(`vX.Y.Z <gitrev>[-dirty]`). If `VJ_EXPECT_BUILD` is set, a core whose version
doesn't token-match fails the load loudly instead of silently testing stale code:

```bash
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/your_harness ...
```

`scripts/build-id.sh` prints the short git rev plus `-dirty` when tracked files
are modified; `scripts/gen-version-h.sh` stamps that same string into the core,
so the two sides always agree. This exists because `make` can skip a rebuild
when file mtimes are second-identical — the guard catches that class. Manual
fallback: `nm -gU <dylib> | grep <newsymbol>`.

### Performance / profiling

`make benchmark` runs `test/tools/test_benchmark` headlessly against a fixed ROM (default `test/roms/yarc.j64`, 600 frames) and prints FPS / ms-per-frame. Use as a same-host commit-to-commit delta — don't compare across machines. Full guide: [`docs/profiling.md`](docs/profiling.md) covers Instruments / `perf` / flame graphs and the SIMD A/B knob.

### Runtime crash watchdog

`src/core/crash_detect.c` runs once per frame from `retro_run` and logs to the RetroArch log on these signatures:

- `gpu_pc_escape` — GPU running with PC outside `[$F03000,$F03FFF]` ∪ `[$0,$E3FFFF]` (matches the JaguarReadX address decoding: main RAM mirrors at the bottom 8MB, cart ROM, boot ROM)
- `dsp_pc_escape` — DSP running with PC outside `[$F1B000,$F1CFFF]` ∪ `[$0,$E3FFFF]`
- `gpu_wedge` / `dsp_wedge` — same PC for ≥180 / 600 frames while still flagged running
- `video_stall` — framebuffer hash unchanged for 300 frames while a processor is running
- `cd_seek_wedge` — a CD seek was started but FIFO drain progress is frozen for 300 frames while a processor is still running; dumps the CD trace ring (see above, "Key harnesses") to the log. **Known benign case:** a title that goes CD-idle >5s after finishing a transfer fires this too — e.g. Myst (bios) fires it during the intro movie's ~6s all-black pause (drain parked at payload end LBA 21189, movie playing from RAM, clock still ticking); HLE shows the identical black window (fires `video_stall` instead). Corroborate before treating a lone line as a wedge.

Toggled via core option `virtualjaguar_crash_detect = enabled` (default) / `disabled` / `verbose`. Verbose mode adds a state heartbeat every 600 frames. Cost when enabled: one indirect call + ~256-pixel hash per frame; off-mode short-circuits at the first instruction.

When triaging "X crashes / hangs / goes to a black screen" reports, the user's RetroArch log should show the signature. No save state, no input recording needed — the log line at the moment-of-crash points at which subsystem broke. **Add new signatures here when you find a recurring failure mode that isn't already covered**; don't sprinkle one-off `LOG_ERR` calls across the subsystem files.

### Audio / DSP work — required tests

**Any change to `src/jerry/dac.c`, `src/jerry/dsp.c`, the HLE BIOS DSP/audio engine path in `src/core/jaguar.c`, or the DSP IRQ return-address logic MUST be validated against both audio tests, not just one.** A clipping check alone is insufficient: PR #170 (closed) shipped a "fix" that took Iron Soldier 2 from 17% saturated samples to RMS=521 (silent), and the clipping test passed because silence has 0% saturation.

Required runs before declaring an audio change done:

1. `make TEST_EXPORTS=1 test` — must exit 0. Both `test_audio_clipping` and `test_audio_presence` are part of the suite. The presence check on Iron Soldier 1 uses develop's measured envelope (`--rms-floor 200 --rms-ceiling 25000`). If your change moves IS1's RMS outside that band, you've changed audio behavior — verify it's intentional.
2. Sanity-check that previously-clipping titles (Skyhammer, IS2) didn't go from "loud broken" to "silent broken" — if one suddenly passes clipping but presence drops to silence, that's the masked-failure pattern. **Both are fixed as of 2026-08-05 and both assert clean**: the MMULT secondary-bank fix resolved them. Measured on `Skyhammer (World).j64`: 0.000% saturated, window RMS 3079.8, first audio at frame 171 — clean *and* not silent. (An older note here said Skyhammer "should still fail clipping"; that was stale, and it was inert anyway — the Makefile looked for `Skyhammer_(1999).jag`, which matches nothing, so the sentinel silently skipped while the suite reported exit 0.)
3. **Verify in RetroArch on a real game.** Headless tests cannot tell "music plays" from "structured noise at the right RMS" or catch BIOS-mode crashes. Memory: PR #170's BIOS crash + HLE silence in Skyhammer were both invisible to the test suite.

Do not relax thresholds in `test_audio_clipping.c` or `test_audio_presence.c` to make a PR pass. If a real fix makes a known-broken title legitimately quieter, that's a separate, deliberate baseline update — call it out in the commit, not as a side effect.

### Acid suite CI gating

The "Acid suite (linux x86_64)" job can show `conclusion: failure` for two unrelated reasons. Read the summary before assuming a real regression:

- `make -C test/acid test` exits non-zero by design (returns FAIL count). The job uses `set +e` and gates on `check-baseline.py` instead. Real regression = `Regressions: N` (N>0) in `acid-summary.txt`.
- Job conclusion `failure` with `OK: no regressions` in the step output = the artifact-upload step timed out (`Operation could not be completed within the specified time`). Re-run the job; no code action needed.

## GitHub Copilot PR reviews

- List unresolved threads: `gh api graphql -f query='{repository(owner:"libretro",name:"virtualjaguar-libretro"){pullRequest(number:N){reviewThreads(first:30){nodes{id isResolved comments(first:1){nodes{id author{login} body}}}}}}}'`
- Inline reply: `gh api -X POST repos/libretro/virtualjaguar-libretro/pulls/N/comments/<REST_ID>/replies -f body="..."` — parent is the REST `id` from `gh api .../comments`, NOT the GraphQL `PRRC_*` id (returns 404).
- Resolve thread: `gh api graphql -f query='mutation { resolveReviewThread(input: {threadId: "PRRT_..."}) { thread { isResolved } } }'`.
- Trigger a Copilot review: `gh pr comment N --body "@copilot review"`. The `requested_reviewers` REST endpoint rejects `copilot-pull-request-reviewer` as "not a collaborator".
- Always reply AND resolve when addressing feedback — leaving a thread open after a fix is noise for the next reviewer.

### Headless framebuffer caveat

The miniretro harness used by `test/regression_test.sh` doesn't expose the same composited framebuffer that RetroArch reads. Symptom: `jag_240p_test_suite` main menu shows ~1k non-black pixels via miniretro vs tens of thousands via RetroArch. Treat that as a **headless read-path / presentation bug** (OP+blitter output vs what the host reads), not a 240p timing or `__muldi3` performance bug. Verify against RetroArch before treating a regression as real.

## Distilled hardware reference

`docs/jtrm-*.md` — synthesized from the Jaguar Technical Reference Manual, optimized for LLM consumption:
- `jtrm-clocks-timing.md` — clock hierarchy, video timing, PIT formulas, memory map, bus priority
- `jtrm-register-map.md` — complete register addresses + bit fields (TOM, GPU, blitter, JERRY, DSP)
- `jtrm-gpu-dsp.md` — RISC ISA, pipeline, score-boarding, interrupts, MAC, wave table ROM
- `jtrm-blitter.md` — address generators, B_CMD, LFU truth table, modes of operation
- `jtrm-jerry.md` — PIT timers, JINTCTRL, I2S/DAC, UART, clock dividers, EEPROM
- `jtrm-object-processor.md` — object types, bit fields, display pipeline, colour space

Read these **before** making hardware-accuracy decisions. They supersede comments in source code.

## Sub-agent guidelines

When spawning agents for work in this repo, include these rules:

1. **C89 strict.** No mid-block declarations, no `for(int i…)`, no C99. All vars at top of block. Run `bash scripts/c89-lint.sh src/YOURFILE.c` before declaring done.
2. **Branch from develop.** Use `git worktree` or branch off develop. Never target main.
3. **Hardware reference.** For any emulation-accuracy work, read `docs/jtrm-*.md` first. Do NOT trust source-code comments for clock rates or register behavior.
4. **Test after changes.** Run `make -j$(getconf _NPROCESSORS_ONLN)` to verify build. Run `make test` for the full suite. For blitter changes, also run `test/tools/test_blitter_compare` if available. **For audio / DSP / HLE-engine changes**, both `test_audio_clipping` and `test_audio_presence` must pass; running only one masks the silencing-regression class (see "Audio / DSP work — required tests" above). Verify in RetroArch on a real game before declaring done — headless tests cannot tell music from structured noise, and they don't catch BIOS-mode crashes.
5. **No unnecessary changes.** Don't refactor surrounding code, add abstractions, or clean up unrelated files. Surgical changes only.
6. **Commit message style.** Use conventional commits: `fix(component):`, `perf(component):`, `test(component):`, `docs:`.

## Release process (GitFlow)

Full details in [`docs/release-process.md`](docs/release-process.md). Quick reference:

**Nightlies:** every push to `develop` reruns `release.yml`'s full 16-platform
matrix and replaces the rolling `nightly` prerelease plus its pinned tracking
issue. Gated on *compiling*, not on the test suite — don't describe nightlies as
"CI-verified". The `nightly` tag sits outside the `v*` filter so it can never
trigger a real release.

### Cutting a release

1. **Branch**: `git checkout develop && git checkout -b release/vX.Y.Z`
2. **Bump version** in these files (all must match):
   - `Makefile` → `CORE_BASE_VERSION := vX.Y.Z`
   - `dist/info/virtualjaguar_libretro.info` → `display_version = "vX.Y.Z"`
   - `src/core/version.h` is auto-generated (gitignored) — `bash scripts/gen-version-h.sh` or just rebuild.
3. **Write release notes**: `docs/RELEASE_NOTES_vX.Y.Z.md` — use `docs/RELEASE_NOTES_v2.3.0.md` as a template. Include: highlights, bug fixes, performance, testing, known issues, stats (`git diff --shortstat vPREV..HEAD`), downloads, maintainers.
4. **Verify**: `make clean && make -j$(getconf _NPROCESSORS_ONLN)` builds clean, `make test` passes, `strings *.dylib | grep vX.Y.Z` confirms version in binary.
5. **Commit**: `chore: bump version to vX.Y.Z, add release notes`
6. **Push + PR**: `git push -u libretro release/vX.Y.Z` then `gh pr create --base master`.
7. **After merge to master**: tag `vX.Y.Z` and push — CI (`release.yml`) builds 16 platforms and publishes the GitHub release using the release notes file as the body.
8. **Back-merge**: `git checkout develop && git merge master && git push libretro develop`.
9. **libretro-super**: send a PR updating `dist/info/virtualjaguar_libretro.info` there.

### What NOT to do

- Don't tag before the PR is merged to master.
- Don't put new features on a release branch — bug fixes only.
- Don't forget the back-merge to develop (step 8) — otherwise develop diverges from the tagged version string.

## Known limitations

- Blitter not fully cycle-accurate (some games need fast mode).
- No bus contention modeling.
- VC register behavior not fully accurate.

## Private ROM tree lives OUTSIDE the repo

`test/roms/private` is a **symlink** to `../jaguar-roms-private` (outside every
git checkout). It used to be a real directory inside the repo, and on
2026-07-30 several concurrent sessions each created/removed that path and the
ROM collection was destroyed — the tree is gitignored, so nothing protected it.

Rules for agents:
- **Never** run `git clean -xfd` (or any recursive delete) at the repo root; it
  targets exactly the gitignored paths that hold irreplaceable data.
- To make the ROMs visible in a fresh worktree, symlink the shared location —
  set `JAGUAR_ROMS_PRIVATE` to wherever your collection lives (it sits beside
  the repo checkouts, not inside any of them):
  `ln -sfn "${JAGUAR_ROMS_PRIVATE:?set to your private ROM tree}" test/roms/private`
  The `-n` matters: without it, a second `ln -sf` onto the existing symlink
  creates the new link *inside* the ROM tree instead of replacing it.
- Cleanup may remove that symlink (`rm -f test/roms/private`), never its target.
- `find` does NOT follow symlinks by default — use `find -L test/roms/private`.
- Disc images from the iCloud restore nest one level deeper than before
  (`<Title>/<Title>/*.cue`), so discover cues with `find -L` rather than a
  hardcoded depth.

## Set DEVELOPER_DIR for host builds

`xcode-select` points at `/Applications/Xcode.app`, so `make`/`cc` resolve to
binaries *inside an app bundle* — which makes macOS raise an App Management
("access data from other apps") prompt on every invocation, dozens of times
across a multi-agent run. Prefix host builds with
`DEVELOPER_DIR=/Library/Developer/CommandLineTools` (same Apple clang, no
bundle access, no prompts). Do NOT `xcode-select --switch` globally — full
Xcode is needed for the iOS cross-builds.

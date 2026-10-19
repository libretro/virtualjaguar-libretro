# AGENTS.md

Virtual Jaguar libretro core — Atari Jaguar emulator on the libretro API. C, GPLv3. Upstream:
`http://shamusworld.gotdns.org/git/virtualjaguar`.

Shorthand, LLM-oriented. Load-bearing rules are inline below; depth lives in `docs/agent/*.md`
and the linked docs — **read the relevant one before working in that area** (they load on demand,
so they cost nothing until you open them).

## Reference index

- [`docs/agent/build.md`](docs/agent/build.md) — build commands, C89 exempt list, test ABI /
  `TEST_EXPORTS` relink, build-identity guard, stale-.o hazard.
- [`docs/agent/hardware.md`](docs/agent/hardware.md) — 4-processor model, memory map, clocks,
  source layout, distilled JTRM index (`docs/jtrm-*.md`), known limitations.
- [`docs/jtrm-errata.md`](docs/jtrm-errata.md) — JTRM errata; Atari bug lists vs what we model.
- [`docs/agent/testing.md`](docs/agent/testing.md) — shared harness, full harness catalog,
  vjtrace, audio-test requirements, crash watchdog signatures, acid gating, profiling, headless
  caveat.
- [`docs/agent/github.md`](docs/agent/github.md) — PR↔issue linking, Copilot/Kimi reviews,
  release process, site publishing, roadmap.

## Always-on rules

- **Branch off `libretro/develop`** (GitFlow integration branch; local develop may be stale).
  `master` is release-only. PRs → `develop` unless `hotfix/*` / `release/*`. Detail:
  [`docs/agent/github.md`](docs/agent/github.md).
- **C89 / GNU89, strict.** No mid-block declarations (all vars at top of block, before any
  statement — most common violation); no C99 (`for(int i…)`, compound literals, designated
  initializers, VLAs). Run `bash scripts/c89-lint.sh src/YOURFILE.c` before pushing. Exempt
  files + detail: [`docs/agent/build.md`](docs/agent/build.md).
- **Host builds: prefix `DEVELOPER_DIR=/Library/Developer/CommandLineTools`** or macOS raises an
  App Management prompt on every `make`/`cc`. Don't `xcode-select --switch` globally (iOS
  cross-builds need Xcode).
- **Hardware accuracy: verify clocks/registers against the JTRM PDFs directly, never source
  comments** (comments have been wrong, e.g. PIT clock) **and never assume a `docs/jtrm-*.md` line
  is JTRM-verified just because it lives in that file** — per-line provenance is a mix: check
  whether the section's own `Source:`/`Derived from:` tag cites a manual page or just a source
  file (issue #522). A `Derived from: ... NOT verified against the JTRM` tag is not sufficient
  grounds for a hardware-accuracy decision; read the cited PDF page yourself. Refs:
  `docs/jtrm-*.md`, `docs/atari-jaguar-1999/`.
- **Audio/DSP changes (`src/jerry/dac.c`, `dsp.c`, HLE audio path in `src/core/jaguar.c`, DSP IRQ
  return logic) MUST clear BOTH `test_audio_clipping` AND `test_audio_presence`** — clipping alone
  misses the silencing-regression class (PR #170 lesson). Then verify in RetroArch. Detail:
  [`docs/agent/testing.md`](docs/agent/testing.md).
- **Every PR must be linked to its issue in the GitHub Development panel — put a link tag in the
  PR body and CI makes the link.** `Closes #N` does NOT link by itself on this repo (default
  branch is `master`, PRs target `develop`, so GitHub ignores the keyword), but
  `pr-issue-link.yml` scans the body for `Closes/Fixes/Resolves/Refs #N` or
  `<!-- link-issue: #N -->` and creates the link via `addCloseIssueReferences`. Same thing by
  hand: `scripts/pr-link-issue.sh <pr> [issue]`. Unlinked and untagged PRs still fail CI; use the
  `no-issue` label to opt out. Detail: [`docs/agent/github.md`](docs/agent/github.md).

## Data-safety rules (irreplaceable / can hang forever)

- **Private ROMs.** `test/roms/private` is a **symlink** to `../jaguar-roms-private` (outside every
  checkout, gitignored). Concurrent sessions once destroyed the tree.
  - **Never** `git clean -xfd` (or any recursive delete) at repo root — it targets exactly the
    gitignored paths holding irreplaceable data.
  - Fresh worktree: `ln -sfn "${JAGUAR_ROMS_PRIVATE:?}" test/roms/private` (the `-n` matters —
    without it a second `ln -sf` links *inside* the tree).
  - Cleanup may remove the symlink (`rm -f test/roms/private`), never its target.
  - `find` doesn't follow symlinks — use `find -L test/roms/private`. iCloud restores nest one
    level deeper (`<Title>/<Title>/*.cue`) — discover with `find -L`, not a fixed depth.
- **Shell aliases `rm`/`cp`/`mv` are `-i`** in this user's shell; a prompt with no TTY blocks
  forever (a hung `rm -i` once sat for 9 hours; `cp a b` silently does nothing and measurements
  then lie).
  - Delete with **`trash`** (`/usr/bin/trash`; recoverable, non-interactive).
  - If it must go: `command rm -f` / `command cp -f` / `command mv -f`. Never bare `rm`/`cp`/`mv`
    in a chain.
  - Verify a background task actually exited — don't infer from side effects. Check task output;
    `ps -eo pid,etime,command | grep ' -i '` if something feels slow.

## Hardware model (one-paragraph)

Four processors (68000 main @13.3 MHz `src/m68000/`; GPU RISC @26.6 MHz `src/tom/gpu.c`; DSP same
ISA `src/jerry/dsp.c`; Object Processor `src/tom/op.c`), unified big-endian memory map. TOM
(`src/tom/tom.c`) = video/GPU/OP/blitter; JERRY (`src/jerry/jerry.c`) = audio/DSP/timers/EEPROM.
RAM `0x000000` (2 MB), cart `0x800000`, TOM regs `0xF00000`, JERRY regs `0xF10000`.
`GET/SET16/32` byte-swap on LE hosts. Frame loop event-driven (`JaguarExecuteNew()` in
`src/core/jaguar.c`). System clock 26.590906 MHz NTSC / 26.593900 MHz PAL; 68K = half. Full map +
layout: [`docs/agent/hardware.md`](docs/agent/hardware.md).

`libretro.c` implements the API (video XRGB8888 dynamic res, audio 48 kHz 16-bit stereo, options
in `libretro_core_options.h`).

## Build & test (quick)

```bash
make -j$(getconf _NPROCESSORS_ONLN)          # build
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1  # debug -O0 -g
make TEST_EXPORTS=1 test                       # wide-ABI test suite (auto-relinks; see build.md)
```

Runtime crash watchdog (`src/core/crash_detect.c`, option `virtualjaguar_crash_detect`) logs
`gpu_pc_escape`/`dsp_pc_escape`/`gpu_wedge`/`dsp_wedge`/`video_stall`/`cd_seek_wedge`/`inframe_hang` at
the moment of crash — the RetroArch log line points at the broken subsystem. Signatures + all
harnesses: [`docs/agent/testing.md`](docs/agent/testing.md).

## Codex workflow

- Codex reads this file automatically. Keep shared repository rules here; `CLAUDE.md` is a
  compatibility entry point. Read the relevant `docs/agent/` guide before area-specific work.
- Use `.agents/skills/` for repository skills; Claude-specific hooks and permissions in
  `.claude/` do not run in Codex. Apply the data-safety rules above explicitly.
- Avoid dumping ROMs, disc images, compiled libraries, objects, or savestates into context.
  Inspect source and logs first; use bounded `xxd`, `nm`, or `otool` output when needed.
- Claude shell hooks (including RTK rewriting) do not run here. If using RTK, invoke it
  explicitly and keep raw failure logs available for diagnosis.
- Existing worktrees may contain active work. Inspect `git status` and `git worktree list`,
  and reuse the matching PR checkout when clean; preserve unrelated changes and local config.
- For a failed PR check, read the failing job log first. If evidence supports a transient
  failure, rerun only that job (`gh run rerun RUN_ID --job JOB_ID`) and preserve the passing
  checks. Reruns use the original commit; a source fix needs fresh validation of the new head.
  Never mark a failing check successful or bypass a known defect to avoid CI costs.
- Do not request paid Copilot/Claude reviews unless explicitly authorized. Review locally
  using `.github/skills/code-review/SKILL.md` and report concrete defects with evidence.
- Delegate only when requested or required by applicable instructions. Use the least expensive
  available model suitable for the task; Claude model names are not Codex model identifiers.

## Sub-agent guidelines

When spawning agents for work in this repo, brief them with:

1. **C89 strict** — no mid-block declarations, no C99, all vars at top of block; run
   `bash scripts/c89-lint.sh` before declaring done.
2. **Branch off `libretro/develop`** (worktree or branch); never target `master`.
3. **Hardware/emulation work: read `docs/jtrm-*.md` first**; do NOT trust source comments for
   clocks/register behavior.
4. **Test after changes** — `make -j…` to build, `make TEST_EXPORTS=1 test` for the suite. Audio/
   DSP/HLE changes: BOTH `test_audio_clipping` and `test_audio_presence` (one alone masks the
   silencing regression). Blitter: `test/tools/test_blitter_compare`. Verify in RetroArch before
   "done" — headless can't tell music from structured noise, nor catch BIOS-mode crashes.
5. **Surgical changes only** — no refactors, abstractions, or unrelated cleanup.
6. **Conventional commits** — `fix(component):`, `perf(component):`, `test(component):`, `docs:`.

## Linux cloud environments

On a **Linux (Ubuntu 24.04)** cloud VM, the macOS-only notes above
(interactive `rm`/`cp`/`mv` aliases, `DEVELOPER_DIR`/Xcode) do **not** apply.
Plain `rm`/`cp`/`mv` are non-interactive and `make`/`cc` resolve to the
system GCC/Clang.

### Toolchain / dependencies

- No package-manager dependencies to install: `libretro-common/` is vendored and
  the core links only libc (`-ldl`/`-lm` for test harnesses). Requirements are
  just the system C/C++ toolchain (`gcc`/`g++`/`make`) plus `python3` and `git`,
  all preinstalled and ensured by the startup update script. The core build needs
  **no** SDL2, OpenGL, or pkg-config.

### Build / lint / test (see this guide for the full matrix)

- Build: `make -j$(getconf _NPROCESSORS_ONLN)` → `virtualjaguar_libretro.so`.
- Lint: `make lint` (C89/MSVC gate). The NEON header check is skipped on this
  host (`clang` can't target NEON here) — CI covers it; that SKIP is expected.
- Test: `make test` — full white-box suite. Non-obvious: it silently rebuilds the
  whole tree with `TEST_EXPORTS=1` (wide ABI + `-DVJ_TRACE`), and flipping back to
  a plain `make` triggers another full rebuild (~2 min each). See the "Test ABI
  and re-linking" section of this guide.
- `make test` reports **~15 skipped checks** here (Iron Soldier / Atari Karts /
  Skyhammer / Doom / CD discs, etc.). These are **not failures** — they need the
  private ROM corpus, which is absent in the cloud VM (`test/roms/private` is an
  external symlink that does not exist here). A clean run is `0 failed`.

### Running the emulator (there is no GUI app)

This is a libretro core, not a standalone app. To run it headless:

- `./test/regression_test.sh ./virtualjaguar_libretro.so` clones+builds
  `miniretro` from GitHub (**needs network**) and runs the two committed public
  ROMs `test/roms/yarc.j64` and `test/roms/jagniccc.j64`. Baselines in
  `test/baselines/` are committed and should match ("PASS ... identical"); it
  also exercises determinism, frameskip, savestate and rewind.
- Set `MINIRETRO_BIN=/path/to/miniretro-bin` to skip the clone/build.
- `imagemagick` is not installed; regression comparison falls back to `cmp` and
  dumped screenshots are plain PNG (viewable directly), so the diff-image path is
  the only thing lost.
- A direct one-ROM run for a quick screenshot:
  `miniretro --core ./virtualjaguar_libretro.so --rom test/roms/yarc.j64 --output OUT --system OUT --frames 600 --dump-frames-every 599 --no-alarm`

---
name: code-review
description: Review pull requests to the Virtual Jaguar libretro core (C89 Atari Jaguar emulator). Use for any PR touching src/, libretro.c, test/, scripts/, Makefile, or dist/info. Focuses on emulation-accuracy, over-generalized fixes, tests that cannot fail, savestate coverage, and build-list drift.
---

# Virtual Jaguar PR review

Output: one line per finding, `file:line — severity — defect — input that triggers it`.
No findings: say `No findings.` Do not summarize the diff. If you cannot state a concrete
failing input or state, do not post the finding. Hard rules (C89, vendored paths, branch
base) are in `.github/copilot-instructions.md`; do not restate them.

## 1. Fix evidence (check first)

- A fix derived from one to five ROMs is a hypothesis. The first OP-window fix regressed a
  third of the corpus. Ask whether the PR shows before/after across the corpus
  (`docs/cart-boot-matrix.md`, `docs/cd-boot-matrix.md`), not only the reported title.
- A regression test must fail on the unpatched code. If the PR does not show it failing
  before the fix, flag it.
- "make test passes" proves little for CD/HLE changes: the boot classifier once passed a
  frozen title (Baldies). Prefer evidence that the title keeps rendering (`cd_wedge_probe`,
  `make cd-visual`) over a pass/fail label.
- Flag a measurement that could not have differed: a harness `--option` key that does not
  exist (silently ignored), `--bios` (does not switch CD boot mode), fixed-frame scripted
  input used for timing, or a baseline built from a dirty or stale tree.

## 2. Hardware accuracy

- Any new clock, register bit, timing constant, or address must cite a JTRM page, a
  `docs/jtrm-*.md` section whose own `Source:` tag cites a manual page, or a jag_sim
  netlist. A citation to a source-code comment is not evidence.
- Check units: system clocks vs 68K clocks (68K = half), field vs frame vs halfline.
- Check direction and order: OP HSCALE/VSCALE below $20 shrinks; OB is LSW-first; 32-bit
  accesses to TOM/JERRY/GPU space are two 16-bit accesses. Memory is big-endian (`GET`/`SET`).
- Behavior changes for one title must be gated (per-title DB row) or justified for all.

## 3. Tests that cannot fail

- An assertion that passes when its fixture is broken, a script that exits 0 after
  skipping everything, a SKIP that prints as PASS, or a check made tautological by the code
  it guards. This repo has shipped all three.
- Audio/DSP changes (`src/jerry/dac.c`, `dsp.c`, HLE audio in `src/core/jaguar.c`, DSP IRQ
  return) need BOTH `test_audio_clipping` AND `test_audio_presence`. Clipping alone misses
  silencing regressions.
- A parser fix (CUE/CHD/CDI/TOC) needs a fixture or a byte-for-byte comparison against a
  known-good extraction. Check LBA arithmetic at track boundaries and pregap offsets.

## 4. State and lifecycle

- New persistent emulator state must be in the savestate blob, including enhancement-path
  state (hi-res epoch), or run-ahead and netplay desync. One savestate version bump per
  release; do not add a second.
- Statics must be reset in `retro_deinit` (iOS cannot dlclose a core).
- Nothing allocating or expensive on the per-instruction or per-pixel path. Anything
  hooked there needs an A/B/B/A measurement, not a single run.

## 5. Build lists and metadata

- A new or renamed source file must appear in every list: `Makefile`, MSVC project,
  package/SPM manifests. CI scripts to run: `scripts/check-msvc-sources.sh`,
  `scripts/check-package-sources.sh`, `scripts/check-export-lists.py`.
- Version string lives in THREE files: `Makefile` `CORE_BASE_VERSION`,
  `dist/info/virtualjaguar_libretro.info` `display_version`, `src/core/version_fallback.h`.
- New core options go in `libretro_core_options.h`; check option-visibility logic and
  that explicit user choice still beats a per-title override.
- Editing the Makefile flushes the build via BUILD_AXES; flag a PR that changes axes
  without saying so.

## 6. PR hygiene

- Base is `develop` (unless `hotfix/*` or `release/*`). Body needs a `Closes/Fixes/Refs #N`
  or `<!-- link-issue: #N -->` tag, or the `no-issue` label.
- One fix per PR. Flag a perf or refactor PR that also changes game behavior.
- Do not edit `test/roms/private` or anything that deletes recursively at repo root.

## Do not flag

Comment density, formatting, `//` comments, naming taste, or anything in the exempt paths
listed in `copilot-instructions.md`.

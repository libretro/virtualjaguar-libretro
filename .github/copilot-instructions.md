Return code only. No conversational filler, no restatement of the diff, no
markdown commentary unless asked. One line per finding.

Deliberately short: this file is input on every request, and GitHub bills
Copilot by token since 2026-06-01. Depth belongs in `.github/prompts/*.prompt.md`,
invoked on demand. Do not grow this file.

## What this is

Virtual Jaguar libretro core — Atari Jaguar emulator. C, GPLv3, big-endian
hardware, four processors sharing one memory map.

## Hard rules (a violation is a build break, not a style opinion)

- **C89/GNU89 only.** The libretro buildbot uses MSVC. No mid-block
  declarations, no `for (int i…)`, no compound literals, no designated
  initializers, no VLAs. All variables at the top of a block, before any
  statement. This is the most common violation — check it first.
- **Never "clean up" generated or vendored code**: `src/m68000/cpu*.c` and
  `read*.c` (UAE 68K, machine-generated, ~1.8 MB), `src/bios/jag*bios*.c`
  (bin2c hex tables), `libretro-common/`.
- **Hardware behaviour is settled against the JTRM and the jag_sim netlists,
  never against source comments** — the comments have been wrong before (the
  PIT clock was documented at half its real rate for years). Distilled
  reference: `docs/jtrm-*.md`.
- **Branch from `develop`.** `master` is release-only.

## Review priorities, in order

1. Correctness against the hardware reference, for anything touching
   `src/tom/`, `src/jerry/`, `src/cd/`, or the memory map.
2. C89 violations.
3. Tests that cannot fail — an assertion that passes when its fixture is
   broken, a script that exits 0 having skipped everything, a check made
   tautological by the code it guards. This repo has shipped all three.
4. Savestate coverage: new persistent emulator state must be in the state
   blob, or run-ahead and netplay desync.

## Do not flag

- Missing explanations, comment density, or formatting.
- The `//` comment style — GNU89 allows it and existing code uses it.
- Anything in the exempt paths above.

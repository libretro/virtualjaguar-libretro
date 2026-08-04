# T5 — AvP scripted-input fixture (reach sustained in-game motion)

## Environment (do this first, exactly)

```bash
cd /Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
git fetch libretro
git checkout -b test/avp-input-fixture libretro/develop
[ -e test/roms/private ] || ln -sfn "${JAGUAR_ROMS_PRIVATE:?set me}" test/roms/private
```

## Hard rules (violating any of these fails the task)

- NEVER run `git clean -xfd` or any recursive delete at repo root.
- C89 only: all declarations at top of block, no `for (int i…)`, no
  designated initializers. Run `bash scripts/c89-lint.sh <file>` before commit.
- Branch from `libretro/develop`. Never `master`. Never stack on another task.
- Never relax a test threshold to make something pass.
- Touch ONLY the files in "Files you may touch". If the fix seems to need
  another file, STOP and report.

## Goal (one sentence)

Commit a replayable harness `--press` script that drives Alien vs Predator
from boot into **sustained in-game motion**, banked for reuse by #266 / #267.

## Scope boundary (read twice)

**In scope:** reach a state with ongoing gameplay motion (player can move;
framebuffer keeps changing for ≥300 frames).

**Out of scope for this task:** selecting the shotgun, proving the red
background bug, proving the green overscan bug, closing #266 or #267.
Weapon-select is a follow-on for a strong model after this fixture exists.

## Files you may touch (explicit allowlist)

- `test/fixtures/avp_reach_gameplay.press`  (**create** — plain text)
- `test/fixtures/README.md`  (**create** or append — how to invoke)
- `test/tools/run_avp_fixture.sh`  (**create** — thin wrapper that expands the
  press file into `cd_visual_verify` / harness args)

Do **not** edit blitter, TOM, OP, or core options defaults.

## Press file format

One event per line, comments with `#`:

```
# frame:button[:hold]
700:b:8
720:option:6
```

Buttons (from `test/harness/harness.c`):  
`up down left right a b c pause option 0 1 2 3 4 5 6`

Jaguar mapping reminder: A/B/C = `a`/`b`/`c`, Pause = `pause`, Option = `option`.

Wrapper must turn the file into repeated `--press` flags.

## ROM path

```bash
AVP='test/roms/private/ROMS/Alien vs Predator (1994).jag'
test -f "$AVP" || AVP="$(find -L test/roms/private -iname 'Alien vs Predator (1994).jag' | head -1)"
echo "AVP=$AVP"
test -f "$AVP" || { echo "STOP: AvP ROM missing"; exit 1; }
```

Use BIOS off unless you discover menus require BIOS on — if you switch, record
it in the fixture README as a required `--option`.

## Steps (numbered, copy-pasteable commands)

1. Build core + `cd_visual_verify`:

   ```bash
   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     make -j"$(getconf _NPROCESSORS_ONLN)" TEST_EXPORTS=1

   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     cc -O2 -Wall -std=c99 \
       -I. -I./libretro-common/include \
       -o test/tools/cd_visual_verify test/tools/cd_visual_verify.c \
       test/harness/harness.c -ldl -lm
   ```

2. Create `test/fixtures/avp_reach_gameplay.press` and
   `test/tools/run_avp_fixture.sh`.

   Suggested wrapper behaviour:

   ```bash
   # run_avp_fixture.sh <core> [extra harness args...]
   # expands fixtures/avp_reach_gameplay.press into --press args
   # invokes cd_visual_verify with --frames high enough to cover the script
   # writes shots under /tmp/avp_fixture_$$ 
   ```

3. Iterate the press script. Strategy that usually works for Jaguar title
   menus: wait through boot/attract, press `b` or `a` to start, use
   `option` / d-pad as needed, then hold `up`/`right` periodically in-game.

   Capture smoke evidence:

   ```bash
   OUT=/tmp/avp_fixture_try1
   mkdir -p "$OUT"
   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
     bash test/tools/run_avp_fixture.sh ./virtualjaguar_libretro.dylib \
       --outdir "$OUT" --shot-every 300
   # Read the timeline printed by cd_visual_verify.
   # Convert a late PPM if useful:
   #   sips -s format png "$OUT"/frame_*.ppm --out "$OUT"
   ```

4. You get **three candidate attempts**. After the third failure against the
   acceptance gate, STOP (see STOP conditions).

5. When the gate passes twice in a row, commit the fixture + wrapper + README.

## Acceptance gate (literal command + expected exit code / expected output)

Mechanical — not "looks like AvP to a human":

```bash
# Run 1
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  bash test/tools/run_avp_fixture.sh ./virtualjaguar_libretro.dylib \
    --outdir /tmp/avp_fix1 --shot-every 0 2>&1 | tee /tmp/avp_fix1.log

# Run 2 (must also pass — reproducibility)
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  bash test/tools/run_avp_fixture.sh ./virtualjaguar_libretro.dylib \
    --outdir /tmp/avp_fix2 --shot-every 0 2>&1 | tee /tmp/avp_fix2.log
```

Define pass in `run_avp_fixture.sh` (exit 0) iff the `cd_visual_verify`
timeline shows **all** of:

1. Across windows covering the **last 300 frames**,
   `sum(moving_frames) ≥ 40` (AvP first-person tops out ~12/60 per window
   under `cd_visual_verify`'s 0.5%-change detector — a per-window 30% bar
   is unreachable in-game).
2. Peak `nonblack` in those late windows ≥ **5000** pixels.
3. Process exit 0 from the verifier wrapper (`run_avp_fixture.sh` encodes
   this gate).

If `cd_visual_verify` stdout format makes scripting awkward, parse it in the
wrapper with `awk`/`rg` and exit 1 on failure — do not weaken thresholds.

**Explicit non-goals for the gate:** shotgun on screen, red background absent,
green pixels absent. Do not close #267.

## STOP conditions (abort triggers — report, do not improvise)

- **Three failed candidate scripts** against the gate — STOP and report the
  three press files + timelines. Do not invent a fourth without maintainer
  input.
- Headless shows motion but you cannot tell if it is the menu attract loop —
  if nonblack+motion pass, that is enough for this task; do not chase
  weapon-select.
- Tempted to commit a save state instead of a press script — STOP unless the
  press path is impossible; savestates are a different ticket (#268 world).
- Tempted to declare #267 fixed because fast==accurate on the fixture window —
  that is a strong-model follow-on; mention it in the PR, do not close #267.
- AvP ROM missing — STOP.

## Deliverable (exact commit message, PR title, PR body, issue comment text)

**Commit message:**

```
test(fixtures): AvP scripted input to sustained gameplay (#267 unlock)
```

**PR title:**

```
test(fixtures): AvP scripted input to sustained gameplay (#267 unlock)
```

**PR body template:**

```markdown
## Summary
- Adds `test/fixtures/avp_reach_gameplay.press` + runner.
- Gate: late-window motion + non-black coverage, two consecutive runs.
- Does **not** reach shotgun / does not close #267.

## Acceptance gate transcript
$ bash test/tools/run_avp_fixture.sh …
<paste run1 and run2>
```

**Push + PR:**

```bash
git add test/fixtures/avp_reach_gameplay.press test/fixtures/README.md \
        test/tools/run_avp_fixture.sh
git commit -m "$(cat <<'EOF'
test(fixtures): AvP scripted input to sustained gameplay (#267 unlock)

EOF
)"
git push -u libretro HEAD
gh pr create --base develop --title "test(fixtures): AvP scripted input to sustained gameplay (#267 unlock)" --body-file - <<'EOF'
## Summary
- Adds AvP press fixture + runner for sustained in-game motion.
- Unlock for #267 / reusable by #266. Does not close either issue.

## Acceptance gate transcript
(paste here)
EOF
```

**Issue comment on #267:**

```markdown
PR <N> banks a scripted-input fixture that reaches sustained AvP gameplay
headlessly (`test/fixtures/avp_reach_gameplay.press`).

Still **not** closing: shotgun / red-background confirmation needs a strong
model + RetroArch check. Headless ≠ composited output (CLAUDE.md caveat).
Next: extend the press script to weapon-select, then `test_blitter_compare`
with `--frame-window` if the bug still reproduces on a nightly.
```

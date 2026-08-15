# Test fixtures

Plain-text `--press` scripts and captured savestates for the shared harness
(`test/harness/`).

## `avp_reach_gameplay.press`

Drives **Alien vs Predator (1994)** from boot into sustained Alien
first-person gameplay. Unlocks #266 / #267 follow-on work; does **not**
reach weapon-select / shotgun.

```bash
bash test/tools/run_avp_fixture.sh ./virtualjaguar_libretro.dylib
```

Requires `test/roms/private` → private ROM tree and a `TEST_EXPORTS=1` core
build. Gate is mechanical (late-window motion + non-black), not visual.

Input runs to frame ~6300 so runs longer than the 3000-frame default still
land on a **live** scene. Without that keep-alive the player stands still
after frame 3000 and every later frame is byte-identical (AvP keeps
re-blitting the same view, so `crash_detect` logs `video_stall` while nothing
is actually wrong) — a frozen frame silently invalidates any measurement that
assumes motion. Presses past the run length never fire, so short runs are
unaffected.

## `avp_reach_marine_shotgun.press`

Reaches **Colonial Marine** gameplay and picks up the shotgun, so the #267
artefact (red backdrop behind the shotgun HUD icon on the accurate blitter)
can be reproduced headlessly. The Alien fixture above cannot reach it - the
shotgun is a Marine weapon.

~~~bash
PRESS_FILE=test/fixtures/avp_reach_marine_shotgun.press FRAMES=12200 \
  bash test/tools/run_avp_fixture.sh ./virtualjaguar_libretro.dylib
~~~

Needs **~12200 frames**: pickup lands around frame 10520 and the artefact shows
from ~10560. The route after the briefing is a seeded wander, not a solved
path, so it depends on the core staying deterministic - re-verify the pickup
frame if emulation timing changes.
A savestate captured mid-route does **not** carry the artefact, so drive
the fixture from boot rather than via `--load-state`.

## `dragons_lair_death_branch.press` / `dragons_lair_death_branch_bios.press`

Drives **Dragon's Lair (USA)** (Jaguar CD) out of the attract loop into
gameplay and kills Dirk, forcing a **scene branch** — a near-full-stroke CD
read (LBA 16026 → 154061, +138 035 sectors) to the death clip and back. Used
to measure branch-transition timing for #297; see
[`docs/fmv-drift-notes.md`](../../docs/fmv-drift-notes.md) §10.

~~~bash
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  bash test/tools/run_dl_branch_fixture.sh ./virtualjaguar_libretro.dylib hle
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  bash test/tools/run_dl_branch_fixture.sh ./virtualjaguar_libretro.dylib bios
~~~

Two files because BIOS boot reaches the same state **+439 fields** later, and
`--bios` does **not** switch CD boot mode — the runner passes
`--option virtualjaguar_cd_boot_mode=bios` and then verifies the mode from the
core's own `[BOOT] CD game, mode=…` log line.

Needs `test/tools/fmv_seek_probe` built (build line in the runner's error
message). The gate is the CD trace ring — a read whose LBA delta exceeds
100 000 sectors — **not** the `seeks`/`seekstarts` CSV columns, which are
structurally 0 in HLE mode because HLE never touches the BUTCH `$12xx` path.
A no-press control run fails the gate, so it is falsifiable.

## `avp_corridor_gameplay.state`

Savestate captured during an **Alien vs Predator (1994)** Alien first-person
corridor walk — no combat, no cutscene. It drops straight into the steady
state instead of paying the ~3000-frame run-up of `avp_reach_gameplay.press`,
and it carries no keep-alive input, which is what the idle-vs-moving
comparison in #411 needs. Issue #435; unblocks #378 and #411.

| | |
|---|---|
| ROM | `Alien vs Predator (1994).jag` |
| ROM MD5 | `96bc77cfd1b2df85b5e6ae05594e74b0` |
| ROM CRC32 | `0xDC187F82` — the `src/core/titledb.c` seed |
| Savestate version | **11** (`STATE_VERSION`, `src/core/state.h`) |
| Verified on build | `v3.3.0 eb19f12` |
| Options at capture | as reported by the capturer: video enhancements on, all three timing-model booleans on, both clock scales stock `1x`. Not recoverable from the file — see below |

Stored as RetroArch's **uncompressed** `RASTATE` container (`MEM ` block =
2 621 440 bytes of core state). RetroArch writes `.state` files RZIP-compressed
by default (`#RZIPv` magic) and `harness_load_state()` unwraps `RASTATE` but
not RZIP, so a re-captured state must be decompressed before it lands here.

Needs `test/roms/private` → private ROM tree. `retro_unserialize` is in the
shipped ABI, so a plain `make` build restores the state fine:

~~~bash
cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
   -o /tmp/vjt_smoke test/tools/vjtrace_smoke.c \
   test/harness/harness.c test/harness/trace_probe.c -ldl -lm
~~~

~~~bash
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) /tmp/vjt_smoke \
  ./virtualjaguar_libretro.dylib \
  "$(bash scripts/find-rom.sh 'Alien vs Predator (1994).jag')" \
  --frames 300 --load-state test/fixtures/avp_corridor_gameplay.state
~~~

Adding `--field-csv /tmp/avp.csv` (the per-field `fb_hash` column the numbers
below come from) additionally needs a `TEST_EXPORTS=1` core build —
`trace_probe_attach()` resolves the `vjtrace_*` symbols and aborts the run
outright on a production build, rather than quietly writing an empty CSV.

Measured on `eb19f12`: restores with no size warning, 92.9% non-black at rest,
**18** distinct `fb_hash` values over 300 idle fields (the Alien HUD cycles, so
the scene animates standing still — unlike the post-frame-3000 window of the
`--press` fixture above, which is byte-identical) and **42** with
`--press 10:up:280`. Two runs produce byte-identical CSVs.

The capture options are **not** recoverable from the file — `serialize_size()`
is identical with per-title defaults enabled or disabled, so the state restores
under any option set. What differs is the presented geometry: 652×480 with the
per-title defaults in force (#368 gives AvP 2x + true color),
326×240 under `virtualjaguar_pertitle_defaults=disabled`. Since 2x + true color
roughly doubles absolute cost, a number measured under one option set is not
comparable with one measured under another — **record the options of the run
you measure**, and do not assume they match the capture.

That matters here because the capture was *not* taken at stock: the capturer
reports the video enhancements and all three timing-model booleans
(`virtualjaguar_dram_timing`, `virtualjaguar_gpu_pipeline_timing`,
`virtualjaguar_blitter_timing`, all default `disabled`) enabled, with
`virtualjaguar_m68k_clock_scale` and `virtualjaguar_risc_clock_scale` left at
`1x`. None of that constrains the fixture — the timing models change pacing,
not the saved machine state — but it does mean the state is not a stock-options
artefact, so the numbers above (taken at stock) and any #378/#411 numbers must
each name their own configuration.

Savestate compatibility breaks on `STATE_VERSION` bumps (one bump per release
by policy), so this file needs re-capturing after a bump; update the version
row above when that happens.

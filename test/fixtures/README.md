# Scripted-input fixtures

Plain-text `--press` scripts for the shared harness (`test/harness/`).

## `avp_reach_gameplay.press`

Drives **Alien vs Predator (1994)** from boot into sustained Alien
first-person gameplay. Unlocks #266 / #267 follow-on work; does **not**
reach weapon-select / shotgun.

```bash
bash test/tools/run_avp_fixture.sh ./virtualjaguar_libretro.dylib
```

Requires `test/roms/private` → private ROM tree and a `TEST_EXPORTS=1` core
build. Gate is mechanical (late-window motion + non-black), not visual.

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

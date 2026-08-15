# Test fixtures

Plain-text `--press` scripts for the shared harness
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
## Why there is no committed savestate

`#435` originally shipped `avp_corridor_gameplay.state`, a mid-route Alien vs
Predator capture, to skip the ~3000-frame run-up that `avp_reach_gameplay.press`
pays. It is deliberately **not** in this tree.

A Virtual Jaguar savestate is an *uncompressed* dump of the emulated machine:
2 621 440 bytes, of which the bulk is the Jaguar's 2 MB main RAM. For a
commercial title that RAM holds the game's own code and data — `strings` on the
AvP capture returns its in-game text verbatim (`PULSE RIFLE`, `AIRLOCK DOOR`,
`SUBLEVEL ?`). Committing one to a public repository publishes a substantial
part of the copyrighted work, which is the same reason `test/roms/private` is a
symlink to a tree outside every checkout rather than a directory in it.

Two practical problems came with it, independent of the licensing one:

- **It expires.** A state is pinned to `STATE_VERSION`, and the policy is one
  bump per release — the AvP capture was version 11 and #429 takes the core to
  12, so it would have been stale before it was ever used.
- **It hid its own configuration.** A state restores the machine, not the
  options that produced it, so any measurement taken from it has to re-declare
  its configuration anyway.

### What to use instead

Drive the fixture from boot with the `--press` scripts above. They are our own
input recordings, a couple of kilobytes each, carry no game data, never expire
against a `STATE_VERSION` bump, and state their own timeline so a run is
reproducible from a cold start.

If a mid-game state genuinely cannot be reached by replaying input, keep the
`.state` beside the ROMs in the private tree and have the test **skip loudly**
when it is absent — `exit 77`, per the repo convention — rather than committing
it here.

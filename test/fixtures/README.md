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

```bash
PRESS_FILE=test/fixtures/avp_reach_marine_shotgun.press FRAMES=12000 \
  bash test/tools/run_avp_fixture.sh ./virtualjaguar_libretro.dylib
```

Needs **12000 frames**: pickup lands around frame 10520 and the artefact shows
from ~10560. The route after the briefing is a seeded wander, not a solved
path, so it depends on the core staying deterministic - re-verify the pickup
frame if emulation timing changes. A save state taken after the pickup does
**not** carry the artefact, so drive it from boot rather than via
`--load-state`.

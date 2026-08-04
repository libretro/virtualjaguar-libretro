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

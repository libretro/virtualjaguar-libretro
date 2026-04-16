# Private Test ROMs

This directory is for commercial ROM files used in local testing.
Files here are git-ignored and must NOT be committed.

## Expected files

Place any of the following for game-specific testing:

### Cartridge ROMs (.j64)
- `doom.j64` — Doom (resolution hack testing, #85-related)
- `avp.j64` — Alien vs Predator (map rendering, issue #85)
- `cybermorph.j64` — Cybermorph (DSP voice test, issue #27)
- `tempest2000.j64` — Tempest 2000 (performance testing)
- `ironsoldier.j64` — Iron Soldier (black screen, issue #86)

### CD images (.cue/.bin or .chd)
- `bcd/` — Blue Lightning CD
- Any Jaguar CD game in CUE/BIN or CHD format

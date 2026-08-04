# Savestate compatibility

Status as of 2026-08-03. Tracker: issue [#268](https://github.com/libretro/virtualjaguar-libretro/issues/268).

## Version window

Defined in `src/core/state.h`:

| Constant | Value | Meaning |
|---|---|---|
| `STATE_MAGIC` | `0x564A5353` (`"VJSS"`) | Header magic |
| `STATE_VERSION` | `7` | Version this build **writes** |
| `STATE_MIN_VERSION` | `2` | Oldest version this build will **load** |

`retro_unserialize()` refuses anything outside `STATE_MIN_VERSION … STATE_VERSION`
and returns false (no partial load).

Header fields are stored with host-endian `memcpy` (`STATE_SAVE_VAR`). On a
little-endian host the on-disk magic bytes are `53 53 4A 56`, not the ASCII
string `"VJSS"`.

## Decision (deliberate)

Rejecting states below `STATE_MIN_VERSION` (2) is **expected behaviour**, not
an AvP-specific loader bug. A Battle Sphere Gold state from a current build
loads successfully on the same build — the loader works; old Virtual Jaguar
states simply sit outside the supported window.

A best-effort legacy loader for pre-`STATE_MIN_VERSION` files is **out of
scope**. The format gap is old, and the v3.0.0 freeze at version 7 means the
window is stable rather than drifting.

## Inspecting a file

Build and run the header inspector (PR #284 / `test/tools/vjss_info.c`):

```bash
cc -O2 -Wall -std=c89 -o test/tools/vjss_info test/tools/vjss_info.c
./test/tools/vjss_info path/to/file.state
```

Example output from a freshly serialized current-core state:

```
magic=0x564A5353 endian=le version=7 flags=0x00000000 reserved=0x00000000 verdict=loadable
```

Verdicts: `loadable`, `too_old`, `too_new`, `bad_magic`.

RetroArch `.state` files for this core are raw `retro_serialize()` payloads
(no extra wrapper), so `vjss_info` can read the first 16 bytes directly.

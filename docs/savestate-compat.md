# Savestate compatibility

Status as of 2026-08-04. Tracker: issue [#268](https://github.com/libretro/virtualjaguar-libretro/issues/268).

## Version window

Defined in `src/core/state.h`:

| Constant | Value | Meaning |
|---|---|---|
| `STATE_MAGIC` | `0x564A5353` (`"VJSS"`) | Header magic |
| `STATE_VERSION` | `7` | Version this build **writes** |
| `STATE_MIN_VERSION` | `1` | Oldest version this build will **load** |

`retro_unserialize()` refuses anything outside `STATE_MIN_VERSION … STATE_VERSION`
and returns false (no partial load).

Header fields are stored with host-endian `memcpy` (`STATE_SAVE_VAR`). On a
little-endian host the on-disk magic bytes are `53 53 4A 56`, not the ASCII
string `"VJSS"`.

## What released cores wrote

Only four format versions have ever left a release tag:

| Version | Written by |
|---|---|
| 1 | v2.2.0 (savestate support first shipped here) |
| 2 | v2.3.0, v2.3.1 |
| 3 | v2.3.2 |
| 7 | v3.0.0 |

Versions 4, 5 and 6 existed only on `develop` / nightlies. All four released
layouts load on the current core.

## Two bugs, both fixed (issue #268)

### 1. v1 was gated out

`STATE_MIN_VERSION` was `2`, so v2.2.0's states were refused outright. The
complete layout difference between v1 and v2 is 24 bytes — the four DAC I2S
resampler fields (`i2sWritePos`, `i2sWriteCount`, `i2sPhase`, `i2sRateRatio`)
— verified by extracting and diffing every `*StateSave` / `*StateLoad` body
between the `v2.2.0` and `v2.3.0` tags.

`DACStateLoad` now skips those four fields for a v1 header and leaves them at
their `DACInit()` defaults. That is exact, not best-effort: `DACPrepareFrame`
(libretro.c, top of `retro_run`, before `JaguarExecuteNew`) re-seeds
writePos/writeCount, truncates the phase and re-derives the rate ratio from
the restored SMODE/SCLK registers, so the defaults never reach the audio
output.

### 2. v1, v2 AND v3 mis-parsed from the CDROM chunk onward

This one was live on `develop` for v2 and v3 — versions already inside the
accepted window. `retro_unserialize()` returned `true` and the game kept
running, so nothing looked wrong, but every chunk from the CD block onward was
read at the wrong offset.

The CD-support work restructured `CDROMStateSave`/`Load`: it dropped the two
`cdBuf2` / `cdBuf3` staging buffers (`uint8_t [2532 + 96]` each, 5256 bytes
together) and put the BUTCH/FIFO/DSA/SSI working set there instead. Only the
last 28 bytes of that change were version-gated
(`STATE_VERSION_CDROM_DSA_QUEUE`, `..._DRIVE_SPEED`). Net effect on an old
blob: the loader read 5256 bytes of stale sector data as flags and finished
2627 bytes short, desyncing the Joystick, Memory Track and DAC chunks behind
it.

`CDROMStateLoad` now forks on `STATE_VERSION_CDROM_RESTRUCTURE` (4). Below it
the loader consumes the legacy 8004-byte block — 2748 bytes of prefix
(`cdRam` … `firstTime`, byte-identical in every version) plus the 5256 dead
staging bytes — and starts the drive from the idle state `CDROMReset()`
establishes. `cdrom_eeprom_ram` is deliberately left alone: it is
file-backed NVM, and an old blob has nothing to say about it.

Zero-defaulting the CD fields is exact for these states in practice — no core
that wrote v1/v2/v3 had a working CD path (`cdBuf2`/`cdBuf3` belonged to the
unfinished BUTCH stub), so there is no CD session to lose.

### Verification

A genuine state was written by each released core built from its own tag
(v2.2.0, v2.3.0, v2.3.2) against the real Alien vs Predator ROM, then loaded
in the current core. In all four cases (plus a current-version control) the
DAC chunk — the last module, so the accumulated victim of any upstream
mis-size — lands at its correct offset with a unique match, and main RAM, TOM
and JERRY register space come back byte-identical.

`test/test_state_compat` builds v1/v2/v3 fixtures at runtime (see
`synth_legacy_state`) and asserts the same thing in CI. Disabling the legacy
CDROM branch turns three of its assertions red.

### Still refused

Anything below `STATE_MIN_VERSION` (now only the never-released version 0) and
anything above `STATE_VERSION`. Nightly-only versions 4-6 load, but were never
covered by the release-compat policy.

## Inspecting a file

Build and run the header inspector (`test/tools/vjss_info.c`, added in PR #284). If the file is not present in your checkout, update to a revision that includes PR #284 first:

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

# Savestate compatibility

Status as of 2026-08-04. Tracker: issue [#268](https://github.com/libretro/virtualjaguar-libretro/issues/268).

## Version window

Defined in `src/core/state.h`:

| Constant | Value | Meaning |
|---|---|---|
| `STATE_MAGIC` | `0x564A5353` (`"VJSS"`) | Header magic |
| `STATE_VERSION` | `16` | Version this build **writes** (v3.7.1 cycle; v3.7.0 wrote 15) |
| `STATE_MIN_VERSION` | `1` | Oldest version this build will **load** |

`retro_unserialize()` refuses anything outside `STATE_MIN_VERSION … STATE_VERSION`
outright, returning false without touching emulator state. Inside the window the
format is read in full (a CD state from v14 up can still be refused for a
different disc, see below); chunks whose fields an older layout did not carry are
reconstructed rather than read.

Header fields are stored with host-endian `memcpy` (`STATE_SAVE_VAR`). On a
little-endian host the on-disk magic bytes are `53 53 4A 56`, not the ASCII
string `"VJSS"`.

## What released cores wrote

Format versions that have left a release tag (full history in `src/core/state.h`):

| Version | Written by |
|---|---|
| 1 | v2.2.0 (savestate support first shipped here) |
| 2 | v2.3.0, v2.3.1 |
| 3 | v2.3.2 |
| 7 | v3.0.0, v3.1.0 |
| 8 | v3.2.0: trailing Jaguar GameDrive chunk, `STATE_VERSION_JAGGD` |
| 11 | v3.3.0 (the layout carries the v9 I2S ring, v10 blitter busy window and v11 hi-res epoch additions) |
| 12 | v3.4.0: input-device chunk |
| 13 | v3.5.x: Team Tap chunk, extended in place with the netlink wire-speedup word |
| 14 | v3.6.0, v3.6.1: mounted-disc identity |
| 15 | v3.7.0: CDROM `dsaLastMultiWord`, extended in place with the HLE CD streaming chunk (#803) |

No release wrote versions 4, 5, 6, 9 or 10: they were intermediate `develop` /
nightly layouts, and v3.3.0 went straight to 11 (which includes the v9 and v10
additions). As format support, `retro_unserialize()` can read every version from
`STATE_MIN_VERSION` up; v3.6.1's v14 states load and run byte-identically to
v3.6.1 itself (measured in #803). Format support is not a guarantee that a given
file loads: from v14 up, a CD state also records the mounted disc's identity
(sessions, tracks, total sectors) and is refused, returning false, when it does
not match the disc currently mounted in CD mode (`retro_unserialize()`,
`libretro.c`).
A state saved by an older build while an HLE CD read was in flight cannot
resume that read: the transfer was never saved.

## v16: real-BIOS CD chunks (#804, v3.7.1 cycle)

Two trailing chunks, appended strictly after the HLE CD streaming chunk, each
behind its own magic word. `STATE_SIZE` is unchanged (`0x280000`); the chunks
are 5 and 9 bytes.

| Chunk | Magic | Fields | Owner |
|---|---|---|---|
| Real-BIOS boot | `"CDB1"` | `cdBootStubInjected` | `src/cd/jagcd_bios.c` |
| CD drive timing | `"CDX1"` | `fifoRefillAccum`, `cdPrevShouldIRQ` | `src/cd/cdrom.c` |

Both lived outside the blob. A rollback (run-ahead, netplay) to a state taken
before the boot stub was injected kept the flag set by the replay that had
already injected it, so the replay never injected and the game never started;
the refill accumulator picks the next FIFO interval (2 or 3 ticks), so a stale
value shifted every later refill by one tick. Measured on Hover Strike at
warmup 400, real-BIOS path: video diverged 44 frames after the rollback before
the fix, all four `test_runahead_determinism` checks pass after.

Loading: `retro_unserialize()` reads both chunks only for `version >= 16`
(`STATE_VERSION_BIOS_CD_BOOT`). A v15 or older state loads normally and the
chunks' values are left as the live session holds them, exactly as v3.7.0 did.
That is deliberate: defaulting `cdBootStubInjected` to false would re-arm the
`$005E40` GPU-magic stomp and the `$050176` boot-stub re-injection over a
mid-game v15 state's RAM, a regression for every existing real-BIOS state. (A
v15 state loaded into a fresh core still starts with the flag clear, as before.)
A damaged accumulator (outside 0..99) loads as 0. Verified with a genuine v15
file written by a v3.7.0 build (`274fa74`), Hover Strike real-BIOS. Regression:
`test_state_compat` (`v16_*`, `v15_*` rows) and the ROM-gated real-BIOS Hover
Strike run in `make test`.

## v8: Jaguar GameDrive chunk

v8 (one shared bump per release policy — all in-flight changes since v3.1.0
use it) appends a fixed-size 540-byte GameDrive chunk (`JGDStateSave` in
`src/core/jaggd.c`) after the bus-arbiter accumulators: active/write-enable
flags, the six 1 MB bank page registers, and the SPI mailbox engine
(state machine + response FIFO). The chunk is written all-zero for non-GD
content, which keeps the zero-tail property `test_state_compat`'s
`dac_block_is_last` structural check relies on. Loading a pre-v8 state
resets the GameDrive to its power-on mapping (identity pages, write
protect, idle SPI) — the game re-runs `GD_Install` after a console reset
anyway, so nothing is lost. The 16 MB SDRAM image itself is NOT serialized
(same policy as cart ROM); pages modified via `GD_ROMWriteEnable` do not
survive a load — a deliberate v1 simplification, called out in
`docs/jgd-interface-notes.md` §9.

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
magic=0x564A5353 endian=le version=8 flags=0x00000000 reserved=0x00000000 verdict=loadable
```

Verdicts: `loadable`, `too_old`, `too_new`, `bad_magic`.

RetroArch `.state` files for this core are raw `retro_serialize()` payloads
(no extra wrapper), so `vjss_info` can read the first 16 bytes directly.

# Jaguar CD CHD images

CHD is supported for Jaguar CD **only** when the file carries MAME's `CHSE` session-metadata tag ([mamedev/mame#15886](https://github.com/mamedev/mame/pull/15886), merged 2026-08-15). That tag is the CHD equivalent of a CUE `REM SESSION nn` line.

## Why most `.chd` files on the internet will not load

Jaguar CDs are two-session discs: session 1 is Red Book audio, session 2 is game data mastered as audio-type 2352-byte tracks. Classic `chdman createcd` flattened that into one session and dropped the ~11400-sector inter-session gap, so the TOC the BIOS and HLE walk is wrong.

Those files are **refused** at load with a `[CD-CHD]` error pointing here. Reconvert from a Redump CUE/BIN (or from CDI, after turning it into CUE/BIN). Do not try to "fix" an old CHD in place.

`chdman` 0.288 from Homebrew, and every CHD made before PR 15886, is in this class. `chdman info` on a good file shows a `CHSE` / `SESSION:2` metadata blob; a bad file has only `CHT2` track lines.

## Convert

1. Get the `jagcd-tools-{linux-x64,macos,windows-x64}.zip` from this repository's GitHub Releases (tagged `v*` and the rolling `nightly`). Each zip contains a `chdman` built from the commit in [`tools/jagcd/CHDMAN_PIN`](../tools/jagcd/CHDMAN_PIN) plus `jagcd-to-chd` and `jagcd-chd-check`. They ship next to the cores from `release.yml`.
2. From a CUE:

```
./jagcd-to-chd "Game (USA).cue"
```

That runs `chdman createcd` and then `jagcd-chd-check`. Exit 0 is loadable. Exit 1 means the `chdman` you invoked is still too old — set `JAGCD_CHDMAN` to the binary from the zip, not the one on `PATH`.

`createcd` does not read `.cdi`. Use CUE/BIN as the source, or load the CDI directly in the core.

## Virtual pregaps (exit 2 / log warning)

`CHT2` `PGTYPE` values that start with `V` (e.g. `VAUDIO`) mean the pregap is **silence**, not stored audio. That is the same situation as a Redump CUE/BIN: HLE boot works (auth is bypassed), real-BIOS authentication may fail. The core **warns and loads**. CDI-class dumps that preserve INDEX 00 audio are the right source if you need BIOS-mode auth.

PR 15886 did not change pregap-byte encoding; it only added session tags.

## Core behaviour

- `.chd` is a path-loaded disc image, same as `.cue` / `.cdi`.
- Missing `CHSE` on a Jaguar-shaped image (2+ all-audio tracks) → refuse.
- `CHSE` present, virtual pregaps → warn, load.
- CHD audio frames are stored as 16-bit PCM. The core byte-swaps them back to Jaguar I2S (CUE/BIN) order on read.
- The ~11400-sector session gap is synthesized when `CHSE` shows a session change and the first session-2 track's `PREGAP` is not already that large (so a CHD that encoded the gap as a pregap is not double-counted). Measured on a post-15886 `createcd` of Frog Feast: every `CHT2` `PREGAP` is 0 and `PGTYPE` is `MODE1` (not virtual); `extractcd` emits `REM SESSION 02`. Retail Redump CUEs have one audio track in session 1, so `CHSE` index 1 is session 2's first track. Homebrew dumps with two session-1 tracks may see MAME tag session 2 one track early — reconvert from a CUE the core already boots if a title looks wrong.

Tracking: issue #322.

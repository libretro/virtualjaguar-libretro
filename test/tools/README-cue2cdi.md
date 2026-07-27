# cue2cdi — CUE/BIN → DiscJuggler CDI converter

Standalone tool (no core, no libretro deps) that converts a — typically
multi-session — CUE/BIN Jaguar CD dump into a DiscJuggler `.cdi` image.
BigPEmu and several other Jaguar tools prefer `.cdi`; this core loads both,
and the emitted file is laid out exactly the way this repo's own parser
(`src/cd/cdintf.c :: ParseCDI`) walks the format.

## Build

```bash
make cue2cdi
# equivalent to:
cc -O2 -Wall -std=c99 -o test/tools/cue2cdi test/tools/cue2cdi.c
```

Single self-contained C file, libc only (batch mode uses POSIX
`opendir`/`readdir`). Not part of the default build or `make test`.

## Usage

```
cue2cdi input.cue [output.cdi] [--verify] [--quiet] [--version]
cue2cdi [--batch] DIR [--verify] [--force] [--quiet]
```

- Default output: input path with the extension replaced by `.cdi`.
- `--verify`: re-parses the produced CDI with the same walk `cdintf.c`
  performs (trailer → header table → per-session/per-track blocks) and
  byte-compares every track's payload against the source BIN(s). Prints a
  per-track table (session, track, mode, sector size, LBA, length, OK/FAIL).
  Exit code 0 = converted, 2 = verify failed.
- `--quiet`: suppress progress output (the verify table still prints).

Handles multi-file CUEs (redump style, one BIN per track) and single-file
CUEs (INDEX offsets slice the BIN). `AUDIO`, `MODE1/xxxx`, `MODE2/xxxx`
tracks with 2048/2336/2352-byte sectors are supported; sectors are written
at the source's raw size (no 2048→2352 re-framing).

## Batch mode

Point the tool at a directory — either explicitly with `--batch` or just by
passing a directory as the input — and it recursively finds every `*.cue`
(case-insensitive) and converts each one in place to
`<same-dir>/<same-basename>.cdi`:

```bash
make cue2cdi
./test/tools/cue2cdi --batch --verify ~/jaguar-dumps/
./test/tools/cue2cdi ~/jaguar-dumps/          # directory implies --batch
```

Behavior:

- **Recursive walk**: subdirectories are followed; hidden directories/files
  (leading `.`) and symlinks are skipped. Files are processed in sorted
  path order.
- **Progress**: one line per file —
  `[3/17] Converting: path/to/Game.cue ... OK (11 tracks, 2 sessions)` —
  and a final summary: `converted X, skipped Y, failed Z`.
- **Skip logic**: if the target `.cdi` already exists and is newer than the
  `.cue` and every referenced BIN, the file is skipped with a note.
  `--force` reconverts regardless.
- **Failures don't stop the queue**: a broken CUE or missing BIN prints its
  error, is counted as failed, and the run continues. Exit code is 0 when
  nothing failed, 1 otherwise.
- `--verify` composes with batch mode: each converted image gets the full
  per-track verify table (header lines print before each table instead of
  the single-line format).

Batch mode takes no output-path argument (outputs are always placed next to
their sources). The per-track conversion chatter of single-file mode is
always suppressed in batch runs — the progress lines and summary are the
output, and they print regardless of `--quiet`.

## The Jaguar multi-session caveat

Jaguar CDs are multi-session: session 1 holds short audio warning track(s),
session 2 holds the game "data" tracks — which are recorded as **audio-type**
tracks containing byte-swapped data. `cue2cdi` preserves the session
structure and the track types exactly as the CUE declares them. It never
forces tracks to a data mode; doing so would break both the byte-swapped
read path and BIOS authentication.

Session numbering comes from `REM SESSION nn` lines (redump convention).
A CUE without them is treated as a single session — fine for conversion,
but note a bootable Jaguar disc needs the 2-session layout.

For multi-file CUEs the disc-absolute LBAs are rebuilt the same way
`ParseCueSheet` does: file sizes accumulate, and an 11400-sector
inter-session gap (session 1 lead-out + run-out + session 2 lead-in) is
inserted at each session boundary, so the CDI's TOC matches what the core
computes when loading the CUE directly. Single-file CUEs keep their
already-absolute INDEX positions (no gap — the BIN contains no gap data),
again matching the core's CUE path.

## Why plain `.iso` input is unsupported

An ISO is a bare single-track Mode1/2048 payload: no TOC, no sessions, no
audio tracks, no pregap. Everything a Jaguar CD needs to boot — the
session-1 audio program, the session-2 placement of the byte-swapped boot
track, the pregap the BIOS's authentication reads — is precisely the
metadata an ISO throws away. There is nothing meaningful to convert, so the
tool rejects `.iso` with an error instead of fabricating a fake
single-session disc. (`src/cd/cdintf.c::ParseIso` documents the same
limitation on the loader side.)

## Emitted format details

- DiscJuggler **V3** trailer id `$80000005`, 8-byte little-endian trailer at
  EOF: version, then the **absolute** offset of the header table
  (`cdintf.c` reads V2/V3 offsets as absolute, V3.5 as offset-from-end).
- Layout: all track payloads first (disc order, native sector size,
  pregap data included for tracks that have INDEX 00), then the header
  table, then the trailer.
- Header table: u16 session count; per session u16 track count, then per
  track: u32 0, the 20-byte track-start marker, 4 pad bytes, filename
  (length byte + basename of the source BIN), 19 pad bytes, u32 0 + 2 pad
  bytes, the 0x70-byte track-data block, and the V3 tail (5 pad bytes +
  u32 0). Per-session trailer: 13 zero bytes.
- Track-data block field offsets are the ones `ParseCDI` reads:
  `+0x00` pregap_length, `+0x04` length, `+0x10` mode, `+0x20` start_lba,
  `+0x24` total_length, `+0x38` sector-size code (0=2048, 1=2336, 2=2352).
  `start_lba` is the disc-absolute start of the track region *including*
  its pregap, matching the parser's `startLBA`/`dataLBA = startLBA +
  pregap` interpretation.

Note: those track-block offsets are `cdintf.c`'s reading of the format.
Some third-party CDI readers derived from the cdirip/DreamShell lineage
document the mode/start_lba/total_length/sector-size fields 2 bytes
earlier (`+0x0e/+0x1e/+0x22/+0x36`). If an external tool rejects a
`cue2cdi` image, that 2-byte discrepancy is the first thing to check —
in-core loading is what `--verify` guarantees.

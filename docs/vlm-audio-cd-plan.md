# Audio CDs and the Virtual Light Machine — investigation and plan

Issue: [#291](https://github.com/libretro/virtualjaguar-libretro/issues/291) —
"Jaguar CD: audio CDs never reach the Virtual Light Machine (single-session discs rejected)".

Investigated on `develop` @ `0ecf924` (v3.0.0), macOS/clang, embedded retail CD BIOS,
2026-08-04. Every trace line quoted below was produced by the runs described here.

**Headline: the root cause was not the loader.** Single-session audio discs load
fine; the loader synthesises a correct one-session TOC already. The blocker was a
**wire-protocol bug in the `$03nn` Read-session-TOC response** in `src/cd/cdrom.c`,
which made the CD BIOS spin forever in its DSA read loop. Fixing it takes the
synthetic music CD all the way to the BIOS's **CD player front-end and into the
VLM screen** — no loader change at all. That fix is implemented on this branch;
what remains is CDDA audio routing.

---

## 1. Confirmation of the prior finding

> "No game title touches CD command `$03`/`$14` — the DSA TOC path is exercised
> only by the audio-CD flow, so the fix is low-risk."

**Confirmed, and with wider scope than originally claimed.** The concern was that
the statement might only hold for *game-resident* drivers while the real BIOS —
which runs for all matrix titles in `bios` mode — uses `$03`/`$14` on game discs
too. It does not.

Method: `VJ_CD_TRACE=1 VJ_CD_TRACE_LIVE=1 VJ_HARNESS_LOG_INFO=1` +
`cd_visual_verify`, `--option virtualjaguar_cd_boot_mode=bios`, 1800 frames per
title, then a census of `DSA_TX` high bytes. Seven titles, covering the whole
sensitive set:

| Title (bios mode, 1800 frames) | DSA command high bytes observed | `$03` | `$14` |
|---|---|---|---|
| Myst (USA)                       | `$12`×9, `$11`×9, `$10`×9, `$15`×2, `$70`, `$18`, `$05`, `$04` | no | no |
| Hover Strike - Unconquered Lands | `$15`×11, `$12`×9, `$11`×9, `$10`×9, `$70`×3 | no | no |
| Primal Rage (USA)                | `$12`×17, `$11`×17, `$10`×17, `$15`×10, `$70`×5, `$02` | no | no |
| Battle Morph (USA)               | `$12`×3, `$11`×3, `$10`×3, `$15`×2, `$05`×2, `$04`×2, `$70`, `$18` | no | no |
| Iron Soldier 2 (Songbird)        | `$12`×7, `$11`×7, `$10`×7, `$15`×4, `$70` | no | no |
| Blue Lightning (USA)             | `$12`×4, `$11`×4, `$10`×4, `$15`×2, `$70`, `$18`, `$05`, `$04` | no | no |
| Vid Grid (USA)                   | `$12`×5, `$11`×5, `$10`×5, `$15`×4, `$70`×2 | no | no |

Zero `$03xx` and zero `$14xx` across all seven, in real-BIOS mode.

Why: on a 2-session game disc, boot-stub extraction succeeds and `src/core/jaguar.c`
redirects the 68K past the BIOS's disc-probe code, so the BIOS's own TOC read never
runs. The TOC path is reached **only** when boot-stub extraction fails, i.e. exactly
the single-session audio-disc case.

Corroborating static check: `minTrack`/`maxTrack` (`cdrom.c:1900-1902`), the only
other consumers of `CDIntfGetSessionInfo()` inside `cdrom.c`, are assigned solely
on a `$1400` write — which nothing sends.

Caveat, stated honestly: each census is an 1800-frame window from cold boot. It
covers boot and early gameplay, not every minute of every title. It does not cover
`hle` mode, but `hle` never executes BIOS code by construction.

---

## 2. Current behaviour — where it stalled, with evidence

### 2.1 Test asset

Synthesised, no copyrighted audio. No file was added to the repo — the generator is
inline in the repro (§7) and writes to a scratch dir. Two 20-second audio tracks of
tremolo'd sine, 2352-byte Red Book sectors, single session, no `ATRI` header:

```
FILE "track01.bin" BINARY
  TRACK 01 AUDIO
    INDEX 01 00:00:00
FILE "track02.bin" BINARY
  TRACK 02 AUDIO
    INDEX 01 00:00:00
```

### 2.2 The loader is *not* the blocker

`CDIntfLoadDisc()` handles the one-session case correctly (`src/cd/cdintf.c:401-443`):
`numSessions = 1`, `sessions[0].firstTrack = 1`, `lastTrack = 2`, and the
"single session: lead-out after last track" branch computes `leadOutLBA = 3000`
(→ absolute MSF 00:42:00). `CDIntfGetSessionInfo(0, 0..4)` therefore returns
`1, 2, 0, 42, 0` — a perfectly good session TOC.

The `numSessions >= 2` gates do fire, but only where they should:
`CDIntfExtractBootStub()` (`cdintf.c:1335`) early-exits with

```
[CD-BOOTSTUB] Early exit: loaded=1 numSessions=1
[CD-BOOTSTUB] CDIntfExtractBootStub failed
```

which is *correct behaviour* for an audio disc — there is no boot stub. In `bios`
mode the BIOS then falls through to its own disc probe, which is what we want.
(In `hle` mode the same failure aborts `retro_load_game()`; see §5, Phase 3.)

### 2.3 The actual stall: `$03nn` never delivers word 2

Unfiltered trace ring, `develop` before the fix:

```
[CD-TRACE-LIVE] tick=247972 kind=DSA_TX  value=$7001   Set DAC Mode
[CD-TRACE-LIVE] tick=247986 kind=DSA_RX  value=$7001
[CD-TRACE-LIVE] tick=247986 kind=DSA_TX  value=$150A   Set Mode: 2x, data
[CD-TRACE-LIVE] tick=247990 kind=DSA_RX  value=$170A
[CD-TRACE-LIVE] tick=248008 kind=DSA_TX  value=$0300   Read session TOC, session 0
[CD-TRACE-LIVE] tick=248009 kind=DSA_RX  value=$0301   ... and nothing, ever again
```

Temporary instrumentation on the `BUTCH+2` and `DSCNTRL` read paths (since removed)
pinned it exactly. (`who` is the access-source tag from `src/core/vjag_memory.h:59`;
the `68K_PC` field is sampled unconditionally and so shows where the 68K is at the
time of the read, not necessarily who issued it.)

```
[VLMDIAG] BUTCH+2 read data=$3000 dsaRdy=1 cdPtr=0 68K_PC=$05046A GPU_PC=$F03A8A who=2
[CD-TRACE-LIVE] tick=248009 kind=DSA_RX value=$0301
[VLMDIAG] DSCNTRL read (ack) dsaRdy=1->0 cdPtr=1 multi=1 qcount=0 68K_PC=$05046A who=2
[VLMDIAG] DSCNTRL read (ack) dsaRdy=0->0 cdPtr=1 multi=1 qcount=0 68K_PC=$05046A who=2
[VLMDIAG] BUTCH+2 read data=$1000 dsaRdy=0 cdPtr=1 68K_PC=$050468 GPU_PC=$F03A8C who=2
[VLMDIAG] BUTCH+2 read data=$1000 dsaRdy=0 cdPtr=1 68K_PC=$050478 GPU_PC=$F03A8C who=2
   ... identical line forever, PCs cycling over $050464-$050484 ...
```

Read: the BIOS's DSA read sequence is **poll `BUTCH+2` bit 13 → read `DS_DATA` →
read `DSCNTRL` (ack)**. The ack path at `cdrom.c:1146` unconditionally clears
`dsaResponseReady`, and re-arms only when `dsaQueueCount > 0`. TOC words are
synthesised on demand from the disc layout, never pushed onto `dsaQueue`
(`qcount=0` above), so nothing ever re-raised bit 13. `data=$1000` is TX-empty
only; bit 13 (`$2000`) is gone for good. `BUTCH+2` is re-read indefinitely while the
68K cycles a tight loop at `$050464-$050484` (BIOS code running from RAM) for the
rest of the run, and the boot animation keeps drawing — which is exactly the
reported symptom.

**Note this contradicts the "edge starvation" reading** that the same trace also
admits (a level-held `dsaResponseReady` would yield exactly one IRQ, because
`BUTCHExec`'s delivery is edge-gated at `cdrom.c:1050/1082`). The measurement
settles it: the BIOS **polls**, it does not wait on the IRQ, and the flag was
cleared rather than pinned. The fix is "re-arm after ack", not "re-pace the edge".

### 2.4 The second, deeper bug: wrong response tags

With the re-arm in place, all five words flowed — as `$0301 $0302 $0300 $032A $0300` —
and the BIOS **still** spun, now at `cdPtr=5`. Adding a sixth `$0300` end-marker
word did not help either (it spun at `cdPtr=6`).

Ground truth resolved it. `test/mister_ground_truth.h:230-234`, transcribed from
MiSTer's `butch.v`, says a `$03` Read-TOC response is **not** a run of `$03nn`
echoes — each word carries its own response opcode:

```
#define DSA_RSP_TOC_MIN_TRK   0x20
#define DSA_RSP_TOC_MAX_TRK   0x21
#define DSA_RSP_TOC_LO_MIN    0x22
#define DSA_RSP_TOC_LO_SEC    0x23
#define DSA_RSP_TOC_LO_FRM    0x24
```

Our code echoed `$03nn` for all five (`cdrom.c:1246` pre-fix) and terminated with a
sixth `$0300`. The BIOS's collector discarded every word and kept asking. The
`$14nn` long-TOC path next door already used the correct `$60..$64` tags — the
short-TOC path was simply never exercised, so the bug sat unnoticed.

The terminator is likewise `$0400`, not `$0300`: the Primal Rage 68K CD driver's
multi-word collector at `$C20C` uses an `$04xx` word as end-of-response (already
documented at `cdrom.c:1703-1715`).

### 2.5 After the fix — full disc dialogue, and the VLM

```
DSA_TX $7001            Set DAC Mode
DSA_TX $150A  -> $170A  Set Mode 2x/data
DSA_TX $0300  -> $2001 $2102 $2200 $232A $2400     session 0 TOC: trk 1..2, lead-out 00:42:00
DSA_TX $1400  -> $6001 $6101 $6200 $6302 $6400     long TOC track 1 @ 00:02:00
                 $6002 $6102 $6200 $6316 $6400     long TOC track 2 @ 00:22:00
DSA_TX $0301  -> $0400  session 1 TOC: absent (correct — single-session disc)
DSA_TX $1501  -> $1701  Set Mode: single speed, AUDIO  <- the is-this-a-music-CD verdict
DSA_TX $0200            Stop
       I2S_CTRL $000F   I2S path to JERRY enabled
DSA_RX $0200
```

The BIOS probes the disc, finds one session with no data, re-modes the drive to
**single-speed audio**, and hands off. On screen at frame 1200 (`cd_visual_verify
--outdir ... --shot-every 600`, PPM → PNG) is the Jaguar CD **audio-CD player
front-end**: STOP / REW / PLAY / FF / PAUSE transport, `TRK 2`, `TIME 0:42`,
`NORMAL` / `NO REPEAT` / **`VLM`** buttons, and a track grid with cells 1 and 2 lit.

With `--press 600:a`, the player accepts input, switches to a **distinct BIOS
screen** — a black field carrying a `1-4` indicator and the `2 0:42` track/time
readout — and issues `DSA_TX $0101` — **Play Title, track 1**.

Stated precisely: the disc is accepted, the TOC is correct, the player front-end
runs and responds to input, and pressing A leaves the player for another BIOS
screen while starting playback. That screen is *consistent with* the VLM (the
player's own `VLM` button, the mode indicator, the black visualisation field with
no audio to draw), but "this is VLM code executing" is an inference, not a
measurement — nothing here checked the PC region. Confirming it is Phase 4 work,
and Phase 4's gate ("the visualisation reacts to the audio") is the real proof.
Nothing else in this plan depends on the identification.

What is missing either way is the audio itself.

---

## 3. The minimal change set

### 3.1 Implemented on this branch (`src/cd/cdrom.c`, 3 hunks)

1. **`$03nn` response tags** — emit `$20/$21/$22/$23/$24` for the five session-TOC
   data words instead of five `$03nn` echoes, and terminate with `$0400` instead of
   a sixth `$0300`. Deleted the now-unreachable `cdPtr >= 5` clear branch: the
   `$0400` terminator hits the existing `data == 0x0400` clear one line above.
2. **Re-arm RX-full across a multi-word response** — `DSCNTRL` ack path
   (`cdrom.c:1152`) and the `BUTCHExec` turnaround countdown (`cdrom.c:960`) now
   treat `isMultiWordResponse` like a non-empty `dsaQueue`.

No new state, no savestate fields, no version bump: `isMultiWordResponse` and
`cdPtr` were already serialised.

One adjacent hazard was checked and is **not** present: `trackNum` (file-scope
`static`, `cdrom.c:1095`) walks up to `maxTrack + 1` during a `$14nn` long-TOC
read and is not reset by the CDROM reset block, which would make a *second*
`$1400` return an immediate `$0400`. The `$1400` write handler does reset it
(`trackNum = minTrack;`, `cdrom.c:1900`). This matters because `$14` was as dead
as `$03` before this change and is now newly reachable.

**Loader changes required: none.** The `numSessions >= 2` gates are all correct as
written; audio discs simply have no boot stub, and the BIOS handles that itself.

### 3.2 Still required for a working VLM (not implemented)

**CDDA routing on `$01nn` Play Title.** The SSI head that feeds JERRY's I2S port
(`SetSSIWordsXmittedFromButch`, `cdrom.c:2110+`) is positioned **only by a `$12xx`
Goto-Frame seek** (`ssiBlock = block + 1; memcpy(ssiBuf, cdBuf, ...)` at
`cdrom.c:1852-1854`). The VLM plays via `$01nn` Play Title, which sets
`cdPlaying = true` but never positions the head. Result today: RMS stays at 980 —
identical to the boot animation, byte-for-byte the same non-silent sample count —
i.e. no CDDA at all.

The fix is to make `$01nn` position the drive like a seek does: resolve track `nn`
to its `dataLBA` via the existing `disc.tracks[]` table, set `block`, load
`cdBuf`/`ssiBuf`, reset `cdBufPtr`/`ssiBufPtr`/`ssiBlock`. All the machinery below
that point already works — this is the same path Primal Rage's CDDA uses, and it
was fixed and validated in that context (`docs/cd-diagnosis/primal-rage-cdda-diagnosis.md`).

Open sub-questions for that phase:
- Does the drive stop at the track's lead-out, or run on into the next track?
  `NORMAL` / `NO REPEAT` in the player UI implies the *BIOS* sequences tracks, so
  the drive should probably free-run and the BIOS re-issue `$01nn`. Verify against
  the trace rather than guessing.
- Does the VLM's FFT read the DAC path (`lrxd`/`rrxd`) or the BUTCH FIFO? The
  `I2S_CTRL $000F` write in the handoff says the I2S-to-JERRY path is on, which
  points at the DAC path.
- `$04`/`$05` Pause/Unpause and `$51` Set Volume from the transport buttons should
  be checked once audio flows.
- The `$61nn` word of the `$14nn` long-TOC response is `LONG_TOC_CA` (control/adr)
  per `test/mister_ground_truth.h:243`; we return the track number again
  (`cdrom.c`, `cdPtr < 0x62` branch). The BIOS accepted it, so this is
  non-blocking, but it is wrong and should be corrected when the audio work
  touches that path.

---

## 4. CDDA routing plan

The Jaguar CD drive in audio mode streams Red Book samples over I2S straight into
JERRY's serial port; the DSP mixes them with synth output. We already model this:

```
CDIntfReadBlock(ssiBlock, ssiBuf)          src/cd/cdrom.c
  -> SetSSIWordsXmittedFromButch()         lrxd/rrxd  (sample-aligned, L then R, LE)
    -> JERRY SSI / I2S receive             src/jerry/dac.c  (LRXD/RRXD reads)
      -> DSP mix -> LTXD/RTXD -> DAC       src/jerry/dsp.c, dac.c
        -> libretro audio batch cb         libretro.c
```

`ButchIsReadyToSend()` gates on `ssiBufPtr < 2352`, and
`SetSSIWordsXmittedFromButch()` already returns silence when `!cdPlaying ||
seekDelay > 0` — the pause/stop semantics the transport buttons need are in place.

Plan:

1. **Position the head on `$01nn`** (§3.2). Smallest correct change; makes CDDA flow.
2. **Verify SMODE.** Red Book playback wants JERRY's SSI in *slave* mode (the drive
   clocks it). `dac.c:301-305` already traces SMODE master/slave transitions —
   confirm the BIOS/VLM sets slave, and that our first-I2S-IRQ deferral
   (`project_cd_dsp_sclk_irq_fix`, commit `b63fb7d`) does not swallow the start.
3. **Level/mix.** Once samples arrive, run `test/test_audio_clipping` and
   `test/test_audio_presence` against the synthetic disc: a pure sine at ±20000
   is a clean saturation probe, and the presence test catches a silencing
   regression. Do not adjust either threshold.
4. **Track sequencing and transport.** `$02` Stop, `$04`/`$05` Pause/Unpause,
   `$51` Set Volume, and end-of-track behaviour, driven from the player UI via
   `--press`.

---

## 5. Regression risk

**Assessed low for the implemented change, on direct evidence rather than
inspection.** The changed code is reachable only via `$03nn`/`$14nn`, which §1
shows no title sends in either mode — the seven-title census includes the whole
sensitive set (Hover Strike, Myst, Primal Rage, Iron Soldier 2, Battle Morph) plus
Blue Lightning and Vid Grid.

Risk is nonetheless *not* zero and the gate is the boot matrix, not this document:

| Risk | Why it is small | Residual |
|---|---|---|
| `isMultiWordResponse` re-arm leaks into the seek/stop queue path | The two conditions are OR'd; the queue path's behaviour is unchanged when `isMultiWordResponse` is false, which it is for every `$10/$11/$12/$02/$70/$15/$18/$50/$54` write | A title that sends `$03`/`$14` *after* frame 1800 would newly get a paced multi-word stream instead of a stall — strictly closer to hardware |
| `dsaResponseReady` pinned true forever if software issues `$03nn` and never reads | Delivery is edge-gated (`cdrom.c:1050`), so no IRQ storm; one re-arm per ack, same as the queue | Bounded; matches existing queue semantics |
| Response-tag change breaks a driver that expected `$03nn` | Nothing consumes `$03nn` today (§1); MiSTer's `butch.v` table is the hardware reference | Would surface as a matrix regression |
| Savestate | No fields added or reordered | None |

Evidence actually collected on this branch: `cd_visual_verify`, 1800 frames, five
titles (Myst, Hover Strike, Primal Rage, Iron Soldier 2, Battle Morph), **`bios`
mode before vs after the change is byte-identical** — same avg RMS, same non-silent
sample counts, same resolutions, all `PASS`. The same five in `hle` mode also pass
(as expected: `hle` never executes BIOS code, so `$03nn` is unreachable there).
`make TEST_EXPORTS=1 test` exits 0.

**Non-negotiable gate before merge:** `test/tools/cd_boot_matrix.sh` — all 22 rows
still `GAME_CODE` in both `hle` and `bios`. That has *not* been run for this branch;
a full matrix run is the first thing the PR needs. Spot checks done here (Myst,
Hover Strike via `cd_visual_verify`) are necessary, not sufficient.

Also required per `CLAUDE.md`: verification in RetroArch, not only headless — the
headless framebuffer read path is not proof of what is presented.

---

## 6. Phased PR breakdown

| Phase | Scope | Effort | Gate |
|---|---|---|---|
| **1. DSA session-TOC protocol fix** *(implemented on this branch)* | `src/cd/cdrom.c`: `$20..$24` response tags, `$0400` terminator, multi-word RX-full re-arm | ~0.5 day incl. validation | `make TEST_EXPORTS=1 test` green; full `cd_boot_matrix.sh` all-`GAME_CODE`; audio disc reaches the CD player UI |
| **2. CDDA routing for `$01nn` Play Title** | Position `block`/`cdBuf`/`ssiBuf`/`ssiBlock` from the track table on Play; verify SMODE slave | 1–2 days | Audible CDDA on the synthetic disc; `test_audio_clipping` + `test_audio_presence` both pass; Primal Rage CDDA unchanged |
| **3. Loader/frontend acceptance for audio discs** | Let `hle` mode stop rejecting audio-only images — either force the real-BIOS path when `numSessions == 1 && no ATRI` (cheap, honest: the VLM is BIOS code and `hle` can never produce it), or fail with a clear message. Touches `libretro.c:1507-1517`, `jagcd_hle.c:1586-1591` | 0.5–1 day | Audio disc boots regardless of the `CD Boot Mode` setting |
| **4. VLM polish + transport** | Track sequencing, Pause/Unpause/Volume, repeat modes, end-of-disc | 1–2 days | Player transport works end to end; VLM visualisation reacts to the audio |
| **5. Follow-ons (separate tickets, do not bundle)** | `supports_no_game` (boot BIOS with no disc), libretro disk control — both correctly disabled today and blocked on a disc-status DSA opcode the jump table does not have | — | File only after Phase 4 lands |

Phases 1 and 2 are the feature. Phase 3 is a frontend nicety. Phase 4 is what makes
it pleasant. Phase 5 is explicitly out of scope for this issue.

---

## 7. Repro

```bash
# Build core (wide test ABI) + harness
DEVELOPER_DIR=/Library/Developer/CommandLineTools make TEST_EXPORTS=1 -j$(getconf _NPROCESSORS_ONLN)
DEVELOPER_DIR=/Library/Developer/CommandLineTools cc -O2 -Wall -std=c99 \
  -I./libretro-common/include -I./src -o test/tools/cd_visual_verify \
  test/tools/cd_visual_verify.c test/harness/harness.c -lm

# Synthesise a music CD (two audio tone tracks, one session, no ATRI header)
mkdir -p /tmp/vlm_musiccd && cd /tmp/vlm_musiccd && python3 - <<'EOF'
import math, struct
SR, SECTOR, SPS = 44100, 2352, 75
def tone(path, freq, secs):
    buf = bytearray()
    for i in range(secs * SPS * SECTOR // 4):
        t = i / SR
        a  = 0.55*math.sin(2*math.pi*freq*t) + 0.25*math.sin(2*math.pi*freq*2*t)
        a *= 0.6 + 0.4*math.sin(2*math.pi*1.5*t)
        v = int(max(-1.0, min(1.0, a)) * 20000)
        buf += struct.pack('<hh', v, v)
    open(path, 'wb').write(bytes(buf))
tone('track01.bin', 440.0, 20)
tone('track02.bin', 660.0, 20)
open('musiccd.cue', 'w').write(
    'FILE "track01.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n'
    'FILE "track02.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n')
EOF
cd -

# Boot it with the real CD BIOS; press A at frame 600 to enter the VLM
mkdir -p /tmp/vlm_shots
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) VJ_CD_TRACE=1 VJ_CD_TRACE_LIVE=1 VJ_HARNESS_LOG_INFO=1 \
  ./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib /tmp/vlm_musiccd/musiccd.cue \
  --option virtualjaguar_cd_boot_mode=bios --frames 2400 \
  --outdir /tmp/vlm_shots --shot-every 300 --press 600:a

for f in /tmp/vlm_shots/*.ppm; do sips -s format png "$f" --out "${f%.ppm}.png"; done
```

`VJ_CD_TRACE_LIVE=1` is what makes the drive dialogue visible — the `[CDDA] DSA cmd`
log line filters on command high byte (`cdrom.c:1608-1610`) and can never show
`$03`, `$14`, `$10`, `$11`, `$12`, `$02` or `$18`. Do not infer absence from it.

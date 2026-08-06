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

> **Update, 2026-08-04, `develop` @ `b399eb1`.** §3.2's diagnosis ("Phase 2:
> CDDA routing on `$01nn` Play Title") is **withdrawn — it was a measurement
> error, not a bug.** The SSI head is already streaming correct CD-DA at
> 44.1 kHz before and after Play Title; nothing needs positioning. The real
> blocker is one bit inside the VLM's own DSP code, and everything downstream
> of it works: forcing that bit clear makes the Virtual Light Machine render
> and react to the music. Full evidence, and the new committed regression
> test, in **§8**. Read §8 before acting on §3.2 or on the Phase 2 row of §6.

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

> **WITHDRAWN — see §8.** The paragraph below is wrong. It inferred "no CDDA"
> from the output RMS not moving, and inferred from `cdrom.c:1852` that only a
> `$12xx` seek can position the head. Direct instrumentation of
> `SetSSIWordsXmittedFromButch()` shows the head *is* primed and streaming (it
> refills from `ssiBlock` on its own when `cdPlaying` goes true) and delivers
> the disc's samples to `lrxd`/`rrxd` at 44.1 kHz, correctly, including the
> silence of an inter-track pregap at exactly the right offset. The RMS did not
> move because the DSP never forwards the samples to LTXD/RTXD — for a reason
> that has nothing to do with head position. **Do not implement this.** The
> only surviving item from this section is the `$61nn` `LONG_TOC_CA` nit at the
> end, which remains true and remains non-blocking.

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
| ~~**2. CDDA routing for `$01nn` Play Title**~~ **CANCELLED (§8)** | Nothing to do: the SSI head already streams correct CD-DA on Play Title, verified sample-for-sample against the disc image. Superseded by "unmute the VLM's DSP audio path" — see §8.4 | — | Now pinned by `test/test_cd_synth_cdda.c` |
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

---

## 8. Second pass — the VLM works; one bit in its DSP code keeps it muted

Investigated on `develop` @ `b399eb1` (post #300, #305, #307), macOS/clang,
embedded retail CD BIOS, 2026-08-04. Every number below came from the runs
described in §8.6.

### 8.1 The disc

Still synthetic — no real audio CD exists in the corpus (`find -L
test/roms/private -name '*.cue'` returns 35 CUEs, all Jaguar game discs). But a
much more informative one than §2.1's two-tone pair, built so that **the decoded
audio itself says what is playing and where**:

- **multi-file CUE, one BIN per track**, `REM SESSION 01` — the exact shape
  every CUE in the corpus uses, so `dataLBA != startLBA` and the loader's
  pregap handling is exercised rather than bypassed;
- **6 tracks** (manual repro), each preceded (2..6) by a real **150-sector
  `INDEX 00` pregap** of digital silence;
- **track *t* is a square-wave chirp sweeping `882*t` → `882*t + 441` Hz**, so
  a zero-crossing count identifies the track, and the instantaneous frequency
  identifies the position within it;
- **left ≠ right**: right is the second harmonic at a different amplitude, so a
  channel swap — invisible on a mono disc, and this repo has SSI channel
  history — is glaring;
- **constant known amplitude** (±20000 L / ±12000 R), never zero, so the pregap
  is the only silence anywhere on the disc.

Generator: §8.6. The waveform/PCM model is shared with
`test/test_cd_synth_cdda.c`; note that the committed CI test uses **4 tracks**
(2 s each) to keep runtime and disc size small.
### 8.2 What the BIOS does with it — both modes, mode taken from the log

`bios` (`[BOOT] CD game, mode=BIOS -- boot ROM forced on`): the full drive
dialogue completes, including the six-track long TOC —

```
DSA_TX $7001  $150A  $0300  $1400  $0301  $1501  $0200  $0101
```

— and the **CD player front-end renders correctly for a 6-track disc**:
STOP/REW/PLAY/FF/PAUSE transport, `TRK 6`, a `TIME` readout showing the disc's
lead-out, `NORMAL` / `NO REPEAT` / `VLM`, and a track grid with cells 1–6 lit.
(The player-UI screenshot was read on an earlier 6-track disc whose lead-out is
`1:21`; the chirp disc's is `1:00`. Everything else about the screen is
identical — the readout tracks the disc, which is the point.) Navigating with **B** (right) to
PLAY and pressing **A** issues `$0101` Play Title and switches to the VLM
screen. That screen is black apart from a `1-4` preset indicator and a
track/time readout. RMS 980, 240573 non-silent samples — *byte-identical to a
run where PLAY is never pressed*. No CD audio reaches the output.

`hle` (`[BOOT] CD game, mode=HLE`): `retro_load_game()` fails —
`[CD-BOOTSTUB] Early exit: loaded=1 numSessions=1` → "unsupported or invalid
content format". This is the documented pre-existing state, Phase 3 scope, and
is **not** a regression from anything here. The VLM is BIOS code; `hle` can
never produce it (§5 trap). There is therefore no HLE run to compare against
for an audio disc, and any "both modes" comparison for the VLM itself is
meaningless by construction.

### 8.3 The SSI head is fine — §3.2 refuted

Instrumenting `SetSSIWordsXmittedFromButch()` (`src/cd/cdrom.c`) directly:

```
[VLMDIAG] SSI live #1      nonzero=0      lrxd=$0000 rrxd=$0000 ssiBlock=1    ptr=0
[VLMDIAG] SSI live #44101  nonzero=44094  lrxd=$0378 rrxd=$0378 ssiBlock=76   ptr=0
[VLMDIAG] SSI live #88201  nonzero=88188  lrxd=$B756 rrxd=$B756 ssiBlock=151  ptr=0
...
[VLMDIAG] SSI live #617401 nonzero=617283 ... ssiBlock=1051
[VLMDIAG] SSI live #661501 nonzero=617283 ... ssiBlock=1126   <- nonzero frozen
[VLMDIAG] SSI live #705601 nonzero=617283 ... ssiBlock=1201   <- inter-track pregap
[VLMDIAG] SSI live #749701 nonzero=661376 ... ssiBlock=1276   <- audio resumes
```

44100 samples/s, `ssiBlock` advancing 75 sectors/s (the drive rate), nearly all
samples non-zero — and the plateau in `nonzero` lands exactly on the 150-sector
pregap the disc has between tracks. The head is streaming the right bytes from
the right place. Positioning it on `$01nn` would fix nothing.

`cdPlaying` is true, `seekDelay` is 0, `ButchIsReadyToSend()` is true (the
`I2S_CTRL $000F` write sets the I2CNTRL bit), JERRY is in slave mode
(`[CDDA] SMODE $0015 -> $0014 (slave: CD -> I2S)`), and the DSP takes the SSI
interrupt 44100 times a second (`ssiAssert` and `ssiDispatch` both advance
44060 per 60 frames). Every stage up to the DSP is working.

### 8.4 The actual blocker: alt-bank R13 bit 30 in the VLM's DSP code

The output is silent because **the DSP writes nothing to LTXD/RTXD**. Counting
`DACWriteWord` calls per frame (`i2sWrites`, reset each frame in
`DACPrepareFrame`):

```
frame 360: i2sWrites=348 dspRun=1 dspPC=$F1B11E flags=$00004461   <- BIOS boot audio
frame 420: i2sWrites=348 dspRun=1 dspPC=$F1B120 flags=$00004461
frame 480: i2sWrites=2   dspRun=0 dspPC=$F1B26E flags=$00000000   <- DSP reloaded: VLM
frame 540: i2sWrites=2   dspRun=1 dspPC=$F1BA5A flags=$00000420
...           (2 = the two seed values DACPrepareFrame writes; zero real writes)
```

Disassembling the DSP RAM at that point: the SSI vector `$F1B010` jumps to
`$F1BC54`, and the handler opens with

```
$F1BC54  LOAD   (R26), R27
$F1BC56  MOVEFA R13, R30
$F1BC58  BTST   #30, R30
$F1BC5A  JR     EQ, $00F1BC66      ; bit 30 clear -> pass-through path
$F1BC5E  LOAD   (R28), R23        ; bit 30 set   -> read LRXD, discard
$F1BC60  MOVEQ  #0, R30           ;                 zero the FFT input
$F1BC64  MOVEQ  #0, R29
$F1BC66  MOVEFA R11, R25          ; pass-through path:
$F1BC6A  LOAD   (R28), R23        ;   R28 = $F1A148
$F1BC6E  IMULT  R25, R23          ;   scale by volume
$F1BC74  STORE  R23, (R28)        ;   R28 += 4 -> write RTXD
$F1BC80  STORE  R23, (R28)        ;   R28 -= 4 -> write LTXD
```

so **bit 30 of alternate-bank R13 gates both the audio pass-through and the FFT
input**. Measured at every ISR entry, it is `$40000000` — set — for the whole
VLM session. It is set by the VLM's own init:

```
$F1BA1A  MOVEI  #$40000000, R00
$F1BA20  MOVETA R00, R13
```

i.e. **the VLM starts muted by design and something must clear the bit**. The
only other writer of alt-R13 in the whole 8 KB of DSP RAM is `MOVETA R25, R13`
at `$F1BD92`, inside the block at `$F1BD34..$F1BE04` — and that block polls
`$DFFF1A`:

```
$F1B9CA  MOVEI  #$00DFFF1A, R00
$F1B9D0  MOVETA R00, R21
...
$F1BD44  MOVEFA R21, R30
$F1BD4C  LOADW  (R30), R29
$F1BD4E  BTST   #4, R29
```

`$DFFF1A` is `SUBDATA+2` — **BUTCH's CD subcode data register**, which this core
does not implement (RAM-backed only; `src/cd/cdrom.c:1486` has had a
`[SUBCODE] read` diagnostic on it for a while). Confirmed live:

```
[SUBCODE] write SBCNTRL+0 = $0000 who=6 68kpc=$080082
[SUBCODE] write SBCNTRL+2 = $00F2 who=6 68kpc=$080082
[SUBCODE] read  SUBDATA+2 -> $0000 who=2 68kpc=$193040     (who=2 = DSP)
```

The VLM's 68K side arms subcode capture via `SBCNTRL`, and its DSP side polls
`SUBDATA` and always reads zero. The `0  0:01` readout on the VLM screen —
**track 0** with a ticking time — is the same story from the other end.

**Not yet proven:** that supplying valid subcode is what clears bit 30. The
static read of `$F1BD92` places the writer inside the subcode block, and the
DSP demonstrably polls the register, but the delay-slot/computed-jump structure
of that block is not safely readable statically. A crude probe (forcing
`SUBDATA+2` reads to return `$0010`, `$0011`, `$FFFF`) changed nothing —
output stayed byte-identical at RMS 980 / 240573 non-silent — so a naive
bit-4 stub is not enough, and the real Q-channel frame format Butch presents
has no local ground truth (MiSTer's `butch.v`, our reference for the DSA
protocol, does not implement subcode either; `test/mister_ground_truth.h`
has the register addresses and nothing more).

### 8.5 Everything downstream of that bit is correct

Diagnostic hack, **not shipped**: clear bit 30 of `dsp_reg_bank_1[13]` on every
SSI interrupt. Same disc, same input script, `bios` mode confirmed from the log:

| | muted (as shipped) | bit 30 forced clear |
|---|---|---|
| non-silent samples / 1200000 | 240573 | **785123** |
| average RMS | 980 | **1268** |
| frame motion, VLM window | 0/60 | **60/60** |
| avg frame change | 0.00 % | **9.8–14.8 %** |
| non-black pixels | 0.3 % | **up to 16.5 %** |

and on screen: a full radial spectrum analyser — concentric rings of coloured
cells pulsing outward, a cyan waveform trace across the middle, the `1-4`
preset indicator and the track/time readout — visibly changing between
screenshots as the chirp climbs. **The Virtual Light Machine runs, renders, and
reacts to the audio.** The one thing between it and working is the mute bit.

That table is the oracle for the eventual fix: a correct implementation must
reproduce those numbers with no hack in the DSP.

### 8.6 Regression test, and the repro

`test/test_cd_synth_cdda.c` (new, in `make test`) pins the part that works, on
its own synthetic disc — it never touches `test/roms/private`, so unlike
`test/test_cd_ssi_stream.c` (which SKIPs without `VJ_SSI_DISC`) it actually
runs on a fresh clone and in CI:

| assertion | negative control | result |
|---|---|---|
| LRXD = left, RRXD = right, sample-aligned from byte 0, across sector boundaries | swap the two reads in `SetSSIWordsXmittedFromButch` | 4 of 5 tests red |
| measured frequency identifies the seeked track, right = 2× left | `CDIntfGetTrackInfo` uses `startLBA` instead of `dataLBA` | 5 of 5 red |
| a redundant `$12xx` does not rewind the head; the chirp keeps climbing | restore `ssiBufPtr = 0` on the redundant-seek branch (pre-#307) | exactly that one test red |
| the pregap is silence and the tone starts at `INDEX 01` | as row 2 | red |

Each control was run at the same git rev with `src/cd/*.o` and the dylib
deleted first (`make` skips rebuilds on second-identical mtimes, and
`VJ_EXPECT_BUILD` cannot catch that when both builds are dirty at the same
rev). Verified green with `test/roms/private` removed, then restored.

Manual repro of §8.2 and §8.5:

```bash
DEVELOPER_DIR=/Library/Developer/CommandLineTools make TEST_EXPORTS=1 -j8
DEVELOPER_DIR=/Library/Developer/CommandLineTools cc -O2 -Wall -std=c99 \
  -I./libretro-common/include -I./src -o test/tools/cd_visual_verify \
  test/tools/cd_visual_verify.c test/harness/harness.c -lm

# 6-track chirp audio CD, real pregaps, L != R -- same PCM model as
# test/test_cd_synth_cdda.c
mkdir -p /tmp/vlmcd && python3 - /tmp/vlmcd <<'EOF'
import os, struct, sys
SECTOR, SAMP_SEC, BASE_INC, MASK = 2352, 588, 85899346, 0xFFFFFFFF
outdir, ntracks, sectors, pregap = sys.argv[1], 6, 600, 150
def gen(track, sectors):
    n = sectors * SAMP_SEC; dinc = (BASE_INC // 2) // n
    ph, inc, buf = 0, track * BASE_INC, bytearray()
    for _ in range(n):
        buf += struct.pack('<hh', -20000 if ph & 0x80000000 else 20000,
                                  -12000 if ph & 0x40000000 else 12000)
        ph = (ph + inc) & MASK; inc = (inc + dinc) & MASK
    return bytes(buf)
cue = ['REM SESSION 01']
for t in range(1, ntracks + 1):
    name = 'track%02d.bin' % t; pcm = gen(t, sectors)
    cue += ['FILE "%s" BINARY' % name, '  TRACK %02d AUDIO' % t]
    if t > 1:
        pcm = b'\x00' * (pregap * SECTOR) + pcm
        cue += ['    INDEX 00 00:00:00', '    INDEX 01 00:02:00']
    else:
        cue += ['    INDEX 01 00:00:00']
    open(os.path.join(outdir, name), 'wb').write(pcm)
open(os.path.join(outdir, 'musiccd.cue'), 'w').write('\n'.join(cue) + '\n')
EOF

# Boot the real CD BIOS.  B moves the transport cursor right (STOP -> REW ->
# PLAY), A activates -- that is what issues $0101 Play Title.  Pressing A
# from the default cursor position goes straight to the VLM WITHOUT playing.
mkdir -p /tmp/vlmshots
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) VJ_CD_TRACE=1 VJ_CD_TRACE_LIVE=1 \
VJ_HARNESS_LOG_INFO=1 VJ_HARNESS_LOG_DEBUG=1 \
  ./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
  /tmp/vlmcd/musiccd.cue --option virtualjaguar_cd_boot_mode=bios \
  --frames 1500 --outdir /tmp/vlmshots --shot-every 300 \
  --press 700:b --press 760:b --press 820:a
for f in /tmp/vlmshots/*.ppm; do sips -s format png "$f" --out "${f%.ppm}.png"; done
```

Traps, both of which cost time here:

- **`--bios` does not select the CD boot mode.** Only
  `--option virtualjaguar_cd_boot_mode=bios` does. Confirm from the run's own
  `[BOOT] CD game, mode=...` line, never from the flag.
- **Pressing A immediately enters the VLM without starting playback** (`$0101`
  never goes out), which looks exactly like the failure being investigated.
  Drive PLAY explicitly: `--press N:b --press N+60:b --press N+120:a`.

### 8.7 What is left

*(Items 1 and 2 are RESOLVED — see §8.8.  Items 3 and 4 remain.)*

1. ~~**Find what clears alt-R13 bit 30**~~ — answered in §8.8: a CRC-valid
   Q-subcode frame whose CONTROL nibble says "audio track".
2. ~~**Probably: model BUTCH's subcode Q-channel.**~~ — implemented, data-only
   as prescribed (no SBCNTRL bit 2/3 interrupts are asserted).
3. **Phase 3 (loader/frontend acceptance in `hle` mode)** and **Phase 4
   (transport: `$02` Stop, `$04`/`$05` Pause/Unpause, `$51` Volume, track
   sequencing, repeat modes)** are unchanged from §6.
4. **RetroArch verification is still owed.** Everything above is headless.

### 8.8 Third pass — RESOLVED: bit 30 is the Q-channel "data track" flag, and the VLM unmutes with real subcode

Investigated and implemented on `feat/291-vlm-subcode` off `develop` @
`f36ae03` (v3.1.0), 2026-08-05.  The §8.4 hypothesis is **proven**, by
implementation: serving Q subcode — nothing else changed, no DSP hack —
clears bit 30 and reproduces the §8.5 oracle numbers.

**Why the static read failed before, in one line:** the bit-30 writer
`$F1BD92 MOVETA R25, R13` is the **delay slot of the `JUMP T,(R30)` at
`$F1BD90`** — it belongs to the code path that jumps *to* `$F1BD8E`, not to
any fall-through — and that path is the tail of the store-a-CRC-valid-Q-frame
handler at `$F1BD94`.  R25 there holds the **first 4 assembled Q bytes**
(CONTROL/ADR, track, index, min), so bit 30 of alt-R13 is bit 6 of Q byte 0 =
**Q CONTROL bit 2, the Red Book "data track" flag**.  The VLM boots with
alt-R13 = `$40000000` — "assume data track, stay muted" — and unmutes the
instant a CRC-valid frame with an audio CONTROL nibble arrives.  It is a
real CD player's data-track mute, working exactly as designed.

**The recovered SUBDATA wire format** (from disassembling the deserializer
state machine at `$F1BD42..$F1BE02` in a live DSP RAM dump, plus the
constants its init left in the alternate bank):

- The DSP `LOADW`s **`$DFFF1A`** (SUBDATA low word) and expects
  `(Q_byte << 8) | $10 | seq`: bit 4 = valid, bits 3..0 = byte sequence
  0..11 within the 12-byte Q frame.  It polls with a ~1 ms cooldown
  (40 ISR entries) and dedupes on the sequence tag, so re-reads are free.
- Bytes are CRC'd as they arrive with table-driven **CRC-16/CCITT, poly
  `$1021`, init 0** (table at DSP `$F1C400` — verified identical to the
  standard table), and after byte 11 the running CRC over ALL 12 bytes must
  equal **`$1D0F`** (alt-R10) — the standard residue when the stored CRC is
  **inverted** per Red Book.
- A CRC-valid frame's 12 bytes are stored to **`$F1C000 + ADR*16`**
  (alt-R09 base; ADR 1 = position frames → `$F1C010`), which is where the
  68K reads the on-screen track/time readout — the `0  0:01` display was
  this buffer never being written.
- The 68K side arms capture by writing **`$00F2` to SBCNTRL's low word**;
  the individual bits remain undocumented.

**Implemented** (`src/cd/cdrom.c` + a lookup helper in `src/cd/cdintf.c`):
a data-only Q serializer clocked off the SSI sample stream — 588 samples
per sector / 12 bytes = one Q byte per 49 samples, so Q position is locked
to the audio actually playing.  Reads of SUBDATA+2 return the word above
when armed (nonzero SBCNTRL low-word write) and playing; disarmed, stopped,
seeking, or paused reads fall through to the RAM-backed zeros exactly as
before, and **no subcode interrupt is ever asserted** (BUTCH bits 2/3
untouched, per §8.7's warning).  Frames are mode-1/ADR-1, BCD, CONTROL
`$01` for audio tracks / `$41` for data tracks, INDEX 00 with countdown in
pregaps, CRC inverted.  No savestate fields: arming re-derives from the
serialized SBCNTRL register on load, everything else resyncs within one
sector because the DSP ignores unexpected sequence numbers.

**Proof, same disc and input script as §8.5, no forced-unmute hack:**

- alt-R13 at the SSI ISR: `$40000000` until Play, then `$01010100`
  (audio/ADR1, track 01, index 01) — bit 30 clear.  Later `$01020000`
  (track 2 pregap) and `$01020100` (track 2 program) as playback crosses
  tracks: the mute bit is being driven by live Q data.
- `cd_visual_verify`, 2100 frames: **avg RMS 1267** (oracle: 1268),
  VLM-window motion **60/60**, avg frame change **10.4–16.5%** (oracle
  9.8–14.8%), non-black up to 26%.  The motion dips land exactly on the
  disc's two inter-track pregaps — the spectrum collapses during pregap
  silence and resumes, i.e. the FFT is chewing on the actual CD audio.
- Screenshots (read, not inferred): full radial spectrum analyser with the
  `1-4` preset indicator, and the track/time readout now advancing —
  `1 0:03` → `2 0:03` → `3 0:03` across the run (track-relative time, per
  Q bytes 3..5).  It was `0  0:01` frozen.

**Regression pinning:** `test/test_cd_synth_subq.c` (in `make test`)
re-implements the DSP's consumer contract independently and asserts the
word format, the 49-sample pacing, the `$1D0F` residual, BCD position
content, pregap INDEX 00, advancement, disarmed-silence and stop-mutes.
All six tests have run-verified negative controls (gate without the arm
check, valid bit dropped, CRC not inverted, frame never rebuilt → each
turns exactly the documented tests red at the same rev with `cdrom.o` and
the dylib deleted between builds).

**Still open** (unchanged): §8.7 items 3 and 4 — HLE-mode loader acceptance,
transport polish, and RetroArch (non-headless) verification.  On real
hardware subcode also flows during data-track streaming and while the VLM
runs on game-disc session-1 audio; nothing in the boot matrix arms SBCNTRL,
so game discs take byte-identical paths (gate = the matrix run, not this
paragraph).

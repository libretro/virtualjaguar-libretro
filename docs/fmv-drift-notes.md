# FMV scene-jump drift (#297) — measurement notes

Investigation of issue #297, "CD FMV titles: scene-jump schedule drift, no
clean full playthrough (Dragon's Lair / Space Ace / BrainDead 13)", against
`libretro/develop` @ `0ecf924`.

**Verdict up front.** The drift *mechanism* named in the ticket does not
survive measurement: the FMV presentation clocks are locked to the video
field, not to the CD stream, so their ratio to delivered bytes is fixed by
construction, and 5.5-minute headless runs are bit-deterministic and
exactly periodic — no accumulating jitter anywhere. Two premises stated in
the ticket are factually wrong and are corrected in §2.

What *is* real is a constant-rate deficit, and it is confined to one path:

| path | sustained rate | vs 352 800 B/s hardware |
|---|---|---|
| HLE (`jagcd_hle.c`) | 352 799.911 B/s | **-0.000025 %** — effectively exact |
| real BIOS (`cdrom.c` FIFO) | 348 881 B/s | **-1.11 %** |

Dragon's Lair demands **349 590 B/s** while streaming (§5.1), so the
real-BIOS path runs **0.20 % below the game's demand** and the HLE path
runs 0.92 % above it. That is the one measured number in this
investigation that points at a defect, it is in the boot mode the reporter
said also fails, and its mechanism is identified in §3.2. It was **not
fixed** — see §7 lead 1 for why. Remaining leads are ranked in §7.

---

## 1. Method

Purpose-built probe (scratchpad, not committed) on top of
`test/harness/harness.h`, dumping one CSV row per emulated field:

* watched main-RAM 32-bit big-endian words (`--watch $ADDR`, repeatable)
* HLE stream state — `JaguarCDHLEStreamArmCount` / `…Active` / `…Bytes` /
  `…Dest`
* `CDROMDiagGetCounters` — `butchExec`, `fifoIRQs`, `dsaIRQs`, `fifoReads`,
  `seeks`
* `tomRam8` PIT0/PIT1 (`$F00050/52`), VP (`$F0003E`), HP (`$F00028`)
* full 2 MB main-RAM snapshots at chosen fields, for counter hunting

`butchExec` is the halfline counter (`BUTCHExec()` runs once per halfline
from `HalflineCallback`), so it converts field counts to exact emulated
time: **1 emulated NTSC field = 524.0000 halflines = 16.65155 ms**,
measured, not assumed.

Disc images: `test/roms/private/Jaguar CD/BinCue/`. Runs: Dragon's Lair
20 000 fields HLE and real-BIOS (≈5 min 33 s emulated each), BrainDead 13
12 000 fields HLE, Space Ace 2 000 fields HLE, plus a `1x / 2x / 4x`
`virtualjaguar_cd_read_speed` sweep and a scripted-input gameplay run.
Real-BIOS delivery was measured separately with `VJ_CD_TRACE=1
VJ_CD_TRACE_LIVE=1` (618 925 events over 2 500 fields); every
`FIFO_DRAIN` event carries `tick` (halfline) and `block` (LBA), which is a
direct read of disc-position advance versus emulated time.

---

## 2. Corrections to the ticket's premises

### 2.1 `$129AD6/$129ADE` is not a clock

The ticket and `docs/cd-known-issues.md` §2 state "Dragon's Lair's clock at
`$129AD6/$129ADE` advances +7680/s". It does not. Sampling those two longs
every field gives non-monotonic garbage (`4179773776`, `1937340794`,
`88016133`, …) — they are movie payload, not a counter.

An exhaustive scan of all 2 MB of main RAM (every 2-byte-aligned 32-bit
word, three snapshots at fields 1800 / 1860 / 1920, keeping only words
whose two deltas are positive and in exact proportion) finds **exactly four
constant-rate monotonic longs in the whole address space**, and
`$129AD6/$129ADE` is not among them:

| Title | Address | value @f1800 | delta / 60 fields | per field |
|---|---|---|---|---|
| Dragon's Lair | `$00562E` | 661 | 30 | 0.5 (integer part) |
| Dragon's Lair | `$005630` | 43380792 | 1962324 | 32705.4 (16.16) |
| Dragon's Lair | `$005688` | 662 | 30 | 0.5 (integer part) |
| Dragon's Lair | `$00568A` | 43384832 | 1966080 | **32768.0 exactly** |
| Space Ace | `$00582E` / `$005830` | 661 / 43380792 | identical | identical |
| Space Ace | `$005888` / `$00588A` | 662 / 43384832 | identical | identical |

`$562E`/`$5630` is the 48-bit fixed-point presentation clock the poll loop
at `$004C0A` reads (`move.l $562E,d0 / cmp.l d0,d2 / bgt`), already
identified in `docs/cd-boot-matrix.md` §"Re-run notes 2026-07-29" — that
disassembly-derived address is the correct one and supersedes the
counter-hunt note. `$5688`/`$568A` is a second accumulator advancing at
*exactly* 0.5 per field.

Space Ace is the same ReadySoft player relinked +512 bytes, and its two
accumulators hold **bit-identical values at the same field number** as
Dragon's Lair's — both engines start their attract movie at the same field
and run the same arithmetic.

BrainDead 13's counter at `$72DA` is a plain integer, +1 per 2.4957 fields
= 24.06 Hz — the 24 fps film rate its player targets.

### 2.2 The presentation clock is video-locked, not CD-locked

TOM's PIT is **disabled** for the whole run (`PIT0 = 0` in every sampled
field, in both boot modes), so the "GPU PIT ISR" attribution in
`docs/cd-boot-matrix.md` is wrong about the source. The clock advances by a
byte-exact **32760 (`$7FF8`) per field, once per field, with no jitter**:

```
DL per-field raw delta histogram, 1800 fields HLE 2x:
  32760 x 1402    13100 x 265    0 x 113    29484 x 12    (rest <5 samples)
```

Two regimes (32760 and 13100) correspond to two movie segments; within a
segment the delta is the same integer every single field. A counter driven
by an asynchronous timer cannot do that — it is a video-interrupt ISR
adding a per-title constant once per field.

**Consequence:** the presentation clock and the CD byte stream are both
functions of emulated time only. Their ratio is fixed by construction, so
"the game's delivery clock drifts against our transfer pacing" cannot
happen through this mechanism. Confirmed empirically — the per-field delta
histogram is *identical* at `cd_read_speed` 1x, 2x and 4x:

| read speed | 32760/field | 13100/field |
|---|---|---|
| 1x  | 1362 | 291 |
| 2x  | 1402 | 265 |
| 4x  | 1423 | 249 |

(The small counts differ only because a faster/slower disc reaches scene
boundaries at different fields, not because the rate changed.)

---

## 3. Measured delivery rates versus hardware

Hardware reference: a Jaguar CD disc is recorded as audio-type 2352-byte
sectors; double speed = 150 sectors/s = **352 800 B/s**. Clock math from
`docs/jtrm-clocks-timing.md` (NTSC system clock 26.590906 MHz, horizontal
period 63.5556 us, so halfline = 31.777777 us).

### 3.1 HLE path — `src/cd/jagcd_hle.c`

`HLE_STREAM_BYTES_PER_SEC 352800.0`, converted to a 16.16 per-halfline
budget in `JaguarCDHLEStreamTick()`:

```
inc = trunc(352800 * 31.777777 / 1e6 * 65536) = 734737  (= 11.2111969 B/halfline)
effective rate = 734737 / 65536 / 31.777777e-6 = 352 799.911 B/s
error vs hardware = -0.000025 %
```

PAL: `inc = 739875` → 352 799.892 B/s, same error. **The HLE cadence
constant is correct to five decimal places. There is nothing to fix here.**

Measured end-to-end over the 20 000-field DL run: every steady-state
transfer is 1 047 820 bytes completing in 178–179 fields
(93 272 / 93 796 halflines) = 351 543–353 518 B/s, the spread being pure
field-sampling granularity.

### 3.2 Real-BIOS path — `src/cd/cdrom.c`

`FIFO_REFILL_PERIOD_X100 285` paces a 32-byte FIFO batch:

```
exact hardware period for 32 B = 32 / 352800 = 90.702948 us = 2.854289 halflines
code uses 2.85 (error-diffused in hundredths)  ->  353 330.9 B/s
error vs hardware = +0.1505 %
```

That 353 331 B/s is the **armed ceiling, not the achieved rate**. Measured
from the live trace (per-second buckets of sequential LBA advance,
excluding seek discontinuities), while the drive is continuously streaming,
the buckets *alternate* — they are a duty cycle, not a fast case with
outliers:

```
t=14s 352800  t=15s 355152  t=16s 338688
t=17s 352800  t=18s 355152  t=19s 338688
t=20s 352800  t=21s 355152  t=22s 338688
t=23s 352800  t=24s 352800  t=25s 341040
t=26s 352800  t=27s 355152  t=28s 338688
t=29s 352800  t=30s 352800  t=31s 341040
```

Integrated over that whole 18-second continuous-streaming window:

```
18.0000 s, 196442 drains, 2670 sectors
  -> 348 881 B/s  = 98.889 % of hardware  (-1.11 %)
  -> 10 913.4 drains/s (armed ceiling 11 041)   -1.16 %
  ->     31.968 bytes per drain (model 32)      -0.10 %
```

Sub-windows agree: 14-24 s = 349 276 B/s, 24-32 s = 348 390 B/s,
17-31 s = 349 443 B/s. **Supply-limited, not demand-limited** — most
buckets sit exactly on the armed ceiling, meaning the GPU ISR is waiting on
the FIFO rather than the reverse.

Mechanism of the -1.16 % drain-rate loss: `fifoFillDelay` is re-armed only
*after* the drain completes (`cdrom.c` FIFO_DATA read path,
`fifoReadCount >= FIFO_DRAIN_READS` -> `fifoFillDelay =
CDROMNextRefillDelay()`). GPU-ISR latency between `FIFO_FILL` and the 16th
read therefore *adds* to the refill period instead of overlapping it. Real
BUTCH fills the FIFO continuously at the I2S rate, independent of when the
ISR happens to drain it.

**So: `HLE_STREAM_BYTES_PER_SEC` is effectively exact (-0.000025 %) and is not responsible for
anything. `FIFO_REFILL_PERIOD_X100` is also very nearly right (+0.15 %) —
but the state machine around it gives back 1.26 %, netting -1.11 %, and
that is below Dragon's Lair's 349 590 B/s demand.** The offending code is
exactly what `docs/cd-known-issues.md` warns encodes the DSA-steal and
FIFO-storm races, which is why §7 lead 1 does not act on it blind.

---

## 4. Constant-rate versus jitter: the discriminator

Two independent tests, both negative for drift.

**(a) Determinism.** Two DL HLE runs with identical arguments produce
byte-identical CSVs (`cmp` clean, 1800 rows). Emulated time has no
non-determinism to accumulate.

**(b) Periodicity over 5.5 minutes.** Segmenting the run by presentation-
clock resets (a reset = a new scene) gives an attract loop that repeats
with a period of **3005–3006 fields, six times, with the same internal
structure and the same end-of-scene clock value to two decimal places** —
in both boot modes:

```
HLE                                     real BIOS
  475- 3140 (2665f) -> 1325.965          914- 3579 (2665f) -> 1325.97
 3141- 3479 ( 338f) ->   94.512         3580- 3919 ( 339f) ->   94.95
 3480- 6145 (2665f) -> 1325.905         3920- 6584 (2664f) -> 1325.95
 6146- 6485 ( 339f) ->   94.862         6585- 6924 ( 339f) ->   94.90
 6486- 9150 (2664f) -> 1325.845         6925- 9589 (2664f) -> 1325.93
 9151- 9490 ( 339f) ->   94.712         9590- 9929 ( 339f) ->   94.85
 9491-12156 (2665f) -> 1325.985         9930-12594 (2664f) -> 1325.91
12157-12495 ( 338f) ->   94.562        12595-12934 ( 339f) ->   94.80
12496-15161 (2665f) -> 1325.925        12935-15599 (2664f) -> 1325.89
15502-18166 (2664f) -> 1325.865        15940-18604 (2664f) -> 1325.87
```

A constant-rate mismatch would show as a monotonically shifting scene
length; accumulating jitter would show as growing variance. Neither is
present — the +-1 field wobble is the clock's fractional part crossing a
field boundary, and it does not accumulate (the 6th cycle is the same
length as the 1st). BrainDead 13 is likewise exactly periodic at
3262–3263 fields per attract cycle.

**Verdict: no accumulating jitter, and no clock-versus-data drift of the
kind the ticket describes — that mechanism is structurally impossible once
§2.2 establishes the clock is field-locked. There *is* a constant-rate
deficit, but only on the real-BIOS path (§3.2), and it is small enough
(-1.11 %) that the attract loop absorbs it: the BIOS-mode scene structure
above is identical to HLE's, so this evidence does not by itself prove the
deficit is user-visible.**

**Scope limit:** the attract loop is *sequential* streaming. It contains no
scene branches, therefore no seeks. These periodicity and determinism
results say nothing about branch behaviour — see §6.

---

## 5. What the measurement *did* expose

### 5.1 Dragon's Lair runs the drive at 99.1 % duty — no slack anywhere

Steady state, HLE 2x, from the 20 000-field run:

* the game arms a CD_read of **1 047 820 bytes every 180 fields**, and the
  arm cadence is **not completion-driven** — at 4x the transfer finishes in
  89 fields but the next arm still comes at field 180/240. It is not a
  fixed schedule either: the 4x gaps are 253/236/240/180/49/238/240/180/48,
  where the 48-49 field gaps are mid-run re-arms that never occur at 2x;
* the transfer occupies **178–179 of those 180 fields**;
* demand rate = 1 047 820 B / (180 x 16.65155 ms) = **349 590 B/s =
  99.09 % of the 352 800 B/s hardware rate**;
* margin per read = **9 620 bytes = 0.92 %**, i.e. under two fields.

This is the real fragility: any perturbation worth more than ~1 % of the
drive rate — anywhere in the chain, not just in the CD code — pushes a
read past its deadline. When that happens the game arms the next CD_read
while the previous one is still in flight, and `jagcd_hle.c` silently
overwrites `hleStream` (no `HLEStreamFinish()`, no completion flags, the
undelivered tail is simply lost). In the 20 000-field run that overlap
happens exactly 4 times, all at attract-loop restarts (fields 8953, 11958,
14964, 17969 — spacing 3005/3006/3005), i.e. the game deliberately
abandoning a read to branch. Benign here, but it is the mechanism by which
*any* future timing regression would turn into corrupted movie data rather
than a visible stall. Worth a diagnostic log line at minimum
(see §7).

### 5.2 The emulated NTSC field is one halfline short — unverified, out of scope

`src/tom/tom.c:1159` sets `VP = 523`, i.e. 524 halflines per field:

```
524 halflines x 31.777777 us = 16.65155 ms -> 60.05445 Hz
525 halflines x 31.777777 us = 16.68334 ms -> 59.94006 Hz  (NTSC standard)
```

PAL is the same shape (`VP = 623` -> 624 halflines -> 50.08 Hz instead of
50.00 Hz). `HP` is exactly right — `HP = 844` gives
(844+1) x 2 / 26.590906 MHz = 63.5556 us, the NTSC line period — and the
comment above `HalflineCallback` in `src/core/jaguar.c:880` says the field
should be "525 for NTSC, 625 for PAL", so the code disagrees with itself.

If 525 is correct, every field-locked game clock — including these FMV
presentation clocks — runs **0.1905 % fast relative to the CD byte
stream** compared with hardware: 5874.67 B/field instead of 5885.88, which
eats 2 018 of Dragon's Lair's 11 638-byte hardware margin (17 % of its
slack). Direction is right for the reported symptom; magnitude is roughly
a fifth of the available margin, so it cannot on its own explain "no clean
playthrough".

**Do not act on this from this ticket.** Two reasons:

1. **Unverified.** The JTRM PDFs in `docs/atari-jaguar-1999/` are
   image-only (`pdftotext` yields 31–103 bytes), so the register value
   could not be checked against the manual. And Alien vs Predator — a cart
   title that boots through the real Jaguar boot ROM and demonstrably
   reprograms the video registers (HP moves 844 -> 1735) — also runs with
   `VP = 523`, so 523 may well be what real Jaguar software programs, and
   a real Jaguar NTSC field may genuinely be 60.05 Hz.
2. **Not small.** Changing it moves every title's speed and audio/video
   sync by 0.19 % and interacts with `info->timing.fps = 60` in
   `libretro.c:911`. That is a corpus-wide timing change needing its own
   ticket, its own JTRM confirmation, and a full A/B sweep — not a rider on
   an FMV bug.

---

## 6. What could not be measured

* **The reported symptom itself was never reproduced.** Attract playback of
  Dragon's Lair (HLE and real BIOS) and BrainDead 13 (HLE) is exactly
  periodic for 5.5 minutes; a scripted-input gameplay run enters the game
  and then loops a die/retry sequence of exactly 118 + 203 + 848 fields
  four times over, with no variation. Everything measured headlessly is
  deterministic and repeatable. "Occasionally jumps scenes early/late" was
  not observed in emulated time.
* **Seek / branch latency — the structural blind spot.** Everything
  measured here is *sequential* streaming: the attract loop never branches,
  so it never seeks. That excludes the one mechanism that fires only at
  scene branches, which is exactly where "jumps scenes early/late" lives:

  - `cdrom.c:52` `SEEK_DELAY_TICKS 100` = ~3.2 ms, against the 30-315 ms
    the file's own comment cites from MiSTer/hardware — **10x to 100x
    short**, and the comment says so explicitly ("shortened for software
    emulation but preserve the required ordering").
  - The HLE `CD_read` path has **no seek model at all**: `hleStream` is
    armed and starts delivering the same halfline.

  A dead-reckoning FMV engine that budgets a fixed number of clock ticks
  between "issue branch seek" and "first byte of the new scene" would see
  data arrive up to 300 ms (9-18 movie frames) early at every branch, in
  both boot modes. Untested. Testing it needs a scripted branch, a trace of
  `SEEK_START` -> first `FIFO_FILL` for the real-BIOS path, and a
  hardware/BigPEmu reference to say what the correct latency is.
* **Real hardware ground truth for scene timings.** No reference capture,
  so "our scene is N fields long, hardware's is M" cannot be stated. Nor
  can the correct seek latency be pinned beyond the manual's 30-315 ms
  range.
* **Space Ace beyond boot.** Its clock pair was located
  (`$00582E`/`$005830`) and matches Dragon's Lair bit-for-bit at the same
  field, but no long run was made.
* **VP against the JTRM** — see §5.2, the PDFs have no text layer.
* **Anything above the core.** Frontend-side pacing (RetroArch audio-sync
  resampling, frame duping/dropping when the host cannot hold 60 fps,
  fast-forward, run-ahead) is invisible to a headless harness and is not
  ruled out. Given that everything inside the core is deterministic, this
  is now the most likely home for an *"occasional"* symptom.

---

## 7. Remaining leads, in priority order

1. **Close the real-BIOS -1.11 % (§3.2).** This is the only measured
   deficit, it is in a mode the reporter says fails, and it sits 0.20 %
   below Dragon's Lair's demand. The fix is to stop charging GPU-ISR
   latency to the refill period — arm the next `fifoFillDelay` from the
   *previous fill* rather than from drain completion, so the refill clock
   free-runs the way BUTCH's I2S fill does. Small in diff, large in blast
   radius: it is precisely the code `docs/cd-known-issues.md` names as
   encoding the DSA-steal and FIFO-storm races.

   **Deliberately not done here.** With no reproduction of the symptom,
   "it improved" would be unfalsifiable, and the only available evidence
   (identical BIOS/HLE attract structure, §4) says the current -1.11 % is
   already absorbed. Do this only behind the full CD regression set below,
   and only once there is a failing case to measure against.
2. **Measure the seek/branch path** (§6). The untested mechanism, and the
   only one whose signature matches "jumps scenes *early*". Cheapest real
   experiment left: script a branch, trace `SEEK_START` -> first
   `FIFO_FILL`, compare against the 30-315 ms the manual gives, and check
   what the HLE path's zero-latency arm does to the same branch.
3. **Get a user-side reproduction with a log.** The symptom is
   "occasional"; nothing inside the core is. Ask for the RetroArch log
   (`crash_detect` signatures), the frontend's audio-sync / frame-throttle
   settings, and whether the host holds 60 fps. If it only happens under
   frame drops, this is a presentation-layer bug, not a CD-pacing one.
4. **Instrument the silent overlap in `jagcd_hle.c`.** Arming a CD_read
   while `hleStream.active` discards the in-flight remainder with no trace.
   A one-line `LOG_WRN` (bytes delivered / bytes requested / dest) turns
   the failure mode in §5.1 from invisible into a grep. Cheap, no
   behavioural change, and it is the line that would prove or kill any
   starvation theory from a user log.
5. **Settle `VP` in its own ticket** (§5.2), with JTRM confirmation from a
   text-bearing copy or an OCR pass, plus an A/B framebuffer sweep across
   the cart corpus.

### Regression risk if any of this is acted on

| Title | Exposure |
|---|---|
| Hover Strike | Streamed CD_read is what keeps its LVL overlay from stomping its own poll loop; any rate change re-tests the instant-read lockup class. |
| Myst | Long idle windows after transfers; a refill-cadence change re-tests the benign `cd_seek_wedge` boundary. |
| Primal Rage | DSA-steal race is tuned against the current response/refill cadence (`DSA_RESPONSE_DELAY_TICKS 4`). |
| Iron Soldier 2 | Match-load checksums run *through* the streamed write pointer; anything touching stream granularity re-tests it. |
| Battle Morph | 917 KB read concurrent with GPU-worker upload; the per-read `statusBase` latch assumes one live stream. |

Leads 2, 3 and 4 are behaviour-neutral (measurement and logging only) and
touch none of these. Leads 1 and 5 require the full set.

---

## 8. Reproducing these numbers

The probe is scratchpad-only by design (it is a measurement instrument, not
a test). To rebuild it:

```c
/* per field: --watch $ADDR (repeatable), --ramdump FRAME, --ramdump-prefix P */
harness_dlsym(cfg, "jaguarMainRAM")            /* uint8_t ** — deref it */
harness_dlsym(cfg, "JaguarCDHLEStreamArmCount|…Active|…Bytes|…Dest")
harness_dlsym(cfg, "CDROMDiagGetCounters")     /* butchExec = halfline clock */
harness_dlsym(cfg, "tomRam8")                  /* +0x3E VP, +0x50/52 PIT */
```

```bash
DEVELOPER_DIR=/Library/Developer/CommandLineTools make TEST_EXPORTS=1 -j10
cc -O2 -Wall -std=c99 -I. -I./test -I./libretro-common/include \
   -o fmv_clock_probe fmv_clock_probe.c test/harness/harness.c -ldl -lm

VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./fmv_clock_probe \
   ./virtualjaguar_libretro.dylib "…/Dragon's Lair (USA).cue" \
   --frames 20000 --system-dir test/roms/private \
   --watch 562E --watch 5630 --csv-out dl.csv

# real-BIOS delivery rate: every FIFO_DRAIN carries (halfline tick, LBA)
VJ_CD_TRACE=1 VJ_CD_TRACE_LIVE=1 VJ_HARNESS_LOG_INFO=1 ./fmv_clock_probe … \
   --option virtualjaguar_cd_boot_mode=bios
```

Note `CDROMDiagGetCounters`'s `hleBytes` output is dead (`cdrom.c:871`
hard-codes 0 — "HLETransferTick removed"); use the arm counter plus
`JaguarCDHLEStreamBytes`, or the trace ring, instead.

---

## 8. Branch/seek path measured (2026-08-04) — §6's blind spot closed

§6 named seek/branch latency "the structural blind spot": every earlier
measurement was of the *sequential* attract loop, which never branches and
therefore never seeks. That gap is now closed. Tooling:
`test/tools/fmv_seek_probe.c` (committed) plus a `--press` script that
drives Dragon's Lair into gameplay and loops its die/retry branch.

### 8.1 The branch script reproduces prior results in BOTH boot modes

```
HLE   --press 500:pause --press 560:pause --press 700:a
BIOS  --press 939:pause --press 999:pause --press 1139:a   (BIOS boot is +439 fields)
```

Both give the same die/retry cycle §6 reported — **849 / 118 / 204 fields**,
repeating, endclk 423.7 / 58.6 / 101.7. Attract runs still reproduce the
§4 table exactly (2666f -> 1325.97, 339f -> 94.51; BIOS 2666f -> 1325.97,
340f -> 94.95), and 1 field = 524.0 halflines as before.

### 8.2 Branch seeks are near-full-stroke, and are served in 3.18 ms

Real-BIOS trace (`VJ_CD_TRACE=1 VJ_CD_TRACE_LIVE=1`), DL branch run.
`SEEK_DONE` always lands exactly `SEEK_DELAY_TICKS` = 100 ticks = **3.178 ms**
after `SEEK_START`, regardless of distance:

| context | seek distance (sectors) |
|---|---|
| steady-state streaming read | ~340-430 |
| attract-loop restart | 5 171 |
| **death/retry branch** | **130 361 - 138 035** |

Each death/retry does two near-full-stroke seeks (out to LBA ~154 000, back
to 20 491 about 128 fields later) plus a 1 641-sector step. A physical CD
drive cannot cross ~130 000 sectors in 3.18 ms; `cdrom.c:42` cites
30-315 ms from MiSTer in the very comment block that then defines 100 ticks.
The HLE path (`jagcd_hle.c`) still has **no seek model at all** — `seeks=0`
for the whole run, in attract and at branches alike.

Space Ace and BrainDead 13 (BIOS, attract) show the same shape: a first
seek from `block=0` of 15 479 / 22 656 sectors, then ~360-600 sector
sequential steps.

### 8.3 Seek latency displaces branch scenes 1:1 — but only pads a black gap

Experiment: distance-tiered seek delay, `>10 000` sectors -> 9 440 ticks
(~300 ms), everything else unchanged at 100. DL, BIOS, same branch script:

| scene | flat 100t | tiered | delta | endclk flat | endclk tiered | delta |
|---|---|---|---|---|---|---|
| s1 | 255 | 273 | **+18** | 126.87 | 135.87 | +9.00 |
| s2 | 204 | 222 | **+18** | 101.73 | 110.72 | +9.00 |
| s3 | 849 | 849 | 0 | 423.70 | 423.70 | 0.00 |
| s4 | 118 | 136 | **+18** | 58.59 | 67.58 | +8.99 |
| s5 | 205 | 223 | **+18** | 101.98 | 110.92 | +8.94 |

+18 fields = 18 x 16.65 ms = **300 ms — exactly the delay injected**, and
the field-locked clock advances +9.0 (18 x 0.5/field) to match. So branch
scene boundaries *are* gated by seek completion, one-for-one. §4's "no
clock-versus-data drift" verdict concerned sequential playback and is not
contradicted: this is a branch-only coupling.

**But this is an accounting identity, not a demonstrated fix.** PPM capture
of the affected window (probe's `FMV_SHOTDIR`/`FMV_SHOT_FROM/TO/EVERY`)
shows fields 1160 and 1180 are **fully black** and the "LIVES 5" retry card
appears at 1200: the injected 300 ms lands inside an already-black
inter-scene load gap. The branch content plays identically, just later.
Nothing shows a mis-timed scene being corrected.

### 8.4 Why the tiered model was NOT shipped

1. **No hardware reference for the magnitude.** MiSTer's 30-315 ms is a
   secondhand comment in our own source. `06 - Jaguar CD-ROM.pdf` has no
   text layer (`pdftotext` -> 39 bytes); read as images, p.7 fn.4 and p.12
   give landing *uncertainty* (start the read 6 blocks early, the partition
   marker may be anywhere in the first 31 blocks = 72 912 bytes) — that is
   positional tolerance, not access time. No JTRM section states a seek time.
2. **The only visible effect is a longer black pause** (§8.3), with no
   reference saying how long that pause should be.
3. **Boot blast radius.** The tier keys on `|target - block|`, and `block`
   is 0 at reset (`cdrom.c:794`). Space Ace's and BrainDead 13's *first*
   seeks are 15 479 / 22 656 sectors, so a 10 000-sector threshold fires the
   long tier on the boot seek of essentially every CD title. DL absorbed it
   (its boot path has slack — first four seek ticks were bit-identical), but
   Myst / Hover Strike / IS2 / Primal Rage / Battle Morph are untested
   against it. See §7's regression-risk table.
4. The 10 000-sector threshold is arbitrary, and it classifies the
   5 171-sector attract-restart seek as "short" — an asymmetry that is
   itself evidence the calibration is not sourced.

Shipping it would be unfalsifiable in exactly the way §7 lead 1 warns
about. It stays a lead, now with numbers.

### 8.5 New finding: HLE discards 25-77 % of in-flight reads at branches

§7 lead 4 (instrument the silent `hleStream` overwrite) is **done**
(`jagcd_hle.c`, `LOG_WRN`, no behaviour change). In a 4 000-field DL
branch run it fires **8 times**:

```
237385/1047820 bytes delivered, dest $03E2F4 -- tail discarded   (77 % lost)
756901/1048576 ... $03E000   (28 %)     554046/1048060 ... $03E204   (47 %)
695665/1048576 ... $03E000   (34 %)     757406/1048576 ... $03E000   (28 %)
```

§5.1 saw this 4 times in a 20 000-field *attract* run; under branching it is
~10x more frequent. Caveat before this becomes a second unfalsifiable
theory: a game abandoning a read to branch does the same thing on hardware
(new `CD_read` -> drive reseeks -> old delivery stops). Nothing here shows
our handling differs from hardware. It is a lead with numbers, not a bug.

### 8.6 Still not answered

The reported symptom remains unreproduced as a *defect*: everything
measured is deterministic and repeatable in both modes, and no reference
capture exists for what DL's branch timing should be. Ranked leads §7.1
(real-BIOS -1.11 %), §7.3 (user-side log / frontend pacing) and §7.5 (`VP`)
are untouched by this pass. What #297 most needs now is **hardware or
BigPEmu ground truth for branch-scene timing** — without it, any seek-latency
value is unfalsifiable.

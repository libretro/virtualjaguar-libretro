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

> **Update 2026-08-04 — the real-BIOS row above is misleading; see §9.**
> That -1.11 % is an *average over the read cycle*, not a streaming rate.
> Measured fill-to-fill, the real-BIOS FIFO streams at 353 332 B/s
> (**+0.1508 %**, i.e. slightly fast) and does so on Dragon's Lair, Myst
> and Battle Morph alike. The deficit is entirely inter-read turnaround
> (~90 % game-side think time, ~10 % `SEEK_DELAY_TICKS`). §3.2's mechanism
> and §7 lead 1 are closed as refuted.

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

> **SUPERSEDED by §9 (2026-08-04).** The mechanism this section derives —
> GPU-ISR latency being charged to the refill period — was implemented and
> measured, and it does not exist: `FIFO_DRAIN.tick - FIFO_FILL.tick` is
> **0** for 99.99 % of drains on DL, Myst and Battle Morph alike. The
> streaming cadence already hits the armed 2.8500 ticks exactly, so the
> real-BIOS path is 0.1508 % *fast*, not 1.11 % slow; the ~1 % average
> deficit is inter-read turnaround (§9.3). The "31.968 bytes per drain"
> figure below is a measurement artifact too (§9.4). Read §9 before acting
> on anything here.

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

1. ~~**Close the real-BIOS -1.11 % (§3.2).**~~ **CLOSED — REFUTED, see §9
   (2026-08-04).** The fix described below was implemented and measured:
   the fill->drain latency it targets is zero, the streaming cadence is
   already exactly the armed 2.85 ticks, and the deficit is inter-read
   turnaround instead. Do not re-attempt it. Original text kept for
   context:

   **Close the real-BIOS -1.11 % (§3.2).** This is the only measured
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

> **Re-verified 2026-08-05 on `libretro/develop` @ `674f600` — both sequences
> still work, and are now committed as fixtures (§10.1). Do not judge an HLE
> run by the `seeks`/`seekstarts`/`seekdones` columns: they are structurally
> 0 in HLE (§8.2), so a real HLE branch reads as "no seek". The HLE witness is
> the `HLE_READ` trace event's LBA. See §10 for the falsifiable gate.**

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

**That zero is a property of the instrument, not of the run.** `cdSeekStartCount`
is incremented at exactly one site, `src/cd/cdrom.c:1691`, on the BUTCH `$12xx`
register write path. HLE intercepts `CD_read` above BUTCH, so the counter can
never move in HLE no matter what the game does. `seeks == 0` in an HLE run is
therefore evidence of nothing. The equivalent HLE observable is the `HLE_READ`
trace event, which carries the requested LBA (`cdrom.c` trace enum comment;
pushed from `jagcd_hle.c:5300`) — and its LBA sequence is **identical** to the
BIOS `SEEK_START` sequence, field-for-field at the +439 offset (§10.1).

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
   long tier on the boot seek of essentially every CD title. DL's first four
   seek ticks were bit-identical with and without the tier, but that does
   **not** prove the tier stayed silent there — the gap to the next seek is
   269 fields, easily wide enough to swallow 9 340 ticks unobserved, and
   `seekDist` was never logged. Treat DL as untested too. Myst / Hover
   Strike / IS2 / Primal Rage / Battle Morph likewise. See §7's
   regression-risk table.
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
(real-BIOS -1.11 % — since **closed as refuted**, §9), §7.3 (user-side log
/ frontend pacing) and §7.5 (`VP`) are untouched by this pass. What #297 most needs now is **hardware or
BigPEmu ground truth for branch-scene timing** — without it, any seek-latency
value is unfalsifiable.

---

## 9. §7 lead 1 executed (2026-08-04) — §3.2's root cause is REFUTED

Lead 1 said the real-BIOS FIFO path runs 1.11 % slow because `fifoFillDelay`
is re-armed only *after* a drain completes, so GPU-ISR latency is added to
the refill period instead of overlapping it. That was implemented, measured,
and **the mechanism does not exist**. No code change ships from this pass.

Headline: **the real-BIOS streaming rate is not slow, it is 0.1508 % fast.**
The ~1 % average deficit is read/seek turnaround between transfers — roughly
90 % game-side think time, 10 % our `SEEK_DELAY_TICKS` — which §7 lead 2
already flags as blocked on missing ground truth.

### 9.1 Method

`test/tools/fmv_seek_probe` + `VJ_CD_TRACE=1 VJ_CD_TRACE_LIVE=1
VJ_HARNESS_LOG_INFO=1`, `--option virtualjaguar_cd_boot_mode=bios`
(`--bios` alone does NOT switch boot mode — a run labelled BIOS without
this option is byte-identical HLE). Every `FIFO_FILL` and `FIFO_DRAIN`
carries the `diag_butchExecCalls` halfline tick, so fill->fill and
fill->drain intervals are directly measurable rather than inferred.

Baseline reproduced on `libretro/develop` @ `9d276bb`, Dragon's Lair,
real BIOS, fields 1400-2500 (18.3167 s):

```
199 939 drains  ->  349 301 B/s  (99.008 % of 352 800)
```

Two independent metrics agree to 0.003 %: drain count x 32 B, and
`diag_fifoReads` x 2 B / 2352 -> sector advance (349 312 B/s). This
matches §3.2's 348 881 B/s within run-window variance.

### 9.2 The fill->drain latency §3.2 blames is zero

Histogram of `FIFO_DRAIN.tick - FIFO_FILL.tick`, DL, same window:

```
elapsed = 0     181 930 drains
elapsed = 964 / 970 / 989 / 1013 / 1030      1 each
```

`BUTCHExec()` is a halfline callback and the GPU ISR drains *inside* that
same halfline, so the ISR's 16 reads are always complete before the next
tick samples anything. There is no latency to charge. The five outliers
are not latency either: each is the last fill of a transfer, left sitting
ready across the inter-read gap and drained when the next read begins.

Not DL-specific — same probe, same option, real BIOS:

| title | fills | elapsed == 0 | streaming mean period | streaming rate |
|---|---|---|---|---|
| Dragon's Lair | 181 935 | 99.997 % | 2.8500 ticks | 353 332 B/s (+0.1508 %) |
| Myst | 75 071 | 99.991 % | 2.8500 ticks | 353 332 B/s (+0.1509 %) |
| Battle Morph | 86 002 | 99.998 % | 2.8500 ticks | 353 328 B/s (+0.1497 %) |

### 9.3 Where the ~1 % actually goes

Fill->fill gap histogram, DL, fields 1400-2500:

```
gap = 2 ticks       27 291        gap = 3 ticks    154 638
gap = 1071 / 1077 / 1096 / 1121 / 1137 ticks        1 each
                                        (181 934 gaps over 181 935 fills)
```

Only 2 and 3 — no drift, no jitter. Mean over the 181 929 streaming
intervals is **2.8500 ticks**, i.e. exactly what `FIFO_REFILL_PERIOD_X100
285` arms, to five decimals. The state machine gives back *nothing*.

The five long gaps are the game's read cycle:

```
last FIFO_FILL  1172688      (ISR stops: transfer complete)
   ... 1029 ticks of game-side processing ...
I2S_CTRL disable, DSA_TX $1003/$1119/$120B, SEEK_START   1173719
SEEK_DONE (= SEEK_DELAY_TICKS, 100)                      1173819
I2S_CTRL enable, DSA_RX $0100                            1173822
first FIFO_FILL of the next read                         1173825
```

They total 5 502 ticks = **1.050 % of wall time**, which is the entire
deficit: the mean period over *all* 181 934 gaps is 2.8802 ticks =
**349 632 B/s = 99.102 % of hardware** — the measured average, recovered
from the streaming rate plus these five gaps and nothing else.

Spacing between the gaps is 94 400 / 94 419 / 94 444 / 94 394 ticks =
**180.15 fields** — §5.1's independently measured 180-field `CD_read` arm
cadence, arrived at from a completely different instrument. That is the
strongest single piece of evidence that these are the game's read cycle
and not a FIFO artifact.

Of each ~1 100-tick gap only ~106 ticks are ours (`SEEK_DELAY_TICKS` plus
DSA turnaround); the other ~1 000 is the game between reads. Reducing our
share means touching seek latency, which §7 lead 2 blocks for want of
ground truth — and the direction is wrong anyway: `cdrom.c:42` cites the
manual's 30-315 ms real seek, so our 3.18 ms turnaround is already an
order of magnitude *shorter* than hardware.

### 9.4 §3.2's "31.968 bytes per drain (-0.10 %)" is also spurious

Counted directly, `diag_fifoReads / drains = 8.0002` — and since
`diag_fifoReads` counts only the `FIFO_DATA` half of the pair
(`I2SDAT2` reads at `FIFO_DATA+4..7` are not counted), that is exactly
16 word reads = **32.000 B per drain**. §3.2's 31.968 came from dividing
sector advance by a drain count whose window included stall periods.

### 9.5 Hard constraint found: the refill floor is 2 ticks

Worth recording because nothing in the source says it. BUTCH's half-full
IRQ edge is detected once per `BUTCHExec` tick (`cdPrevShouldIRQ`, end of
`BUTCHExec`). The GPU drains between ticks, so a refill armed for **1**
tick re-asserts `fifoDataReady` before the tracker ever samples a
deasserted level: no rising edge, the ISR is never re-entered, and the
transfer stops dead. Observed exactly once, at `FIFO_DRAIN` #1, block
15236, on DL real BIOS — total run output 1 drain, 10 FIFO reads, then
`video_stall` + `cd_seek_wedge`.

Two ticks is the shortest interval the edge detector can represent. Any
future refill-pacing work must respect that floor (2.85 sits safely
above it; single speed at 5.70 more so).

### 9.6 Why the +0.1508 % constant is deliberately NOT fixed

`FIFO_REFILL_PERIOD_X100 285` vs the exact 2.8542884 ticks is the one real
streaming-rate error. Widening the accumulator to `x10000 = 28543` would
give 352 796 B/s (-0.001 %). **Do not ship it alone.** The composite rate
is what the game sees:

```
0.98950 x 352 796 = 349 091 B/s      DL's demand (§5.1) = 349 590 B/s
```

That moves DL from +0.01 % above its own demand to **-0.14 % below** it —
straight into the overlap-and-lose-the-tail failure §5.1 describes. The
streaming term is 0.15 % fast and the turnaround term is ~10x too short;
they are compensating errors. Making one exact while the other stays
wrong is not an accuracy win. If hardware-exact streaming is wanted, it
has to land together with a modelled turnaround — a separate ticket, with
§7 lead 2's ground-truth problem to solve first.

### 9.7 Status of §3.2 and §7 lead 1

Both are superseded by this section. §7 lead 1 is **closed, refuted** —
do not re-attempt the fill-to-fill re-arm; it is a measured no-op
(it fires on 5 of 181 935 drains and saves at most one tick each).

---

## 10. Branch gap measured per-frame (2026-08-05) — our number for the BigPEmu comparison

Purpose: #297 is blocked on comparing our scene-transition timing against a
BigPEmu reference capture. This section is **our** side of that comparison,
measured per-frame across a reproducible Dragon's Lair scene branch in both
boot modes, on `libretro/develop` @ `674f600`.

**Headline: the branch gap is transfer-bound, not seek-bound.** Our
`SEEK_DELAY_TICKS` (3.178 ms) is **0.2 – 0.6 %** of the observed gap, and it
is smaller than one video field — HLE, which has no seek model at all, produces
the *same gap to the field*. Any BigPEmu difference smaller than ~17 ms cannot
be attributed to seek latency, and a difference of a whole field or more is a
delivered-rate or game-logic difference unless it is ~18 fields (= the 300 ms
top of the manual's seek range, §8.3).

### 10.1 The press sequence, and the evidence that it branches

Committed as fixtures plus a gate script:

* `test/fixtures/dragons_lair_death_branch.press` — HLE (`500:pause`,
  `560:pause`, `700:a`)
* `test/fixtures/dragons_lair_death_branch_bios.press` — real BIOS
  (`939:pause`, `999:pause`, `1139:a`; BIOS boot is +439 fields)
* `test/tools/run_dl_branch_fixture.sh` — expands either fixture into
  `fmv_seek_probe --press` args, **confirms the boot mode from the core's own
  `[BOOT] CD game, mode=…` log line** (never the flag), and gates on the trace
  ring.

```bash
cc -O2 -Wall -std=c99 -I. -I./test -I./libretro-common/include \
   -o test/tools/fmv_seek_probe test/tools/fmv_seek_probe.c \
   test/harness/harness.c -ldl -lm
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  bash test/tools/run_dl_branch_fixture.sh ./virtualjaguar_libretro.dylib hle
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  bash test/tools/run_dl_branch_fixture.sh ./virtualjaguar_libretro.dylib bios
```

**Gate:** a CD read whose LBA delta from the previous read exceeds 100 000
sectors (`SEEK_START` in BIOS, `HLE_READ` in HLE). Steady-state streaming reads
step 339–428 sectors and the attract-loop restart steps 5 171, so the classes are
~2.5 orders of magnitude apart. Both modes PASS:

```
hle   BRANCH field= 699.7  LBA 16026 -> 154061 (+138035)
      BRANCH field= 828.5  LBA 154061 -> 20491 (-133570)
bios  BRANCH field=1139.0  LBA 16026 -> 154061 (+138035)
      BRANCH field=1267.9  LBA 154061 -> 20491 (-133570)
```

The BIOS run's `seekstarts` CSV column steps 10 -> 11 at field 2311 and the HLE
run's `hlearm` steps 10 -> 11 at field 1872 — the same field at the +439 offset.

**Negative control (falsifiability).** Same build, same disc, *no presses*,
BIOS, 4 000 fields: seeks are `+15236` (boot), then `+362 +428 +376 +353 +376
+342 +354 +344 +367 +352 +340 +377 +375 +425`, then `-5171` (attract restart),
then `+362`. **No branch-magnitude read.** `run_dl_branch_fixture.sh` with an
empty press file exits 1. The presses are causally necessary.

The branch is a **death**: the outgoing frame is the dark falling-into-the-pit
clip, the incoming one is the gold "LIVES 4" retry card. After the scripted
input the die/retry loop self-repeats every ~1 171 fields with no further input
(branches at BIOS 1139 / 1268 / 2310 / 2439 / 3482 / 3611), so a longer run
gives free repeats of the same measurement.

### 10.2 Why the earlier HLE attempt read as "no branch"

Two instrument errors, both fixed above; neither was a bit-rotted sequence.

1. **`seeks`/`seekstarts`/`seekdones` are structurally 0 in HLE** (§8.2, now
   annotated). The HLE run *did* branch; the column cannot show it.
2. **Non-black pixel count cannot detect motion in this game.** Dragon's Lair
   renders its movie into a fixed OP window: the non-black count is a constant
   **62 208** for every frame of playback, changing only at scene boundaries
   (to 0 for the black gap, to 4 648 for the "LIVES" card). A window of
   "62 208, unchanged" is a *playing movie*, not a frozen one. Motion needs a
   **changed-pixel count** between consecutive frames — during playback that
   alternates 0 / 9 000–40 000, because the 24 fps film is presented on
   alternate fields.

### 10.3 The measurement

`fmv_seek_probe` with `FMV_SHOTDIR` + `FMV_SHOT_FROM/TO/EVERY=1`, one PPM per
field, then non-black and changed-pixel counts per frame offline. Windows chosen
around the second (unprompted) branch pair so no button press confounds them.

**Branch A — gameplay clip → "LIVES 4" card** (LBA 16026 → 154061):

| | BIOS | HLE | HLE +439 (aligned) |
|---|---|---|---|
| branch issued | field 2310.13 (`SEEK_START`) | field 1871.17 (`HLE_READ`) | 2310.17 |
| `SEEK_DONE` | field 2310.32 (+100 ticks) | — (no seek model) | — |
| last frame with outgoing content | 2311 (5 472 px, partial) | 1871 (55 584 px, partial) | 2310 |
| fully black run | 2312 – 2340 (**29** fields) | 1872 – 1901 (**30** fields) | 2311 – 2340 |
| first frame with incoming content | 2341 (724 px) | 1902 (4 648 px) | **2341** |
| **gap, last-out → first-in** | **30 fields** | **31 fields** | — |

**Branch B — "LIVES 4" card → retry scene** (LBA 153995 → 20491):

| | BIOS | HLE | HLE +439 (aligned) |
|---|---|---|---|
| branch issued | field 2439.47 (`SEEK_START`) | field 2000.51 (`HLE_READ`) | 2439.51 |
| last frame with outgoing content | 2439 | 2000 | 2439 |
| fully black run | 2440 – 2544 (**105** fields) | 2001 – 2105 (**105** fields) | 2440 – 2544 |
| first frame with incoming content | 2545 (9 424 px) | 2106 (62 208 px) | **2545** |
| **gap, last-out → first-in** | **106 fields** | **106 fields** | — |

In milliseconds — reported both ways, because the emulated field is 524
halflines (§1, §5.2) and NTSC is 525:

| | fields | @16.65155 ms (emulated) | @16.68334 ms (59.94 Hz) |
|---|---|---|---|
| branch A, BIOS | 30 | 499.5 ms | 500.5 ms |
| branch A, HLE | 31 | 516.2 ms | 517.2 ms |
| branch B, both | 106 | 1 765.1 ms | 1 768.4 ms |

**Fields are the primary unit**; the ms columns are a conversion, and which one
is right depends on §5.2's unresolved `VP` question.

### 10.4 Seek versus game-side: the split

**Seek is 0.2 – 0.6 % of the gap.** `SEEK_DONE` lands exactly 100 ticks =
**3.178 ms = 0.191 fields** after `SEEK_START` in both branches — 0.64 % of
branch A, 0.18 % of branch B. The HLE path issues no seek at all and lands its
first incoming frame on the *same aligned field* (2341 and 2545), which is the
direct experimental confirmation that 3.178 ms is below the resolution of the
symptom.

**The remainder is CD data transfer, at a game-chosen buffer threshold.**

**Both branches are transfer-bound at a game-chosen partial threshold**, and in
both the drain stream is continuous from ~3 ticks after `SEEK_DONE` to the first
displayed frame — there is no measurable think-time term anywhere in the gap.

*Branch A.* `I2S_CTRL $0005` (enable) at tick 1210813 — 3 ticks after
`SEEK_DONE` — then `FIFO_DRAIN` runs at a flat **184 drains/field = 5 888 B/field
= 353 500 B/s** for the whole black window, and `I2S_CTRL $0001` (disable) fires
at tick 1226548, block 154070. That is **75 sectors = 176 400 bytes**, which at
the measured 353 332 B/s streaming rate (§9.2) takes **499.4 ms = 29.99 fields**
— the black window, to within one part in 3 000.

Note that 176 400 bytes is **not** the size of the read. The same game action in
HLE arms `hleStream.total` = **1 048 576 bytes** (`JaguarCDHLEStreamBytes()`
returns the *requested* total, `jagcd_hle.c:1216`). So the game requested ~1 MB,
took 16.8 % of it, and started playback — it does not wait for the read to
finish.

*Branch B.* Same picture at a different threshold: drains run at 184/field from
field 2440 continuously through 2545 and past it (still in flight when the scene
starts). 19 368 drains × 32 B ≈ **619 800 bytes** land before the first displayed
frame of a 1 047 820-byte read — **59 %**. The start-playback threshold is
content-dependent, not a fixed latency; branch A's is 17 %, branch B's 59 %.

*Side finding — an HLE/BIOS divergence in what gets written, not when.* At
branch A the real-BIOS path stops the transfer after 176 400 bytes (the game's
own `I2S_CTRL` disable). HLE has no such stop: the `jagcd_hle.c` in-flight-arm
warning (§8.5) reports **756 901 / 1 048 576 bytes delivered** for the same read,
i.e. HLE streams for the whole 128 fields until the next `CD_read` overwrites it,
writing ~580 KB more into `$03E000` than the BIOS path does. It does not move any
timing measured here — both modes land the "LIVES" card at aligned field 2341 and
the retry scene at 2545 — but it is a real behavioural difference and belongs
with §8.5's lead. **Not investigated further; measurement pass only.**

**Summary of the split:**

| term | branch A | branch B |
|---|---|---|
| our seek (`SEEK_DELAY_TICKS`) | 3.18 ms (0.6 %) | 3.18 ms (0.2 %) |
| CD transfer at the emulated drive rate | ~496 ms (99.4 %) | ~1 745 ms (98.9 %) |
| bytes landed before first displayed frame | 176 400 (17 % of the read) | ~619 800 (59 %) |
| game-side think time | not detectable | not detectable |

This is a different shape from the *inter-read* gaps of §9.3, which were ~90 %
game-side think time. At a **branch** the game issues its next read immediately
and then waits on bytes.

### 10.5 What this implies for the BigPEmu capture

Sensitivities, so a measured difference can be attributed rather than guessed:

* **To move branch A by one field** (16.7 ms) needs a **3.3 %** change in
  delivered byte rate, or +16.7 ms of seek latency.
* **To move branch B by one field** needs a **0.94 %** change in delivered rate,
  or +16.7 ms of seek latency.
* Our streaming rate is within **+0.15 %** of hardware (§9.2), i.e. worth
  **0.05 fields** on branch A and **0.16 fields** on branch B — invisible.
* The manual's 30–315 ms seek range (`cdrom.c:42`, secondhand from MiSTer)
  would add **1.8 – 18.9 fields** to *every* branch. That is the one term big
  enough to matter, and it is the one with no ground truth.

**So the single question worth putting to the BigPEmu capture is: how many
fields of black sit between the last frame of the dying-Dirk clip and the first
frame of the "LIVES" card?** Ours is **30 fields = 500 ms** (BIOS) / **31 fields
= 517 ms** (HLE); branch B is **106 fields = 1 765 ms** in both. Quote the ms as
well as the field count when comparing — a BigPEmu capture is a video file at
its container's framerate (and may dupe or drop frames), so its frame count is
not directly commensurable with our emulated fields. If BigPEmu is
also ~30, seek latency is not the #297 mechanism and lead §7.2 can be closed. If
BigPEmu is ~32–49, the difference maps directly onto a 30–315 ms seek model. If
BigPEmu is *shorter* than 30, the mechanism is delivered rate or a different
buffer threshold, not seek — and no seek model can fix it.

Branch B (106 fields) is the more sensitive of the two (0.94 %/field vs 3.3 %),
so capture both if possible.

### 10.6 Caveats

* Both branches were measured once per mode. Everything in this core is
  deterministic (§4a) and the die/retry loop repeats the same LBA sequence three
  times in a 4 000-field run, but the per-frame PPM windows cover one instance
  each.
* "Last frame with outgoing content" is ambiguous by one field: the final
  outgoing frame is a *partial* clear (5 472 px BIOS / 55 584 px HLE), which is
  why the two modes differ by one field on branch A and not at all on branch B.
  The **first incoming content frame is unambiguous and identical in both modes**
  — prefer it as the comparison anchor against BigPEmu.
* Measurement only. No emulation behaviour was changed in this pass.

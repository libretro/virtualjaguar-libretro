# BizHawk's Virtual Jaguar CD implementation vs ours

Research note, 2026-07-29. Target question: why do **BrainDead 13**,
**Dragon's Lair** and **Primal Rage** park at `BIOS_INTRO` in real-BIOS
boot mode when all three reach `GAME_CODE` in HLE mode
(`docs/cd-boot-matrix.md`, `docs/cd-known-issues.md` item 1)?

Reference tree: `TASEmulators/BizHawk`, `waterbox/virtualjaguar/src`
(shared ancestry with us — David Raingeard / Niels Wagenaar / James
Hammons Virtual Jaguar, GPL-family, so adaptation with attribution is
legitimate).

Nothing in `src/` was changed for this note.

---

## 1. Does BizHawk have Jaguar CD support?

**Yes — but HLE only. They have no real-BIOS mode at all.** This is the
single most important fact for this comparison, and it hollows out most of
the intended side-by-side.

Evidence:

| Fact | Evidence |
| --- | --- |
| A CD HLE module exists | `waterbox/virtualjaguar/src/cdhle.cpp`, 16,552 bytes, 708 lines; implements all 19 CD BIOS jump-table entries (`CD_init` … `CD_switch`) |
| Their BUTCH emulation is a **stub** | `cdrom.cpp` is 3,457 bytes / 170 lines. `void BUTCHExec(uint32_t cycles) { }` — empty body. `CDROMReadWord`/`WriteWord` do nothing but read/write a 256-byte `cdRam[]` array, except for the `$DFFF2E` bit-banged serial bus |
| No FIFO, no DSA engine, no seek state machine, no subcode | `cdrom.cpp` has no `fifo`, no seek/LBA state, no `I2CNTRL` handling, no interrupt generation of any kind. `CDROMReadWord` explicitly returns `0x0000` for the `FIFO_DATA` range (<code>offset &lt; FIFO_DATA || offset &gt; FIFO_DATA+7</code>) |
| No CD BIOS ROM is ever loaded | grepping the whole tree for a CD BIOS load path finds nothing. `CDHLEReset()` synthesises the boot state itself: `SET32(jaguarMainRAM, 4, cd_boot_addr)` (reset vector), `SET16(jaguarMainRAM, 0x3004, 0x0403)` (fake "BIOS VER"), `DACWriteByte(0xF1A153, 9)` (SCLK) |
| HLE is entered by PC trap, not by running BIOS code | `jaguar.cpp:56-69` — `M68KInstructionHook()`: `if (pc >= 0x3000 && pc <= 0x306C) { CDHLEHook((pc - 0x3000) / 6); /* pop return address, RTS */ }` |
| Their own history says so | `6113f3c1 2022-09-23 partial jagcd support (doesn't seem to completely work here)` → `c2ae5bfa jagcd cd_initm support` → `94bb881d better completely wrong cd timings … fixes FMVs in jag cd games`. Every CD commit touches `cdhle.cpp`; none adds BUTCH behaviour |

Consequence: **the direct comparison the task asked for — their
BUTCH/FIFO/DSA path vs ours — has nothing on the other side.** See §6.

What *is* comparable and useful: their HLE encodes reverse-engineered
knowledge about what the real CD BIOS *does*, which constrains what our
real-BIOS path must produce. Two of those constraints turned out to be
independently valuable, and one unrelated 68K core bug fix in their tree
applies verbatim to ours (§4).

---

## 2. Side-by-side

### 2.1 Architecture

| | ours (`bios` mode) | ours (`hle` mode) | BizHawk |
| --- | --- | --- | --- |
| CD BIOS | real ROM at `$800000` (`src/cd/jagcd_bios.c:212-229`), auth byte cleared (`jagMemSpace[0x80040B] &= 0xFE`) | none; jump table trapped | none; jump table trapped |
| Jump-table entry | BIOS's own RAM driver executes | `$3000-$3072` trap, 19 entries | `$3000-$306C` trap, 19 entries |
| BUTCH | full model: DSA queue, seek state machine, FIFO half-full latch, IRQ edge pacing (`src/cd/cdrom.c`, 2548 lines) | mostly bypassed (`FIFOFeedAllowed()` gates it off during a stream) | stub |
| Data delivery | guest's GPU CD ISR drains `FIFO_DATA` 16 words at a time | halfline-paced `memcpy` into dest, ISR write-pointer semantics simulated | `SetCallbackTime` callback writes 64 bytes/`GPUWriteLong` into dest |
| CD data interrupt | `GPUSetIRQLine(GPUIRQ_DSP, …)` → GPU IRQ1, vector `$F03010` (`cdrom.c:976`) | not needed | **none** — `//GPUSetIRQLine(GPUIRQ_DSP, ASSERT_LINE);` is commented out (`cdhle.cpp:316`) |
| Boot executable load | we extract it from the image and inject it at `$050176` (`jagcd_bios.c:59-207`) | injected at boot | injected at reset, PC set via reset vector |

Cross-validated agreements worth recording (both trees arrived at these
independently):

* **GPU IRQ1 / vector `$F03010` is the CD data interrupt.** BizHawk's
  `LoadISRStub()` (`cdhle.cpp:361-385`) writes a `movei ISR_ADDR,r30 /
  jump (r30)` trampoline at exactly `$F03010`, and the ISR body it
  installs does `movei $F02100,r30; load (r30),r29; bclr 3,r29;
  bset 10,r29; …; store r29,(r30)` — clear `IMASK`, clear the **DSP/I2S**
  interrupt latch (G_FLAGS bit 10 = `INT_CLR1`). That is IRQ1's
  fingerprint, matching the `cdrom.c:922-955` analysis and the Task-6A
  IRQ0→IRQ1 fix.
* **The `$2C00` TOC is track-indexed, 8 bytes per entry, 1024 bytes
  total**, with `+0` track number, `+1..+3` start MSF, `+4` session
  number (0-based), `+5..+7` track duration MSF. BizHawk's `struct TOC` /
  `struct Track` (`cdhle.cpp:52-75`, `static_assert(sizeof(TOC) == 1024)`)
  is byte-for-byte our layout from `jagcd_bios.c:129-179`, including the
  duration field whose zeroing caused the Primal Rage CDDA silence.
  Independent confirmation that layout is right.
* **The one-word capture skew.** BizHawk computes
  `cd_word_alignment = -j & 3` where `j` is the byte offset of the
  `ATARI APPROVED DATA HEADER` string in the sector. For the standard
  layout (`j = 0x42`) that evaluates to **2** — exactly our hardcoded
  `cdBufPtr = 2` (`cdrom.c:1751`). Ours is a constant, theirs is derived;
  same value for every retail disc.
* **CD EEPROM seed values.** Their `$DFFF2E` bus answers `busCmd`
  `0x180..0x185` with `0x0024, 0x0004, 0x0071, 0xFF67, 0xFFFF, 0xFFFF`.
  Our NM93C14 model reaches the same answers for indices 0-3 via
  `cdrom_eeprom_ram[]` (`cdrom.c:757-763`) and diverges only at indices
  4/5 (`0x892F, 0x8000` vs their `0xFFFF, 0xFFFF`). Non-finding; recorded
  so nobody re-investigates it.

### 2.2 Delivery pacing — the one number that differs

| | rate | source |
| --- | --- | --- |
| BizHawk, single speed | 64 B / 270 µs = **237,037 B/s** | `CD_DELAY_USECS` (`cdhle.cpp:28`), NTSC |
| BizHawk, double speed | 64 B / 135 µs = **474,074 B/s** | `CD_DELAY_USECS >> (cd_mode & 1)` (`cdhle.cpp:319`) |
| ours, HLE | **352,800 B/s** nominal, wall-clock paced | `HLE_STREAM_BYTES_PER_SEC` (`jagcd_hle.c:143`) |
| ours, real BIOS | **352,800 B/s ceiling**, drain-limited in practice | `FIFO_REFILL_PERIOD_X100 = 285` (`cdrom.c:69`) |

BizHawk's own comment on their constant is worth quoting in full, because
it is the closest thing to external corroboration of the FMV-timing
hypothesis that exists:

```c
// arbitrary number, but something is needed here
// too short, and games FMVs misbehave (Space Ace is entirely FMVs!!!)
// too long, and games will time out on CD reads
#define CD_DELAY_USECS (vjs.hardwareTypeNTSC ? 270 : 280)
```

Their commit `94bb881d` is titled *"better completely wrong cd timings …
**fixes FMVs in jag cd games**"*. Their numbers are admittedly ad hoc
(neither is a hardware rate); ours is derived (150 double-speed sectors/s
× 2352 B). But their empirical finding — that FMV correctness is a
function of this constant, in both directions — transfers.

Two structural differences follow:

1. **They scale delivery by the CD mode speed bit; we do not.**
   `cdhle.cpp:437-444` latches `cd_mode = D0 & 3` on `CD_mode`
   (bit 0 = speed, bit 1 = audio/data) and every callback re-arms at
   `CD_DELAY_USECS >> (cd_mode & 1)`. On our real-BIOS path the
   equivalent command is DSA `$15nn` (Set Mode), and `cdrom.c:1641-1642`
   answers it with a `$17nn` Mode Status echo and **discards the payload
   entirely** — the speed bit is never stored and `FIFO_REFILL_PERIOD_X100`
   is a compile-time constant fixed at the 2× rate. **BizHawk is right and
   we are wrong here on hardware grounds; adjudicated against the JTRM in
   §4.2.**

2. **Our BIOS-path rate is a ceiling, not a rate.** `cdrom.c:1350-1359`:
   the refill delay is armed *only after* the guest has performed
   `FIFO_DRAIN_READS = 16` word reads of `FIFO_DATA`. So

   ```
   achieved = 32 bytes / (guest drain time + 2.85 ticks + IRQ re-arm + ISR dispatch latency)
   ```

   which is strictly below 352,800 B/s and depends on how fast the
   *guest's* GPU ISR runs. Our HLE path has no such coupling — it is a
   wall-clock-paced `memcpy` that hits its nominal rate regardless of what
   the guest does.

   **How far below, measured from data already in the boot matrix**
   (`fifo_drains × 32 bytes ÷ (frame ÷ 59.94)`):

   | Title (bios) | frame | fifo_drains | achieved | % of 352,800 | stage |
   | --- | --- | --- | --- | --- | --- |
   | Highlander | 1750 | 92,617 | 101,512 B/s | 28.8 % | GAME_CODE |
   | Baldies | 990 | 45,895 | 88,917 B/s | 25.2 % | GAME_CODE |
   | BrainDead 13 | 1106 | 38,915 | 67,486 B/s | 19.1 % | **BIOS_INTRO** |
   | Battle Morph | 905 | 28,672 | 60,766 B/s | 17.2 % | GAME_CODE |
   | Primal Rage | 884 | 17,996 | 39,047 B/s | 11.1 % | **BIOS_INTRO** |

   Two readings, and honesty requires both. (a) Every real-BIOS row runs
   at **11–29 % of the nominal rate** the constant claims to deliver — the
   FIFO path is drain-limited, and the carefully derived
   `FIFO_REFILL_PERIOD_X100` is not what actually paces it. (b) The
   run-average rate does **not** separate stuck from working: Battle Morph
   works at 17.2 % while BrainDead 13 is stuck at 19.1 %. So there is no
   simple global rate threshold. These are whole-run averages that include
   idle tails (the `cd_seek_wedge` rows froze for the last 300 frames by
   definition), which is why the experiment in §3 asks for the
   *instantaneous* rate inside the critical window, not the average.
   Dragon's Lair and Space Ace have no `fifo_drains` datum at all (their
   rows report `(none)` for the watchdog), so the most useful control pair
   is not available from the matrix and must be run. A further floor
   caveat: bytes are popped from `cdBuf` whenever `fifoDataReady ||
   cdPlaying` (`cdrom.c:1337`) but `cdFifoDrainCount` only advances while
   `fifoDataReady`, so these are a floor on bytes consumed as well as on
   rate. Neither caveat changes reading (b).

### 2.3 Other concrete deltas

* **Boot-executable locator.** Ours (`cdintf.c:1272-1305`) unconditionally
  word-swaps the session-2 track and requires the magic at **exactly byte
  `$42`** of the first sector, with load address at `$62`, length at `$66`,
  payload at `$6A`. Theirs (`cdhle.cpp:112-159`) scans **every sector of
  the boot track at every byte offset**, accepts both normal and
  byte-swapped magic (`"TARA IPARPVODED TA AEHDAREA RT?I"`), and derives
  all offsets from the match position. Ours is stricter; every disc in our
  library satisfies it, so this is a robustness gap for unusual dumps, not
  a cause of the handoff gap (and in `bios` mode the locator only runs at
  `$050176`, which the three stuck titles never reach).
* **Boot-track selection.** Theirs picks the first track with
  `session_num == 1` (0-based → second session). Ours picks the first
  track with `session >= 2` (1-based). Same track.
* **`NO_ERR` is 1, not 0.** `cdhle.cpp:17-20`: `#define NO_ERR()
  SET16(jaguarMainRAM, 0x3E00, 1)` with the comment *"0 should be no
  error, yet some games expect 1?"*. We model `$3E00` as the BIOS's DSA
  status word (`jagcd_hle.c:84-95`) and already know a nonzero value is
  what a `do { CD_spin } while ($3E00.w == 0)` loop needs. Agreement.
* **They install a do-nothing GPU CD ISR; we simulate the real one.**
  Their `LoadISRStub` overwrites the game-supplied ISR buffer with 9
  instructions that just ack and return, and they never raise the CD
  interrupt — games are expected to notice progress through `CD_ptr`
  (which returns the *advancing* `cd_read_addr_start`). Our HLE instead
  reproduces the real BIOS's GPU data-area struct and pre-decremented
  write pointer (`jagcd_hle.c:895-1085`). Ours is the more faithful of the
  two; nothing to adopt.

---

## 3. Ranked candidate root causes for the FMV handoff gap

Ranking constraint used throughout: **any candidate must be
mode-specific.** All three stuck titles reach `GAME_CODE` in HLE mode
through the same 68K core, the same GPU, the same image layer and the same
`352,800 B/s` nominal rate. A defect present in both modes cannot be what
distinguishes them. The discriminating control pair inside `bios` mode is
**Space Ace (`GAME_CODE`) vs Dragon's Lair (`BIOS_INTRO`)** — same
publisher, same ReadySoft FMV engine, same dead-reckoning schedule design
(both named together in the `FIFO_REFILL_PERIOD_X100` comment). Every
experiment below states what it predicts for the Space Ace row, not only
for the stuck ones.

Second constraint: **these three rows are not stably stuck.** The matrix's
own dated re-run notes for 2026-07-14 (Task 8, GPU delay-slot IRQ dispatch
clobber) record BrainDead 13 (bios) reaching `GAME_CODE` `final_pc=$00B2FA`
and Dragon's Lair (bios) moving `BIOS_INTRO $0060D8` → CD service band
`$0036B8`, while the current baseline table has both back at `BIOS_INTRO`.
Rows in this cluster have historically moved in both directions in response
to *unrelated* GPU/IRQ timing changes. That is itself the strongest
available evidence that the blocker is a pacing/dispatch sensitivity rather
than a missing feature — and a caution that any single-run result here is
weak evidence.

### Do this first: identify the blocker instead of correlating against it

All three candidates below are rate perturbations, and none of them answers
the question the stuck rows actually pose: **what is the BIOS waiting
for?** The three final PCs are *different* addresses in the BIOS RAM-driver
band — BD13 `$004FCA`, Dragon's Lair `$004C12`, Primal Rage `$0044CC` — so
they may not even share a blocker. `docs/cd-known-issues.md` item 1 already
names the tool, and it is cheaper than any sweep:

```
test/tools/cd_wedge_probe <core> "<disc>.cue" --arm 600 --freeze-frames 900 \
    --ram-dump /tmp/dl_bios --option virtualjaguar_cd_boot_mode=bios
```

Disassemble the loop at each stuck PC from the snapshot. Note the range
must cover `$3000-$5FFF`, not the `$3000-$3DFF` the Myst work used —
`$44CC`–`$4FCA` is above `$3DFF`. The loop's memory operand discriminates
directly among the candidates: a `$DFFF00` status-bit poll implicates
delivery/IRQ (candidate 1), a `$DFFF0A` DSA response wait implicates the
command layer, and a RAM counter spin implicates schedule drift
(candidates 2/3).

Alongside it, check the JTRM's own documented fingerprint for this failure
class. *'06 - Jaguar CD-ROM.pdf'* p.11 §2.7.11 (`CD_ptr`): if the returned
pointer is "one longword prior to the start of the read buffer", that is
"**often a symptom of the GPU interrupt code not running**". Reading the
BIOS's data-area write pointer at stall time and finding it parked at
`dest-4` would say the CD ISR never ran even once — a completely different
diagnosis from "ran too slowly", and one none of the rate experiments would
distinguish.

### Candidate 1 — the real-BIOS path's *achieved* delivery rate (highest)

**Claim.** The BIOS path delivers at 11–29 % of its nominal rate because
it is gated on the guest's GPU-ISR drain loop (§2.2), while the HLE path
is wall-clock paced and hits nominal. That is the only structural
difference between our two modes with the right shape: mode-specific,
continuous rather than binary, and timing-sensitive in exactly the way the
PR #169 contention result and BizHawk's `94bb881d` both point at.

**Supporting evidence.** (a) The measured 11–29 % table above. (b) The
PR #169 bus-contention observation: with GPU bus contention *enabled*, the
Dragon's Lair and Primal Rage `bios` rows move **forward**. Sourcing
caveat — this comes from the PR #169 working notes, **not** from
`docs/cd-boot-matrix.md`, which contains no contention section at all
(`grep -i contention` → no hits); the stage each row reached with
contention on is therefore not established here and should be re-measured
before it is leaned on. Taken at face value it still means the failure is a
function of the GPU-vs-CD-delivery ratio, since contention changes nothing
else. Direction of the required correction is **not** established:
contention slows the GPU ISR, which should slow achieved delivery further,
so the naive "we're too slow" reading and the observation disagree. That
contradiction is itself the most informative thing to resolve.
(c) BizHawk's constant is empirically two-sided ("too short … too long").
(d) The 2026-07-14 movement of BD13 and Dragon's Lair in response to a GPU
IRQ-dispatch fix (see above).

**Experiment.**
```
VJ_CD_TRACE=1 VJ_HARNESS_LOG_INFO=1 test/tools/cd_visual_verify \
    <core> "<disc>.cue" --frames 1800 --option virtualjaguar_cd_boot_mode=bios
```
for Dragon's Lair (bios, stuck), Space Ace (bios, works) and Dragon's Lair
(hle, works). From the trace ring, bucket `FIFO_DRAIN` events into
one-second windows and plot instantaneous B/s (32 bytes per event) rather
than the run average. Also record the wall-clock position of the last
`FIFO_DRAIN` before the stall.

**Expected observable if this is the cause.** DL(bios)'s instantaneous
rate inside its boot-read window differs materially from Space Ace(bios)'s
in the same window, and from DL(hle)'s. Confirmation would be DL(bios)
reaching `GAME_CODE` when the rate is moved toward Space Ace's — the
existing knob for a probe run is the (HLE-only today)
`virtualjaguar_cd_read_speed`; on the BIOS path the equivalent probe is
`FIFO_REFILL_PERIOD_X100` and `FIFO_DRAIN_READS`. Falsified if the two
titles' instantaneous rates are indistinguishable, which would send the
investigation to candidate 3.

### Candidate 2 — CD mode speed bit discarded on the BIOS path

**Claim.** BizHawk scales delivery by `cd_mode & 1`; we never store it.
This is a **confirmed hardware-behaviour gap** (§4.2), not a hypothesis —
the open question is only whether it is *reachable* in these three titles.
If the drive is put in **single** speed while we keep streaming at the 2×
rate, the schedule error is a clean factor of two, which is the size of
error a dead-reckoning FMV engine notices (the previous 201 KB/s →
352,800 B/s change, a factor of 1.75, is what fixed DL and Space Ace in HLE
mode; see the `cdrom.c:56-68` comment).

Framing caution: since the stuck titles never execute their own code, only
the **BIOS's own** `$15nn` traffic during its boot read can matter here.
The experiment tests exactly that, but the candidate is dead if the BIOS
issues no Set Mode before the stall.

**Experiment.** No code change needed: DSA `$15nn` traffic is already
traced. Run each title in `bios` mode with `VJ_CD_TRACE=1` and grep the
dumped ring for `DSA_TX` values matching `$15xx`; record the low byte
(bit 0 = speed, bit 1 = data/audio). Do the same for Space Ace and
Highlander (both `GAME_CODE`). Also watch for a single→double **pair** —
per JTRM §2.6 that is the documented read-error recovery sequence, so
seeing it means the BIOS already believes the read failed.

**Expected observable if this is the cause.** The stuck titles' BIOS sends
`$15x0` (single speed), or a single→double recovery pair, while the working
titles send `$15x1` (double) and stay there. Falsified — cleanly and
cheaply — if all titles send the same speed bit, or if none of the three
sends `$15nn` at all before the stall. Note that a *falsified* candidate 2
still leaves the §4.2 correctness bug worth fixing on its own.

### Candidate 3 — non-hardware serialization constants

**Claim.** `SEEK_DELAY_TICKS 100` (~3.2 ms) against the 30–315 ms our own
comment attributes to the MiSTer reference (`cdrom.c:41-52`),
`FIFO_FILL_TICKS 8` (~254 µs) and `DSA_RESPONSE_DELAY_TICKS 4` (~127 µs)
are all chosen for convergence speed, not fidelity. For an engine that
dead-reckons disc position against a clock, an unrealistically *fast* seek
is as wrong as a slow one: the game's schedule assumes hundreds of
milliseconds of seek latency it never gets, so its position estimate runs
ahead of the stream. BizHawk offers **no cross-check here** — their seek is
instantaneous — so this candidate rests entirely on our own admitted
approximation plus the JTRM-adjacent MiSTer figure.

**Experiment.** Sweep `SEEK_DELAY_TICKS` upward (100 → 400 → 1000 →
3000 ticks ≈ 3.2 / 12.7 / 31.8 / 95 ms) and re-run
`test/tools/cd_boot_matrix.sh` restricted to BD13, DL, Primal Rage,
Space Ace, Highlander, Baldies, Battle Morph, `bios` mode only.

**Expected observable if this is the cause.** One of the stuck rows moves
to `BOOT_STUB` or `GAME_CODE` at a realistic seek latency **without**
regressing Space Ace / Highlander / Baldies / Battle Morph. Any sweep
value that fixes a stuck row while breaking a working one is a symptom of
the same relative-pacing sensitivity, not a fix.

---

## 4. Hardware-adjudicated correctness findings

Both are real bugs where BizHawk's behaviour is right and ours is wrong.
Neither is established as *the* cause of the handoff gap.

### 4.1 The 68K exception path does not switch to the supervisor stack

BizHawk commit **`1adb2b45`** (CasualPokePlayer, 2023-05-10), *"swap user
and interrupt stack pointers when going to supervisor mode for external
interrupts, fixes Black ICE"*:

```diff
 STATIC_INLINE uint32_t m68ki_init_exception(void)
 {
 	MakeSR();
 	uint32_t sr = regs.sr;
-	regs.s = 1;
+
+	if (!regs.s)
+	{
+		regs.usp = m68k_areg(regs, 7);
+		m68k_areg(regs, 7) = regs.isp;
+		regs.s = 1;
+	}
 
 	return sr;
 }
```

**We have the identical defect.** `src/m68000/m68kinterface.c:292-301`:

```c
static INLINE uint32_t m68ki_init_exception(void)
{
   uint32_t sr;
	MakeSR();
	sr = regs.sr;					// Save old status register
	regs.s = 1;								// Set supervisor mode
	return sr;
}
```

This is the *interrupt* exception entry (`m68ki_exception_interrupt`,
line 234 — the path every TOM/JERRY/BUTCH IRQ takes). Note the asymmetry
inside our own tree:

* `src/m68000/cpuextra.c:97-109` — `Exception()`, used for TRAP / illegal
  / address-error, **does** perform the swap correctly.
* `src/m68000/cpuextra.c:53-77` — `MakeFromSR()` (the RTE path) **does**
  swap when S goes 1→0.
* `src/m68000/m68kinterface.c:292-301` — interrupt entry does **not**.

So for an interrupt taken in user mode the frame is pushed on the *user*
stack, the handler runs in supervisor with `A7` still the user SP, and the
`RTE` then hits `MakeFromSR()` with `olds == 1`, which sets
`regs.isp = <user SP>` and loads `A7` from a **stale `regs.usp`**. `A7` is
clobbered and the supervisor stack pointer is poisoned for every
subsequent exception — precisely the "corrupting the stack with a bogus
return address" failure class our own `cdrom.c:936-940` comment describes
for blind 68K IRQ2 delivery.

**Adjudication.** The correct behaviour is not a JTRM question — it is
core 68000 architecture: on exception recognition the processor sets S=1
and *all* subsequent stacking uses the supervisor stack pointer
(*M68000 Family Programmer's Reference Manual*, exception processing;
*MC68000 User's Manual* §6, "Exception Processing Sequence"). Cite that,
not `docs/jtrm-*.md` — the distilled JTRM notes cover Jaguar silicon, not
68000 exception semantics. BizHawk's fix is correct; our code is wrong.

**Why it is nevertheless NOT the explanation for this gap.**

1. It is mode-independent. All three stuck titles boot fine in HLE mode
   through this same broken path.
2. It only bites when S=0 at interrupt entry. A scan of the embedded
   retail CD BIOS (`src/bios/jagcdbios.c`, 262,144 bytes, entry point
   `$00802000` from offset `$404`) finds **zero** `MOVE #imm,SR` (`$46FC`)
   instructions anywhere, and only six `ANDI #imm,SR` (`$027C`) byte
   matches — all at offsets `$01B0FA`–`$02E1D2`, far above the 68K code
   region, i.e. incidental matches inside the ROM's DSP/GPU code and data
   blobs. The CD BIOS never drops the 68K to user mode, so the three
   titles stuck *in BIOS code* cannot be hitting this.

**File it as its own fix, not as part of the CD work.** It is a real
accuracy bug that will bite user-mode titles (Black ICE is the known one)
and is worth a standalone PR.

**Attribution for the eventual commit.** Target:
`src/m68000/m68kinterface.c`, function `m68ki_init_exception`. Suggested
trailer:

```
Adapted from BizHawk commit 1adb2b45a256beac4c23bae045c63951535db930
("(VirtualJaguar) swap user and interrupt stack pointers when going to
supervisor mode for external interrupts, fixes Black ICE") by
CasualPokePlayer. Both trees inherit the defect from the shared
UAE / Virtual Jaguar ancestor.
```

### 4.2 The CD mode speed bit really does set the drive's data rate

Adjudicated against *'06 - Jaguar CD-ROM.pdf'*, **p.10 §2.7.7 `CD_mode`**:

> **Input** — `D0.W  Speed/mode desired:`
> `Bit 0 ⇒ Speed: 0 = Single, 1 = Double`
> `Bit 1 ⇒ Mode: 0 = Audio, 1 = Data`
>
> **Purpose** — "This call sets the **speed of the CD** to either single or
> double-speed and the data mode to either audio or data."

So single/double speed is a property of the drive mechanism, and the data
rate follows from it. BizHawk's `CD_DELAY_USECS >> (cd_mode & 1)` is
therefore the hardware-correct shape; our real-BIOS path's fixed 2× rate
(`FIFO_REFILL_PERIOD_X100`, `cdrom.c:69`) is wrong whenever single speed
is selected, because we discard the bit entirely at `cdrom.c:1641-1642`.

A second, independent consequence: **p.8 §2.6, "Error Recovery Procedure
for CD Read Operations"** prescribes, when a double-speed read fails
(`CD_ptr` reports an error):

> 1. Switch to Single-Speed using CD_mode.
> 2. Switch to Double-Speed using CD_mode.
> 3. Reexecute the CD_read.

Since we store nothing from `$15nn`, that entire documented recovery path
is a **no-op** in our real-BIOS mode — a BIOS or game that hits a read
error and follows the manual gets no state change from us and will retry
into the identical failure. That is worth fixing regardless of whether it
turns out to be this gap.

Related JTRM notes captured while adjudicating, both corroborating existing
decisions rather than changing anything:

* **p.10 §2.7.5 `CD_initm`** — "there must be a primary GPU process running
  in order for GPU interrupts to be processed and this primary process must
  define the interrupt stack in R31." Independent confirmation of the
  Hover Strike stale-r31 analysis in `cdrom.c:944-955`.
* **p.11 §2.7.11 `CD_ptr`** — returns "the address of the last longword of
  memory that was written to. If no data has been read, this value will be
  one longword prior to the start of the read buffer (often a symptom of
  the GPU interrupt code not running)." Confirms our HLE's `A0 = dest-4`
  pre-decremented write-pointer modelling (`jagcd_hle.c:1077-1085`), and
  supplies the diagnostic used in §3.
* **p.11 §2.7.10 `CD_paus`** — "When in data mode, data will still be sent
  but it will not be sensible. When in pause mode, the CD *will not*
  advance along the disc." Our `$0400`/`$0500` handling sets `cdPlaying`
  and leaves the buffer intact, consistent with this.

---

## 5. Found, but does not explain this gap

Recorded so they are not re-derived, and so the ones that are genuine bugs
get their own tickets.

* **`$2C00` header bytes 4-7 are left zero.** BizHawk's `struct TOC` fills
  `+4 = num_sessions` and `+5..+7 = last lead-out MSF`; our
  `jagcd_bios.c:184-193` writes only `+2` (min track) and `+3` (max track)
  and documents the omission as "nothing in scope consumes it". Cannot
  explain `BIOS_INTRO` parking, because we only write that table at
  `$050176` — which the three stuck titles never reach.
* **We clobber the real BIOS's own TOC table.** More interesting than the
  above: `jagcd_bios.c:151` does `memset(&jaguarMainRAM[0x2C00], 0, 0x400)`
  and rebuilds the table from `CDIntf*`, discarding whatever the real BIOS
  had already assembled from DSA full-TOC responses — including the
  session count and lead-out encoding we then leave at zero. For titles
  that *do* reach handoff this is a strictly-less-informative table than
  the hardware would have left. Worth a separate look; not this gap.
* **Boot-executable locator rigidity** (§2.3). Robustness gap for
  non-standard dumps; every disc in our library passes.
* **CD EEPROM indices 4/5 diverge** (`0x892F, 0x8000` vs `0xFFFF, 0xFFFF`).
  Indices 0-3 agree. Non-finding.
* **BizHawk `f0529fde`** — *"stop a CD transfer when address is greater
  than the end, rather than greater than or equal to, fixes battle
  morph"*. Same class as our own resolved IS2 unaligned-tail fix
  (`c0471fc`); we already terminate on "pointer passes the end address"
  (`jagcd_hle.c:155-160`). No action.

---

## 6. Nothing useful found here

Stated plainly, because padding these sections would be worse than
admitting them.

* **BUTCH register behaviour.** No comparison possible. `BUTCHExec()` is
  an empty function in their tree; `cdRam[]` is a passive byte array.
* **DSA command engine.** They have none. Every `$0200`/`$0300`/`$10xx`/
  `$11xx`/`$12xx`/`$1400`/`$15xx`/`$18xx`/`$50xx`/`$54xx`/`$70xx`
  response our `cdrom.c:1560-1774` synthesises has no counterpart to check
  against. Our DSA semantics remain anchored on the MiSTer reference and
  on game-driver disassembly, with no second source.
* **FIFO / I2S delivery mechanics.** Theirs writes into guest RAM directly
  with `GPUWriteLong`; there is no FIFO, no half-full latch, no
  `I2CNTRL` bit 2 gate, no drain accounting. The only transferable datum
  is the pacing constant (§2.2).
* **CD interrupt wiring.** They deliberately raise **no** CD interrupt —
  the line is commented out in two places (`cdhle.cpp:316`, `:602`). Their
  ISR stub exists only so a game's `CD_init(A0)` call leaves something
  harmless behind. No comparison for our GPU-IRQ1 edge pacing, ack
  handling, or `GPUCanCaptureIRQ()` logic.
* **Subcode / `SBCNTRL` / `SB_TIME` / session-layout validation.** Neither
  tree implements subcode. BizHawk validates only `num_sessions >= 2` and
  the ATARI header string; there is no session-layout check to compare
  against, and nothing that would let us infer what the real BIOS
  validates.
* **Explicit BIOS patching or skipping.** They have none to compare —
  no BIOS is loaded, so there is nothing to patch. Our
  `jagMemSpace[0x80040B] &= 0xFE` auth skip and the `$005E40` GPU-magic
  hook (`jagcd_bios.c:52-56`) are unique to us.
* **The distilled JTRM notes have nothing on BUTCH.** `docs/jtrm-*.md`
  yields three incidental mentions (`jtrm-jerry.md:234`, `:416` on I2S
  slave mode; `jtrm-register-map.md:581` noting BUTCH sits on GPIO0) and no
  register or timing detail. The primary source *does* exist, in the main
  checkout at `docs/atari-jaguar-1999/'06 - Jaguar CD-ROM.pdf'` (39 pages;
  gitignored, so it is absent from a fresh `git worktree` — check the main
  checkout, not the worktree). It is a scanned document with **no text
  layer** (`pdftotext` yields 39 bytes), so it must be read as page images
  (`pdftoppm -png -r 110`). §4.2 is adjudicated from it.
* **What the CD-ROM doc did *not* answer: seek latency and the absolute
  data rate.** Neither the `CD_read` nor the `CD_mode` entry gives a
  transfer rate in bytes/second or a seek-time envelope, so candidate 3
  still rests on the MiSTer 30–315 ms figure quoted in `cdrom.c:41-52`,
  and the 352,800 B/s constant still rests on derivation
  (150 double-speed sectors/s × 2352 B/sector, using the doc's own p.2
  statement that Jaguar CDs are CD-DA raw format at 2352 B/sector) rather
  than on a printed rate. Pages 12-39 were not read; if someone needs the
  drive's timing envelope, that is where to look next.
* **No CD test images in this worktree** (`test/roms/private/` is empty),
  so none of the §3 experiments could be run here. They are proposals,
  not results.

---

## 7. Bottom line

BizHawk has Jaguar CD support, and it is HLE-only — so for the specific
question asked (why does *our real-BIOS* BUTCH/FIFO/DSA path fail to hand
off in three FMV titles) **their tree contains no direct answer**, because
they never implemented the thing that is failing.

What the comparison did yield:

1. Independent confirmation that three of our hard-won real-BIOS
   decisions are right (GPU IRQ1 at `$F03010`, the `$2C00` track-indexed
   layout including the duration field, the one-word FIFO capture skew).
2. A sharpened, measurable version of the timing hypothesis: our BIOS path
   achieves 11–29 % of its nominal rate because it is drain-limited, while
   our HLE path — which works for all three stuck titles — is wall-clock
   paced at nominal.
3. Two genuine, hardware-adjudicated bugs, each worth its own PR and
   neither established as this gap's cause:
   * `m68ki_init_exception` never switches to the supervisor stack
     (§4.1) — already fixed upstream in their tree, and demonstrably not
     this gap (mode-independent; the CD BIOS never enters user mode).
   * The CD `$15nn` Set Mode speed bit is discarded, so the drive never
     changes speed and the JTRM's documented read-error recovery sequence
     is a no-op (§4.2).

And one methodological correction to how this gap should be attacked: every
candidate here is a rate perturbation, but the three stuck titles stall at
three *different* BIOS PCs, so the first move is the RAM-driver snapshot
that identifies what each is waiting on (§3, "Do this first") — not another
sweep.

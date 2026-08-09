# Alien vs Predator (1994) — renderer reverse-engineering and hi-res reach

**Date:** 2026-08-09
**Epic:** #338, track 1 ("internal resolution upscaling")
**Branch measured:** `libretro/feature/338-hires-stage2` @ `836baf6` (Stage 1 + Stage 2)
**Status:** research report. All instrumentation was a throwaway compile-time
patch and is not part of this commit.

> **Reading the diff.** This report itself adds **no product code** — the
> report commit touches only this file, and later commits on the branch add
> only screenshots plus review fixes. The branch is *stacked on*
> `feature/338-hires-stage2` (open as PR #357), so a diff taken against
> `develop` also shows that PR's Stage 2 changes to `src/tom/blitter.c`,
> `src/tom/tom.c`, `src/tom/shadowfb.*` and `test/tools/hires_shot.c`. Those
> belong to #357 and disappear from this diff once #357 merges and this
> branch is rebased. Review them there, not here.
**Related:** `docs/hires-upscaling-design.md` (design), `docs/hires-stage0-census.md`
(corpus census)

---

## 0. Executive summary

This investigation was commissioned to explain why Alien vs Predator — counted
by the Stage 0 census as a qualifying Stage 2 title — was measured at
**0.0000% supersampled 2×2 blocks** on screen in gameplay, and to determine
whether some extension to hi-res could reach it.

**The premise no longer holds at branch HEAD, and the cause is already fixed.**

- At `836baf6` AvP measures **30.62%** supersampled 2×2 blocks in a static
  corridor scene and **13.01%** in a moving scene — on *both* blitter engines,
  producing bit-identical output.
- The 0.0000% is exactly reproducible by reverting one constant:
  `HIRES_EPOCH_WINDOW` 16 → 2 in `src/tom/shadowfb.c`. That is the constant
  commit `404cb11` ("hi-res epoch trusted window 2 -> 16 frames") changed to
  fix the identical symptom in Doom. The 0.0000% measurement was therefore
  almost certainly taken on a pre-`404cb11` build (inference — I did not
  observe which commit was used, only that the constant fully explains it).
- The failure was **not** in the blit shapes and **not** in the Stage 2 gate.
  With the window at 2, the blitter still produced 13,117,200 Stage 2 blocks
  of which 6,087,600 carried genuinely non-uniform sub-pixel content — the
  *identical* counts to the working configuration. Every one of them was then
  thrown away at the OP resolve: `op_hit` 42,827,520 → **0**.

Everything else the brief asked for is answered below and is worth keeping
regardless: AvP is a Doom-class span rasterizer driven entirely by the GPU
programming the blitter, its 3D view is blitted straight into the displayed
buffer with no intervening copy, its GPU stores **zero** bytes per frame into
any framebuffer the OP scans, and its rasterizer demonstrably carries
16.16 fixed-point texture coordinates — sub-texel information exists in
abundance and Stage 2 already recovers the recoverable half of it.

**Verdict: no extension needed. AvP is a first-class Stage 2 beneficiary
today.** The residual unreached fraction is ~3.6% of its 16bpp destination
writes and is not worth engineering. See §7.

---

## 0.1 What it looks like

Matched captures, same ROM, same input script, same frame — only
`virtualjaguar_internal_resolution` differs. The 1x frame is magnified with
nearest-neighbour replication to the 2x geometry, which is exactly what the
Stage 1 (box-replicating) path presents, so any difference between the panels
is Stage 2 content and nothing else. Frame 6000 of the §9 reproduction
command; 2x measures **30.6237%** non-uniform 2×2 blocks here, 1x is
box-replicated by construction.

That claim is verified, not assumed: across the whole frame, all **54,280**
uniform 2×2 blocks in the 2x capture equal their 1x source pixel *exactly*,
and so does the top-left sub-pixel of all **23,960** non-uniform ones. The two
runs are therefore the same machine state at the same frame, and Stage 2 is
strictly additive — it never alters the stock sample, it only fills in the
three sub-pixels beside it.

![Alien vs Predator frame 6000 side by side: the left panel is the 1x frame
with every pixel doubled to 652x480, the right panel is the same frame
rendered at 2x internal resolution with Stage 2 supersampling. Both show an
Alien's claw in a textured corridor facing a chevron-panelled
door.](site/pr-avp-hires-fullframe.png)

At full-frame scale the difference is **subtle** — the win is fine texture
detail, not a change in composition or colour. It reads at magnification:

![Magnified 6x crop of the left wall, floor and ribbed right-hand wall. In the
1x panel the wall ribs are flat 2x2 blocks of one colour; in the 2x panel the
same ribs carry intermediate texel values, and the diagonal wall/floor edge
steps in half-pixel increments instead of whole ones.](site/pr-avp-hires-crop-wall.png)

![Magnified 6x crop of the chevron-panelled door. The 2x panel resolves
sub-texel shading along the chevron edges and across the panel face that the
box-replicated 1x panel cannot represent.](site/pr-avp-hires-crop-door.png)

The wall/rib crop is the clearest: those are the vertical-column blits (A2
`XADD0` + `YADD1`, source **Y** fraction) described in §4, and the extra
scanline lands on a different source texel roughly half the time (§7's
honesty check). The door crop shows the same effect along the horizontal
chevron edges; some of what it adds there is a half-step value between two
adjacent texels rather than new structure, which at this magnification reads
as fine stippling. Both are real recovered source content, and neither is a
filter — nothing is interpolated or invented.

---

## 1. Method

Instrumentation (throwaway, reverted before this commit):

1. **Blit-launch census** at the single launch site — `BlitterWriteWord`,
   `(offset & 0xFF) == 0x3A` in `src/tom/blitter_mmio.c`, before dispatch to
   either engine, reading `blitter_ram` directly. Records `B_CMD` flags,
   `A1_FLAGS`/`A2_FLAGS` (depth, width, XADDCTL, YADD1), `A1_FINC`/`A1_FSTEP`,
   `PIXLINECOUNTER` inner/outer, source/destination base addresses, plus a
   distinct-tuple table.
2. **Gate-failure histogram**: for every blit satisfying the Stage 0 census
   predicate, the *first* Stage 2 gate condition that would reject it (union
   of the two engines' gates: `blitter.c:967` fast, `blitter.c:3669`
   accurate).
3. **Per-pixel Stage 2 counters** at both Stage 2 store sites: blocks stored,
   blocks whose `sblk[1..3]` differ from `sblk[0]`, and whether the half-step
   sub-sample lands on a *different source texel* than the stock sample.
4. **OP resolve outcome** in `ShadowHiresLineFromRAM`: hit / miss, plus
   whether a hit block was non-uniform, plus an 8 KB-bucket histogram of the
   addresses the OP actually fetched 16bpp pixel data from.
5. **Write-funnel probe** in `JaguarWrite{Byte,Word,Long}`: bytes into main
   RAM by `who`, bucketed the same way (the census's R2 check, which never
   covered AvP).
6. **Launcher attribution**: `who` and GPU/68K PC at each `B_CMD` write and at
   each `A1_FINC` write, plus a snapshot of GPU local RAM taken at the moment
   of the first texture blit (AvP swaps GPU overlays — a dump taken at exit
   disassembles into *different* code at the same addresses; see §5).

Scene reaching: `test/tools/hires_shot` with
`--press 3300:a --press 3600:a --press 3900:a --press 4200:a --press 4500:a
--press 5000:a`, screenshot-verified at frame 6000 (Alien in a textured
corridor facing a door; converted with `sips -s format png` and eyeballed).
Two windows were measured and are reported separately because the benefit is
scene-dependent:

| window | frames | scene |
|---|---|---|
| **static** | 5500–6100 (600) | standing in the corridor |
| **moving** | 5600–6100 (500), extra `--press 5600:up:400` | walking into the door |

**Reproduction caveat:** `test/harness/harness.c` answers
`virtualjaguar_usefastblitter` with a **built-in default of `enabled`** when
the option is not passed, which is the opposite of the core's shipped default
(`disabled` = Accurate). Unqualified harness runs therefore measure the *fast*
engine. Both engines were measured explicitly here.

---

## 2. What AvP's blit population looks like in gameplay

Static window, 600 presented frames, **151,440 blits = 252.4 blits/frame**.

| Property | Count | Share |
|---|---|---|
| `DSTA2` (A2 is the destination) | 150,600 | 99.4% |
| `SRCEN` | 151,200 | 99.8% |
| **`SRCSHADE`** | 150,480 | 99.4% |
| `GOURD` | **0** | 0% |
| `PATDSEL` | 120 | 0.08% |
| `ADDDSEL` | **0** | 0% |
| `BCOMPEN` / `DCOMPEN` | 0 / 0 | 0% |
| any Z compare or write | **0** | 0% |
| `UPDA1F` (outer fractional step) | **0** | 0% |
| `CLIPA1` | 0 | 0% |
| `DSTEN` | 0 | 0% |
| Destination depth 16bpp | 150,600 | 99.4% |
| Destination depth 8bpp / 32bpp | 240 / 600 | 0.6% |
| Source depth 16bpp | 150,600 | 99.4% |
| A1 `XADDCTL` = XADDINC (fractional inner walk) | 150,600 | 99.4% |
| A1 `XADDCTL` = XADDPHR (phrase copy) | 840 | 0.6% |
| `OUTER == 1` (one span per blit) | 151,080 | 99.8% |
| INNER length | mode 64–127, range 1–~190 | — |

**Stage 0 census predicate satisfied:** 150,600 (251.0/frame).
**Stage 2 gate passed:** 150,480 (250.8/frame) — **99.92%**.
**Gate failures:** 120 blits, all on `!SRCEN`. Every other gate condition
(source depth, destination depth, `BCOMPEN`, data-path selection, phrase mode,
walk shape) rejected **zero** blits.

There is no census-vs-gate predicate gap for this title. The Stage 2 gate as
written is essentially a perfect filter for AvP's renderer.

### Reconciling 85 vs 251 qualifying blits/frame

`docs/hires-stage0-census.md` §6 lists AvP at 85 qualifying blits/frame. That
figure is 405,800 blits over a 4,800-frame run that includes boot, the
attract/title sequence and menu navigation. Measured inside a pure gameplay
window the rate is **251/frame**. Both numbers are correct for what they
measure; the census row is diluted by non-gameplay frames, as its own §2
caveat about "menu/attract rows are lower bounds" anticipates.

### Two blit families = a Doom-style renderer

The distinct-tuple table resolves the whole 3D view into exactly two shapes,
both `B_CMD = $41802801` (`SRCEN | DSTA2 | GOURZ | LFU_AN | SRCSHADE`):

| | Wall columns | Floor/ceiling spans |
|---|---|---|
| `A1_FLAGS` (source) | `$00073820` | `$00033820` |
| `A2_FLAGS` (dest) | `$00064220` | `$00014220` |
| A1 XADDCTL | XADDINC (3) | XADDINC (3) |
| A1 `YADD1` | set | clear |
| A2 XADDCTL | **XADD0 (2)** — dest X fixed | **XADDPIX (1)** — dest X += 1 |
| A2 `YADD1` | set — dest Y += 1 | clear |
| `A1_FINC` fraction | in the **Y** half (`$CB7E0000`, `$C8C00000`, …) | in the **X** half (`$0000FE04`, `$0000FA08`, …) |
| INNER | 162–186 (column height) | 1–100 (span length) |
| Source bitmap | 128 px wide, 16bpp | 128 px wide, 16bpp |
| Destination bitmap | 320 px wide, 16bpp | 320 px wide, 16bpp |

`A1_FLAGS` width decode: `m = (flags>>9)&3 = 0`, `e = (flags>>11)&0xF = 7` →
`((4|m)<<e)>>2 = 128`. `A2_FLAGS`: `m = 1`, `e = 8` → `320`.

That is the textbook Doom decomposition: **vertical texture columns for walls
(destination walks down a column, source walks down the texture with a
16-bit Y fraction) and horizontal texture spans for floors and ceilings
(destination walks along a row, source walks along the texture with a 16-bit
X fraction).** No Z buffer, no gouraud — lighting is done entirely with
`SRCSHADE` (a per-blit intensity increment added to the CRY source byte).

Because `UPDA1F` is never set, AvP exercises **only the inner-walk half** of
Stage 2 (`sh2_qin` = 13,117,200; `sh2_qout` = 0). Anyone using AvP as a
Stage 2 regression fixture should know the outer-walk column-scaler path gets
no coverage from it.

---

## 3. Where the 3D view comes from — it is not GPU-stored

Write-funnel probe, static window, per presented frame:

| Writer | Bytes/frame into main RAM | Bytes/frame into OP-scanned buckets |
|---|---|---|
| Blitter | 46,001 | 46,001 (essentially all) |
| GPU (`who == GPU`) | 1,256 | **0** |

The GPU's main-RAM stores land in exactly five 8 KB buckets — `$004000`,
`$006000`, `$032000`, `$03C000`, `$03E000` — and the OP fetched 16bpp pixel
data from **none** of them at any point in the run. AvP's GPU writes are
engine state and display-list/parameter tables, not pixels.

This closes the R2 question for AvP, which the Stage 0 census left unmeasured
(its §5 table covers IS1, IS2-CD, Battle Morph, Cybermorph, Doom and Checkered
Flag only). **AvP is 100% blitter-rasterized.** The GPU's role is to *program*
the blitter, not to write pixels: 151,200 of 151,440 blits are launched with
`who == GPU`, 240 with `who == M68K`.

---

## 4. Following the pixels to the screen — no detail-destroying copy

OP 16bpp fetch footprint, static window: **72,960 words/frame** — exactly
304 × 240, i.e. the OP scans one full-screen 16bpp bitmap object per frame.
Those fetches span 37 buckets, `$094000`–`$0DDFFF` (~296 KB ≈ two
320 × 240 × 2 = 153,600-byte buffers). Each bucket averages 2,048 words/frame
against its 4,096-word capacity, the signature of **double buffering**: a
given buffer is scanned on roughly half the presented frames.

The blitter's destination base for the 3D blits (`$0B9600` in the sampled
tuples) sits inside that same range, and the blitter's per-frame byte
footprint overlaps the OP fetch footprint bucket-for-bucket.

**There is no composite/copy stage between rasterization and display.** The
counter that proves it end-to-end is the OP resolve hit rate: **97.83%**
(42,827,520 hits of 43,776,000 resolves) at `HIRES_EPOCH_WINDOW 16`. A
replicating phrase-mode copy into a different buffer would have shown up as a
near-total miss rate (a copy rewrites the destination word through a path
that stores a *replicated* Stage 1 block, destroying the sub-pixel content),
and phrase-mode blits are only 840 of 151,440 (0.6%) in the first place.

---

## 5. The rasterizer, disassembled

Launcher attribution puts the whole 3D view in GPU local RAM:

| GPU PC (at `B_CMD` write) | `B_CMD` | Count (600 f) | Role |
|---|---|---|---|
| `$F0358A` | `$41802801` | 56,040 | texture blit, first half |
| `$F035CA` | `$41802801` | 56,040 | texture blit, second half |
| `$F03630` | `$41802801` | 38,400 | texture blit |
| `$F0319C` | `$01800001` | 600 | 1/frame — buffer clear |
| `$F03676` | `$00010800` | 120 | pattern fill |
| `$00012C58` / `$00012D6C` (68K) | — | 240 | HUD |

**GPU overlays:** AvP reloads GPU local RAM with different programs. A dump
taken at process exit disassembles into completely different code at these
same addresses. All disassembly below is from a snapshot taken **at the
moment of the first `$41802801` blit**.

### 5.1 Per-polygon setup — the 16.16 step split

```
$F033B2  MOVE   R17, R00        ; R17 = du step, 16.16 fixed point
$F033B4  MOVE   R18, R01        ; R18 = dv step, 16.16 fixed point
$F033B6  SHRQ   #16, R00        ; integer part of du
$F033B8  SHRQ   #16, R01        ; integer part of dv
$F033BA  SHLQ   #16, R00
$F033BC  OR     R01, R00        ; R00 = (du.int << 16) | dv.int
$F033BE  MOVE   R18, R02
$F033C0  MOVE   R17, R01
$F033C2  SHLQ   #16, R02        ; fractional part of dv
$F033C4  SHLQ   #16, R01        ; fractional part of du
$F033C6  SHRQ   #16, R02
$F033C8  OR     R02, R01        ; R01 = (du.frac << 16) | dv.frac
$F033CA  MOVEFA R10, R28        ; \
$F033CC  LOAD   (R28), R29      ;  |  wait for blitter idle
$F033CE  BTST   #0, R29         ;  |  (B_CMD read, bit 0 = idle)
$F033D0  JR     EQ, $00F033CC   ; /
$F033D2  NOP
$F033D4  MOVEFA R14, R02        ; R02 = A1 register base ($F02200)
$F033D6  ADDQ   #28, R02        ; -> $F0221C  A1_INC
$F033D8  STORE  R00, (R02)      ; A1_INC  = integer step
$F033DA  ADDQ   #4, R02         ; -> $F02220  A1_FINC
$F033DC  STORE  R01, (R02)      ; A1_FINC = 16-BIT FRACTIONAL STEP
```

### 5.2 Per-span setup — the 16.16 start phase

```
$F03518  MOVE   R05, R01        ; R05 = v coordinate, 16.16
$F0351A  MOVE   R04, R02        ; R04 = u coordinate, 16.16
$F0351C  SHLQ   #9, R01         ; \  extract a 7-bit integer texel index
$F0351E  SHLQ   #9, R02         ;  |  (texture is 128 wide -> wraps mod 128)
$F03520  SHRQ   #25, R01        ;  |
$F03522  SHRQ   #25, R02        ; /
$F03524  SHLQ   #16, R01
$F03526  OR     R01, R02
$F03528  ADDQ   #12, R00        ; -> $F0220C  A1_PIXEL
$F0352A  MOVETA R02, R07
$F0352C  STORE  R02, (R00)      ; A1_PIXEL  = integer texel start
$F0352E  MOVE   R04, R01
$F03530  MOVE   R05, R02
$F03532  SHLQ   #16, R01        ; \  keep the low 16 bits = the fraction
$F03534  SHLQ   #16, R02        ; /
$F03536  SHRQ   #16, R01
$F03538  OR     R01, R02
$F0353A  ADDQ   #12, R00        ; -> $F02218  A1_FPIXEL
$F0353C  MOVETA R02, R26
$F0353E  STORE  R02, (R00)      ; A1_FPIXEL = 16-BIT SUB-TEXEL START PHASE
$F03540  MOVE   R16, R03        ; \
$F03542  MOVE   R15, R06        ;  |  screen-space extents
$F03544  SHLQ   #9, R03         ;  |
$F03546  SHLQ   #9, R06         ;  |
$F03548  NOT    R00, R06        ;  |
$F0354A  NOT    R00, R03        ;  |
$F0354C  SHRQ   #9, R06         ;  |
$F0354E  SHRQ   #9, R03         ; /
$F03550  DIV    R19, R06        ; hardware divider -> span length
$F03552  DIV    R20, R03
...
$F0357A  MOVEFA R11, R02        ; R02 = $F0223C  PIXLINECOUNTER
$F0357C  MOVETA R06, R19
$F0357E  STORE  R06, (R02)      ; INNER = span length, OUTER = 1 (BSET #16)
$F03580  MOVEFA R10, R02        ; R02 = $F02238  B_CMD
$F03582  MOVEI  #$41802801, R01
$F03588  STORE  R01, (R02)      ; LAUNCH
```

### 5.3 What this establishes

**AvP maintains texture coordinates and per-pixel steps in 16.16 fixed point
and hands the blitter the full 16-bit fraction of both the start phase
(`A1_FPIXEL`) and the step (`A1_FINC`).** This is the direct answer to "does
sub-texel information exist to recover": yes, unambiguously. The game does not
compute integer texel coordinates and round — it computes a sub-texel phase
and a sub-texel step, and the stock hardware simply truncates each sample to
one texel. Stage 2's half-step resample is reading information the game
genuinely produced.

Two further details from the same code, both consistent with the register
census: `A1_INC`/`A1_FINC` are written **once per polygon** (85.2 writes/frame
against 251 blits/frame), and only `A1_PIXEL`/`A1_FPIXEL` plus
`PIXLINECOUNTER` are rewritten per span — an affine (constant-step) mapper.
Each visible column issues **two** blits (`$F0358A` and `$F035CA`, equal
counts), split at the screen-width wrap check at `$F0356A`
(`ADD R06,R13 / CMP #$140,R13 / JR MI`).

---

## 6. Why Stage 2 "missed" — measured, with the A/B

Everything above says AvP should be reached, and at branch HEAD it is. The
0.0000% report is reproduced exactly by one constant.

**A/B on `HIRES_EPOCH_WINDOW` (`src/tom/shadowfb.c`), identical static window,
identical build otherwise:**

| Counter | window = 16 (HEAD) | window = 2 (pre-`404cb11`) |
|---|---|---|
| Stage 2 gate passed (blits) | 150,480 | **150,480** |
| Stage 2 blocks stored (`sh2_fast`) | 13,117,200 | **13,117,200** |
| Blocks with real sub-pixel content | 6,087,600 | **6,087,600** |
| OP resolves | 43,776,000 | 43,776,000 |
| OP resolve **hits** | 42,827,520 (97.83%) | **0** |
| OP resolve misses | 948,480 | 43,776,000 (100%) |
| On-screen non-uniform 2×2 blocks | **30.6237%** | **0.0000%** |

The blitter-side production is bit-identical. The detail was created and then
discarded, 100% of it, at the value+epoch check in
`shadow_hires_block()` — the tag matched, the epoch did not.

**Why AvP trips it:** the title double-buffers and its 3D engine takes more
than one presented frame per rendered view (46 KB/frame of blitter writes
against a ~150 KB buffer). The buffer the OP scans is therefore always older
than 2 presented frames — `op_hit` is not merely low at window 2, it is
*exactly zero*. This is the same failure mode `404cb11` documented for Doom's
~10–15 Hz double-buffered engine; AvP is a second instance of it, and the
16-frame window already covers both with margin (97.83% hit rate; the 2.17%
residual includes buckets never written in the window at all).

**Control:** Cybermorph at 2x measures 0.0000% non-uniform blocks on the same
build — the tool reports a real zero when there is nothing to report.

**Both engines, measured:**

| | Fast (`usefastblitter=enabled`) | Accurate (`disabled`, shipped default) |
|---|---|---|
| 16bpp A2 destination writes seen at the Stage 2 site | 13,731,600 | 13,117,200 |
| Stage 2 blocks stored | 13,117,200 (95.5%) | 13,117,200 (100%) |
| Non-uniform blocks | 6,087,600 | 6,080,040 |
| OP resolve hits | 42,827,520 | 42,827,520 |
| On-screen non-uniform 2×2 blocks | 30.6237% | **30.6237%** |

Identical output. (The 95.5% vs 100% difference is an artifact of where the
counter sits: the fast engine's site is reached for inhibited pixels too, the
accurate engine's is not.)

---

## 7. How much of AvP's detail is actually recoverable

The honest question is not "does Stage 2 fire" but "is there anything under
the sample when it does". Measured at the store site, by comparing the
half-step sub-sample's source texel against the stock sample's:

| | Static scene | Moving scene |
|---|---|---|
| Stage 2 blocks stored | 13,117,200 | 13,895,882 |
| …landing on a **different** source texel | 6,784,920 (51.7%) | 4,985,742 (35.9%) |
| …landing on the **same** source texel | 6,332,280 (48.3%) | 8,910,140 (64.1%) |
| Blocks with non-uniform content | 6,087,600 (46.4%) | 4,402,127 (31.7%) |
| OP resolves carrying non-uniform content | 17,354,655 (39.6%) | 7,018,407 (19.2%) |
| **On-screen non-uniform 2×2 blocks** | **30.62%** | **13.01%** |

Both figures were re-measured independently when the §0.1 screenshots were
taken, from a clean rebuild: static **30.6237%** at frame 6000, moving
**13.0138%** over frames 5650–6100 with `--press 5600:up:400`.

Reading:

- **Roughly a third to a half of AvP's textured pixels are minified** — the
  texture step exceeds one texel per screen pixel, the 1x sample discards real
  texels, and Stage 2 recovers them. That is genuine upscaling, not smoothing.
- The remainder are **magnified** (walls close to the camera, texel larger
  than a pixel). There the half-step sample legitimately lands in the same
  texel and the block is legitimately uniform. **No value of N can invent
  detail there** — the information does not exist. Making those pixels look
  better is interpolation/filtering, which is a different feature with a
  different (and much more intrusive) contract than the shadow surface's
  "content, never decisions" rule.
- The scene dependence is large and expected: standing in a corridor looking
  down its length is minification-heavy (30.6%); walking face-first into a
  door is magnification-heavy (13.0%).

The drop from 39.6% of OP resolves to 30.6% of screen blocks is the CRY
conversion collapsing some distinct 16-bit values to the same RGB, plus the
black border/overscan area of the 652 × 480 presented frame.

---

## 8. Verdict — ranked

**1. Do nothing. AvP is already served.** (Recommended.)
No code change reaches more of this title for any defensible cost. The gate
accepts 99.92% of its qualifying blits, both engines produce identical
supersampled output, the OP resolve retains 97.8% of it, and the unreached
remainder is bounded below.

**2. The residual, itemised — and why each is not worth taking:**

| Unreached | Size | Why it is not worth it |
|---|---|---|
| 120 blits/600 f fail the gate on `!SRCEN` | 0.08% of qualifying blits | No source ⇒ nothing to resample. Structurally unreachable, correctly so. |
| Inhibited pixels + non-Stage-2 16bpp writes | ~3.6% of 16bpp destination writes (fast engine counter) | Transparency-rejected pixels write nothing to the screen. |
| 8bpp/32bpp destination blits | 840 of 151,440 (0.6%) | HUD elements; a CLUT-destination extension would cost a new entry format and an OP CLUT resolve for a fraction of a percent of AvP's pixels. |
| OP resolve misses | 2.17% | Mostly words never written inside the measurement window. Widening the epoch window further buys nothing measurable — `404cb11` already found 16 saturates. |
| Magnified pixels | 48%/64% of Stage 2 blocks | **No information exists.** Only a filter would change these, and filtering is out of the Stage 2 charter. |

**3. Explicitly ruled out for AvP** (each was hypothesised in the brief and is
now measured false):

- *A GPU-store shadow hook.* AvP's GPU writes **0 bytes/frame** into any
  OP-scanned buffer. §3.
- *Phrase-mode destination support.* 0.6% of AvP's blits are phrase-mode and
  none of them touch the 3D view. §2.
- *Composite-copy detail propagation.* There is no composite copy — the
  rasterizer writes the displayed buffer directly. §4.
- *A census-vs-gate predicate gap* (source depth, `GOURD`-without-`SRCSHADE`,
  `XADDCTL`/`q_out` mismatch). Zero blits are rejected for any of these. §2.

**4. Transferable finding.** AvP is the second title (after Doom) whose entire
Stage 2 benefit was gated by `HIRES_EPOCH_WINDOW`, and the failure mode is
silent and total: full production, zero delivery, 0.0000% on screen and no
log line *naming* it (a `video_stall` line may well be present, but it is
unrelated and fires on healthy runs too — see §9). Any future "title X shows
no supersampling" report should check the
OP resolve hit rate **before** investigating blit shapes.

**Implemented.** That check is no longer throwaway instrumentation:
`src/tom/shadowfb.c` now carries permanent OP resolve counters
(`shadowHiresResolveHits` and the `MissEpoch` / `MissValue` / `MissNoPage`
buckets), reported by the existing `crash_detect` verbose heartbeat every 600
frames while hi-res is active:

```
[CRASH-DETECT] hires_resolve frame=2400 N=2x hits=79588699 misses=68948450 \
  (epoch=24224010 value=44388198 nopage=336242) rate=53.6% window_rate=98.2%
```

Read `window_rate` (last 600 frames) rather than the cumulative `rate`, which
AvP's ~1500 frames of menus dilute. The same fixture on the same build with
the epoch window forced back to 2 reads `window_rate=0.0%` with the whole miss
population in `epoch=` — i.e. the counter would have answered this
investigation in one line. Bucket-by-bucket triage lives in
`docs/hires-upscaling-design.md` §8, Stage 2; the counters are exported in the
test ABI so harnesses can `dlsym` and assert on them.

---

## 9. Reproduction

```bash
ln -sfn "${JAGUAR_ROMS_PRIVATE:?}" test/roms/private
DEVELOPER_DIR=/Library/Developer/CommandLineTools make -j"$(getconf _NPROCESSORS_ONLN)"
cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
   -o test/tools/hires_shot test/tools/hires_shot.c test/harness/harness.c -ldl -lm

# working (branch HEAD): 30.6237%
./test/tools/hires_shot ./virtualjaguar_libretro.dylib \
  "test/roms/private/ROMS/Alien vs Predator (1994).jag" \
  --option virtualjaguar_internal_resolution=2x --frames 6100 \
  --press 3300:a --press 3600:a --press 3900:a --press 4200:a \
  --press 4500:a --press 5000:a --shot 6000

# reproduce the reported 0.0000%: set HIRES_EPOCH_WINDOW to 2 in
# src/tom/shadowfb.c, rebuild, rerun the exact command above.

# the §0.1 screenshots: same command at each resolution, with --out-prefix,
# then crop/magnify the matched PPM pair (1x nearest-neighbour to the 2x
# geometry).  1x measures 90.0051% and 2x measures 30.6237% at frame 6000 --
# those two numbers are NOT comparable, see "Reading the number" below.
#   ... --option virtualjaguar_internal_resolution=1x ... --out-prefix /tmp/1x/avp
#   ... --option virtualjaguar_internal_resolution=2x ... --out-prefix /tmp/2x/avp
```

Add `--option virtualjaguar_usefastblitter=disabled` to measure the Accurate
engine (the harness otherwise defaults to Fast — see §1).

### Reading the number

`hires_shot`'s percentage is the share of 2×2 blocks whose four pixels are not
all equal. It is a **2x-only** metric: at 2x, box replication makes every block
uniform by construction, so anything above zero is Stage 2 content. At 1x the
same walk compares four independent scene pixels and reads 60–90% on ordinary
gameplay (measured: 90.0051% at frame 6000). Never quote a 1x-vs-2x delta in
this number.

Four ways to get a **0.0000%** that is not "Stage 2 failed to reach this
title", all measured on this ROM at `836baf6`:

1. **The frame isn't a Stage 2 scene.** AvP's title art, character select and
   briefing screens read an exact `0.0000%` at 2x (frames 400 / 700 / 1000 /
   1300) — no qualifying fractional-walk blit lands there. Only the
   first-person 3D view benefits. Check the PPM before believing the number.
2. **Nothing was dumped.** `--shot F` with `F` beyond `--frames` dumps nothing
   and the summary prints `total_blocks=0 ... pct=0.0000`. Check
   `shots=` and `total_blocks=` on the `VARIANCE` line.
3. **Wrong build.** A core without Stage 2 (e.g. `develop`, or a stale dylib)
   box-replicates and reads exactly `0.0000%` at 2x with no error. Run with
   `VJ_EXPECT_BUILD=$(./scripts/build-id.sh)`.
4. **Shadow content aged out** — the original failure this report explains,
   reproducible by setting `HIRES_EPOCH_WINDOW` to 2.

**A `video_stall` line in the log does *not* imply any of these.** AvP keeps
re-blitting an identical view when the player stands still, so the presented
framebuffer freezes (the watchdog fires) while the shadow surface stays fresh
and the metric stays high. Measured on the committed
`test/fixtures/avp_reach_gameplay.press`: frames 3200 / 4000 / 5000 / 5800 /
6000 are **byte-identical PPMs** and every one reads 25.0601%. The doc's own
30.6237% frame likewise sits inside a `video_stall` window. Conversely a
frozen frame invalidates anything that assumes motion, so the fixture now
carries keep-alive input out to frame ~6300; with it, the same frames read
23.59 / 10.94 / 22.46 / 21.13 / 21.05% — non-zero *and varying*, which is the
signature of a live scene.

The instrumentation pattern, for re-running or extending: counter struct in a
throwaway header, defined in `blitter_mmio.c`, dumped from an `atexit`
handler gated on a `VJ_AVP_PROBE=<path>` environment variable, with a
`VJ_AVP_FROM`/`VJ_AVP_TO` frame window so menu and attract frames do not
dilute gameplay rates. Hooks at: the `0x3A` blit-launch site, both Stage 2
store sites in `blitter.c`, `ShadowHiresLineFromRAM` in `shadowfb.c`, the
three `JaguarWrite*` main-RAM branches, and a GPU-local-RAM snapshot taken at
the first texture blit (not at exit — AvP swaps overlays).

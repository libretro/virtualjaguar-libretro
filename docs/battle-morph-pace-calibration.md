# Battle Morph pace calibration — RISC clock sensitivity

Measurement record for the question "is Battle Morph gameplay RISC-bound, and by
how much?" No emulator code changes were made for this document.

Companion to [`doom-pace-calibration.md`](doom-pace-calibration.md), which
establishes the same kind of ground truth for Doom. Read that one first for the
`gametic`-per-frame coupling argument; this note is narrower — it is a clock
sweep on a second title, run to find out whether BM is a usable calibration
scene for RISC-throughput work (issues #313, #319).

**Measured on `v3.1.0 59f7f9c`** (release commit, clean tree), macOS arm64,
HLE CD mode. Numbers are same-host relative ratios and are not comparable across
machines.

---

## 1. Method

Start point is a RetroArch save state captured in live gameplay (not attract, not
a menu), RZIP-unwrapped to the raw RASTATE container the harness accepts. 1800
frames (~30 s) per arm.

Each arm emits one CSV row per frame — `frame,w,h,duped,nonblack,hash` — where
`hash` is FNV-1a over the entire XRGB8888 framebuffer
(`test/tools/frame_hash_ab.c`). Arms are then compared by **when each distinct
visual state first appears**, so the metric is pace rather than pixel equality:

* **render rate** — how many distinct framebuffer states an arm produced in the
  window. Fewer = the GPU finished fewer renders.
* **pace ratio** — slope of (arm frame index) vs (1x frame index), fitted over
  states both arms reach. `1.000` = identical pace; `>1` = states arrive later.

Deliberately *not* per-frame screenshot dumps: an earlier attempt at this sweep
wrote ~113 MB of PPMs across four arms and filled the volume. Total output here
is ~430 KB of CSV, and the answer is a number instead of a judgement call.

## 2. Determinism gate

Before comparing any two arms, the same config was run **twice**. From a loaded
CD state this is not a formality — DSA response delay, FIFO drain pacing and I2S
timing are all live.

> Two identical 1x runs: **all 1800 frame hashes matched exactly.**

Cross-arm divergence below is therefore signal, not run-to-run noise. This gate
exists because three separate findings in this repo turned out to be measurement
artifacts rather than emulator bugs (the Myst "stall", the Battle Morph boot
flake, and the FMV handoff-gap band classifier).

## 3. Results

Baseline `risc_clock_scale=1x`, `dram_timing=disabled` → 959 distinct states.

| arm | renders vs 1x | pace ratio | linear prediction |
|---|---|---|---|
| RISC 0.5x | 0.588× | **1.794** (slower) | 2.000 |
| RISC 1x | 1.000× | 1.000 | 1.000 |
| RISC 1.5x | 1.268× | **0.827** (faster) | 0.667 |
| RISC 2x | 1.349× | **0.796** (faster) | 0.500 |
| dram_timing enabled, RISC 1x | 0.853× | **1.212** (slower) | — |

Pace ratios are the common-state-set fits from §3.1. The 0.5x ratio locks in by
frame ~116 and holds flat (per-state ratios stay within 1.785–1.797) for the rest
of the run.

### 3.1 Robustness — sampling bias and duped frames

Each arm renders a different number of distinct states, so a naive pairwise fit
uses a differently-composed sample per arm (267 shared states at 0.5x vs 478 at
1.5x). Re-fitting every arm over the 24 states common to *all five* arms:

| arm | pace (common 24-state set) | pace (pairwise) |
|---|---|---|
| RISC 0.5x | 1.7939 | 1.7934 |
| RISC 1.5x | 0.8269 | 0.7918 |
| RISC 2x | 0.7963 | 0.7590 |
| dram enabled | 1.2120 | 1.1884 |

The 0.5x figure is unchanged to three decimals. The fast arms move by ~0.035 and
they move *toward each other* — on the common set 1.5x and 2x differ by 0.031
against a linear prediction of 0.167. The saturation in §4.2 is therefore not a
sampling artifact; it is slightly stronger than the pairwise fit suggested. (n=24
is small, but those states span frame 1 to ~1500 and the per-state lag is
extremely regular.)

`duped` (core reported a NULL framebuffer) is **0 in every arm**, so the
distinct-state counts are genuinely renders completed, not frame duplication.

## 4. Findings

### 4.1 BM gameplay is strongly RISC-bound

Halving RISC throughput slows the scene by 1.79× and cuts completed renders to
59%. The same visual state arrives measurably later and the offset grows
linearly. This is what a scene with no RISC headroom looks like, and it is what
makes BM usable as a calibration target — a scene with slack would return a null
here regardless of what the timing model does.

### 4.2 Pace saturates; it is not linear in RISC throughput

Going 1.5x → 2x buys almost nothing (0.827 → 0.796) against a linear prediction
of 0.667 → 0.500. An Amdahl-style split gives an *inconsistent* RISC-bound
fraction that shrinks as the clock rises:

| scale | implied RISC-bound fraction |
|---|---|
| 0.5x | 0.79 |
| 1.5x | 0.52 |
| 2x | 0.41 |

A fixed serial/parallel split cannot produce that. The shape is the signature of
quantisation to a video field boundary: once the renderer beats the field
deadline, extra RISC speed buys nothing.

This is independent corroboration, from a second title, of
`doom-pace-calibration.md` §"Sizing — an upper bound, not a target": *"Real frame
time varies with visible geometry and then quantises hard to a field boundary — a
flat multiplier scales every scene identically and cannot do that."*

### 4.3 `dram_timing` is a live lever on this title

+18.8% pace and −15% renders on BM gameplay. For contrast,
`doom-pace-calibration.md` line 478 records dram_timing at **+0.0–0.1%** on the
Doom attract demo.

That comparison moves two variables at once — different title *and* different
scene type (attract demo vs live gameplay) — so it does **not** establish which
one accounts for the gap, and it is not evidence that the Doom measurement is
wrong. The defensible claim is the narrow one: the attract-demo near-null must
not be assumed to transfer to other titles or scene types, and `dram_timing`
needs per-title measurement before its cost model is described as calibrated.

### 4.4 `risc_clock_scale` honours its documented contract

The core option's description states that audio sample pacing stays at stock so
audio does not pitch-shift. Confirmed: `total_samples` is exactly 1,440,000 and
`batch_calls` exactly 1800 in **every** arm including 0.5x and 2x. Non-silent
sample counts move by <0.01% (1,439,056 → 1,438,928), consistent with the DSP
computing slightly different content rather than with a pacing change.

## 5. What this does not show

**This is not validation of #313.** A `risc_clock_scale` sweep is a flat
multiplier, and #313's premise is that the missing term is per-access /
per-hazard, *not* a multiplier. A null result here would have meant "BM is the
wrong calibration title", not "#313 is wrong".

What it does establish, for whoever implements the pipeline-hazard model:

* BM gameplay is a valid calibration scene — it responds strongly and
  reproducibly to RISC throughput.
* **Validate at stock-and-slower throughput.** Above ~1.5x this title is
  field-quantised and the metric goes blind, so an A/B run conducted only in that
  region would report a false null.
* `dram_timing` interacts here at ~19%, so it must be pinned (not left at
  whatever the default is) when measuring a hazard model on this title, or the
  two levers will be confounded.

## 6. Reproduce

Build the instrument:

```bash
cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
   -o test/tools/frame_hash_ab test/tools/frame_hash_ab.c \
   test/harness/harness.c -ldl -lm
```

RetroArch writes save states RZIP-compressed; the harness wants the RASTATE
container. Strip the outer layer (any tool that inflates the per-chunk deflate
streams after the 20-byte header will do), then:

```bash
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/frame_hash_ab \
  ./virtualjaguar_libretro.dylib "<Battle Morph (USA).cue>" \
  --load-state bm.raw.state --frames 1800 \
  --option virtualjaguar_risc_clock_scale=0.5x \
  --option virtualjaguar_dram_timing=disabled \
  --csv r05x.csv --system-dir test/roms/private
```

Run the 1x arm twice first and diff the CSVs — if they are not identical, stop:
the comparison is uninterpretable and the divergence is noise. Then compare arms
by first-appearance frame of each distinct hash.

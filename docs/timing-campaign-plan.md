# GPU timing-accuracy campaign plan (#408 / #313 / #401)

Scoping document for the remaining timing-accuracy work, written 2026-08-17
against `develop` @ `01b791b`, after the #434 hardware measurement landed.
This is the implementation plan the #408 board asked for: mechanism first,
then ordered stages with their own gates, effort and risk, then the
calibration protocol and a go/no-go recommendation.

Companion documents (none of this is re-derived here):

* [`gpu-timing-spec.md`](gpu-timing-spec.md) — netlist-pinned GPU/DSP stall
  rules; §14 holds the Verilator-pinned constants.
* [`doom-render-cost-census.md`](doom-render-cost-census.md) — where Doom's
  frame time actually goes in this core, component by component.
* [`doom-pace-calibration.md`](doom-pace-calibration.md) — the game side
  (`MiniLoop`, `vblsinframe`, the `I_Update` gates).
* [`lightgun-design.md`](lightgun-design.md) — the HC-granularity
  discrepancy evaluated in §6 below.

---

## 1. Ground truth: the three calibration references

### 1.1 Doom attract demo — the primary bar (#434, measured 2026-08-17)

**Hardware presents the JagDoomEX attract demo at ≈ 4.0 fields/flip
(15.01 flips/s). We present 2.00.** The #434 measurement survives the
camcorder pathologies (CRT-camera beat, long exposure, 24 fps aliasing): the
discrete-cadence simulation excludes k=2 in all nine camera-parameter
combinations, k=4 fits across the mid-range, and 4 matches the demo's own
hard-coded `vblsinframe = 4`. Measurement band 3.2–4.9; cadence-selected
4.00.

Known caveat carried forward: a 24 fps long-shutter capture cannot
distinguish "always exactly 4" from "mostly 4 with occasional 5-field
overruns". The bar is therefore **mean fields/flip in [3.5, 4.5] over the
demo steady state**, not a point equality, until a 60+ fps capture exists.

This number **supersedes** the earlier BigPEmu screen-capture reference
(median 2.00 fields/flip): BigPEmu does not model this — it ships a
per-title throttle script for Doom (v1.093 changelog), and the #401
real-hardware comparison confirmed both emulators run the demo fast. BigPEmu
remains useful for one thing: its gap *distribution* has a load-dependent
tail (26% of flips at 3+ fields) where ours is a delta function at 2
(98.7%), which independently points at load-dependent cost as the missing
term.

### 1.2 Battle Morph (#331) — CD-side reference, reporter has hardware

Runs unplayably fast here; BigPEmu "emulates the lag". The reporter owns
real hardware and has offered side-by-side comparisons. A 2026-08-15 note
says it may already be paced with `gpu_pipeline_timing` on — unverified.
Protocol in §5.2.

### 1.3 Checkered Flag — the overshoot canary

CF's frame limiter is decoded: budget RAM `$43B0` (0/1/2 = 60/30/20 Hz),
limiter code at ROM `$8026EE`. In our core the budget reads 0 (60 Hz)
throughout a race across the whole overclock grid, and BigPEmu's notes say
CF is "generally playable at 60 Hz" on their side too. CF is therefore not a
pace-up target but a **did-we-overcharge canary**: a timing model that
pushes `$43B0` to 2 (20 Hz) in ordinary scenes has overshot. Watching
`$43B0` with a vjtrace watch during a scripted race is a one-command
regression probe. A move 0 → 1 in genuinely heavy scenes is *plausible*
hardware behaviour and needs corroboration (reporter/BigPEmu) before being
called a regression.

---

## 2. Mechanism: why we present 2.00 where silicon presents 4.00

### 2.1 The clock-scale immunity, explained (this is measured, not inferred)

#378's sweep found the Doom demo cadence **immune to `risc_clock_scale`**:
0.5x, 1x and 2x all measure exactly 2.00 fields/flip. This is not a
paradox and it required no new instrumentation to explain — the mechanism
was pinned by the #408 vjtrace investigation and the #401/#422 census:

1. **The flip is gated by Doom's own software floor, not by compute.** The
   GPU-side presenter (`r_phase9.gas:562–612`) spins on
   `ticcount - lastticcount >= 2` — `ticcount` increments once per VBL — and
   only then swaps. Flips are therefore field-quantized with a hard floor of
   **2 fields**, enforced by a counter the GPU clock cannot touch.
2. **Our render work never comes close to filling the 2-field window.**
   Census: with all timing options off, non-spin GPU work is 0.45–0.62
   fields per flip; the GPU spends ~75% of every field in three spin loops
   (idle mailbox 26.6%, display-list adopt handshake 34.5%, the 2-field gate
   itself 13.8%) — only 19.3% of GPU time is the renderer. Halving or
   doubling the clock just moves the completion point around *inside* a
   window it never fills; the spin loops absorb the difference and the flip
   still lands on the same field boundary. Hence exactly 2.00 at every
   scale.
3. **The floor makes present rate an insensitive instrument below the
   threshold — and only below it.** The census counterfactual multiplied
   only non-spin instruction cost: x2 → 1.99–2.14 fields/flip (still mostly
   absorbed), x4 → 2.70–3.33 (floor crossed, cadence moves). The lever is
   real; it engages only once total work-to-flip exceeds 2 fields.

**Conclusion the campaign is built on:** the binding constraint in our model
is Doom's own 2-field software floor. The question "why does hardware take
4?" is *not* "why is our GPU so fast?" — it is "what occupies ~2 additional
fields of wall time on silicon that our model never charges anyone for?"

### 2.2 Why the answer cannot be GPU instruction cost

* The `gpu_pipeline_timing` model is **within ~15% of its Verilator-pinned
  netlist ceiling** (`gpu-timing-spec.md` §14: write-back port conflict 2.00
  ticks/instr; external load `L = 7 + D`). Doom's renderer is ~250k GPU
  instructions/flip; at the ceiling that is ~1.1–1.3 fields of a 4-field
  frame. Reaching 4 fields through instruction pricing alone would need ~3x
  beyond what the silicon's own design files permit. (Census "do not
  re-attempt" list: instruction re-pricing, cart-ROM re-pricing, the
  `$F0308A` "inner loop" — it is a mailbox poll.)
* External-access pricing was verified, not assumed: mean charge 5.05
  sysclks over 24,132 GPU external accesses/field, page-miss latency 12 =
  exactly the Verilator constant; only 226/24,132 accesses touch cart ROM.

### 2.3 Hypothesis chain for the missing ~2 fields, ranked by evidence

**H-A — cross-master bus occupancy/queueing is unmodeled (primary).**
Every master in this core pays its *idle-bus* cost, charged to its own
budget only; no master ever waits for another's traffic
(`bus_arbiter.h` header, and `contention_scale` — the diagnostic multiplier
— defaults to 1). On silicon, one memory controller serves the OP's video
fetch (priority 5), the blitter, DRAM refresh (9), the GPU (2) and the 68K
(0, default owner) — and during active display **OP fetches win every
arbitration against a normal-priority GPU** (spec R18/R19), so GPU external
accesses wait for horizontal gaps. Evidence this is the residual's owner:
  * The census's serialized component budget (~2.3–2.5 fields incl. an
    estimated 68K term) leaves ~1.5 fields with no owner, and §5 names DRAM
    contention as the best-supported candidate.
  * BigPEmu's load-dependent tail vs our delta function (§1.1).
  * There is real traffic to queue: Doom issues ~400 blits/field and
    42–45k px/field (the "zero blitter traffic" claim was a tooling false
    negative, #422), 24k GPU external accesses/field, plus the OP's
    framebuffer fetch on every displayed line.
  * Plausibility arithmetic: if contention lifts the mean GPU external
    access from 5 to ~15–18 sysclks during active display, that alone is
    24k × ~11 ≈ 260–310k sysclks/field ≈ **1.2–1.4 fields/flip** of new
    wall time, before the 68K's share. The magnitude fits; the *shape*
    (load-dependent) matches BigPEmu's tail.
  * History: the cross-processor half of PR #169 was removed because naive
    per-timeslice deduction was harmful (bursty pacing, slowed innocent
    68K-paced work). The work item is to model it correctly (per-halfline
    occupancy → expected grant latency), not to restore the old code.

**H-B — 68K memory cost is underpriced (secondary, needs measurement
first).** The census's 68K row (~1 field of non-spin work/flip) is a
sample-share **estimate** charged at UAE published timings with essentially
no DRAM wait states. On silicon the 68K is the lowest-priority master;
every one of its DRAM cycles can be stretched by refresh/OP/blitter/GPU.
First deliverable is a real work/spin cycle counter for the 68K (mirroring
the GPU one), because right now the term's size is not known to better than
±0.5 field.

**H-C — DSP pays nothing on the bus (documented asymmetry, small).**
`bus_arbiter.h`: "DSP external accesses currently pay NOTHING (no charge
hook in dsp.c)". Doom's DSP does audio mixing with external traffic; the
term is real but bounded small for pacing. It rides along with the S1 DSP
work.

**H-D — remaining GPU-model gap (≤ ~15%, not the lever).** Closing it is
part of making the model defaultable, not a pacing strategy.

### 2.4 The overclock-cancellation problem, and why H-A solves it structurally

The census's blocking finding against defaulting the existing options on:
at `risc_clock_scale=2x` the pipeline model's gain is exactly cancelled
(back to 2.00 fields/flip) — "an accuracy default any user can switch off
by raising an unrelated performance option is not an accuracy model."

Per-instruction stall costs live in the **core-time** domain and shrink
under overclock. Memory-system latencies live in the **wall-time** domain
(#318 cycle-domain contract, already implemented in `bus_arbiter.h`): DRAM
does not speed up when a core option overclocks a processor. A contention
model charges the missing time as memory-system occupancy — so its pacing
effect is **invariant under `risc_clock_scale` by construction**. That is
the structural reason H-A is the right vehicle for the eventual default
flip, independent of it also being the best-evidenced residual owner.

---

## 3. Work breakdown

Ordered, independently-landable stages. Every stage that touches the timing
model inherits the #406 gate philosophy: **liveness floors across a
parameter sweep, never single-point runs** — wedges in this core relocate
rather than disappear (#406 wedged at scales 4 and 8 before the Verilator
constants and at 3 after; #456 repeated the class on the DSP). Cycle-source
precedence throughout: **jag_sim netlists > JTRM > MAME**; netlists break
JTRM ties (established by #354).

### S0 — Calibration bar as an automated gate (2 days, risk: low)

* `doom_pace_gate.sh`: `present_rate_probe`/`frame_hash_ab` over the demo
  steady state (frames 600–1500, windowed), reporting mean fields/flip
  **and the gap histogram** (the distribution is the sensitive instrument —
  §1.1). Target band [3.5, 4.5]; current develop asserts ~2.00 as the
  frozen baseline so drift in either direction is loud.
* Add the **GPU busy-time per flip** counter (census §0's temporary
  instrument) as a permanent `TEST_EXPORTS` perf counter — below the
  2-field floor it is the *only* sensitive instrument (§2.1.3).
* CF canary: scripted race + vjtrace watch on `$43B0` (`--watch` low bound
  rounded down to a multiple of 4 per the coverage rule), assert budget
  stays 0.
* Exit 77 when private ROMs are absent (sweep convention), never a silent
  pass.

### S1 — #313 remainder: DSP scoreboard + DSP bus hook (4 days, risk: medium)

The GPU side of #313 exists (`virtualjaguar_gpu_pipeline_timing`,
netlist-pinned, default off). The DSP uses the same pipeline blocks
(`jag_sim/netlists/jerry/SBOARD.NET` etc. — spec preamble), so the rules
port verbatim:

* Mirror the R2/R3/R5/R9/R15/R16 + §14 write-back-conflict stall model from
  `gpu.c` into `dsp.c` under the same option.
* Add the missing DSP external-access charge hook (H-C), in the wall-time
  domain per the `bus_arbiter.h` contract.
* **Gate:** `make TEST_EXPORTS=1 test` incl. **both** audio tests
  (`test_audio_clipping` AND `test_audio_presence` — IS1 RMS envelope
  200–25000; the DSP is the audio engine and the silencing-regression class
  is the known failure mode), `test_dsp_mac40`, `dram_scale_sweep.sh` at
  `VJ_SCALES="1..16"` with the option on, acid suite no-regressions,
  RetroArch listen test on a real game. Re-verify #331 after this stage
  (§5.2), not before.
* Risk: audio timing shifts (I2S cadence is measured-fragile — #393); DSP
  IRQ-return timing interacting with the CD HLE engine.
* Expected pacing effect on Doom: small (audio DSP is not the long pole).
  This stage is correctness/completeness of #313, not the Doom fix — the
  2026-08-16 calibration comment on #313 says exactly this.

### S2 — Measure and pin contention (5 days, risk: medium — decision gate)

Measurement before model; this stage writes no shipped behaviour.

* **68K work/spin cycle counter** (H-B's missing input): split 68K cycles
  by PC-in-spin-loop exactly as the GPU census did, turning the ±0.5-field
  estimate into a number.
* **Per-master occupancy census**: extend the existing perf counters to
  report, per halfline, sysclks of bus occupancy by OP / blitter / GPU-ext /
  68K / refresh on the Doom demo (and 2–3 other render-bound titles). This
  is bookkeeping on charges the model already computes, plus the OP row it
  already accumulates (`op_clk_accum`).
* **Verilator pinning** (spec §12 item 6, the one remaining sim-pinned
  item): GPU grant-latency distribution while (i) a normal-priority blit
  runs, (ii) a BUSHI blit runs, (iii) OP fetch is active vs VBLANK, (iv)
  the 68K owns the bus. The jag_sim harness and kernel recipe already
  exist and produced §14; this extends the same rig. Deliverable: an
  occupancy-fraction → expected-grant-latency function with netlist
  provenance, written into `gpu-timing-spec.md` §15.
* **Decision gate for the campaign (see §7):** multiply the measured
  traffic mix by the pinned latency function. If it cannot supply ≥ 1.0
  field/flip of additional wall time for Doom, the accuracy path cannot
  reach 15.01 flips/s and the fallback in §7 activates.

### S3 — The contention model (7 days, risk: HIGH)

Grow the surviving `bus_arbiter` half back toward the full model, correctly
this time:

* Per-halfline occupancy accounting per master; each master's external
  access pays base cost + expected queueing derived from the S2 function of
  *other* masters' concurrent occupancy (OP active-display window modelled
  as an occupancy fraction of the halfline — this deliberately does **not**
  require sub-halfline beam position, see §6).
* Charges stay in the wall-time domain (§2.4), converted to each master's
  scaled cycle domain at the deduction point exactly as
  `bus_arbiter_m68k_access()` does today.
* Replace the `contention_scale` diagnostic multiplier's role: the sweep
  keeps the env knob, the model supplies the physics.
* Avoid the PR #169 failure mode by construction: no cross-processor
  budget deduction in per-timeslice chunks; only per-access expected
  latency, smooth by design.
* **Gate:** `dram_scale_sweep.sh` liveness floors at scales 1..16 (both
  option directions), the S0 Doom gate (histogram must grow mass at 3–4
  fields; below-2 mass must not appear), CF canary, acid suite, full
  `make test`, fb_ab corpus sweep (§5.4), cd_boot_matrix spot-run for CD
  regressions.
* Risk: this is exactly the terrain where #406-class wedges live (a
  GPU/68K handshake that tolerates N sysclks of skew deadlocks at N+1). The
  sweep-with-floors gate is the mitigation; expect at least one relocated
  wedge during development and budget for it.

### S4 — 68K memory-cost calibration (4 days, risk: high blast radius)

With S2's 68K numbers in hand, route the 68K's DRAM accesses through the
same occupancy-derived latency (the plumbing —
`bus_arbiter_m68k_access()`, `m68k_pending_stall`, the scaled-domain carry
— already exists). Every title's 68K pace moves, so this stage leans
hardest on the corpus sweep and on the real-BIOS boot checks that killed
PR #169's first attempt (BIOS logo animation pacing is the known canary).
Gate: as S3, plus SRAM/EEPROM timing tests and the netlink lag tests
(receive-side quantization is pacing-sensitive).

### S5 — Defaults flip + recalibration + title verification (4 days, risk: medium)

The #408 end state: **one correct system, no toggles.**

* Fold `gpu_pipeline_timing` (+ DSP mirror + contention) into the
  accurate-mode default; the overclock-cancellation objection is retired by
  §2.4 (verify: Doom gate must hold at `risc_clock_scale` 0.5x/1x/2x —
  memory-domain time does not scale).
* Retire or repurpose the experimental option surface (keep `VJ_DRAM_SCALE`
  as the dev-only sweep knob).
* Savestate: new arbiter occupancy fields join the release's single
  savestate bump (policy: one bump per release).
* Title verification per §5; re-run the #378 immunity sweep expecting
  immunity to be *broken* in the right direction (0.5x should now slow the
  demo below 15 flips/s; 1x lands in band).

**Total: ~26 focused days** (S0 2 + S1 4 + S2 5 + S3 7 + S4 4 + S5 4).
Honest calendar estimate for "Doom paces like hardware": 5–6 working weeks,
with the S2 decision gate at roughly the 2-week mark.

---

## 4. Effort summary

| Stage | What | Days | Risk | Independently landable? |
|---|---|---:|---|---|
| S0 | Doom 15.01 gate + busy-time counter + CF canary | 2 | low | yes (test-only) |
| S1 | DSP scoreboard + DSP bus hook (#313 remainder) | 4 | medium (audio) | yes |
| S2 | Contention measurement + Verilator pinning | 5 | medium | yes (no shipped behaviour) |
| S3 | Bus contention/queueing model | 7 | **high** | yes, behind existing option until S5 |
| S4 | 68K memory-cost calibration | 4 | high blast radius | yes |
| S5 | Defaults flip, corpus recalibration, titles | 4 | medium | terminal |

---

## 5. Calibration protocol

### 5.1 Doom (primary, automated)

The S0 gate, run at every stage boundary: mean fields/flip + histogram over
frames 600–1500, NTSC, stock clocks. Stage acceptance is **monotone
progress toward [3.5, 4.5] with no mass below 2** (below-2 mass would mean
we broke the game's own floor — a correctness bug, not overshoot). The
sensitive instrument below the floor remains GPU busy-time per flip; report
both numbers always. `VJ_EXPECT_BUILD=$(./scripts/build-id.sh)` on every
run; measurements from a private worktree + private binary (parallel-agent
dylib-swap hazard is documented history).

### 5.2 Battle Morph (#331, reporter's hardware)

1. Ours: `cd_visual_verify` motion timeline + `present_rate_probe` on an
   agreed scene, BIOS **and** HLE modes, stock clocks, model on — after S1
   (the 2026-08-15 comments require exactly this before closing).
2. Reporter: same scene on hardware, ideally with an in-game timer or
   countable animation in frame, so the ratio is measured, not felt.
3. Acceptance: our motion rate within ~10% of hardware's. If it matches
   after S1+S3, close #331; if it matches after S1 alone, close early and
   record which term paid for it.

### 5.3 Checkered Flag (overshoot canary)

vjtrace watch on `$43B0` during a scripted race at each stage boundary.
Budget 0 throughout = pass. Budget 1 in heavy scenes = investigate with
reporter/BigPEmu before judging. Budget 2 anywhere ordinary = overcharge,
stage fails.

### 5.4 Corpus blast-radius sweep

`fb_ab_sweep.sh` (A/B framebuffer, private corpus) per S3/S4/S5: every
title's frame-hash stream A/B'd old-vs-new. Expected-to-change list is
declared *before* the sweep (render-bound titles: Doom, AvP, Cybermorph,
Wolf3D, Battle Morph, Hover Strike…); any title outside the list that
changes pace fails the stage until explained. DEMO1B/DEMO1C trap rules from
the fb_ab tooling notes apply. CD titles additionally get a
`cd_boot_matrix.sh` chunk (build-id-stamped rows). Audio-sensitive stages
(S1, S3) add the clipping+presence pair on the known-broken-history titles
(Skyhammer, IS2, Raiden).

---

## 6. HC granularity (maintainer scope item, from the #438 lightgun design)

**The discrepancy:** `TOMExecHalfline()` sets HC to one coarse value per
halfline (`tom.c:~1282-1295`); real silicon's horizontal counter advances
every video clock, and MiSTer hit-tests a live per-clock beam. The `$F00004`
read path additionally synthesizes an advancing phase (`tomHCReadPhase`
increments per read, since #119) so polling software sees motion that
elapsed time did not produce.

**6.1 Does it matter beyond the lightgun? — No, with evidence.**

* **No commercial consumer:** the #438 corpus scan found **zero** readers of
  `$F00004`/`$F00006` — games use LPH/LPV, not raw HC/VC
  (`lightgun-design.md` §2.1, corroborated by the Balloons disassembly:
  3 LPH + 3 LPV literals, 0 HC/VC). Doom never reads HC.
* **Not load-bearing for the pacing gap:** the 2.00 → 4.00 mechanism (§2)
  is aggregate wall time per flip. The contention model (S3) needs the
  OP's occupancy *fraction* of each halfline — which
  `bus_arbiter_op_charge()`/`op_clk_accum` already accumulate — not the
  beam's instantaneous position. Sub-halfline placement redistributes who
  waits when within a line; it does not change the per-field occupancy sums
  that set the pace. First-order pacing is invariant to it.
* **Not load-bearing for pipeline hazards:** the scoreboard stalls (spec
  §3–§6) are functions of the instruction stream, not of beam position.
* The only in-core consumer of HC semantics beyond the read path is the
  OP's `CONDITION_SECOND_HALF_LINE` branch, which tests bit 10 only —
  already exact at halfline granularity.

Verdict: **a fidelity gap that is inert for everything in this campaign's
scope; it is a #438-local concern** (the lightgun design already works
around it by synthesizing LPH/LPV, which is the correct electrical model —
the TTL edge latches whatever HC/VC read, and #438 controls what they read).

**6.2 Cost if/when it is done.** A per-clock ticking counter is the wrong
implementation for an event-driven core and is not needed: HC can be
**derived on read** from sysclks-elapsed-in-current-halfline, which the
slice accounting already knows — O(1) per read, no per-tick work, ~zero
performance cost. Blast radius: the `$F00004` read path (retiring the
`tomHCReadPhase` synthetic phase, one savestate field), the halfline
updater, and whatever relied on the synthetic phase (acid suite is the
suspect — it must be checked before the hack is removed). As a TOM
timing-model change it inherits the campaign gates (acid suite, fb_ab
corpus; `dram_scale_sweep` trivially since no charge changes). Estimate:
**1–2 days, inside #438**, only when a consumer (lightgun latch accuracy)
demands it.

**6.3 Toggle recommendation: no shipped option.** #408's end state is one
correct system with no toggles; a shipped "HC granularity" option would
contradict the epic for a behaviour with zero known commercial consumers.
If experimentation is wanted before committing, use a **dev-only env knob**
(`VJ_HC_DERIVED=1`, the `VJ_DRAM_SCALE` precedent: compiled in, not a core
option, invisible to users) and delete it when the derived path is proven.
Preference order: **neither until #438 needs it > dev env knob during that
work > shipped option (never)**.

---

## 7. Go/no-go recommendation

**GO — with a decision gate after S2, and one cheap early win first.**

* **Smallest user-visible improvement:** S1 + the #331 re-verify (§5.2). If
  Battle Morph paces correctly with the completed #313 model, the first
  user-visible fix of the campaign ships in ~1 week, delivered per-title via
  the existing titledb mechanism (`virtualjaguar_gpu_pipeline_timing` row —
  user-set values already win) without waiting for the S5 default flip.
  Doom does *not* get a titledb throttle in the meantime: BigPEmu's
  throttle-script precedent is noted, but the project rule is substantive
  fixes over per-title tweaks, and the S2 gate is only ~2 weeks out.
* **The honest total for "Doom paces like hardware": ~26 focused days /
  5–6 weeks**, dominated by S3 (contention model) and its gating. The number
  to hit is now measured (15.01 flips/s), the residual's best-evidenced
  owner (bus contention) has a plausibility calculation that covers the gap
  (§2.3 H-A), and the wall-time domain argument (§2.4) removes the
  overclock-cancellation objection that blocked every previous default-flip
  attempt.
* **The no-go branch, defined in advance:** if S2's Verilator-pinned
  latency function times the measured traffic mix cannot supply ≥ 1.0
  field/flip for Doom, then no evidence-backed model reaches 15.01 and we
  stop paying for archaeology: ship S1 (it is correctness regardless),
  keep the S0 gate, and open a separate, honestly-labelled discussion about
  a per-title pacing mechanism (the BigPEmu approach) versus living with
  2.00 until better ground truth (a 60+ fps hardware capture) exists.
  What we do **not** do in that branch: guess a contention scale factor to
  hit the number — that is unfalsifiable curve-fitting and the census
  already rejected it.

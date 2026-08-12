# Doom render-cost census (#401)

Where Jaguar Doom's per-frame time actually goes in this core, measured
component by component. Written to close out a #401 attempt that set out to
"make the GPU cost model accurate enough that render time lands in the right
band" and found that **the GPU cost model is already at its ground-truth
ceiling, and the render is not where the missing time is.**

No emulator behaviour was changed to produce this document. Everything below
is measurement against `develop` @ `adb5ed4` plus throwaway instrumentation
that was reverted.

Companion documents:

* [`doom-pace-calibration.md`](doom-pace-calibration.md) — the game side
  (`MiniLoop`, `vblsinframe`, the two `I_Update` copies and their gates).
* [`gpu-timing-spec.md`](gpu-timing-spec.md) — the Flare-netlist timing rules
  the GPU model implements; §14 holds the Verilator-pinned constants used as
  the ceiling below.

---

## 0. Method and units

Title: `Doom - Evil Unleashed (1994).jag`, attract demo, NTSC, headless
harness, windows over frames 900-1500 (the demo's steady state; the first
~600 fields are title/idle and are excluded).

One NTSC field = **442,717 GPU cycles**. Doom's 3D view presents once per
**2 fields** — the GPU-side `I_Update` in `r_phase9.gas` gates on
`ticcount - lastticcount >= 2`. So a *rendered frame* ("flip") is 885,434 GPU
cycles of wall budget, and every "fields" figure below is
`cycles / 442,717`.

Two instruments:

* **GPU PC census** — the `BENCH_PROFILE` halfline-rate PC sampler
  (`gpuPCSample`, 524 samples/field, 628,800 samples/run) read by
  `test/tools/m68k_pc_histogram` with `PC_GPU=1`. Sampling is uniform in
  time, so bucket share *is* time share.
* **Work-cycle counter** — a temporary counter in `GPUExec` accumulating
  `opcode_cycles + gpu_bus_stall + gpu_pipe_core_stall` split by whether the
  PC was inside one of Doom's three spin loops (below). This is the
  "GPU busy-time per rendered frame" instrument #401 asks for; the raw
  opcode count is useless because the GPU is **budget-saturated in every
  configuration** (442,717 ops/field with all timing off — it never idles, it
  spins).

---

## 1. The GPU spends ~75% of every field in three spin loops

Disassembled from GPU local RAM at runtime (`test/tools/gpu_disasm_dump`).

| Region | Share | What it is |
|---|---:|---|
| `$F0308A-$F0309E` | 26.6% | **Idle mailbox poll.** `store #1,($F0304C)` / `load ($F03048),r1` / `or r1,r1` / `jr Z` — GPU advertising "idle" and waiting for the 68K to post a job. Both addresses are GPU **local** RAM. |
| `$F03672-$F0369A` | 34.5% | **Display-list adopt handshake.** `[$40634] = [$47DBC]`, then spin until `[$40634] == [$21FD0]` — waiting for the 68K video ISR to adopt the new list. Main-RAM external loads. |
| `$F0362C-$F03658` | 13.8% | **The `r_phase9` 2-field gate.** `delta = [ticcount] - [lastticcount]`, `moveq #2`, `cmp`, loop while `delta < 2`. This is Doom's own floor. |
| `$F03000` | 4.2% | GPU interrupt vector. |
| `$F030C4-$F030D4` | 1.6% | Block-copy loop — real work. |
| everything else | **19.3%** | The actual renderer. |

> **Correction to #401's problem statement.** The issue names
> `$F0308A-$F0309E` as "the texel inner loop". It is not: it is the
> local-RAM idle-mailbox poll above. Optimising or re-pricing that address
> range changes nothing but spin iteration count.

Because two of the three loops are the GPU *waiting on the 68K*, and the
third is the game's own field gate, **making instructions more expensive
mostly buys fewer spin iterations, not a later flip** — until render work
alone crosses 2 fields. This is why present rate and tic rate are insensitive
instruments below that threshold, and why halving the RISC clock moved the
tic rate by 3%.

---

## 2. Render cost per rendered frame, by configuration

`work` = cycles executed with the PC outside the three spin windows.

| Configuration | work cyc/flip | work fields/flip | work ops/flip | GPU ext acc/flip | fields/flip |
|---|---:|---:|---:|---:|---:|
| all timing OFF (default) | 200k-274k | **0.45-0.62** | 200k-274k | 0 | 2.00 |
| `gpu_pipeline_timing` | 407k-500k | **0.92-1.13** | 167k-206k | 10.3k-11.7k | 2.00-2.22 |
| `blitter_timing` + `dram_timing` | 553k-644k | **1.25-1.45** | 223k-283k | 0 | 2.00-2.04 |
| all three | 780k-974k | **1.76-2.20** | 228k-299k | 14k-20k | 2.19-2.52 |

The renderer is ~**250k GPU instructions per rendered frame** and that count
is configuration-independent (as it must be — it is set by the scene).

---

## 3. The GPU model is already at the netlist ceiling

Apply `gpu-timing-spec.md` §14's Verilator-pinned rules to the measured
instruction mix (250k instructions, ~10-20k external accesses, ~13k local-RAM
ops per flip):

| Term | Rule | Cost/flip |
|---|---|---:|
| Register-writing instructions | §14: write-back port conflict retires a disjoint-register stream at **2.00 ticks/instr** | ~500k = 1.13 field |
| External accesses | §14: `L = 7 + D`; model charges issue 2 + grant 2 + DRAM 5 + return 3 = **12**, exactly the page-miss value | 124-240k if fully exposed, mostly concealed — ~16 instructions separate consecutive accesses, and 16 instructions at 2 ticks already exceeds 12 |
| Local-RAM loads | §5: 3-tick latency, non-blocking, engine accepts one op per 2 ticks | largely concealed |
| **Ceiling** | | **~1.1-1.3 fields** |

Measured with `gpu_pipeline_timing` on: **0.92-1.13 fields**. The model is
within ~15% of the ceiling its own ground truth permits.

External pricing was verified directly rather than assumed: mean
`bus_arbiter_charge_access` cost over 24,132 GPU external accesses per field
is **5.05 sysclks** (the DRAM page-miss average), the `cost == 0 →
GPU_PIPE_IO_CLKS` fallback fires **4 times per field out of 24,132**, and
only **226 of 24,132** accesses are cart ROM — Doom's GPU render is
essentially all main DRAM, so ROM wait states are not a hidden lever either.

**For the render alone to pace the loop at 4 fields/flip it would have to
reach 1.77M ticks over 250k instructions — about 7 ticks per instruction,
3.5x beyond what the netlist permits.**

### The counterfactual confirms the lever is real but out of reach

Multiplying *only* non-spin instruction cost (spins excluded, so the floor
cannot absorb it):

| Render cost multiplier | work fields/flip | fields/flip |
|---:|---:|---:|
| x1 | 0.45-0.62 | 2.00 |
| x2 | 0.86-1.24 | 1.99-2.14 |
| x4 | 2.05-2.80 | **2.70-3.33** |

So render cost *does* drive present rate once it crosses the 2-field floor,
exactly as #401 predicted — but the multiplier needed (~x6-x8 over stock,
~x3 over the netlist-accurate model) has no hardware justification.

---

## 4. The other components, priced

| Component | Cost per rendered frame | Source |
|---|---:|---|
| GPU render, netlist-accurate | 0.9-1.3 field | §2/§3 above — measured |
| Blitter | **0.34 field** | `blitter_budget_probe` — measured, but see the configuration seam in §4.1 |
| 68K | ~1 field of non-spin work | **ESTIMATE** — sample-share only, §4.2 |
| Object Processor | field-locked by construction | — |
| **Serialized upper bound** | **~2.3-2.5 fields** (68K term estimated) | |

Only two of the three non-zero rows are measured cycle counts. The 68K row is
a share-of-samples estimate, so the total is a shape, not a figure — it
supports "no single component is an order of magnitude cheap" and does **not**
support any precise claim about the size of the residual.

### 4.1 Blitter — Doom is *not* blitter-free

`blitter_budget_probe` on the accurate blitter: **~400 blits/field, 42-45k
pixels/field, 31-33k phrases/field → 74k sysclks = 16.7% of a field**
(peak 74.8%). Per rendered frame that is ~0.34 field. Real, but not the
missing time.

> **Configuration seam — read before reusing this number.** Every other
> figure in this document was measured in the **default fast-blitter**
> configuration; this row alone had to be taken with
> `virtualjaguar_usefastblitter=disabled`, because that is the only mode with
> the perf counters (§4.1 correction below). `blitter_generic` and
> `blitter_blit` are not identical renderers — divergence between them is the
> whole premise of `test/tools/test_blitter_compare`. The blit *count* was
> corroborated across the seam: the vjtrace `--field-csv` `blit_cmd` column in
> a default fast-blitter run reads **389 blits/field**, against **~400** from
> the accurate-blitter probe. Pixel and phrase counts were **not** corroborated
> and are accurate-blitter figures. The conclusion is insensitive to this (0.34
> vs 0.4 field does not change "no single component is 10x cheap"), but do not
> quote 42-45k px/field as a fast-blitter number.

> **Correction to #401.** The issue states "Doom performs zero blitter
> traffic (renders via GPU/OP)". That reading came from a `blitter_calls`
> perf counter that reads zero under the **fast** blitter:
> `PERF_INC(blitter_calls)` exists only in `blitter_generic`/the accurate
> path, not in `blitter_blit` (`src/tom/blitter.c:1158`), and fast is the
> default. Run blitter censuses with
> `--option virtualjaguar_usefastblitter=disabled`.

### 4.2 68K

68K PC census over the same window: `$004662-$00466E` **31.3%**,
`$009C80-$009C88` **14.1%**, `$005282-$00528E` **6.9%** — three tight loops
holding **52%** of all samples, the signature of a processor spending half
its time waiting (`MiniLoop`'s `while (!I_RefreshCompleted())` and the DSP
handshake). The remaining ~48% is charged at UAE published timings with
essentially no DRAM wait states. This number is an estimate: unlike the GPU
it was not separated with a cycle-accurate work/spin counter.

---

## 5. Conclusion and the remaining gap

Aggregated, this core delivers a Doom rendered frame in **2.00 fields** with
all timing options off and **~2.2-2.5 fields** with all three on, against a
reference expectation of **3-4 fields** on hardware.

That reframes #401 substantially:

* The headline "our GPU executes Doom's render ~10x cheaper than hardware"
  is **not supported**. Priced at its netlist ceiling the render is ~1.1
  fields of a 3-4 field frame; stock it is ~0.5. The GPU error is roughly
  **2x**, and `gpu_pipeline_timing` already recovers most of it.
* No single component is an order of magnitude cheap. The residual (~1-1.5
  fields, sized off a budget whose 68K term is estimated — see §4) is spread
  across everything and does not have an identified owner.
* The best-supported candidate for the residual is **DRAM bandwidth
  contention** — the OP, blitter, refresh, GPU and 68K share one memory
  controller, and this core charges every master its *idle-bus* cost with no
  queueing (`bus_arbiter.contention_scale` defaults to 1). `gpu-timing-spec.md`
  §4 already flags "bus grant latency under load (blitter/OP running)" as the
  one remaining sim-pinned item. Calibrating it needs a Verilator run or a
  hardware capture; guessing a scale factor would be unfalsifiable.

### Is the existing option combination safe to default on?

Measured, since it is the obvious follow-on question: with
`gpu_pipeline_timing` + `blitter_timing` + `dram_timing` all enabled, Doom's
attract demo runs clean at `risc_clock_scale` 0.5x / 1x / 1.5x / 2x (every
value the option offers) — no wedge, no deadlock, so #406's knife-edge does
not reproduce on this combination at these scales.

But the gain does not survive overclocking, which is the point against
shipping it as an accuracy default:

| `risc_clock_scale` | fields/flip, all three on |
|---|---:|
| 0.5x | 2.05-2.54 |
| 1x | 2.19-2.52 |
| 1.5x | 2.01-2.17 |
| 2x | 2.00-2.08 |

At 2x the extra cost is exactly cancelled and the demo is back on the
2-field floor. A default-on accuracy model that any user can switch off by
raising an unrelated performance option is not an accuracy model. That, plus
the fact that the combination only recovers ~0.3 of the missing ~1-1.5
fields, is why this attempt did not flip the defaults.

### What NOT to try next

* Re-pricing GPU instructions. The ceiling is measured and documented above.
* Re-pricing cart ROM. 226 of 24,132 GPU external accesses per field.
* Optimising or re-pricing `$F0308A-$F0309E`. It is a spin loop.

### Load-bearing unverified input

Every "we are Nx fast" statement rests on **hardware taking 3-4 fields per
Doom frame**, which is a reference expectation, not something measured here.
The supporting game-side evidence is that Doom's demo hard-codes
`vblsinframe = 4` (`d_main.c:284`) and gameplay clamps it to 8 — the authors
budgeted for 4-8. A BigPEmu or hardware capture of `lasttics` in the attract
demo would either confirm the residual or shrink it, and is the single
highest-value next measurement.

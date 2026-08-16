# Verilator measurements — Jaguar GPU (jag_sim netlist translation)

Rig: `vsim/` — Verilator 5.050, top module `tbtop` wrapping the original
Atari/Flare **`graphics`** island (GPU + gateway + gpu_mem + gpu_ram + blitter)
as translated by Torlus in `jag_sim/verilog/tom/`. No BIOS, no full system.
Kernels are hand-assembled into `gpuram.mem` (the `$readmemh` init of the GPU's
1024x32 local RAM, `aba032a`), then G_PC ($F02110) and GPUGO in G_CTRL
($F02114) are written over the CPU-side io port.

Build/run: `cd vsim && ./build.sh && ./obj_dir/Vtbtop <mode> [args]`

---

## 0. Rig calibration (read this before trusting the numbers)

**Bit order.** `genwrap.py` mapped netlist bus index 0 — which is the **LSB** in
these Flare netlists — onto the **MSB** of each Verilator vector. Every
multi-bit port of `tbtop` is therefore the bit-reversed image of the real
value. Confirmed three independent ways:

* `bitrev16(0x2110) == 0x884`, and 0x884 was the only `ima` value (out of
  65536) whose write moves the GPU PC — i.e. G_PC at $F02110.
* the `pcbits` one-hot sweep fits `program_count == bitrev32(dwrite) − 2`
  across bits 8..30 with no exceptions.
* instruction opcodes read back correctly from `p_ins` after `bitrev16`.

Also: `ima` bit 0 (netlist `io_addr[15]`) is the 32-bit-write flag, and within a
32-bit local-RAM longword the **low** 16 bits hold the instruction at the lower
address. `p_ins` shows the *next* instruction, not the retiring one; `p_pc` is
the retiring one.

**Clock unit.** The netlist's logical clock is `clk`/`tlw` (`xpclk`, generated
by the /6 two-phase divider in `jag_sim/verilog/jaguar.v`). One `clk`-high
phase = one netlist tick = **one Jaguar system clock (26.59 MHz)**. Verified by
the advisor-suggested discriminator: a straight-line NOP kernel resident in
local RAM retires **exactly 1 instruction per tick**, with one 32-bit program
fetch every 2 ticks (`progreq`/`progack`). That is the documented 1/clk
ceiling, so tick == system clock. All numbers below are in system clocks.

**Data path validated, not just the timing.** A pointer chase proves the loaded
bits actually reach the register file: `load (r1),r2` from $00100000 returns
0xC0DE0000, and the following `load (r2),r3` puts **$DE0000** on the address
bus. (`mode verify`.)

---

## 1. External LOAD end-to-end latency, idle bus — **9 system clocks**

Kernel (`mode loadsweep 0 1`): `movei #$00100000,r1` / nops / `load (r1),r2` /
k nops / `add r2,r5`. Latency = retire tick of the dependent `add` minus retire
tick of the `load`. `gpu_back` is tied granted (bus parked on the GPU, zero
arbitration), so this is a pure idle-bus figure.

`D` = ticks the TOM memory controller holds its cycle (`ack` low). mem.v drives
`ack` as the controller's **cycle-complete/idle state** (`ack_obuf = mtb`), not
as a pulse — modelling it as a pulse hangs the GPU in `xlddwait` forever, which
is what the previous attempt's bus model was doing wrong.

| D (memory cycle, sysclks) | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| load → dependent use (sysclks) | 6 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |

Perfectly linear, slope exactly 1, for every load/use distance k = 0..5
(latency is `max(k+1, L)`, as expected). The k-independence and unit slope are
the evidence that this is a real critical path and not an artefact.

### Decomposition (as requested — do not collapse it)

```
L  =  7  +  D          (D >= 1, every valid point on the line)
```

The D = 0 column (6) is **not** a zero-latency memory and must not be quoted as
a floor: with D = 0 the modelled controller never drops `ack` at all, which is a
malformed handshake, not a fast one. A = 7 is an extrapolated intercept from
D >= 1, not an observed point — which is exactly why there is a 2-tick jump from
the D = 0 column to the D = 1 column.

* **A = 7 sysclks** is the netlist-grounded GPU-internal cost: issue →
  scoreboard `xlddwait` → gateway request → bus request/drive → data return →
  register write-back → dependent instruction retire.
* **D** comes from §8 of `GPU-TIMING-SPEC.md` (MEM.NET:150-273), not from this
  island and not from MiSTer's `mem.v` (which §13 flags as carrying GE-tagged
  `ram_rdy` edits that alter DRAM pacing).

**D = 2 is confirmed to be §8's page hit**, by walking MEM.NET rather than
assuming it. `ack_obuf = mtb`, and `mtb` is a single flop (`fd4q`, MEM.NET:333)
holding the controller's idle/"ack" state. A DRAM page hit leaves that state via
`d3a` (`ack_obuf & mreqb & fdram & match & …`, MEM.NET:359) into **3a**, then
`d3b = q3a & ram_rdy` into **3b**, then back to ack — i.e. `ack` is low for
exactly the two states 3a,3b and returns high on the third tick after mreq is
sampled. My model with D = 2 returns `ack` high on that same tick, so the two
line up with no off-by-one. (`ram_rdy` on the 3a→3b edge is the GE-tagged
MiSTer/FPGA sync §13 warns about; in silicon that edge is unconditional, still
2 states.)

Plugging in §8's **DRAM page hit = 2 sysclks**:

> ### **External LOAD, idle bus, DRAM page hit = 9 system clocks.**

Page miss (§8: +3..+7 by `dramspeed`) → **12–16**. Fast internal device read
(§8: 2) → also 9. So the spec's 6–9 bound resolves to its top end: 9.

**Confidence: high.** `L = 7 + D` is directly measured (linear, unit slope,
k-independent, data path verified), and D = 2 for a page hit is now pinned from
the MEM.NET state walk above rather than inherited on faith. The residual
uncertainty is only that the TOM memory controller itself is outside this
testbench, so D is modelled rather than simulated.

Not included, deliberately: bus-grant arbitration latency (`gpu_back` was tied
granted), 68K/blitter/OP contention, and refresh steals.

---

## 2. Instruction-fetch starvation — load-dense vs pure ALU

All kernels are 64 instructions of code resident in GPU local RAM; the window
measured is the 63 ticks between the retire of the 1st and the 64th. `mode rate`
and `mode density`.

### 2a. Sustained rate by instruction stream (`mode rate 2`)

| repeated pattern | cyc/instr |
|---|---|
| `nop` | **1.0000** |
| `add rN` (distinct dests, no RAW) | 1.9841 |
| `move rN` | 2.0000 |
| `or rN` | 1.9841 |
| `add r2,r2` (dependent chain) | 2.0000 |
| `add` / `nop` alternating | 1.4921 |
| `add r16+i,r8+i` — **no r0 anywhere** | 1.9841 |
| `add r16,r8+i` — fixed non-zero source | 1.9841 |
| `add r16+i,r8+i` / `nop` | 1.4921 |
| **`load (r1),rN` — r1 in local RAM** | **2.0000** |
| `load` local / `nop` | 1.0000 |
| `load` local / 3x `nop` | 1.0000 |
| **`load (r1),rN` — r1 external, D=2** | **6.0000** |
| `load` ext / `nop` | 2.9683 |
| `load` ext / 3x `nop` | 1.4762 |

Two separate mechanisms are visible, and they must not be conflated:

* **Every register-writing ALU instruction costs 2 ticks back-to-back**, and a
  following NOP does *not* absorb the second tick (`add`/`nop` = 1.49, i.e.
  2 + 1 ticks per pair). It is not fetch — NOPs from the same RAM sustain
  1/tick. **Controlled for r0:** the first ALU baselines all read r0, so the
  test was repeated with r0 appearing nowhere (`add r16+i,r8+i`, rotating both
  source and dest) and with a fixed non-zero source — both still 1.9841. The
  2.0 baseline is real and not an r0 artefact. The stall line asserted during
  the bubble is `wbwait`/`sbwait`, but note the *polarity* of those netlist
  nets was never independently established here, so treat "it is the write-back
  stage" as the likely mechanism rather than a proven attribution; the 2.0
  timing itself is measured and solid.
* **A local-RAM load carries no bubble of its own** — `load`/`nop` = 1.0000,
  i.e. the load costs exactly 1 tick. Its cost appears only when loads run
  back-to-back and empty the 2-deep prefetch queue. That is §9's starvation.

### 2b. The requested delta

> **Load-dense kernel (100 % local-RAM loads) vs pure-ALU kernel of the same
> instruction count: delta = 2.0000 − 1.9841 = +0.016 cyc/instr ≈ ZERO.**

A load-dense kernel is *not* slower than an ALU kernel of the same length. Both
sit at 2.0 cyc/instr, for different reasons (ALU: write-back bubble; loads:
RAM-port starvation of the prefetcher). Measured directly by `mode starve 2 0`:
126 vs 125 ticks over 63 instructions.

Against the **1 instr/tick ceiling** (the NOP stream) the same kernel costs
**+1.0 sysclk per load**.

### 2c. Density curve — where the bubble actually appears (`mode density`)

Groups of `g` back-to-back local loads followed by `m−g` NOPs, extra ticks
charged per load relative to 1/tick:

| g \ m | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| 1 load | 1.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| 2 loads | — | 1.000 | 0.500 | 0.508 | 0.516 | 0.524 |
| 3 loads | — | — | 1.000 | 0.677 | 0.688 | 0.698 |

The rule this fits exactly:

> **A run of `g` consecutive local-RAM loads costs `g − 1` extra system clocks.**
> The first load of any run is free — the 2-deep prefetch queue absorbs it, so
> one load per >=2 instructions costs nothing. Every additional back-to-back
> load steals a RAM service tick the prefetcher needed and costs +1.

This confirms §9's prediction ("dense load/store sequences intermittently empty
the queue, 1-tick bubbles") and makes it exact.

### 2d. External-memory loads, for contrast

A load-dense kernel whose loads go **external** (D=2 page hit) runs at 6.0
cyc/instr: **+4.0 cyc/instr vs the ALU kernel**, +5.0 vs the 1/tick ceiling.
This is external-bus transaction occupancy (one external load per ~6 sysclks),
not fetch starvation — and NOPs interleaved with it are free (`load` ext / 3x
`nop` = 5.9 ticks per 4-instruction group, essentially the same as a lone
external load), because they hide under the outstanding transaction.

**Confidence: high.** Both the 0-delta result and the `g − 1` rule come from
direct retire-tick counting with a validated data path, and the density curve
is internally consistent across 15 independent kernels.

---

## Reproduction

```bash
cd vsim && ./build.sh
./obj_dir/Vtbtop alu             # 1/tick calibration + ALU baseline
./obj_dir/Vtbtop verify 2        # data-path pointer chase
./obj_dir/Vtbtop loadsweep 0 1   # #1: external load latency vs memory-cycle length
./obj_dir/Vtbtop loadsweep 0 0   # local load latency (= 2 sysclks)
./obj_dir/Vtbtop rate 2          # #2a: sustained rate table
./obj_dir/Vtbtop starve 2 0      # #2b: load-dense vs ALU delta
./obj_dir/Vtbtop density         # #2c: density curve
./obj_dir/Vtbtop trace 0 1 30    # raw per-tick signal trace of an external load
```

## Side results (free, from the same runs)

* **Local-RAM load → dependent use = 2 system clocks** (`loadsweep 0 0`,
  constant across ack models — the local path never touches the bus).
* **Straight-line local-RAM code retires 1 instruction/tick**, one 32-bit pair
  fetch every 2 ticks — §9's ceiling, confirmed.
* **Every register-writing ALU instruction costs 2 ticks back-to-back**;
  interleaving a non-writing instruction does not hide it, and it is not caused
  by r0 (controlled). This is not in the spec's rule list and is worth adding —
  it also means §9's "sustained 1 instr/tick" ceiling is reachable only by
  non-register-writing streams, so real GPU code sits nearer 0.5 IPC.

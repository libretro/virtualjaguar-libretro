# Jaguar GPU (TOM RISC) Cycle-Level Timing Specification

Ground-truth timing rules for the GPU cost model in virtualjaguar-libretro
(issues #401 / #313: emulated GPU finishes work 2-4x faster than hardware).

**Primary source:** `jag_sim/netlists/tom/*.NET` — these are the ORIGINAL
Flare/Atari design source files (netlist language with the designers' own
comments, dated 1991-92, including dated bug-fix notes). They are not
reverse-engineered: they are the files the TOM silicon was compiled from.
The MiSTer core (`Jaguar_MiSTer/rtl/Rework/*.v`, e.g. `sboard.v` module
`_sboard`) and `jag_sim/verilog/tom/*.v` are 1:1 mechanical translations of
these netlists into structural Verilog (Torlus's translator), so the .NET
files are strictly more readable and equally authoritative. The DSP in JERRY
uses the same pipeline blocks (`jag_sim/netlists/jerry/SBOARD.NET`,
`EXECON.NET`, `DSP_MEM.NET`, `DSP_GATE.NET`), so every rule below applies to
the DSP as well, with "local RAM" = 8K DSP RAM and the gateway talking to
JERRY's bus interface.

All costs are in **GPU ticks = system clocks** (26.590906 MHz NTSC; the GPU,
blitter, OP and memory controller all run in this one clock domain;
the 68000 runs at sysclk/2). Evidence tags:

- `[RTL file:lines]` — read directly from the netlist source.
- `[RTL-derived]` — follows from the netlist state machines by short hand
  analysis (1-2 flops of reasoning); high confidence.
- `[JTRM prose]` — stated in the Technical Reference Manual.
- `[inference — needs simulation]` — bounded but exact count should be
  confirmed with a Verilator run of jag_sim (recipe in §9).

Source citations below are relative to the root of a checkout of
[ElectronAsh/jag_sim](https://github.com/ElectronAsh/jag_sim) (the
`netlists/tom/*.NET` and `netlists/jerry/*.NET` files are the original
Flare design sources, comments intact). Clone it anywhere to follow
along; nothing in this repository depends on its location.

---

## 1. Pipeline shape (what the model must represent)

Three overlapping stages [PREFETCH.NET:9-21, EXECON.NET:8-18, SBOARD.NET:7-27]:

1. **Prefetch** — a 32-bit queue holding **two 16-bit instructions**; program
   words are always fetched **in pairs from a long-word boundary**; a fetch
   is initiated whenever there is room for two more instructions
   [PREFETCH.NET:14-17, 227-241].
2. **Decode/execute** — one instruction is decoded through the microcode ROM
   into a latched microword and executed in **one tick** when no wait is
   asserted (`exe = vins & !wait & exec`) [EXECON.NET:80-93, INS_EXEC.NET:267-284].
3. **Write-back** — results (ALU, move, immediate, internal load, external
   load, divide) are written to the register file **in a later tick**, and the
   scoreboard *conceals* the write behind the read ports when it can; when it
   can't, it stalls execution [SBOARD.NET:414-449, 523-541].

The emulator does not need to model the structure — only the stall rules
below, which are exactly what the structure produces.

---

## 2. Baseline issue rate

**R1. Every instruction issues in 1 tick when no hazard applies.**
ALU ops (ADD/SUB/logic/shifts/SAT/MOVE/CMP/BTST...), MULT/IMULT (the 16x16
multiplier is combinational — `MP16` [ARITH.NET:103-107]), MOVEQ, MOVETA,
MOVEFA, NOP: **1 tick**. Confirms JTRM "all instructions execute in one
cycle unless noted". [RTL: EXECON.NET:80-93; ARITH.NET:103-110]
Confidence: pinned.

IMULTN/IMACN/RESMAC also issue 1/tick — the product/accumulate is pipelined
(`macop_p`, `macop_pp` [ARITH.NET:71-73]) and the sequence is made atomic
with the next instruction [INS_EXEC.NET:210-223], so a MAC chain sustains
1 tick per IMACN. Confidence: pinned.

---

## 3. Scoreboard interlocks (the missing 30-50%)

The scoreboard compares the pending write-back address at pipe stage 1
against the source and destination fields of the **current** instruction and
suspends execution on a match [SBOARD.NET:20-26, 144-156].

**R2. ALU result-use interlock: +1 tick.**
If instruction N+1 *reads* the register written by instruction N (ALU-class
result, `reswr`), N+1 stalls exactly **1 tick** (`aludwait = alu_wback &
(dst-match & dst-read-enabled | src-match & src-read-enabled)`)
[SBOARD.NET:132-156]. The pending window is one stage deep (single
`alu_wbaddr` register), so an instruction two slots later pays nothing.
Note: the *destination* operand of a normal ALU op is also a **read** (it is
`dstrrd`), so `ADD r1,r2 ; ADD r2,r3` stalls, and so does
`ADD r1,r2 ; ADD r3,r2`. MOVE/MOVEQ write without reading dst (`dstrwr`,
`dstwen`) — `ADD r1,r2 ; MOVEQ #0,r2` does NOT match on dst-read but the
write-write is resolved by write-back priority, not a stall
[SBOARD.NET:106-130, INS_EXEC.NET:305-318]. Confidence: pinned.

**R3. Flags interlock: +1 tick.**
An instruction that *uses* the ALU flags issued immediately after one that
*loads* them stalls 1 tick (`flagwait = flag_pend & flag_depend`,
`flag_pend` = `flagld` delayed one tick) [SBOARD.NET:405-412].
`flag_depend` = conditional JUMP/JR (condition field non-zero) or ADDC/SUBC
[INS_EXEC.NET:448-457]. **The canonical `CMP ; JR cc` pair therefore costs
+1 tick.** Put one instruction between the compare and the branch to avoid
it. Confidence: pinned.

**R4. Write-back port steal: +1 tick (second-order).**
A write-back is invisible when it can go out on a register port the current
instruction is not using, or whose address matches the operand being read.
If the current instruction reads **both** ports and the pending write-back
address matches neither, or if **two** write-backs mature in the same tick,
execution stalls 1 tick (`wbwait = (!wbdeq & !wbseq & bothen & wback) +
mult_wback`) [SBOARD.NET:523-541, 414-449]. This mostly fires in
load-heavy code where an internal/external-load write-back lands while a
two-operand instruction executes. Confidence: pinned (condition), frequency
in real code needs simulation.

---

## 4. Loads and stores

Decoded `memrw` instructions request a local-bus cycle; the local bus
arbitration priority is (highest first): **I/O (68K register access) >
gateway (returning external program data) > data (load/store) > program
fetch** [GPU_MEM.NET:30-35, 121-127]. A local memory transfer is a one-tick
cycle following the arbitrated request [GPU_MEM.NET:128-143].

**R5. Internal load (GPU local RAM / GPU-visible internal regs): 3-tick
latency, non-blocking, max 1 in flight.**
"Internal loads take three ticks, but only one of these may be pending.
(This also applies to stores.)" [SBOARD.NET:177-184 — designer's words].
Timeline: T0 `exe` + `datreq`; T1 RAM cycle + `datack`; T2 data pipelined
(`mem_data` [GPU_MEM.NET:249-252]) and written back. Execution continues
underneath. Stalls only when:
- the load's **target register is read** before write-back (`ldwait` via
  `dlaeq/slaeq` while the load engine is not idle) [SBOARD.NET:236-245];
- **another load/store issues** while the engine is busy (`ldwaitt[2] =
  !idle & memrw`) [SBOARD.NET:244] — so back-to-back local loads sustain
  **1 load per 2 ticks**;
- `LOAD rX,rY ; <op using rY>` immediately: ~2 stall ticks
  [RTL-derived].
Confidence: pinned.

**R6. Indexed addressing (`(R14/R15+n)`, `(R14/R15+Rn)`) — address
precompute adds 2 ticks before the memory request.**
These opcodes set `precomp` (instructions 43,44,49,50,58-61
[INS_EXEC.NET:361-365]); the load engine walks `comp1`, `comp2` (address
through the ALU) before raising the request [SBOARD.NET:223-258, 264-271].
Indexed **local** load: request at T2, RAM cycle T3, data T4 → engine busy
~4 ticks; a dependent use right after costs ~3-4 stalls. Confidence:
RTL-derived (state machine explicit); exact overlap with next instruction —
needs simulation for the ±1.

**R7. Indexed STORE: +1 tick on the instruction itself.**
"An extra wait state is generated for computed address cycles, to read this
data" (`compdwait`) [EXECON.NET:173-189]. So `STORE rN,(R14+n)` costs 2
ticks of issue even before any bus effect. Plain STORE issues in 1 tick.
Confidence: pinned.

**R8. Local STORE: fire-and-forget, engine busy ~2 ticks.**
Store data is latched and the RAM cycle happens in the background; the next
instruction runs unless it is itself a load/store (R5 busy rule).
Confidence: pinned.

**R9. External LOAD (DRAM/cart/TOM regs): non-blocking, up to 2 in flight,
latency = local handoff (2) + bus grant + memory cycle + return (2).**
Mechanism: the local cycle "completes with no data" (`del_xld`
[GPU_MEM.NET:183-192]), the **gateway** takes over and raises a bus request,
and the scoreboard tracks the outstanding target register(s)
[SBOARD.NET:273-343, GATEWAY.NET:75-137]. Costs:
- Pipeline keeps executing. Stall only when: target register of either
  outstanding load is read (`xlddwait`) [SBOARD.NET:325-339]; a **third**
  load is issued (`twold`) ; **any load/store issues while the gateway is
  active** (`mbusywait = memrw & (gate_active + twold) ...`); or a **store
  issues while any external load is pending** (`datwe_raw & (oneld+twold)`)
  [SBOARD.NET:345-357].
- Absolute-best-case latency from `exe` to register-usable, bus already
  owned by TOM, DRAM page hit: **~7-8 ticks** — T0 exe, T1 local serv
  (del_xld), T2 gateway active + breq, T3 arbitration latch (`arben` at ack
  boundary [ARB.NET:119-124]), T4-T5 DRAM page-mode cycle
  [MEM.NET:150-165,185-197], T5 data latched (`ddatld`), T6 `xld_ready`
  (FD1Q delay [GATEWAY.NET:160-168]), T7 write-back. [inference from RTL
  chain — needs simulation for the exact constant]
- Add **bus-grant latency** if TOM does not own the bus (§7).
- Add DRAM page-miss / ROM wait states (§8).
Confidence: mechanism and stall conditions pinned; the constant
"7-8" needs simulation (bound: 6-9).

**R10. External STORE: non-blocking issue (1 tick), but the gateway stays
`gate_active` for the whole bus transaction; the *next* load/store stalls
until it completes.** Write data is enabled the tick after the bus ack
[GATEWAY.NET:253-260]. So isolated external stores are nearly free to the
GPU; a burst of external stores runs at bus speed (one per grant+cycle).
Confidence: pinned (mechanism); per-store bus cost from §7/§8.

---

## 5. Jumps

**R11. The instruction after a jump ALWAYS executes (mandatory delay slot).**
The jump is latched and only passed to the prefetcher when the next
instruction is ready, precisely so programmers can rely on the delay slot
[PREFETCH.NET:67-99 — designer's words: "To allow programmers to rely on the
instruction after jump being executed"]. Jumps are atomic with the following
instruction (no interrupt between) [INS_EXEC.NET:141-208].

**R12. Taken JUMP (Rn): ~4 ticks for {jump + delay slot} when running from
local RAM** = 1 (jump) + 1 (delay slot, useful work) + **~2 dead ticks**
while the queue is flushed (`force0` [PREFETCH.NET:129]) and the pair at the
target is fetched (request, RAM cycle+ack, queue-ready). Net penalty vs.
straight-line: **+2 ticks**. [RTL-derived from PREFETCH queue/ack chain —
needs simulation for the exact refill count (bound 2-3)]

**R13. Taken JR (relative): +1 tick over JUMP.**
The relative target is computed from the *pipelined* source one tick later
(`jrel`), and `insrdy` is masked while `jrel` is active
[PREFETCH.NET:82-99, 174-179]. Net penalty **+3 ticks**. Same confidence
as R12.

**R14. Not-taken JR/JUMP: 1 tick** (condition evaluates false, no queue
flush) — plus the R3 flags interlock (+1) if it directly follows the
flag-setting op. Confidence: pinned.

If the jump target/stream is in external memory, replace the 2-tick refill
with a full external program fetch (§7, §8; and R18).

---

## 6. Divide, MMULT, MOVEI

**R15. DIV: 1 tick issue + 16 ticks background; result usable at tick ~18.**
Non-restoring divider computes **2 bits per tick**; `div_active` lasts
**16 ticks** after `div_start` [DIVIDE.NET:7-10, 240-262]; write-back is
requested in the first tick after `div_active` drops [SBOARD.NET:386-396].
Stalls only when: the destination register is read while the divide runs
(`divdwait`), or a **second DIV issues before completion** (`diviwait`)
[SBOARD.NET:369-403]. Reading the remainder register (`G_REMAIN`) is an
internal load and should also only be done after completion. So
`DIV r1,r2 ; <17 independent instructions>` hides the divide completely;
`DIV r1,r2 ; use r2` costs ~17 stall ticks. Confidence: pinned.

**R16. MOVEI: 3 ticks minimum** (opcode + two immediate words each consumed
on `insrdy` through states `imm1`, `imm2` [EXECON.NET:118-133]); it also
drains 2 extra words from the prefetch queue, so out of local RAM with the
queue previously full it is exactly 3; when fetch-starved it is longer.
Interrupts cannot split it [INS_EXEC.NET:141-208]. Confidence: pinned.

**R17. MMULT (systolic matrix multiply): per-element pipelined memory read +
MAC; N-element column ≈ 2 ticks/element + setup; it holds the data-request
path (`mtx_mreq` ORed into `datreq` [INS_EXEC.NET:601-606]) and other memory
ops stall behind it (`mbusywait` gate) and interrupts are locked out
(`mtx_atomic`).** [SYSTOLIC.NET; INS_EXEC.NET:155-158, 234-241]
Confidence: mechanism pinned; per-element constant [inference — needs
simulation].

---

## 7. External bus: arbitration, grant latency, blitter/OP interaction

**R18. Bus priority (highest → lowest), definitive list**
[ARB.NET:85-95, matches JTRM]:

| pr | master |
|----|--------|
| 10 | daisy-chained external bus master |
| 9  | DRAM refresh |
| 8  | DSP (high priority, DMAEN) |
| 7  | **GPU (high priority, DMAEN)** |
| 6  | Blitter (high priority, BUSHI) |
| 5  | **Object Processor (video fetch)** |
| 4  | DSP (normal) |
| 3  | 68K under interrupt |
| 2  | **GPU (normal)** |
| 1  | Blitter (normal) |
| 0  | 68K (always requesting; default owner) |

**R19. Re-arbitration happens at every memory-cycle ack** (`arben = q2 &
ack` — ownership can change only when TOM holds the bus off the 68K and the
memory state machine finishes a cycle) [ARB.NET:105-124]. Consequences:

- **GPU (2) vs. blitter at normal priority (1): the GPU WINS.** A running
  blit does NOT block a GPU external access beyond the *current* memory
  cycle: the blitter requests the bus continuously for the whole blit
  (`blit_breq` active while outer loop busy [OUTER.NET:134-141]) but the
  GPU's pending request preempts it at the next ack boundary. GPU grant
  latency over a normal-priority blit ≈ remainder of the in-flight bus
  cycle (0..cycle_len-1) + 1 arbitration tick. The blit stretches by the
  GPU's cycles (this is the "GPU steals blitter cycles" direction, not the
  reverse).
- **BUSHI blits (pr 6) beat normal GPU (2):** the GPU's external access
  waits until the blit **finishes** (or until refresh preempts) unless the
  GPU sets DMAEN (pr 7 > 6), which is the documented escape hatch: FLAGS
  bit 15 gives GPU data transfers DMA priority [GATEWAY.NET:66-73,272-283].
- **OP video fetch (5) beats normal GPU (2):** during the active-display
  portion of a line, OP object fetches win every arbitration; GPU external
  accesses effectively wait for horizontal gaps. On text/HUD-heavy lines
  this is the dominant stall for GPU external traffic. [RTL: priority list;
  magnitude needs simulation/measurement]
- **68K interaction:** the 68K is the default owner. When no internal
  master held the bus, TOM must raise BR and wait for BG
  (state machine 0→1→2 [ARB.NET:33-45]); the 68K grants between its own
  bus cycles — add **~2-12 sysclks** (a 68K bus cycle is 4 CPU clocks = 8
  sysclks) for the handoff when the 68K was running. When a daisy-chained
  master releases, the bus returns to the 68K first [ARB.NET:126-134].
  [RTL + 68K datasheet; exact distribution needs simulation]

**R20. BUS_HOG (G_CTRL bit 11): after an external program fetch the GPU
holds its bus request for 4 extra ticks** so consecutive external fetches
chain without re-arbitrating [GATEWAY.NET:262-270]. Confidence: pinned.

**R21. GPU program fetch and data load may both be pending externally at
once** (gateway handles one bus transaction at a time; program returns via
`gatereq` onto the local bus, stealing one local-bus tick from the next
fetch/data cycle) [GPU_MEM.NET:16-28, 150-213]. Confidence: pinned.

---

## 8. Memory-cycle lengths (TOM memory controller)

State machine in [MEM.NET:150-273]; one state = 1 sysclk; `ack` overlaps the
idle/branch state, so charge the states between grant and ack:

| cycle | length (sysclks) |
|---|---|
| DRAM read/write, **page hit** (`match`) | **2** (states 3a,3b) per 1-4 sub-cycles depending on bus width vs transfer width |
| DRAM **page miss**: RAS precharge + row access first | + (2..4) + (1..3) = **+3..+7** by `dramspeed` (MEMCON1): speed0/1 → 4+3, speed2 → 3+2, speed3 → 2+1 [MEM.NET:167-183, 339-361] |
| CAS-before-RAS refresh | ~9 states, steals the bus at pr 9 [MEM.NET:199-215] |
| ROM (cart) | 2 + wait{2,3,5,7} by `romspeed` (MEMCON2); fastrom skips waits [MEM.NET:217-234, 428-442] |
| fast internal device (TOM/JERRY regs) | write 1, read 2 [MEM.NET:236-249] |
| external I/O device | 2 + wait{1,3,7,15} by `iospeed` [MEM.NET:251-266] |

A 64-bit-wide DRAM bank transfers 64 bits per column sub-cycle; narrower
widths run multiple sub-cycles (`MEMWIDTH` breaks the transfer up, `nextc`
per sub-cycle) — e.g. a phrase access to 16-bit cart ROM = 4 sub-cycles.
The GPU's external data accesses are ≤32-bit (`msize`), i.e. one sub-cycle
on the 64-bit DRAM. Actual `dramspeed/romspeed` values are whatever the
boot ROM programs into MEMCON1/MEMCON2 — read them from the emulated
registers rather than hardcoding. Confidence: state machine pinned; consult
MEMCON defaults at runtime.

**R22. Executing GPU code FROM external memory:** every 2 instructions cost
a full external bus transaction (grant + cycle + gateway return + 1 local
bus tick), lower priority than data loads; with BUS_HOG the transactions
chain. Order of magnitude: **6-15+ sysclks per instruction pair** vs 1 from
local RAM. [RTL-derived; needs simulation for tight numbers]

---

## 9. Fetch starvation from local RAM (why 1/tick is the *ceiling*)

The GPU local RAM (1K x 32, single port [GPU_MEM.NET:325-334]) serves
program fetch at the LOWEST local priority [GPU_MEM.NET:30-35]. A pair
fetch (2 instructions) takes a request tick + a service tick, so
straight-line code exactly keeps the 2-deep queue full: **sustained 1
instr/tick is achievable**, and every scoreboard stall gives the prefetcher
headroom. But each load/store *to local RAM* also consumes one RAM service
tick at higher priority, and 32-bit instructions (MOVEI) drain 3 words, so
dense load/store+MOVEI sequences intermittently empty the queue (1-tick
bubbles). [RTL-derived; per-pattern exact bubbles need simulation]

---

## 10. Interrupt entry

The interrupt control unit hijacks the instruction stream and injects the
call sequence (`intser` overriding the prefetch output
[INS_EXEC.NET:225-247]); it cannot break MOVEI, MMULT, IMULTN/IMACN pairs,
or jump+delay-slot (atomic terms [INS_EXEC.NET:141-223]). Entry cost = the
injected sequence (store return address, redirect) ≈ 4-6 ticks + refetch at
the vector. [inference — needs simulation]

---

## 11. Contrast: current emulator model (what to change)

`src/tom/gpu.c` today:
- `gpu_opcode_cycles[64]` is **all 1s** (gpu.c:178-188) — correct only for
  R1; DIV, MOVEI, indexed stores, jumps are all charged 1.
- `GPU_EXT_ACCESS`/`bus_arbiter_charge_access` (gpu.c:59-63) charges a flat
  external-access cost — the only non-unit cost in the model, and it
  charges it **synchronously** (blocking), whereas hardware external loads
  are split-transaction/non-blocking (R9) — but since VJ retires the load
  instantly, a blocking charge of the full latency is actually the right
  *shape* for an event-driven core; the missing part is everything else.
- **No scoreboard interlocks (R2-R4), no flags interlock (R3), no load
  latency (R5/R6), no jump costs (R12-R13), no divide latency (R15), no
  MOVEI cost (R16).**

### Minimal implementable rule set (per-opcode add-on costs, GPU ticks)

For an interpreter that retires one instruction at a time, model the
scoreboard as "last writer" state:

```
state: last_alu_dst (reg# or NONE), last_flags_set (bool),
       load_busy_until (tick), load_dst (reg#), div_busy_until (tick),
       div_dst (reg#), ext_pending (0..2) with dst regs, gate_active_until
per instruction:
  cost = 1
  if reads(last_alu_dst): cost += 1                       # R2
  if uses_flags && last_flags_set: cost += 1              # R3
  if reads(load_dst) && now < load_ready: cost += load_ready - now   # R5
  if reads(div_dst)  && now < div_ready:  cost += div_ready - now    # R15
  if is_div && now < div_done: cost += div_done - now     # R15 second div
  if is_memop && engine_busy: cost += busy_remaining      # R5/R9/R10
  if is_store && ext_pending: cost += until_ext_done      # R9
  opcode extras: MOVEI +2; indexed STORE +1; JUMP taken +2; JR taken +3;
                 (delay slot then executes normally)
  loads:  local LOAD ready at now+3 (indexed now+5); external LOAD ready at
          now + 2 + grant_latency + mem_cycle + 2  (do not block issue)
  update last_alu_dst := dst for ALU-class writes else NONE
  update last_flags_set := instruction sets flags (and not held in a wait)
```

`grant_latency` and `mem_cycle` come from the existing bus_arbiter, which
should implement the R18 priority table with re-arbitration at cycle
boundaries (R19) — notably GPU-over-blitter preemption at normal priority
and OP wins during active display.

Expected effect: typical GPU kernels (dependent ALU chains + CMP/JR loops +
loads) gain ~40-100% more cycles per instruction, which is the right order
to close the 2-4x gap of #401 without touching machine timing.

---

## 12. What could NOT be pinned by reading — Verilator recipe

Needs simulation (all bounded above, exact constants wanted):
1. Taken JUMP/JR dead-tick count from local RAM (bound 2-3 / 3-4) — R12/R13.
2. External load end-to-end constant with idle bus (bound 6-9 sysclks) and
   its distribution vs. 68K ownership — R9/R19.
3. MMULT per-element rate — R17.
4. Interrupt entry cost — §10.
5. Fetch-starvation bubbles for MOVEI/load-dense code — §9.
6. GPU grant-latency distribution while a normal blit / BUSHI blit / OP
   fetch is running — R19.

### Recipe (jag_sim, already cloned here)

- **Tool:** Verilator; harness exists at `jag_sim/verilog/verilator/`
  (`verilate.sh` builds top module **`emu`** from `jag_sim/verilog/jaguar.sv`
  with `-I../base -I../tom -I../jerry`, plus `main.cpp`, `bios.cpp`,
  `ssram.cpp`, `dram` model in `dram.cpp`; boot ROM image `verilog/os.mem`).
- **Faster targeted alternative:** verilate only the GPU island — top =
  `gpu` (from `verilog/tom/gpu.v`, translation of GPU.NET) plus `gpu_mem`,
  `gateway`; drive `clk[0]`, tie the coprocessor-bus `ack` to a scripted
  bus model. That avoids booting the whole console.
- **Loading a GPU kernel (full-chip route):** in `main.cpp`'s DRAM/68K
  path, poke the kernel into GPU RAM ($F03000+) through the 68K I/O port
  (or bypass: write the words directly into the `gpu_ram` memory array in
  the verilated model — `Vemu___024root` exposes it), write G_PC = $F03010,
  then set GPUGO in G_CTRL ($F02114 bit 0).
- **Instrument:** per-tick sample of the netlist-named signals (Verilator
  `--public` or `/*verilator public*/` on the translated nets):
  `gpu.ins_exec.execon.exe` (instruction retires), `sbwait`, `aludwait`,
  `flagwait`, `ldwait`, `xlddwait`, `mbusywait`, `wbwait`, `divdwait`
  (inside `sboard`), `progreq/progack`, `datreq/datack` (gpu_mem),
  `gpu_breq/gpu_back`, `mreq/ack` (TOM mem). Count ticks between `exe`
  pulses per kernel = ground-truth CPI; the wait lines attribute every
  bubble to a rule above.
- **Kernels to run** (assemble with any Jaguar RISC assembler, e.g. rmac):
  a) 64x dependent ADD chain (R2); b) same with interleaving (R1);
  c) CMP/JR loop (R3+R12-14); d) LOAD/use distance sweep 0..4 (R5);
  e) indexed load/store sweep (R6/R7); f) DIV then use at distance
  0/8/17 (R15); g) MOVEI burst (R16); h) external load loop with 68K
  stopped vs. spinning (R9/R19); i) kernel while a large SRCEN+DSTEN blit
  runs, BUSHI off/on (R19); j) same during active display vs. VBLANK (OP).

---

## 13. Cross-check corpus

- JTRM "Software Reference" pipeline prose agrees with R1/R2/R11/R15 (it
  documents the scoreboard, the jump delay slot, and divide 2 bits/cycle).
  Where JTRM is silent (exact stall counts, arbitration boundaries), the
  netlists above are the only written authority.
- MiSTer `rtl/Rework/{sboard,execon,prefetch,gateway,mem,arb}.v` — same
  logic, usable to confirm the translation introduced no timing edits
  (the only GE-tagged functional edits found: `mem.v` DRAM `ram_rdy`
  synchronization for the FPGA's external RAM — MEM.NET:286-288,364-369 —
  which slightly ALTERS MiSTer's DRAM pacing vs. real silicon; do not treat
  MiSTer DRAM cycle counts as authentic, use the §8 state machine).

> Raw sub-agent audit output (2026-08-22, read-only, sonnet). Line numbers refer to
> `libretro/develop` @ 5f898da. Verified items are promoted to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md);
> treat anything here that is NOT in that file as unverified.

# GPU RISC interpreter perf audit — src/tom/gpu.c (read-only)

Scope read in full: `src/tom/gpu.c` (2766 lines), plus `src/core/jaguar.c` (memory
dispatch), `src/core/jaguar.h`, `src/core/bus_arbiter.{h,c}`, `src/core/perf_iface.h`,
`src/tom/op.c` (GPUExec call sites), `libretro_core_options.h` (option defaults),
`libretro-common/include/retro_inline.h` (INLINE macro).

Context established up front: `virtualjaguar_dram_timing` (`busArbiter.enabled`) and
`virtualjaguar_gpu_pipeline_timing` (`vjs.gpuPipelineTiming`) both **default to
"disabled"** (`libretro_core_options.h:275`, `:298`-ish). So the pipeline-timing model
(`GPUPipeCheckUse`, `GPUPipeMemAccess`, lines 249-388) and the bus-arbiter DRAM charge
(`GPU_EXT_ACCESS`, lines 62-66) are **not** on the profiled hot path that produced the
24-74% GPU-time numbers in the brief — they're opt-in accuracy features. Existing
comments (line 1432-1440, issue #532) show the team already did one round of
"cache the global in a local for the exec loop" hardening for exactly this reason. Only
one gap remains in that hardening (finding 3 below).

---

## Finding 1 — GPUReadLong's external-memory fallback bypasses JaguarReadLong's fast path

**File:line:** `src/tom/gpu.c:885` (inside `GPUReadLong`, `src/tom/gpu.c:837`)

```c
return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset + 2, who);
```

**What it is:** Every GPU 32-bit LOAD that misses GPU-local RAM and GPU control RAM
(i.e. every load from main DRAM, cart ROM, TOM/JERRY registers — the common case for
texture/display-list/blitter-parameter streaming) falls through to this line, which
manually assembles a long out of **two separate `JaguarReadWord` calls**.

Compare `JaguarWriteLong`'s external path, which GPU's own `GPUWriteLong` already uses
correctly (`src/tom/gpu.c:1109`: `JaguarWriteLong(offset, data, who);` — a single call),
and compare `JaguarReadLong` itself (`src/core/jaguar.c:1122`), which has a **fast path**
for the common case:

```c
uint32_t JaguarReadLong(uint32_t offset, uint32_t who)
{
   uint32_t addr = offset & 0xFFFFFF;
   if (busArbiter.enabled && who == OP) bus_arbiter_op_charge(1);
   if (addr < 0x800000)
   {
      VJT_WATCH_RD(addr, 0, who);
      if (blitMemoRecording) BlitMemoNoteRead(addr, 4);
      return GET32(jaguarMainRAM, addr & 0x1FFFFF);      // src/core/jaguar.c:1122-1143
   }
   return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset+2, who);
}
```

`GPUReadLong`'s fallback (line 885) never calls `JaguarReadLong` at all — it reimplements
the slow tail directly. The result: a GPU LOAD from main RAM pays **two** function calls
instead of one, and each of those two `JaguarReadWord` calls independently repeats: an
`offset &= 0xFFFFFF` mask, a `VJT_WATCH_RD` check, a `blitMemoRecording` check, and its
own `if (offset < 0x800000) ... else if ...` range-check chain
(`src/core/jaguar.c:988-1013`) — i.e. the range-check chain and the two watch/memo hooks
are each paid *twice* per long, and `JaguarReadLong`'s single-comparison fast path
(`addr < 0x800000`) is skipped entirely in favor of `JaguarReadWord`'s longer chain
(main RAM / ROM / CD / E00000 mirror / TOM / JERRY / unknown) walked twice.

**Why it costs on in-order ARM:** each `JaguarReadWord` call is opaque from
`GPUReadLong`'s TU-local view once cross-TU (unless LTO devirtualizes/inlines both), so
this is 2x the call/return overhead, 2x the branch-chain traversal, 2x the global-flag
checks, on a core with no OoO to hide the extra latency. This is exercised by
`gpu_opcode_load` (`src/tom/gpu.c:2311`, `GPU_PIPE_LOAD(RM); RN = GPUReadLong(RM &
0xFFFFFFFC, GPU);`), `load_r14/r15_indexed`, `load_r14/r15_ri`, and doubly by `loadp`
(`gpu_opcode_loadp`, line 2323 area — issues two `GPUReadLong` calls for one STOREP
phrase, so the external case pays **4** `JaguarReadWord` calls where 2 `JaguarReadLong`
calls would do).

**Proposed change:** in `GPUReadLong`, replace the two-word fallback with a single call:

```c
return JaguarReadLong(offset, who);
```

This is a **pure refactor** — same final bytes for every address class, since
`JaguarReadLong`'s own fallback for non-fast-path addresses (`addr >= 0x800000`) is
*exactly* the same two-`JaguarReadWord` expression, byte for byte. No behavior change,
only removes duplicated dispatch/hook work on the common (`addr < 0x800000`) case.

**Impact:** HIGH (my best guess — LOAD is one of the most frequent GPU opcodes in
texture/blit-heavy kernels, and this doubles call overhead on that exact path; can't
quantify % without a profile run, but it's a genuine 2x-call-count-per-load regression
relative to what's already sitting one call away).

**Risk:** essentially zero — semantically identical output, does not touch flags, PC,
savestates, or timing accounting (the `GPU_PIPE_LOAD`/`GPU_EXT_ACCESS` charge happens
before this call, unaffected).

**Effort:** S. Mechanical, one-line change plus a comment. A cheap model (sonnet) can do
this safely; recommend running `test/tools/test_blitter_compare` and the general
`make TEST_EXPORTS=1 test` suite afterward (GPU load-heavy paths: texture/Cinepak/blitter
tests) since it's a hot, widely-exercised function.

**No existing issue reference found** for this specific gap — `GPUWriteLong`'s use of
`JaguarWriteLong` (the correct pattern) has no comment explaining why `GPUReadLong`
doesn't mirror it; looks like an oversight, not a deliberate asymmetry.

---

## Finding 2 — `gpu_opcode[64]` function-pointer table is dead code, keeps 64 out-of-line copies alive

**File:line:** `src/tom/gpu.c:524` (definition), used only at `src/tom/gpu.c:1509`
inside `#if 0 ... #endif` (`src/tom/gpu.c:1508-1512`, confirmed the only other reference
via `grep -rn "\bgpu_opcode\b\["`).

```c
void (*gpu_opcode[64])()=
{
    gpu_opcode_add, gpu_opcode_addc, ... // 64 entries
};
...
#if 0
      gpu_opcode[index]();
#else
       executeOpcode(index);
#endif
```

**What it is:** a 64-entry function-pointer table built from every `gpu_opcode_*`
handler, whose only consumer is compiled out (`#if 0`). Because the table takes the
address of every handler, each `gpu_opcode_*` function (all declared `INLINE static`,
i.e. a plain `inline`/`__inline__` hint per `retro_inline.h`, not `always_inline`) is
forced to keep an out-of-line, addressable definition even though `executeOpcode`'s
computed-goto dispatch (line 1583) is the only live call site and — being a single-use
`static` function called from one place per label — should otherwise be a strong
candidate for full inlining by GCC/Clang at `-O2`/`-O3`.

**Why it costs on in-order ARM:** taking a function's address doesn't prevent inlining
the call site, but it does force the compiler to also *emit* a standalone copy of every
handler (up to 64 extra function bodies, some fairly large — `gpu_opcode_mmult`,
`gpu_opcode_div`, `gpu_opcode_normi`). That's dead .text bloat that dilutes icache
locality for the actual hot loop on cache-constrained cores (32KB L1-I on Cortex-A72,
smaller/shared on A10X's efficiency-adjacent paths), and it's linked in unconditionally.

**Proposed change:** delete the `gpu_opcode[64]` table and the `#if 0` dead branch;
keep only the `executeOpcode(index)` call. (Alternatively, if some other reviewer wants
disassembly tooling later, gate the table behind that build, but nothing else in the
tree currently reads it — confirmed via `grep -rln gpu_opcode\\[`.)

**Impact:** LOW-MED (icache-pressure argument is plausible but unmeasured; the dead
table's own storage is trivially small — the win, if any, is in whether the compiler can
now fully absorb the handler bodies into the goto targets and shrink hot .text, not in
removing the table's own bytes).

**Risk:** LOW — dead code with no reachable caller; a pure deletion.

**Effort:** S, mechanical, sonnet-doable. Recompile and check binary size / confirm
`make TEST_EXPORTS=1 test` still passes (no behavior can change since the deleted path
never ran).

---

## Finding 3 — `GPU_PIPE_LOAD`/`GPU_PIPE_STORE` macros re-read the global `vjs.gpuPipelineTiming` from inside every load/store handler, missing the #532 caching

**File:line:** `src/tom/gpu.c:393-402` (macro definitions), used inside ~15 opcode
handlers (`gpu_opcode_load`, `_loadb`, `_loadw`, `_loadp`, `_load_r14/r15_indexed`,
`_load_r14/r15_ri`, `_store`, `_storeb`, `_storew`, `_storep`,
`_store_r14/r15_indexed`, `_store_r14/r15_ri`).

```c
#define GPU_PIPE_LOAD(addr) \
   do { \
      if (vjs.gpuPipelineTiming) GPUPipeMemAccess((addr), 1); \
      else GPU_EXT_ACCESS(addr); \
   } while (0)
```

**What it is:** `GPUExec` (line 1429) explicitly caches `vjs.gpuPipelineTiming` into a
local `pipeTiming` "issue #532" specifically to avoid a per-instruction global reload
across the opaque `executeOpcode` call (see the comment at line 1432-1440: *"executeOpcode()
is an opaque call, so without these locals the compiler must assume the call clobbers
both globals and reloads them on every emulated instruction"*). But that caching only
covers the two direct uses inside `GPUExec`'s own loop body (`if (pipeTiming)
GPUPipeCheckUse(index);` and the post-opcode accounting). The `GPU_PIPE_LOAD`/
`GPU_PIPE_STORE` macros, expanded inside the individual `gpu_opcode_*` handlers, read
`vjs.gpuPipelineTiming` directly — the #532 local never reaches them, so every load/store
opcode still pays a fresh global load of `vjs.gpuPipelineTiming` (and, when
`GPU_EXT_ACCESS` is taken, a second global load of `busArbiter.enabled`).

**Why it costs on in-order ARM:** same shape of cost the #532 comment already diagnosed
for the outer loop — a global field load per instruction that the compiler cannot hoist
because `vjs` (settings struct) could in principle be touched by anything reachable from
the call graph. In practice this is *one byte load* per load/store opcode (small), but
it's the exact class of redundant work #532 set out to eliminate and missed.

**Proposed change:** either (a) pass `pipeTiming` through as an explicit parameter to
every load/store handler (invasive — 15 signature changes), or (b) the cheaper option:
introduce a second file-scope variable, e.g. `static uint8_t gpu_pipeTiming_cached;` set
once at the top of `GPUExec` alongside the existing `pipeTiming` local, and have the
macros test that instead of `vjs.gpuPipelineTiming`. Since it's still a global read
either way, (b) is a wash on its own merits and only "closes the doc gap" — **I'd
recommend not spending effort here** unless profiling shows it matters; flagging mainly
because it's the one loose end of an otherwise-completed optimization pass, and a future
"finish #532" pass should know about it.

**Impact:** LOW (single byte load, well predicted, likely single-cacheline-resident).

**Risk:** LOW if touched (behavior-preserving), but not worth the diff churn for the
likely payoff.

**Effort:** S if pursued; recommend skipping.

---

## Finding 4 — `gpu_opcode_cycles[64]`, `gpu_convert_zero[32]` should be `static const`

**File:line:** `src/tom/gpu.c:512` (`uint8_t gpu_opcode_cycles[64] = {all 1s}`),
`src/tom/gpu.c:653` (`uint32_t gpu_convert_zero[32] = {32,1,2,...31}`).

**What it is:** both tables are declared with external linkage and are mutable
(non-`const`). Confirmed via `grep -rln` across the whole tree that neither symbol is
referenced from any other `.c`/`.h` file (`gpu_opcode_cycles` gets one *comment*
mention in `src/core/bus_arbiter.h:28`, not an actual reference; `gpu_convert_zero` has
zero references outside `gpu.c`). Neither is declared in `gpu.h`. `gpu_opcode_cycles` is
never written anywhere (grep confirms only reads at lines 1515, 1547, 1551, 1832,
1833, 1879, 1880) — every entry is permanently 1.

**Why it costs on in-order ARM:** because the compiler cannot prove (without whole-
program/LTO visibility) that no other TU writes these arrays, it cannot constant-fold
`gpu_opcode_cycles[index]` to the literal `1` even though that's always true, and it must
treat the array as potentially-aliased, mutable global state rather than placing it in
`.rodata` with the "never changes" guarantee that would let it fold `cycles -=
gpu_opcode_cycles[index] + ...` into `cycles -= 1 + ...` at `-O2`/`-O3`. Also relevant to
the branch-prediction/interlock table `gpu_pipe_flags`/`gpu_pipe_sets_flags` (lines
203-215, 236-240) which **are** already correctly `static const uint8_t[64]` — so the
project already knows the right pattern here; `gpu_opcode_cycles`/`gpu_convert_zero`
just weren't updated to match.

`gpu_convert_zero` is read on every `addq`/`subq`/`shrq`/`rorq`/`sharq`/indexed-load/
indexed-store instruction (the "quick" immediate forms, some of the most common GPU
opcodes in compiled Jaguar code per the JTRM's own registers score-boarding write-up).

**Proposed change:**
```c
static const uint8_t gpu_opcode_cycles[64] = { ...same 64 ones... };
static const uint32_t gpu_convert_zero[32] = { ...same values... };
```
Also fix the two call sites (`bus_arbiter.h`'s comment doesn't need a code change; it's
prose). Verify `gpu_opcode_cycles` truly has no writers before applying (grep already
done above; re-verify at patch time with `grep -n "gpu_opcode_cycles\[.*\] *="`).

**Impact:** LOW (each is a small, cheap load already; the win is constant-folding
`cycles -= gpu_opcode_cycles[index]` to `cycles -= 1`, saving one table load + add per
instruction on the accounting path, and letting `gpu_convert_zero[IMM_1]` sit
unambiguously in `.rodata`). Cumulatively low-single-digit-percent at most, but free.

**Risk:** near zero — `static const` only tightens guarantees already true in practice;
purely mechanical, no behavior change.

**Effort:** S, sonnet-doable directly.

---

## Finding 5 — `branch_condition_table` is a heap-allocated pointer, adding one indirection to every conditional JUMP/JR

**File:line:** `src/tom/gpu.c:656` (`uint8_t * branch_condition_table = 0;`),
`src/tom/gpu.c:657` (`#define BRANCH_CONDITION(x) branch_condition_table[(x) +
((jaguar_flags & 7) << 5)]`), built once by `build_branch_condition_table()`
(`src/tom/gpu.c:751`, `malloc(32*8)`), used in `gpu_opcode_jump` (line 1794) and
`gpu_opcode_jr` (line 1845) — i.e. on every taken-or-not conditional branch, one of the
hottest opcode classes in compiled GPU code (loop back-edges, texture-loop counters).

**What it is:** the 256-byte table is `malloc`'d once at startup and accessed through a
global pointer, so `BRANCH_CONDITION(x)` costs a **pointer load + index** rather than a
direct array index. It's shared with `src/jerry/dsp.c` (confirmed via grep — DSP defines
its own copy of the macro against the same global), which is presumably why it's a
pointer rather than a fixed array: whichever of GPU/DSP initializes first calls
`build_branch_condition_table()` and both then read the same 256 bytes.

**Why it costs on in-order ARM:** one extra load-use dependency (load the pointer, then
load through it) versus a `static const uint8_t table[256]` baked into `.rodata` at
compile time and indexed directly, on the single most latency-sensitive part of a
mispredict-prone branch-condition computation.

**Proposed change:** since the table's 256 entries are a pure function of `i`/`j` (see
the nested loop at lines 762-773) and don't depend on any runtime config, it can be
generated at compile time (a static `constexpr`-style array literal, computed offline
once and pasted in, or built via X-macro) and declared
`static const uint8_t branch_condition_table[256]` directly, eliminating both the
`malloc`, the null-check-free-at-shutdown bookkeeping, and the pointer indirection.
Because it's shared with `dsp.c`, this would need to move to a shared header (or be
duplicated — 256 bytes is cheap to duplicate per-file if sharing the header is
inconvenient) and touches two files, so it's a slightly larger change than the other
findings here.

**Impact:** MED (my best guess — every taken/not-taken branch pays this, and branches
are extremely common in RISC loop bodies; the *removed* indirection is small in absolute
cycles but the site is hot).

**Risk:** LOW-MED — must verify the generated table is byte-identical to
`build_branch_condition_table()`'s output (straightforward to check with a one-off test
harness dump, or by keeping the runtime builder as a `#ifdef`-gated fallback/assertion
during development) and that DSP's copy/reference is updated consistently so the two
interpreters can't desync. Needs care, not a blind mechanical edit — recommend sonnet
draft + a human/careful-model check of the generated table against
`build_branch_condition_table()`'s current output before landing.

**Effort:** M.

---

## Finding 6 — `gpu_opcode_div` is a 32-iteration bit-serial loop per DIV instruction

**File:line:** `src/tom/gpu.c:2558` (`gpu_opcode_div`), loop at roughly line 2570-2580
(`for(i=0; i<32; i++) { ... }` — restoring-division bit-serial algorithm, comment credits
"SCPCD").

**What it is:** every GPU `DIV` opcode runs an explicit 32-iteration loop, each iteration
a data-dependent shift+conditional-add+shift chain (`r = (r<<1)|...; r += (sign?RM:-RM);
q = (q<<1)|...`). This is a serial dependency chain with no cross-iteration ILP — on an
in-order core each iteration is latency-bound, not throughput-bound.

**Why it costs on in-order ARM:** ~32 sequential shift/add/compare steps versus a native
32-bit hardware divide (`UDIV` on ARMv7-A/ARMv8, or an emulated `/`/`%` the compiler
lowers efficiently) is a real, measurable per-instruction cost multiplier specifically on
cores without deep OoO to overlap the chain — this is exactly the profile of the
in-order/low-IPC hosts named in the brief (A10X, Cortex-A72).

**Why I am NOT proposing a rewrite:** the comment ("Real algorithm, courtesy of SCPCD:
NYAN!") and the 16.16-fixed-point pretreatment (`if (gpu_div_control & 0x01) q <<= 16, r
= RN >> 16;`) strongly suggest this bit-serial form was chosen to match real hardware's
extended-precision (48-bit effective dividend in the 16.16 case) and possibly its
specific overflow/edge-case behavior bit-for-bit, which a native `/`/`%` would not
necessarily reproduce (divide-by-zero, quotient overflow, the exact remainder on
overflow). Given the repo's stated priority on hardware accuracy over cleverness (see
CLAUDE.md's hardware-accuracy rules) and that I have not verified against JTRM/silicon
whether the two paths are provably equivalent, I'm flagging this as a candidate, not a
recommendation.

**Impact:** MED, *if* DIV is common in profiled titles (perspective texture mapping /
division-heavy render kernels use it); LOW if DIV is rare in practice. Can't tell without
a per-opcode instruction histogram, which is outside this audit's read-only scope.

**Risk:** HIGH — touches emulation correctness/hardware-accuracy directly; per this
repo's CLAUDE.md rule, would need JTRM PDF verification (not source comments) before any
change, and must not regress the acid-test suite's DIV coverage if any exists.

**Effort:** M if attempted, and should NOT be done mechanically — needs a careful/senior
pass with hardware-doc verification, not a cheap-model mechanical edit. Recommend: only
pursue after profiling confirms DIV is actually hot in a real title; otherwise leave
alone.

---

## Investigated, not recommended (already correct or unsafe to touch)

- **`gpu_reg` double indirection (RM/RN macros, `#define RM gpu_reg[gpu_opcode_first_parameter]`,
  line ~597-ish).** `gpu_reg` is a `static uint32_t *` pointing at bank 0 or bank 1,
  re-pointed by `GPUUpdateRegisterBanks()`. I checked both call sites
  (`src/tom/gpu.c:984`, inside `GPUWriteLong`'s `case 0x00` — i.e. a **GPU STORE to its
  own G_FLAGS register**, which happens routinely in ISR epilogues toggling
  IMASK/REGPAGE — and `src/tom/gpu.c:1180`, inside `GPUHandleIRQs`). Because a GPU STORE
  instruction can flip the bank *mid-slice*, `gpu_reg` is **not** loop-invariant across
  `GPUExec`'s instruction loop, so caching the pointer in a local across multiple
  opcodes (the obvious "fix") is unsafe without an invalidation scheme tied to every
  STORE-to-G_FLAGS. Not a good mechanical target; flagging as understood-and-rejected
  rather than silent.

- **Z/N/C flags as three separate byte globals** (`static uint8_t gpu_flag_z, gpu_flag_n,
  gpu_flag_c;`, line ~586). There's an explicit in-code rationale at
  `src/tom/gpu.c:582-585`: *"There is a distinct advantage to having these separated
  out--there's no need to clear a bit before writing a result."* This is a deliberate,
  documented design choice (RMW avoidance), not an oversight. I don't recommend packing
  them into one flags word — that would reintroduce the clear-before-write cost the
  comment explains was avoided, and three adjacent `uint8_t` globals likely already share
  a cache line in practice (declared consecutively). Not flagging as a finding.

- **`GPUHandleIRQs` cost when idle.** Called once per `GPUExec` invocation (not
  per-instruction — confirmed by reading the full exec loop, lines 1429-1573: the call is
  at line 1471, before the `while` loop, with no per-instruction re-check). Early-exits in
  2-3 cheap comparisons (`!GPU_RUNNING`, `gpu_flags & IMASK`, `bits &= mask; if (!bits)
  return;`) when nothing is pending. Not a hot-path concern.

- **`VJP_ENTER`/`VJP_LEAVE` (perf_iface.h).** Per-`GPUExec`-call (slice granularity), not
  per-instruction. The header's own design doc (`src/core/perf_iface.h:1-40`) explicitly
  addresses this: "Every probe here brackets a SLICE ... never an interpreter inner
  loop." Confirmed by reading the actual placement — correct as designed, not a finding.

- **`VJT_PCHIST_GPU`, `crash_detect` hooks.** `VJT_PCHIST_GPU(gpu_pc)` (line 1483) is
  inside `#ifdef VJ_TRACE`, compiled out in release/shipped builds. No `CrashDetect*`
  call exists inside the instruction loop at all (only at `GPUWriteLong`'s G_PC/G_CTRL
  write handlers, i.e. on GPU start/stop transitions, not per instruction). Not a
  per-instruction cost in release builds.

- **`GPU_PIPE_*` pipeline-timing model and `GPU_EXT_ACCESS`/dram-timing.** Both gated by
  core options that **default to disabled** (`libretro_core_options.h`). Zero cost in the
  default configuration beyond the two cheap global-bool checks addressed in Finding 3.
  Not the source of the profiled 24-74% GPU-time hotspot under default settings.

- **`executeOpcode`'s computed-goto dispatch.** Already computed-goto per the task brief;
  confirmed unchanged and correctly structured (lines 1575-1730). GCC-only path with a
  `switch` fallback for MSVC (lines 1730+). No further dispatch-level optimization
  identified.

- **`sqtable[32]` inside `gpu_opcode_cmpq`** (`src/tom/gpu.c:1966`) is a function-local
  `static int32_t` array, never written — should technically be `static const int32_t`
  too, but as a function-local static its initializer is already resolved at load time
  with no per-call cost; this is a style nit, not a performance finding. Mentioned for
  completeness, not worth a separate action.

- **GPU-object OP release-guard loop** (`src/tom/op.c:674-690`,
  `OP_GPU_RELEASE_GUARD_CYCLES=4000`, `OP_GPU_RELEASE_STEP_CYCLES=16`): repeatedly calls
  `GPUExec(16)` in a polling loop when the OP hands a GPU-interrupt object to the GPU,
  paying `GPUExec`'s fixed per-call overhead (VJP_ENTER/LEAVE, `GPUHandleIRQs`,
  `vjs.gpuPipelineTiming`/`riscClockScalePct` reads) once per 16 GPU cycles of real work
  in the worst case. This is title-specific (games that route objects through the GPU —
  Primal Rage, Val d'Isere per project memory) rather than a universal hotspot, and the
  fine polling granularity looks intentional (it's how `op_obf_written` timing is modeled
  accurately — the loop normally exits after the first iteration or two once the GPU's
  ISR responds, so the 4000/16=250 worst case is rare). Flagging as understood, not
  proposing a change without evidence a real title hits the worst case.

- **`JaguarWriteLong`'s per-call CDDA-DIAG debug comparison** (`src/core/jaguar.c:1163`,
  `if (addr == 0xF1B274 && data != 0) LOG_DBG(...)`) — a single cheap, well-predicted
  compare paid on every long write (68K, GPU, DSP alike), explicitly marked in its own
  comment as leftover debug instrumentation to "remove... when resolved" (Primal Rage
  CDDA investigation). Out of gpu.c's scope (shared file, not GPU-specific), LOW impact,
  and already self-documented as scheduled for removal — not raising as a new finding,
  just noting it's there and already tracked in spirit.

---

## Summary table

| # | Finding | Impact | Risk | Effort | Model |
|---|---|---|---|---|---|
| 1 | `GPUReadLong` external fallback → use `JaguarReadLong` | HIGH | ~none | S | sonnet |
| 2 | Delete dead `gpu_opcode[64]` table + `#if 0` block | LOW-MED | LOW | S | sonnet |
| 3 | `GPU_PIPE_LOAD/STORE` re-reads `vjs.gpuPipelineTiming` | LOW | LOW | S | skip (not worth it) |
| 4 | `gpu_opcode_cycles`/`gpu_convert_zero` → `static const` | LOW | ~none | S | sonnet |
| 5 | `branch_condition_table` malloc'd pointer → static const array | MED | LOW-MED | M | careful/verify |
| 6 | `gpu_opcode_div` 32-iter bit-serial loop | MED (unverified) | HIGH | M | senior + JTRM check |

Recommended order if pursuing: **1 → 4 → 2** (cheap, safe, mechanical, do all three
together and re-run `make TEST_EXPORTS=1 test` + `test/tools/test_blitter_compare`), then
consider profiling to see if 5 or 6 are worth the larger effort — I would not spend
effort on 5/6 without an instruction-frequency profile confirming JUMP/JR/DIV density in
the titles that show the worst A10X/Pi4 numbers.

> Raw sub-agent audit output (2026-08-22, read-only, sonnet). Line numbers refer to
> `libretro/develop` @ 5f898da. Verified items are promoted to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md);
> treat anything here that is NOT in that file as unverified.

# DSP subsystem perf audit — src/jerry/dsp.c, dac.c, jerry.c

Read-only audit. No files modified. Repo:
/Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro/.claude/worktrees/alien-predator-save-state-ad621b

Baseline confirmed from code (not assumed): dispatch is computed-goto
(`dsp_executeOpcode`, dsp.c:1165-1264, `goto *dsp_dispatch[index]`), delay slots for
JUMP/JR are inlined into `dsp_opcode_jump`/`dsp_opcode_jr` (dsp.c:1353-1430, comment
explicitly says this replaced a recursive `DSPExec(1)` call). Do not re-propose either.

---

## Finding 1 — SH/SHA implemented as a 32-iteration bit-shift loop; hardware (and our own
timing model) charges 1 cycle

**File/line:** `dsp_opcode_sh` dsp.c:2039-2073, `dsp_opcode_sha` dsp.c:1999-2030.

**What:** Both instructions shift by a *variable* amount (`RM`, sign gives direction,
magnitude clamped to 32) via a `while (shift) { _Rn <<= 1; shift--; }` /
`_Rn = (int32_t)_Rn >> 1` loop — up to 32 individual single-bit shifts per instruction.
Contrast with `dsp_opcode_shlq`/`shrq`/`sharq` (dsp.c:1976-1998, 2031-2038), the
*immediate*-shift variants, which already do a single native C `<<`/`>>` with no loop.

**Why it costs on in-order ARM:** `dsp_opcode_cycles[]` (dsp.c:226-235, index 23=SH,
26=SHA) charges these opcodes **1 cycle** — the same as ADD — because the real DSP has a
barrel shifter (arbitrary shift in one cycle). Our interpreter instead burns up to 32
host loop iterations (branch + shift + decrement each) to compute what a single native
shift instruction gives for free. On an in-order core with no branch-heavy
loop-unrolling wins, this is pure waste: emulated-cycle cost is already flat regardless
of shift amount, so making the host implementation flat too is free correctness-neutral
speedup. SH/SHA are common in audio-mixing/sample-scaling code, which is exactly the
class of DSP program the cited titles run.

**Proposed change:** replace both loops with clamped native shifts. For the
left-shift branch (`sRm<0`): `_Rn = (shift >= 32) ? 0 : (_Rn << shift);` (shift==32 must
stay special-cased — native `<<32` on a 32-bit type is UB). For the arithmetic
right-shift branch: clamp `shift` to 31 instead of 32 (after 31 iterations of `>>1` on a
32-bit signed value the result is already fully sign-extended, so 31 and 32 iterations
produce an identical result) and do `_Rn = (int32_t)_Rn >> shift;` — this sidesteps the
shift-by-32 UB case entirely while being bit-identical to the existing loop's output.
Carry-flag computation is already taken from the *pre-shift* value in both functions
(`dsp_flag_c=(_Rn&0x80000000)>>31` / `dsp_flag_c=_Rn&0x1`), so it is unaffected by this
change.

**Impact:** HIGH (uncertain exact %, but this is the single clearest "hardware says O(1),
we implemented O(n)" mismatch in the file, and SH/SHA are common in mixer code).
**Risk:** LOW — same numeric result, same emulated cycle cost (`dsp_opcode_cycles`
unchanged), no savestate/ABI impact. Needs the shift==32 / clamp-to-31 edge cases gotten
exactly right and a few unit vectors checked (shift=0,1,31,32,>32 both directions,
negative and positive `_Rn`).
**Effort:** S. **Suitable for sonnet** mechanically, but the edge-case proof (why 31 not
32 for the arithmetic branch, why 32 needs a branch for the logical branch) should be
verified by whoever reviews it — flag for a second pass rather than blind trust.

---

## Finding 2 — LOAD/STORE opcodes always pay a real, non-inlinable function call, even
for the (very common) DSP-local-RAM case

**File/line:** every `dsp_opcode_load*`/`store*` variant, e.g. `dsp_opcode_load`
dsp.c:1667-1673, `dsp_opcode_store` dsp.c:1626-1633, `dsp_opcode_load_r14_indexed`
dsp.c:1688-1694, `dsp_opcode_store_r14_indexed` dsp.c:1536-1543 — all call
`DSPReadLong`/`DSPWriteLong` unconditionally. Only `loadb`/`loadw`/`storeb`/`storew`
(dsp.c:1601-1660) have an inline range check before falling through to the same calls.

`DSPReadLong`/`DSPWriteLong` (dsp.c:445-527, 686-847) are declared `extern`
(non-`static`) in dsp.h:32,35 and are called from other translation units (m68k memory
map, jerry.c register glue, etc.), so **they cannot be inlined into dsp.c without
LTO**. Confirmed no LTO is enabled for `unix`, `rpi*`, `osx`, `ios-*`, `android` targets
— grep of `Makefile` shows `-flto=4 -fwhole-program -fuse-linker-plugin` only on the
`classic_armv7_a7` block (Makefile:219), gated to that one platform. The
Raspberry Pi 4 / A10X targets this audit cares about build without LTO.

**Why it costs:** Every LOAD/STORE (a large fraction of all DSP opcodes in mixer code)
pays a full ABI call: argument marshalling, a real `call`/`bl`, and — critically — the
compiler must assume the callee can touch any global, so it cannot keep `dsp_flag_z/n/c`,
`dsp_pc`, the `dsp_reg` bank pointer, etc. in registers across the call; they get
spilled before and reloaded after (see Finding 5). This compounds with the fact that the
opcode *fetch* path in `DSPExec` (dsp.c:1137-1143) and the JUMP/JR delay-slot fetch
(dsp.c:1374-1380, 1411-1417) **already** hand-inline exactly this fast path — proving the
authors know the pattern is worth avoiding for fetch, but LOAD/STORE data access never
got the same treatment.

**Proposed change (two options, pick one):**
(a) *Build-level, broad*: enable `-flto` for `unix`/`rpi*`/`osx` (at minimum `rpi*`,
since that's the stated target) the same way `classic_armv7_a7` already does, and
benchmark. Fixes this finding, Finding 5, and part of Finding 1's cousins (any other
inlinable-but-cross-TU call) all at once. Also benefits the GPU core (`src/tom/gpu.c`)
for free if it has the same shape — not audited here.
(b) *Source-level, surgical*: add a `static inline` fast-path wrapper in dsp.c, used
only by the LOAD/STORE opcode handlers, that open-codes the `offset >= DSP_WORK_RAM_BASE
&& offset <= DSP_WORK_RAM_BASE+0x1FFF` check and the `GET32/SET32` on `dsp_ram_8`
directly (mirroring dsp.c:1137-1143), falling back to the real `DSPReadLong`/
`DSPWriteLong` for everything else. **Must not** silently drop the HLE side effects that
already live inside `DSPReadWord`/`DSPReadLong`/`DSPWriteWord`/`DSPWriteLong` for
addresses inside DSP local RAM: the sound-command auto-ack at `DSP_SOUND_CMD_BASE..END`
(dsp.c:420-427, 453-489, documented "TODO(v2.3)" workaround) and the CDDA mailbox
logging/snapshot at `$F1B270-$F1B277` (dsp.c:606-635, 674-686) are both *inside* the
work-RAM range and must be replicated exactly, or reproduced by falling through to the
real function whenever the offset lands in those sub-ranges.

**Impact:** MED-HIGH for (a) (broad, touches every cross-TU call, not just this one);
MED for (b) alone. **Risk:** (a) MED — LTO is a build-wide change, larger blast radius,
needs a build-across-all-targets check and the per-target OPT_LEVEL/-mcpu discipline the
repo just finished tuning (PR #567) respected; (b) MED — correctness risk is real and
specific (the two side-effect windows above), needs careful review, not mechanical.
**Effort:** (a) S to try, M to validate across targets; (b) M.
**Not suitable for a cheap model alone** — the HLE side-effect carve-outs need a human
(or a careful high-effort agent) to get right; a blind "hoist the array access" edit
would reintroduce exactly the Wolf3D/CDDA regressions those TODOs were written to avoid.

---

## Finding 3 — No idle/spin-loop fast-forward for `jr`-to-self / tight poll loops

**File/line:** `dsp_opcode_jr` dsp.c:1397-1430; slice sizing in
`src/core/jaguar.c` `JaguarExecuteNew` (jaguar.c:1682-1750); `DSPExec` main loop
dsp.c:1070-1160.

**What exists already, so don't re-propose it:** the RISC slice handed to `DSPExec`
(`riscCycles` in jaguar.c:1712-1716/1732-1736, via `DSPBeginSlice`/`DSPSliceRemaining`)
is **already bounded to the time until the next scheduled event** (`GetTimeToNextEvent`
against `EVENT_MAIN`/`EVENT_JERRY`, jaguar.c:1688-1690) — so "how many cycles can the DSP
run before something external can change" is already a known, computed quantity every
slice. The missing piece is *inside* that already-bounded slice: nothing detects that
the DSP is burning its whole cycle budget on a 2-3 instruction poll (a `jr`-to-self or
`load`+`cmp`+`jr` loop waiting on an I2S/timer IRQ flag) instead of doing useful work.
`DSPHandleIRQsNP` (dsp.c:859-916) is only invoked when `IMASKCleared` — i.e. it's not a
per-instruction poll cost, and there's no mechanism that recognizes "this instruction
sequence has no externally-visible side effect and will keep re-executing until an IRQ
fires" and jumps straight to slice-end.

`DSPReleaseTimeslice`/`dsp_releaseTimeSlice_flag` (dsp.c:312, 340-343) look related but
are **not** an idle-skip mechanism for this: it's set from a few `DSPWriteLong` control-
register paths (dsp.c:792, 811, 824, when the 68K or DSP pokes `DSPINT0`/`CPUINT`) and
then never read anywhere except the savestate blob (dsp.c:3131, 3191) — grep confirms zero
other reads. It's effectively a **write-only dead flag** today (separate, tiny, LOW-impact
cleanup note — not a hot-path cost since it's set only on rare control writes, not per
instruction).

**What would be required for a real idle-skip:** pattern-match a short basic block
(1-4 instructions) reachable only through a backward branch, prove it has no
observable side effect other than the branch condition itself (no STORE, no register
write visible outside the loop, no MOVEQ/etc. feeding something read outside the loop),
and if so, compute how many *whole* iterations fit in the remaining slice budget and
skip straight to the last partial iteration, leaving `dsp_pc`/flags exactly as if every
iteration had run. Given the slice is already event-bounded, the "next event" the loop
is waiting on doesn't even need separate discovery — the loop is right up against the
same deadline `DSPBeginSlice` already computed.

**Impact:** Unknown but potentially the largest single lever given `dsp_opcode_jr` alone
is 12-14% of profiled DSP time — but that number is the cost of *executing* the loop body
including legitimate (non-spin) branches, not proof that most of it is idle spin; would
need instrumentation to confirm what fraction of `jr` executions are in a provably-idle
loop before investing here. **Risk:** HIGH — this is exactly the kind of change that can
silently break IRQ-timing-sensitive titles (the repo's own history: SCLK IRQ deferral
bug, DSA steal race, lost-wakeup CPUINT fix — all subtle DSP/IRQ-timing bugs from past
work per project memory) and interacts with savestates (mid-loop state must stay
faithfully resumable) and determinism. **Effort:** L. **Not suitable for a cheap model**
— this needs careful design, a written plan, and A/B validation against real titles
(Iron Soldier, AvP, Skyhammer) before it's safe to land; recommend a dedicated design doc
and prototype behind a flag, not a direct PR.

---

## Finding 4 — Global flag/register-bank state forces spill/reload around every
cross-TU call (ties Findings 1/2/5 together)

**File/line:** `dsp_flag_z`, `dsp_flag_n`, `dsp_flag_c` (dsp.c:269, all `static`),
`dsp_reg`/`dsp_alternate_reg` (dsp.c:270, `static` pointers re-pointed by
`DSPUpdateRegisterBanks`, dsp.c:843-855), `dsp_pc` (dsp.c:259, **not** static — has
external linkage per dsp.h — used by libretro-side debug/state code), `SET_ZN` macro
(dsp.c:292) writes two of the three flag globals on almost every ALU opcode.

**What:** Because `dsp_flag_z/n/c` and `dsp_reg` are plain (non-`volatile`) globals
written by nearly all ~60 opcode handlers, and because those handlers are all inlined
into one giant `dsp_executeOpcode`/`DSPExec` translation unit, the compiler *can* in
principle keep them resident in registers across many consecutive non-memory-touching
opcodes within a single `-O2`/`-O3` compile of dsp.c. That register residency breaks at
every call the compiler can't see through — which, per Finding 2, is every LOAD/STORE
opcode plus any DSP opcode that touches JERRY/TOM registers or main RAM
(`JaguarReadByte`/`JaguarWriteByte` etc., also extern, also cross-TU). So the actual cost
of this finding is not separable from Finding 2/LTO — fixing cross-TU call opacity (LTO,
or hand-inlined fast paths) is what would let the compiler stop spilling these flags.

**Impact:** folded into Finding 2's estimate; not independently actionable. **Risk/Effort:**
n/a as a standalone item — listed for completeness so it isn't mistaken for a separate,
smaller "just add `register` keywords" fix (that wouldn't help; the issue is
alias-analysis across TU boundaries, not register allocation pressure within one
function).

---

## Finding 5 — `dsp_opcode_div`: 32-iteration bit-serial loop — likely correctly modeling
real hardware cost, not a clear win

**File/line:** `dsp_opcode_div` dsp.c:1903-1932.

**What:** Restoring-division-style loop, 32 iterations of shift/add/compare per DIV
instruction, regardless of operand size. Unlike SH/SHA (Finding 1), `dsp_opcode_cycles`
(dsp.c:226-235, index 21) charges **9 cycles** for DIV — the *only* non-1/2/3/4 entry in
the whole table — strongly suggesting this reflects genuine bit-serial hardware
division latency (JTRM should be checked before touching this, per repo hardware-
accuracy rule; not done in this audit — flagging as unverified). Replacing the loop with
native `/`/`%` would only save host cycles, not emulated cycles (timing is unaffected
either way since `dsp_opcode_cycles[21]` is unchanged by the host implementation), so the
only benefit is host wall-clock, and DIV is typically far rarer in audio-mixer code than
ADD/MULT/SH.

**Impact:** LOW-MED, uncertain — worth an instruction-frequency profile before
investing (the existing per-title profiling data cited in the task didn't call out DIV
specifically, unlike `jr`). **Risk:** MED — must reproduce the exact quotient/remainder
semantics of the 16.16-mode branch (`dsp_div_control & 0x01`, dsp.c:1917-1919) and the
divide-by-zero convention (`RN=0xFFFFFFFF`, `dsp_remain=0`, dsp.c:1908-1914) bit-exactly;
a naive native `/`/`%` substitution is *not* obviously equivalent for the 16.16 fixed-
point mode without deriving it by hand. **Effort:** M. Lower priority than Finding 1.

---

## Finding 6 — DSPExec / DSPHandleIRQs overhead per call: already lean

Explicitly checked and **not** worth further work — no re-proposal needed:
- `DSPExec`'s outer loop exits immediately when DSP is halted
  (`while (cycles > 0 && DSP_RUNNING)`, dsp.c:1077, `DSP_RUNNING` = `dsp_control & 0x01`,
  dsp.c:276) — cheap early-out already exists.
- `DSPHandleIRQsNP` (dsp.c:859-916) is only called when `IMASKCleared &&
  dspFlagsRetireDelay==0` (dsp.c:1095), not once per instruction; when nothing pending it
  bails after a mask/bits computation (dsp.c:862, 880-887) — a handful of ALU ops, not a
  loop, not a hot-path concern.
- `VJT_PCHIST_DSP(dsp_pc)` (dsp.c:1088) compiles to a true no-op
  (`#define VJT_PCHIST_DSP(pc) do {} while(0)`, vjtrace.h:344) unless `VJ_TRACE` is
  defined, and `-DVJ_TRACE` is only added under `TEST_EXPORTS=1` (Makefile:161) — **never
  in a release/shipped build**. Confirmed by direct read of both the macro and the
  Makefile guard; do not re-propose gating this further.
- `VJP_ENTER`/`VJP_LEAVE` around `DSPExec` (dsp.c:1075, 1157) are guarded by a single
  runtime bool check (`if (vjPerfActive) ...`, perf_iface.h:133-134) — cheap, paid once
  per `DSPExec` *call* (not per instruction), not worth touching.
- No `crash_detect` calls anywhere in dsp.c's instruction loop (grep found only a
  comment referencing the out-of-band watchdog, dsp.c:1119) — confirmed zero per-
  instruction crash-detect cost.
- `riscClockScalePct` is read once per `DSPSyncToM68K` call (dsp.c:1052), not per
  instruction.

These were all explicitly asked about in the brief; reporting them as **already fine**
so they don't get re-investigated.

---

## Finding 7 — ~960 lines of dead code: a second, unreachable pipelined DSP interpreter

**File/line:** dsp.c:2140 (`isLoadStore[]` table) through dsp.c:3097 (`DSP_xor`), plus
the `PipelineStage`/`scoreboard`/`pipeline[4]`/`plPtr*` machinery declared at
dsp.c:66-90, and `DSPExecP`/`DSPExecP2`/`DSPExecComp` prototypes at dsp.h:41-43.

**What:** A complete second implementation of every DSP opcode (`DSP_add`, `DSP_jr`,
`DSP_sh`, `DSP_mmult`, ... ~60 functions, all `INLINE static`, all named `DSP_*` as
opposed to the live `dsp_opcode_*`), dispatched through a separate `DSPOpcode[64]`
function-pointer table (dsp.c:2186), operating on the old 4-stage pipeline/scoreboard
model (`pipeline[plPtrExec]`, `scoreboard[]`, etc.). Grepped the entire repo:
`DSPOpcode[` is referenced only at its own definition (dsp.c:2186) and twice more, both
*inside* `DSP_jr`/`DSP_jump`'s own bodies (dsp.c:2506, 2581) — i.e. it calls itself, not
called from anywhere reachable. `DSPExecP`/`DSPExecP2`/`DSPExecComp` — the presumed old
entry points for this engine — are declared in dsp.h but **have no function bodies
anywhere in the repo** (grepped, zero matches beyond the three prototype lines) and have
**zero callers** anywhere in the repo. This whole block is unreachable: not called at
build time by anything, and half of its would-be entry points don't even exist as
compiled functions.

Note: `pipeline[]`, `scoreboard[]`, `plPtrFetch/Read/Exec/Write` are still written by
`FlushDSPPipeline()` (dsp.c:2247-2258, called from dsp.c:827, 1005) and saved/loaded in
the savestate blob (dsp.c: `STATE_SAVE_BUF`/`STATE_LOAD_BUF` for `pipeline`/`scoreboard`,
near end of file) — so removing this isn't purely deletion-only; it touches the
savestate format (per project convention, any savestate layout change needs a version
bump, see project memory "Savestate version policy"). `IMASKCleared` is a real, live
field saved in the same block — don't remove that one.

**Impact:** Effectively zero runtime perf impact — dead code that's never called is
never fetched into I-cache. The value here is code hygiene / reduced binary size /
reduced maintenance surface (~30% of this file is unreachable), not frame-time. Flagging
because it was encountered while reading every `dsp_opcode_*`/`DSP_*` function per the
brief's instructions, and because someone reading `dsp_opcode_cycles[64]` next to
`isLoadStore[65]` (note the off-by-one array size difference — another sign these are
unrelated/stale) could easily mistake the dead table for load-bearing. **Risk:** LOW
functionally (pure deletion of unreachable code) but touches savestate layout (version
bump needed) — this is exactly the kind of "not really performance" item the repo's
"no bundling into perf PRs" rule (project memory) says should be its own PR, not folded
into a DSP perf change.
**Effort:** S-M (mostly deletion, but needs the savestate version-bump ceremony).
**Suitable for sonnet** as a separate, explicitly-scoped cleanup task — not this one.

---

## Finding 8 — DAC/audio (`src/jerry/dac.c`): already carefully tuned, low priority

**File/line:** `DSPSampleCallback` dac.c:256-324, `DACCaptureSample` dac.c:213-231,
`DACPrepareFrame` dac.c:332-368, `SoundCallback` dac.c:376-425.

**What:** `DSPSampleCallback` runs once per *output* sample (~800/frame NTSC, ~48000/sec
total) and does ~6 double-precision FP operations per call (linear interpolation between
two ring-buffer samples, lag/resync tracking) — genuinely O(samples), genuinely float
math, exactly what the brief asked to look for. However: (1) the volume is tiny relative
to the DSP interpreter (48k calls/sec vs. millions of RISC instructions/sec), so this is
very unlikely to be a measurable fraction of the 22-67% DSP frame-time figures cited —
those are DSP-interpreter time, not DAC-mix time, and are almost certainly counted
separately in the profiler; (2) this exact code was the subject of a recent, carefully
reasoned fix (issue #393 lag-drift, referenced in the surrounding comments at dac.c:88-
109, dac.c:279-294) — the double-precision arithmetic and resync thresholds are there on
purpose to fix an audible bug, not an oversight. `DACCaptureSample` (dac.c:213-231,
called at I2S word-strobe rate, not 48kHz) is pure integer/mask arithmetic — no finding.
`SoundCallback`'s padding loop (dac.c:411-423) only runs for the rare short-frame
hold case, not every frame.

**Impact:** LOW, and this repo's own rule (`docs/agent/testing.md`,
CLAUDE.md "Audio/DSP changes... MUST clear BOTH test_audio_clipping AND
test_audio_presence") plus the project-memory record of this exact code's recent bug
history make it a bad place to spend effort for uncertain gain. **Risk:** MED-HIGH for
any change here specifically because of that history (i2s lag drift, word-strobe
crackle — both previously-shipped regressions in this exact file per project memory).
**Recommendation:** don't touch without first getting an actual profiler sample showing
DAC/resample time as a measurable fraction of frame time; not indicated by the data in
the brief. Not scoring this one with effort/who-can-do-it since it's a "don't" finding.

---

## Summary table

| # | Finding | Impact | Risk | Effort | Model |
|---|---|---|---|---|---|
| 1 | SH/SHA bit-loop → native shift | HIGH | LOW | S | sonnet (+review) |
| 2 | LOAD/STORE cross-TU call opacity (LTO or hand-inline fast path) | MED-HIGH | MED | S(a)/M(b) | needs careful design |
| 3 | jr-spin idle-skip | unknown, possibly HIGH | HIGH | L | needs design doc, not mechanical |
| 4 | Global flag spill (subsumed by #2) | — | — | — | — |
| 5 | DIV 32-iter loop | LOW-MED, unverified | MED | M | needs JTRM check first |
| 6 | DSPExec/IRQ overhead | already lean | — | — | no action |
| 7 | ~960 lines dead pipelined-DSP code | ~0 perf, code hygiene | LOW (savestate version bump) | S-M | sonnet, separate PR |
| 8 | DAC per-sample float math | LOW | MED-HIGH | — | don't, absent profiler evidence |

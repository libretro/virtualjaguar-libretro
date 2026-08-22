> Raw sub-agent audit output (2026-08-22, read-only, sonnet). Line numbers refer to
> `libretro/develop` @ 5f898da. Verified items are promoted to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md);
> treat anything here that is NOT in that file as unverified.

# Blitter performance audit (read-only) — src/tom/blitter.c et al.

Repo: virtualjaguar-libretro, worktree alien-predator-save-state-ad621b
Scope: `blitter_generic` (fast, default engine), `BlitterMidsummer2` (accurate,
opt-in), `blitter_mmio.c`, `blit_memo.c`, `blitter_simd_*.c`, memory-access
helpers. No edits made; no build run.

## Executive summary

The fast blitter (`blitter_generic`, `src/tom/blitter.c:553-1158`) is the
default engine and the one 95%+ of games use. It has **not** received the
memory-access fast-path optimization that already exists in this file and is
used only by the accurate engine: `blitter_read_byte/word/long` and
`blitter_write_byte/word/long` (`blitter.c:60-140`, explicitly commented
"~98% of blitter memory accesses target main RAM... avoiding the full address
dispatch... for the common case"). `blitter_generic`'s `READ_PIXEL` /
`WRITE_PIXEL` / `READ_RDATA` / `WRITE_ZDATA` macros call `JaguarReadByte` /
`JaguarReadWord` / `JaguarReadLong` / `JaguarWrite*` directly — real,
non-inlined, cross-TU function calls with a multi-branch address-space
dispatch chain — for every one of the 3-6 memory ops per pixel. This is
Finding 1, and it's a mechanical, low-risk fix.

Finding 2, also mechanical: `REG(A1_FLAGS)` / `REG(A2_FLAGS)` (4 byte-array
reads + shifts each) are evaluated 30+ times in the `blitter_generic` inner
loop body via macro call sites, even though both registers are invariant for
the entire blit (nothing writes `blitter_ram` between `blitter_blit()`'s
setup and its final `WREG` after the loop). Caching them in two locals
before the outer loop and passing those locals to the macros removes all of
that redundant work.

Finding 3 (bigger, needs design): the inner loop's `if` cascade
(`DSTEN`, `DCOMPEN|BCOMPEN`, `CLIPA1`, `PATDSEL`/`ADDDSEL`/LFU, `GOURD`,
`SRCSHADE`, shadow-fb hooks...) all gate on `cmd`/mode bits that are constant
for the whole blit, but there are too many of them for the compiler to fully
loop-unswitch (2^N combinations), so most are real per-pixel branches. A
template-by-macro specialization (like the accurate engine's own "collapsed
inner loop" pattern, see Finding 7) that decodes the mode once per blit and
dispatches to a handful of specialized inner-loop bodies (copy, copy+key,
Gouraud, LFU) would remove this.

The accurate engine (`BlitterMidsummer2`) is a literal cycle-by-cycle
FDSYNC-cascade simulation of the Oberon gate netlist and is already
heavily tuned where it's been profiled: `ADDARRAY`/`ADD16SAT` are
`BLITTER_ALWAYS_INLINE`-forced and NEON/SSE2-vectorized
(`blitter_simd_neon.h`/`_sse2.c`) per the comment "ADDARRAY at 1910 samples,
... the single largest leaf in the entire emulator" (`blitter.c:1477-1479`).
Row-offset (`addrgen_ya`) caching is already done (`blitter.c:2455-2457`),
and a "COLLAPSED INNER LOOP — PATTERN FILL" fast path already collapses the
2-cycle-per-pixel state machine into 1 iteration for the pure-pattern-fill
command shape (`blitter.c:2500-2506`). This confirms the MEMORY.md note
"state-machine collapse... remaining" is half-done: the next candidate is
the plain textured SRCEN+DSTEN copy shape (no GOURD/pattern), likely AvP's
actual hot path, which still runs the full multi-cycle FSM.

None of the fast engine's SIMD helpers are used by `blitter_generic` at all
— NEON/SSE2 code only accelerates `BlitterMidsummer2`. The fast engine is
scalar top to bottom. This is expected given it's a true per-pixel scalar
loop (no 4-lane phrase batching the way the accurate engine's ADDARRAY is),
so "add SIMD to the fast path" would require restructuring to a phrase-batch
model — out of scope for a quick win.

---

## Findings

### F1 — `blitter_generic` bypasses the existing RAM fast-path helpers [HIGH / S-M / LOW-risk / mechanical]

**Where:** `src/tom/blitter.c:264-345` (`READ_PIXEL_*`, `READ_RDATA_*`,
`WRITE_PIXEL_*`, `READ_ZDATA_16`, `WRITE_ZDATA_16` macros used throughout
`blitter_generic`, `553-1158`) all call `JaguarReadByte`/`JaguarReadWord`/
`JaguarReadLong`/`JaguarWriteByte`/`JaguarWriteWord`/`JaguarWriteLong`
directly (defined `src/core/jaguar.c:954-1170`).

**What:** These are out-of-line, non-`static` functions in a different
translation unit (`jaguar.c`), so (absent LTO — see below) every call is a
real call/return, and each does: mask, `VJT_WATCH_RD/WR` check (cheap, a
global-int guard), `blitMemoRecording`/`blitMemoMode` check (cheap), then an
`if/else if` chain over 5-7 address ranges (RAM, ROM, CD, jagMemSpace, TOM,
JERRY, unknown) before reaching `jaguarMainRAM[]` for the ~98% RAM case.

Meanwhile `blitter_read_byte/word/long` and `blitter_write_byte/word/long`
(`blitter.c:60-140`) already exist, are `BLITTER_ALWAYS_INLINE`, and do
exactly "check `addr < 0x200000`, touch `jaguarMainRAM[]` directly, else
fall back to the full `Jaguar*` call" — but per
`grep -n "blitter_read_byte\|blitter_write_byte" blitter.c` (confirmed by
direct read), every call site of these helpers is inside `BlitterMidsummer2`
(lines 1469, 2629-3006, and more, all ≥2167) — **zero** call sites are inside
`blitter_generic` (553-1158).

**Why it costs on an in-order ARM core (Pi4/A10X):** a mispredicted or even
correctly-predicted indirect-free direct call still pays call/return
overhead, register spill/fill around the call (these functions clobber
plenty), and the full range-check chain (5-7 compares) instead of the
single compare the existing fast-path helper already does. This runs 3-6
times per pixel (source read, optional src-Z, dest read, optional dest-Z,
write, optional Z-write, plus the `PATTERNDATA`/`DSTDATA`/`DSTZ`/`SRCZINT`
register-source reads which also route through `READ_RDATA` → same
`JaguarRead*` path when SRCEN/DSTEN are off). Given the blitter is already
measured at 5-17% of frame time (task context) with a scalar per-pixel loop
doing this multiple times per pixel, this dispatch/call overhead is a
meaningful fraction of that.

Whether LTO can already inline this away: `Makefile:219` shows `-flto=4
-fwhole-program` is enabled **only** for `platform=classic_armv7_a7`. The
`rpi*`/`rpi*_64`/`unix`/`ios`/`tvos` targets that the task calls out
(RPi4, A10X) do **not** enable LTO (checked `Makefile:320-410`, no `-flto`
in the `rpi*` or generic-ARM blocks), so on those targets `blitter.c` and
`jaguar.c` are separate objects and the compiler cannot inline
`JaguarReadByte` etc. into `blitter_generic` regardless of function
attributes on the callee.

**Proposed change:** route `blitter_generic`'s pixel/Z/register reads and
writes through `blitter_read_byte/word/long` / `blitter_write_*` the same
way `BlitterMidsummer2` already does — i.e. change the `READ_PIXEL_n`/
`WRITE_PIXEL_n`/`READ_ZDATA_16`/`WRITE_ZDATA_16` macros (and the
`READ_RDATA_n` register-source macros, which fall back to `JaguarRead*` only
when `SRCEN`/`DSTEN` are off — less hot but same principle) to call the
`blitter_*` helpers instead of `Jaguar*` directly. This is a drop-in
replacement (same signature shape: `addr` in, value out) confined to this
file's macros.

**Risk:** LOW for pixel-exactness — the fast-path helpers replicate
`JaguarRead*`/`JaguarWrite*`'s exact RAM-path semantics (mask, byte-swap,
`blitMemoRecording`/`blitMemoMode` hooks) for the `<0x200000` case and fall
back to the identical full dispatch otherwise; behavior for cart-ROM/TOM/
JERRY-mapped blit sources (rare but real, e.g. reading OP display list
data) is unchanged since those still fall through to `JaguarRead*`. Gate
with `test/tools/test_blitter_compare` (fast-vs-accurate) plus the acid
suite before merging — should be a no-op on output, pure timing win.
Savestates unaffected (no new state).

**Effort:** S-M. Mechanical macro edit, but touches ~10 macros used
pervasively — a cheap model (sonnet) can do this correctly given clear
instructions to mirror `BlitterMidsummer2`'s existing usage pattern, then a
human/stronger pass should verify `test_blitter_compare` and the audio/video
acid tests are unaffected (they should be, since the RAM path is byte-
identical).

---

### F2 — `REG(A1_FLAGS)`/`REG(A2_FLAGS)` recomputed 30+ times per pixel [MED-HIGH / S / LOW-risk / mechanical]

**Where:** `src/tom/blitter.c:553-1055` (the `blitter_generic` inner loop
body). Confirmed by direct count: `REG(A1_FLAGS)` appears 19 times and
`REG(A2_FLAGS)` 17 times as literal macro-call text within that span (not
all execute on every pixel — depends on which branch — but the simplest
"opaque pixel copy" path alone hits `READ_PIXEL(a2, REG(A2_FLAGS))` (src),
either `READ_PIXEL(a1, REG(A1_FLAGS))` or `READ_RDATA(DSTDATA, a1,
REG(A1_FLAGS), ...)` (dst), and `WRITE_PIXEL(a1, REG(A1_FLAGS), writedata)`
— **3 REG() evaluations minimum per pixel**, more with Z/compare/shadow
paths active).

**Why it costs:** `REG(A)` (`blitter.c:180-181`) is
`((blitter_ram[A]<<24)|(blitter_ram[A+1]<<16)|(blitter_ram[A+2]<<8)|blitter_ram[A+3])`
— 4 independent byte loads (defeats word-load coalescing since it's byte-by-
byte from a `uint8_t[]`) plus 3 shifts and 3 ORs, repeated for a value that
cannot change during `blitter_generic` (verified: no `WREG`/`blitter_ram[]`
write occurs between the top of the function and the final register
writeback at `blitter.c:1152-1154`, i.e. after the loop). On an in-order
core this is ~7-10 extra instructions per call site, several call sites per
pixel, for a value that's loop-invariant.

**Proposed change:** at the top of `blitter_generic` (after the existing
`bppSrc` computation, `blitter.c:556`), add
`uint32_t a1Flags = REG(A1_FLAGS), a2Flags = REG(A2_FLAGS);` and replace
every `REG(A1_FLAGS)`/`REG(A2_FLAGS)` inside the loop body with the locals.
Mechanical `sed`-style replacement confined to lines 557-1055.

**Risk:** LOW — provably invariant (verified above), pure textual
substitution, `test_blitter_compare` + acid should be unaffected (bit-
identical results, faster).

**Effort:** S. Good candidate for a cheap model to do as a mechanical
find/replace, with a human spot-check that no `WREG(A1_FLAGS,...)` or
`WREG(A2_FLAGS,...)` was missed inside the loop (there isn't one, confirmed
above, but worth a final grep before merging).

---

### F3 — `blitter_generic` inner-loop mode decode is not hoisted out of the loop [MED-HIGH impact / L effort / MED risk / needs design]

**Where:** `src/tom/blitter.c:574-908` (main inner `while (inner_loop--)`
body).

**What:** ~20 command-bit tests (`SRCEN`, `SRCENZ`, `SRCENX`, `DSTEN`,
`DSTENZ`, `GOURZ`, `Z_OP_INF/EQU/SUP`, `DCOMPEN`, `BCOMPEN`, `CMPDST`,
`CLIPA1`, `PATDSEL`, `ADDDSEL`, `TOPBEN`, `TOPNEN`, `LFU_NAN/NA/AN/A`,
`GOURD`, `SRCSHADE`, plus `shadowFBActive`/`shadowHiresActive`) gate nested
`if`s inside the per-pixel loop. Each test itself is cheap (`cmd & CONST`
against a register-resident local — confirmed at `blitter.c:206-240`), but
`cmd` and the shadow globals don't change during the loop, so all ~20
branches are 100% predictable after the first iteration (branch
misprediction is NOT the cost here — `__builtin_expect` would not help).
The cost is pure **instruction count**: the CPU still executes and retires
every compare+branch every pixel, and because there are too many for the
compiler to profitably loop-unswitch into 2^20 specialized loop bodies, it
does not do this automatically (confirmed: no `static`/specialization
scaffolding exists for `blitter_generic` today — it's a single monolithic
function, unlike `BlitterMidsummer2` which already has a hand-written
collapsed variant for one command shape, see F7).

**Proposed change:** decode the handful of *actually common* command shapes
once per blit in `blitter_blit()` (opaque copy, copy+colour-key, Gouraud
fill, LFU-only) and dispatch to specialized inner-loop functions/macro
expansions for each, falling back to the current fully-general loop for
anything else — the same "collapse the common case, keep the general
path as fallback" pattern the accurate engine (F7) already uses.

**Risk:** MED — this is a real logic restructuring, not a mechanical
transform; must preserve exact semantics (including the DCOMPEN/PATTERNDATA
colour-key fix documented at `blitter.c:648-657`, the ADDDSEL saturating-add
path, and the shadow-framebuffer hi-res hooks). Gate hard on
`test_blitter_compare` (fast-vs-accurate divergence) AND full acid suite;
this is exactly the kind of change that could silently regress an edge
case a specific game depends on (Hover Strike's BCOMPEN kludge at
`blitter.c:563-568` is a warning sign that this loop has game-specific
landmines). Savestates unaffected.

**Effort:** L, needs design (which command shapes are common enough to
special-case — would benefit from instrumenting `blitter_generic` first,
see the Observability Gap note below). Not a mechanical task for a cheap
model; needs a reviewer familiar with the blitter semantics.

---

### F4 — Per-pixel address computation redoes `y*width*(1+pitch)` even when y is loop-constant [MED impact / L effort / MED risk / needs design]

**Where:** `PIXEL_OFFSET_16`/`_8`/`_32`/etc, `blitter.c:270-286`, e.g.
`PIXEL_OFFSET_16(a)` =
`(((((uint32_t)a##_y>>16)*a##_width)+((...x...)&~3))*(1+a##_pitch)+(...))`.

**What:** every `READ_PIXEL`/`WRITE_PIXEL`/`READ_ZDATA`/`WRITE_ZDATA` call
recomputes the full row-offset formula (2 multiplies) from scratch, even
though for the overwhelmingly common axis-aligned blit (`a1_yadd`/`a2_yadd`
== 0 within a row) `y` — and therefore the row base — does not change across
the inner loop; only the x term does.

**Why it costs:** 2 integer multiplies per pixel per address computed (up
to 2-4 addresses/pixel). Modern in-order ARM cores do integer multiply in
1-3 cycles, so this is a smaller cost than F1/F2/F3, but it's paid
unconditionally regardless of blit shape.

**Proposed change:** track a running row-base address incrementally,
recomputed only when y actually changes (i.e. once per outer-loop
iteration, or once per pixel only for diagonal/scaled blits where y does
change every pixel). This is the "O(width*height) that could be O(height)"
case flagged in the task.

**Risk:** MED — must handle A2's mask-wrap (`a2_mask_x`/`a2_mask_y`),
phrase-mode alignment, and the 1bpp/2bpp/4bpp sub-byte bit-shift terms
correctly; a subtly wrong incremental-address scheme is exactly the class
of bug `test_blitter_compare` exists to catch, but only if the accurate
engine takes the same address on the same inputs (it uses its own
`ADDRGEN`, so this is an independent implementation — good for defense in
depth, but means a shared bug wouldn't be caught by comparison alone;
rely on acid + real-game verification (Battle Sphere Gold, Val d'Isere,
AvP) too.

**Effort:** L, needs design. Lower priority than F1-F3 given the smaller
per-pixel cost (multiply vs. function-call-plus-dispatch-chain), but worth
scoping if F1-F3 don't fully close the gap.

**Note:** `PIXEL_OFFSET_1`/`_2`/`_4` (`blitter.c:264-268`) additionally do a
**division** (`a##_width / 8`, `/4`, `/2`) per pixel for sub-byte pixel
sizes. 1/2/4bpp blits are comparatively rare (mostly font/CLUT work), so
this is LOW tier on its own, but if F4 is tackled, hoisting the width/N
division alongside the row-address caching is free.

---

### F5 — `blitter_generic` is not `static`, preventing whole-function inlining/specialization [LOW impact / S effort / LOW risk / mechanical]

**Where:** `src/tom/blitter.c:553`, `void blitter_generic(uint32_t cmd)`.

**What:** `blitter_generic` has exactly one call site
(`blitter_blit()`, same TU, `blitter.c:1380`) and is not declared or used
anywhere else (`grep -rn blitter_generic` across `src/` and `test/` turns up
only that one call plus comments/docs). It is not `static`.

**Proposed change:** mark it `static void blitter_generic(uint32_t cmd)`.
This lets the compiler consider inlining it into `blitter_blit`, enables
better whole-function DCE/analysis, and costs nothing.

**Risk:** LOW — no external references exist. Trivial to verify with grep
before merging.

**Effort:** S, purely mechanical, safe for a cheap model to apply alongside
F1/F2.

---

### F6 — `blit_memo` / memo-hook overhead when memoization is OFF [confirmed cheap — no action needed]

**Where:** `BlitMemoLaunch()` (`blit_memo.c:628-693`), guarded reads/writes
in `blitter_read_*`/`blitter_write_*` and `JaguarRead*`/`JaguarWrite*` via
`blitMemoRecording`/`blitMemoMode` globals.

**Finding:** `BlitMemoLaunch()` returns immediately (`if (!blitMemoMode)
return 0`, `blit_memo.c:635`) when memo is off (the default,
`blitMemoMode = BLIT_MEMO_OFF` at `blit_memo.c:126`), and every per-pixel
hook site (`blitter.c:65,78,93,105,120,138` and the equivalents in
`jaguar.c`) is a single global-int compare before the (skipped) hash/record
work. This is already cheap and not a target for optimization — confirms
task item 4's concern was not realized in this codebase.

---

### F7 — Accurate engine (`BlitterMidsummer2`) is a genuine cycle-accurate FDSYNC simulation; most of its cost is load-bearing [context, secondary per task priority]

**Where:** `src/tom/blitter.c:2167-4041`.

**What:** this function literally simulates the Oberon ASIC's flip-flop
state machine one *hardware clock cycle* per `while(true)` loop iteration
(the `idle`/`inner`/`a1update`/`a2update`/`init_if`/... boolean cascade at
`blitter.c:2299-2410` is a direct transcription of FDSYNC next-state logic,
per the file's own header comment about the Oberon ASIC nets). For a
config that needs N hardware cycles per pixel, this loop runs N iterations
per pixel. This is why it's ~34% of frame time on AvP vs. 5-17% for the
scalar-but-decode-heavy fast engine — it's doing categorically more work
(cycle-accurate vs. result-accurate).

**Already-done optimizations found (do not re-propose these):**
- `ADDARRAY`/`ADD16SAT` are `BLITTER_ALWAYS_INLINE` and route through
  `blitter_simd_add16sat_x4` (NEON: `blitter_simd_neon.h:161-307`; also
  SSE2/scalar variants), explicitly profiled ("ADDARRAY at 1910 samples...
  single largest leaf", `blitter.c:1477-1479`).
- Row-offset caching: `a1_ya_cached`/`a2_ya_cached` computed once per inner-
  loop entry, not per pixel (`blitter.c:2455-2457`, comment "Precompute
  y*width row offsets (invariant when y unchanged)").
- Address-decode constants (`a1_xconst`/`a2_xconst`) and `srcshift`
  precomputed once per inner-loop entry (`blitter.c:2459-2469, 2471-2489`).
- A hand-written "COLLAPSED INNER LOOP — PATTERN FILL" fast path
  (`blitter.c:2500-2506` and following) that detects the pure-pattern-fill
  command shape (`PATDSEL` set, no `SRCEN`/`SRCENX`/`DSTEN`/`DSTENZ`/
  `DSTWRZ`/`GOURD`/`GOURZ`/`SRCSHADE`/`ADDDSEL`/`BCOMPEN`/`DCOMPEN`,
  `zmode==0`) and runs 1 iteration/pixel instead of the state machine's 2,
  doing the same `ADDRGEN`/mask/`DATA`/write/step work directly. This
  confirms and partially resolves the "state-machine collapse... remaining"
  item noted in project memory (`project_blitter_optimization_status.md`).

**What's still open (matches "remaining" in memory notes, not re-audited in
depth per task priority):** only the pattern-fill shape has a collapsed
path. The plain textured-copy shape (`SRCEN`+`DSTEN`, no `GOURD`, i.e. a
straightforward source-to-dest blit with Z/compare off) — very plausibly
AvP's actual per-triangle texture-blit hot path — still runs the full
multi-cycle FSM. Extending the collapse pattern to that shape is the next
highest-value target in this engine, per the existing code's own precedent,
but is a genuinely large, design-level undertaking (state-machine analysis
+ correctness proof against `test_blitter_compare`), not something to
attempt mechanically. Flagged here for prioritization, not detailed further
per the task's "keep secondary" instruction.

---

### F8 — SIMD coverage: NEON/SSE2 accelerate only the accurate engine; fast engine is 100% scalar [informational — no action recommended]

**Where:** `blitter_simd_neon.h`/`blitter_simd_sse2.c`/`blitter_simd_scalar.c`
provide `blitter_simd_lfu`, `blitter_simd_dcomp`, `blitter_simd_zcomp`,
`blitter_simd_byte_merge`, `blitter_simd_add16sat_x4`. All 7 call sites for
these (`blitter.c:1619,1790,1826,1892,2147,2148`) sit inside
`BlitterMidsummer2`'s helper functions (`1393-2166` range, called from
within `2167-4041`). `blitter_generic` calls none of them — it processes one
pixel at a time with scalar C, so there's no natural 4-lane batch to
vectorize without restructuring to a phrase-batched model (an F3/F4-class
redesign, not a SIMD bolt-on). Not recommending action here; noting it so a
"why doesn't the fast blitter use NEON" question doesn't come up as a
surprise later.

**NEON target selection** (task item 5): confirmed correct after PR #562/
issue #560 — `Makefile.common:154-163` selects NEON for `HAVE_NEON=1`
(set for `rpi2`-`rpi5` 32-bit, `Makefile:407-429`), for `ios-arm64`/
`tvos-arm64` explicitly, and for any `ARCH` of `aarch64`/`arm64` (which
covers `rpi*_64` via `Makefile:391-392` setting `ARCH = aarch64`, and
generic `arm64`/`aarch64` platforms via `Makefile:333-338`). No new finding
here — this was the subject of the prior #560 fix and looks complete for
the targets this task named (Pi4, A10X/iOS).

---

## Observability gap (not a runtime-perf finding, but blocks verifying the above)

`PERF_COUNTER`/`PERF_INC` instrumentation (`blitter_calls`, `blitter_outer`,
`blitter_inner`, `blitter_inner_io`, `blitter_inner_idle`,
`blitter_phrase_reads`, `blitter_phrase_writes`, declared `blitter.c:163-
170`) is **only** incremented inside `BlitterMidsummer2`
(`blitter.c:2169,2299,2566,2868,3086`) — `blitter_generic`, the default
engine, increments none of them. `test/tools/blitter_budget_probe.c`
explicitly documents this (`test/tools/blitter_budget_probe.c:48-56`:
"PERF_INC(blitter_calls) exists only in blitter_generic()" — that comment
is itself stale/wrong; it's actually only in `BlitterMidsummer2` — and "a
run that leaves the blitter on fast reports 0 blits"). Net effect: there is
currently no in-tree way to get a per-command-shape breakdown of where the
5-17% fast-blitter frame-time budget actually goes without switching to the
(much slower, differently-shaped) accurate engine first. If F3's "which
command shapes are common enough to special-case" question needs answering
empirically, adding `PERF_INC`/lightweight shape-bucket counters to
`blitter_generic` first would be a cheap, low-risk prerequisite (S effort,
mechanical, mirrors the existing `PERF_COUNTER` pattern).

---

## Priority ranking

1. **F1** (bypass RAM fast-path helpers) — HIGH impact, S-M effort, LOW risk.
   Do first; mechanical; a cheap model can execute it correctly with a
   pointer to `BlitterMidsummer2`'s existing usage as the template.
2. **F2** (cache `REG(A1_FLAGS)`/`REG(A2_FLAGS)`) — MED-HIGH impact, S effort,
   LOW risk. Do alongside F1 in the same PR; trivially mechanical.
3. **F5** (`static` on `blitter_generic`) — LOW impact, S effort, LOW risk.
   Free, bundle with F1/F2.
4. **Observability gap fix** — prerequisite for F3, cheap, do before F3.
5. **F3** (specialize inner loop per command shape) — MED-HIGH impact, L
   effort, MED risk. Needs design + real measurement (see observability
   gap) to pick which shapes to special-case; not mechanical.
6. **F4** (incremental row address) — MED impact, L effort, MED risk. Lower
   priority than F3 given smaller per-op cost (multiply vs. call/dispatch).
7. **F7** (extend accurate-engine collapse to textured-copy shape) — real
   opportunity but explicitly secondary per task scope; large, needs design.
8. F6, F8 — informational, no action.

> Raw sub-agent audit output (2026-08-22, read-only, sonnet). Line numbers refer to
> `libretro/develop` @ 5f898da. Verified items are promoted to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md);
> treat anything here that is NOT in that file as unverified.

# Virtual Jaguar libretro — build-flag + 68K audit (read-only)

Repo: `.claude/worktrees/alien-predator-save-state-ad621b`. All line numbers verified by
direct `Read`/`grep` against `Makefile` (2649 lines), `Makefile.common` (272 lines),
`link.T`, `link-test.T`, `exports.list`, `jni/Android.mk`, `jni/Application.mk`,
`libretro-common/include/libretro.h`, `libretro.c`, `src/core/jaguar.c`,
`src/m68000/m68kinterface.c`, `src/core/vjtrace.h`, `src/core/vjag_memory.h`.

**File-structure correction that matters for the Android finding below**: the whole
platform-selection `ifeq` chain (unix / classic_armv7_a7 / osx / ios / tvos / rpi* /
arm64-generic / consoles / msvc, and all `fpic`/`SHARED`/`GC_STYLE`/`OPT_LEVEL`/`WARNINGS`
assembly) lives in **`Makefile`**, lines ~193–975. **`Makefile.common`** (272 lines) only
holds `SOURCES_C`/`SOURCES_CXX` and the `BLITTER_SIMD_SRC` NEON/SSE2 selection logic —
it carries zero optimization-flag logic. `jni/Android.mk` includes only `Makefile.common`,
never `Makefile`. This is load-bearing for Finding A2.

---

## PART A — build / compiler-flag audit

### A1. HYPOTHESIS CONFIRMED — ELF/GNU-ld targets are `-fPIC` with no compile-time
non-interposition signal; the version script is a link-time-only mechanism

**Evidence:**
- `Makefile:201` (unix), `:214` (classic_armv7_a7), `:337` (arm64/aarch64 generic),
  `:376` (rpi0..rpi5_64), `:769` (Windows/MinGW fallback) all set
  `SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)`.
  `jni/Android.mk:62` does the Android-equivalent: `LOCAL_LDFLAGS := -Wl,-version-script=$(CORE_DIR)/link.T`.
- `link.T` (17 lines) / `link-test.T` (117 lines): GNU `--version-script` syntax,
  `global: retro_*; local: *;` in production mode. `exports.list` (13 lines) is the
  Mach-O mirror (`_retro_*`) via `-exported_symbols_list` (`Makefile.common` doesn't
  carry this; it's `MACHO_EXPORTS_FLAGS` at `Makefile:166`).
- Grepped the entire `Makefile` + `Makefile.common` + `jni/Android.mk` for
  `-fvisibility`, `-flto`/`-fwhole-program`, `-fno-semantic-interposition`, `-fno-plt`:
  **zero hits** on any ELF/GNU-ld platform block (unix, rpi0–rpi5_64, arm64/aarch64
  generic, generic `armv*`, Android). The only `-flto=4 -fwhole-program` in the whole
  tree is `classic_armv7_a7` (`Makefile:219`), which is a separate, self-contained
  recipe (see A1b below).
- Confirmed `RETRO_API` in `libretro-common/include/libretro.h:63-83` already expands to
  `__attribute__((__visibility__("default")))` on non-Windows GCC≥4/Clang. `libretro.c:4`
  `#include <libretro.h>` pulls that declaration in before every `retro_*` definition
  (e.g. `libretro.c:4949 void retro_init(void)`), so the attribute is already inherited
  by the definitions — **no source change needed** to keep `retro_*` exported if
  `-fvisibility=hidden` were added globally.

**Why this costs real cycles:** under `-fPIC` without `-fvisibility=hidden` or
`-fno-semantic-interposition`, GCC/Clang must, at compile time (per-.o, with no
knowledge of the eventual link), assume any non-`static` global function or object
*could* be interposed by another definition at dynamic-link time (classic ELF/System V
symbol preemption semantics — this is independent of what the linker later does with
`--version-script`, because the version script is invisible to the compiler driver).
The compiler therefore routes:
- every call to a non-`static` cross-TU function through the PLT (indirect, through
  `.got.plt`), and
- every access to a non-`static` global object through a GOT-indirect load (`@GOTPCREL`
  on x86_64, `:got:`/`:got_lo12:` on AArch64),
even for calls/accesses that will end up 100% internal to the `.so` after linking with
`link.T`. `src/tom/gpu.h:4`, `src/jerry/dsp.h:2`, `src/core/jaguar.h:13`,
`src/jerry/jerry.h:2`, `src/tom/tom.h:9` (37 `extern` globals across the hottest headers
alone, not counting functions) are exactly the kind of cross-TU state the interpreter's
per-instruction hot loops touch — GPU/DSP register file access, `jaguarMainRAM`,
`busArbiter`, etc.

I could not build a Linux `.so` in this read-only session to produce an `objdump -d`
diff proving the exact GOT/PLT instruction counts (no `make` allowed, and the only
prebuilt artifact in the tree is the macOS `.dylib`, whose ld64/two-level-namespace
codegen story is materially different from ELF's — Mach-O does not default to the same
symbol-preemption assumption, so the dylib's `nm` output, 2423 local `t`/`d`/`b`/`s` vs.
46 global `T`, is NOT evidence either way for this ELF-specific claim). The mechanism
itself is standard, well-documented GCC/Clang behavior (see `-fno-semantic-interposition`
in the GCC manual and Ulrich Drepper's "How To Write Shared Libraries" §2.2), and the
Makefile-side evidence (zero mitigating flags on every ELF platform block, confirmed by
direct grep) is unambiguous, so I'm reporting this as **structurally confirmed, cycle-
count unverified**.

**Proposed change (two independent, stackable options):**

1. **`-fno-semantic-interposition`** — add to `FLAGS` for all non-MSVC platforms
   (near `Makefile:874`, alongside the existing `-fomit-frame-pointer -fno-common`).
   Zero interaction with `TEST_EXPORTS`/`link-test.T`: it changes codegen assumptions
   only, not ELF visibility/binding, so `dlsym` into the wide test ABI is unaffected.
   Safe with the existing version-script setup (this is literally GCC's documented
   sanctioned pairing: version-script-hidden internals + `-fno-semantic-interposition`
   to let the compiler act on that fact). **Effort: S. Risk: LOW. Impact: MED–HIGH
   (unmeasured). Mechanical — sonnet can do this**, but the resulting binary needs an
   A/B on real ARM hardware (RPi4/Android) per `test/tools/opt_ab.sh`/`rpi_perf.sh`
   before claiming a win, same protocol as #515/#516/#517.

2. **`-fvisibility=hidden`**, gated so it does not fight `TEST_EXPORTS`:
   ```
   ifeq ($(TEST_EXPORTS),1)
      LINK_SCRIPT := link-test.T
      ...
   else
      LINK_SCRIPT := link.T
      MACHO_EXPORTS := exports.list
      CFLAGS  += -fvisibility=hidden
      CXXFLAGS += -fvisibility=hidden   # SOURCES_CXX is empty in this tree, but harmless
   endif
   ```
   at `Makefile.common`... **correction**: this ifeq actually lives in `Makefile.common`
   itself at lines 158-165 (the `TEST_EXPORTS`/`LINK_SCRIPT` block is one of the few
   things in `Makefile.common` that isn't `SOURCES_*`) — verified directly. Since
   hidden visibility is a compile-time ELF/Mach-O symbol-table property (stronger than
   the version script — a hidden symbol never reaches `.dynsym` regardless of what
   `link.T` says), it must stay OFF whenever `link-test.T`'s wide ABI (`DSP*`, `GPU*`,
   `m68k_*`, `jaguarMainRAM`, `regs`, `vjs`, ~80 more patterns, see `link-test.T:12-117`)
   needs those symbols to actually land in `.dynsym` for the test harnesses' `dlsym()`
   calls — which is exactly the gating shown above. **This does NOT need per-symbol
   `VJ_TEST_EXPORT` attribute annotations** — gating the single `-fvisibility=hidden`
   flag on `TEST_EXPORTS` is sufficient, because in the `TEST_EXPORTS=1` branch every
   symbol keeps its default (exported) visibility exactly as today. **Effort: M**
   (needs verification that no other file already relies on being able to `dlsym`
   something outside the `retro_*`/test-ABI set in the *production* build — grep for
   any `dlopen`/`dlsym` self-use inside `libretro.c` and `src/**` first; I didn't find
   one, but a design-review pass is warranted since this changes what `.dynsym` exports
   on the *default* `make` target). **Risk: LOW-MED** (the coexistence design is sound
   but touches `Makefile.common`'s core `TEST_EXPORTS` branch — a place the repo's own
   comments (`Makefile.common:174-191`, `Makefile:1008-1075`) flag as historically
   fragile / stale-.o-hazard-prone). **Needs design review, not purely mechanical** —
   sonnet can draft it but a human/opus pass should confirm the `TEST_EXPORTS` gating
   doesn't interact with `BUILD_AXES` in a way that reintroduces #457-class staleness
   (it shouldn't — `TEST_EXPORTS` is already in `BUILD_AXES` at `Makefile:184-186`, so
   flipping it already forces the full-object flush that would also recompile with/
   without `-fvisibility=hidden` correctly — but this deserves an explicit check, not
   an assumption).

Do these together, or `-fno-semantic-interposition` alone first as the lower-risk,
zero-coexistence-hazard step. Neither has been touched by #560/#562/#567 (those were
SIMD-selection and per-SoC `-mcpu`/`-O3` fixes, a completely different axis).

### A1b. `classic_armv7_a7` already gets most of this benefit via LTO, and no other
platform does

`Makefile:219` (`CFLAGS += -flto=4 -fwhole-program -fuse-linker-plugin ...`) means GCC's
IPA at LTO time already privatizes any symbol not visible from outside the linked
program image — `-fwhole-program`'s documented effect — while still respecting the
`default`-visibility attribute `RETRO_API` puts on `retro_*`, so that target likely
already avoids most of the GOT/PLT tax described in A1, incidentally, as a side effect
of a flag added for code-size reasons (`Makefile:919-926` discusses the size win, not
this angle — so this exact benefit appears to be unrecognized/undocumented even for the
one platform that already has it). This is worth a one-line comment addition, not a
functional change. **Effort: S. Risk: none (docs only).**

### A2. CONFIRMED — Android (`jni/Android.mk`) inherits **none** of the project's
optimization flags: no `-O3`/`OPT_LEVEL`, no `-DNDEBUG`, no `-fomit-frame-pointer`,
no `-fno-common`, no `WARNINGS` block

**Evidence:** `jni/Android.mk:35` does `include $(CORE_DIR)/Makefile.common`, never
`include $(CORE_DIR)/Makefile`. As established above, all `OPT_LEVEL`/`-DNDEBUG`/
`-fomit-frame-pointer`/`-fno-common`/`WARNINGS`/`FLAGS`/`CFLAGS` assembly (`Makefile:1-975`)
lives in `Makefile`, not `Makefile.common`. `jni/Android.mk:41` builds `COREFLAGS` from
scratch: `-DINLINE="inline" -D__LIBRETRO__ $(INCFLAGS) $(BLITTER_SIMD_DEFINE)` — four
things, none of which are an optimization level, `NDEBUG`, or any of the other flags
this audit was asked to check. `jni/Application.mk` (2 lines) sets only `APP_ABI := all`
— no `APP_OPTIM`, no `APP_CFLAGS`. So the Android build's `-O` level, whether `assert()`
compiles to a no-op, frame-pointer/common-symbol behavior, and every `-W` choice are
**entirely delegated to ndk-build's own toolchain defaults**, which the project makes
no attempt to pin or verify. (I did not find `assert()` calls in the hot interpreter
files — `grep -rn "assert(" src/tom/gpu.c src/jerry/dsp.c src/core/jaguar.c src/m68000/*.c`
returned 0 hits, and `assert.h` is only pulled in via `src/m68000/sysdeps.h` — so the
missing `-DNDEBUG` is likely low-consequence today, but it's one more implicit
dependency on toolchain defaults that the rest of this Makefile goes out of its way to
pin explicitly, e.g. `Makefile:70-99`'s whole `OPT_O3_PLATFORMS` mechanism exists
*because* an implicit "last -O wins" default silently regressed `classic_armv7_a7` for
years — issue #516, `Makefile:74`.)

**Proposed change:** add an explicit, pinned optimization block to `jni/Android.mk`
mirroring `Makefile`'s non-MSVC release branch — at minimum
`-O3 -DNDEBUG -fomit-frame-pointer -fno-common` appended to `COREFLAGS`, plus
per-ABI `-mcpu`/`-march` tuning analogous to the RPi table in
`docs/agent/build.md:27-33` (e.g. `armeabi-v7a` → a NEON-capable Cortex baseline,
`arm64-v8a` → generic `-march=armv8-a`, since unlike RPi, an Android ABI name does not
identify silicon exactly — this needs the same care `docs/agent/build.md:37-44` gives
the RPi 32-vs-64-bit `-mfpu`/`-mfloat-abi` split, i.e. don't blindly copy an RPi row
onto an ABI that isn't that exact core). **Effort: S–M. Risk: LOW** (this is strictly
additive — nothing currently sets these flags for Android, so there's no "last flag
wins" conflict to navigate). **Impact: uncertain-but-plausible MED-HIGH** — I can't
verify what `ndk-build`'s implicit default optimization level actually resolves to for
release mode without invoking `ndk-build` (out of scope here), so treat the impact
tier as an estimate, not a measurement; verify with `ndk-build V=1` dry output or
equivalent before/after, then A/B on-device. **Mechanical enough for sonnet** to draft,
but the per-ABI `-march` choice needs a design pass (same caution as the RPi table).

### A3. `-Wno-strict-aliasing` is a *warning suppression*, not `-fno-strict-aliasing`
— no live issue here, but flag it so nobody "fixes" a non-problem

`Makefile:959` (`WARNINGS` block) has `-Wno-strict-aliasing` but **no** `-fno-strict-
aliasing` anywhere in the tree (grepped `Makefile`+`Makefile.common`+`jni/Android.mk`,
one hit total, the `-W` form). `-fstrict-aliasing` therefore stays at its `-O2`/`-O3`
default (enabled) on every platform. This means the type-based-alias optimization
*is* active; the codebase evidently has (or had) some type-punning that triggers the
GCC warning, and the project chose to silence the warning rather than either fix the
punning or disable the optimization — the more aggressive (and more dangerous) of the
two options was correctly avoided. **No action needed; this closes off a plausible-
looking but incorrect "dumb win."**

### A4. `-DNDEBUG` / asserts — confirmed disabled in release on every non-Android,
non-MSVC-debug platform

`Makefile:828` (`FLAGS += $(OPT_LEVEL) -DNDEBUG`, the non-DEBUG/non-MSVC branch) and
`Makefile:825-826` (MSVC branch, same). `assert()` is essentially unused in the hot path
(see A2 above) so this is low-consequence either way, but it is correctly wired for
every platform driven through `Makefile` — only Android (A2) misses it.

### A5. PGO is structurally feasible; not attempted; the benchmark harness already
gives a deterministic profiling workload

`Makefile:2460-2482` (`benchmark:` target) drives `test/tools/test_benchmark` against
`test/roms/jagniccc.j64` with **fixed** `BENCH_FRAMES=900`, `BENCH_WARMUP=300`,
`BENCH_BLITTER=fast` — `Makefile.common`'s comment block above it (in `Makefile`,
`:2412-2463`, not `Makefile.common` — corrected location) documents this ROM as "the
only public ROM that exercises GPU, DSP, blitter and 68K in real proportion" and that
warmup is load-bearing because `jagniccc` sits in BIOS boot for the first ~90 frames.
This is exactly the deterministic, representative workload a two-pass
`-fprofile-generate` / `-fprofile-use` PGO build wants. No `-fprofile-generate`/`-use`
flags exist anywhere in the tree today (grepped, zero hits) and there is no
`make pgo`-style target.

**Proposed change:** a new `pgo` make target: (1) build with `-fprofile-generate`,
(2) run `make benchmark` (or a small corpus of 2-3 ROMs spanning cart/CD/GPU-heavy/
DSP-heavy, to avoid over-fitting the profile to one title), (3) rebuild with
`-fprofile-use -fprofile-correction`, discarding the `.gcda` files from the release
artifact. **Determinism note, addressed proactively because this codebase is unusually
touchy about it (#400, #479, #517):** PGO does not itself introduce any run-to-run
nondeterminism — it only changes which optimization decisions the compiler makes
*ahead of time*, baked into the binary at build time; it has no runtime/floating-point-
mode implications like `-Ofast`/`-ffast-math` did, so it does not reopen the #517 class
of hazard. What it *does* require: profile data goes stale as the interpreter tables
(`cpuemu.c`, `cputbl.h`) or hot loops change, so a stale profile silently regresses
rather than erroring — this needs either CI regenerating profile data on every release
tag, or an explicit staleness check. **Effort: M (new build-system machinery + corpus
selection + staleness guard). Risk: MED (build complexity, not correctness — PGO
binaries are still fully deterministic at runtime). Impact: MED-HIGH, unmeasured — PGO
typically nets 5-15% on branch-heavy dispatch code like this interpreter, but that's a
generic prior, not a measurement of this codebase.** This needs a design pass (which
ROMs go in the training corpus, where `.gcda` artifacts live, how staleness is
detected) before a mechanical implementation — **not sonnet-alone**, needs the same
kind of protocol rigor #515/#517 already established for this repo.

### A6. `-O3` rollout is essentially complete; nothing left here

`Makefile:70-98` (`OPT_O3_PLATFORMS`) already covers `unix osx win ios-arm64 ios9
tvos-arm64` plus all nine `rpi*` names (issue #516/#567, already merged per git log:
`6624ef1 perf(build): per-SoC -mcpu and -O3 for every Raspberry Pi target`,
`048f233 fix(test): make the tuning probe independent of the calling make's flags`).
The generic `arm64`/`aarch64` platform block (`Makefile:334-339`) and the generic
`armv*` block (`Makefile:417-430`) are **not** in `OPT_O3_PLATFORMS`, so they build at
the `-O2` fallback (`Makefile:97`) — this is a real gap (a distro packaging a generic
`platform=arm64` build gets `-O2`, not `-O3`) but it's a narrow one: every *named*
concrete target (rpi*, ios, tvos, osx, unix, win) already has it, and the generic
`arm64`/`armv*` blocks exist specifically as an escape hatch for unidentified silicon
(see `Makefile:328-333`'s own comment about issue #560), where `-mcpu` tuning is
deliberately withheld for the same "don't guess the SoC" reason `docs/agent/build.md`
gives. Adding these two names to `OPT_O3_PLATFORMS` is a **one-line, S-effort, LOW-risk**
follow-up (same measurement-transfers-by-ISA-family argument `Makefile:38-39` already
uses to justify `osx`/`ios-arm64`/`tvos-arm64` from a single macOS arm64 measurement) —
flagging it since the prompt asked specifically about missing `-O3`, but noting it's
minor compared to A1/A2/A5. **Impact: LOW (narrow platform surface). Mechanical —
sonnet can do this.**

### A7. `-fno-stack-protector` / unwind-table trimming: only `classic_armv7_a7` has it

`Makefile:221,223` (`-fno-stack-protector -fomit-frame-pointer` /
`-fno-unwind-tables -fno-asynchronous-unwind-tables`) are `classic_armv7_a7`-only.
`-fomit-frame-pointer` is separately applied globally (`Makefile:874`), but stack-
protector and unwind-table trimming are not. **This is a real but genuinely double-
edged tradeoff, not a pure "dumb win"**: stack-protector canaries are one of the few
runtime mitigations standing between a maliciously crafted ROM/savestate that overflows
a fixed-size buffer somewhere in the C emulation code and actual exploitation — removing
it project-wide trades a small, unmeasured perf gain for a real (if narrow) hardening
regression on a codebase that parses untrusted game data. **Recommend: do not blanket-
apply; if pursued, scope it to the RPi/embedded targets only (where the security model
is already "trusted single-user device," matching why `classic_armv7_a7` — an embedded
console-clone target — already has it) and leave `unix`/`osx`/`android`/`ios` alone.**
**Effort: S. Risk: MED (security tradeoff, needs an explicit decision, not silent).
Impact: LOW-MED, unmeasured.**

### A8. No `-DDEBUG`/logging leakage found in release

Grepped for stray debug macros: only `DEBUG_PRESENTATION` (`Makefile:979-982`, opt-in,
off by default) and `BLITTER_TRACE`/`BENCH_PROFILE`/`VJ_TRACE` (all opt-in via their own
`=1` flags, already gated correctly per `Makefile.common:123-165`). `VJT_WATCH_RD`
(`src/core/vjtrace.h:323` vs `:342`) compiles to a true no-op
(`do { } while (0)`) when `VJ_TRACE` is undefined, i.e., in every production build —
verified directly, this is not a hidden per-access cost in the shipped binary. **No
action needed.**

---

## PART B — 68K core (brief, per profiles showing 0.7-2.6% of frame time)

1. **Memory access: direct C dispatch via a chain of address-range `if`/`else if`
   comparisons, not a jump table, not function-pointer-per-access.**
   `src/core/jaguar.c:500` (`m68k_read_memory_8`), `:548` (`_16`), `:595` (`_32`) each
   do `address &= 0x00FFFFFF;` then a linear chain: main RAM (`0x000000-0x1FFFFF`)
   checked **first** (the hottest case), then cart ROM (`0x800000-0xDFFEFF`), then
   CD/TOM/JERRY/unknown. `m68kinterface.h:86-93` declares these as plain
   `unsigned int m68k_read_memory_N(unsigned int)` — ordinary extern functions, called
   directly from `inlines.h:85`'s `get_iword` macro, not through a vtable. This is
   fine and standard for a UAE-derived core; RAM-resident code (the common case) is a
   single range check away.

2. **Instruction fetch fast path:** `inlines.h:85` `get_iword(o)` is literally
   `m68k_read_memory_16(regs.pc + (o))` — every fetch re-enters the full read-dispatch
   chain above (address mask + `M68K_BUS_CHARGE` + `M68KGPURAMSyncRead` + range check),
   there is no separate "instruction fetch from a cached RAM/ROM pointer" fast path
   distinct from data reads. Given the 0.7-2.6% frame-time budget this is very unlikely
   to matter, but if it were ever revisited: a direct-pointer fast path for PC known to
   be inside `jaguarMainRAM`/`jaguarMainROM` (recomputed only on branches, not every
   fetch) is the standard UAE/Musashi optimization for this — **not proposing it**, out
   of scope per the brief ("keep this brief," "no JIT/no core swap"), but noting it's
   the one structural thing that *would* matter if 68K's frame-time share ever grew
   (e.g. from a bug fix that makes more code path through this table).

3. **`m68k_execute` per-instruction overhead** (`src/m68000/m68kinterface.c:144-216`):
   each iteration does: one `regs.spcflags & SPCFLAG_DEBUGGER` check, one
   `checkForIRQToHandle` check, one `regs.intLevel > regs.intmask &&
   TOMIRQRequestActive()` check, a computed dispatch `(*cpuFunctionTable[opcode])(opcode)`,
   and a plain 32-bit subtraction (`regs.remainingCycles -= cycles`, both `int32_t` —
   **no 64-bit division anywhere in this loop**, confirmed by reading the full function
   body). This is about as lean as a table-dispatch interpreter loop gets; nothing
   "silly" here. IRQ is polled via cheap flag checks, not re-derived every instruction
   from scratch.

4. **`cpuFunctionTable[opcode]` dispatch** — confirmed, this is a plain indirect call
   through a 65536-entry function-pointer table (`src/m68000/cpustbl.c`, machine-
   generated), exactly what the brief said to treat as "fine." Linker dead-code
   elimination for the unused second table (`op_smalltbl_4_ff`, "fast" but never bound)
   is already handled — `Makefile:877-940`'s whole `GC_STYLE` block exists specifically
   for this (issue #321, already shipped).

5. **No compile-unit-specific flag divergence for the 68K files.** Grepped `Makefile`
   for any `m68000`/`cpuemu` special-casing outside the `#321` dead-code-elimination
   comment: none. `src/m68000/cpu*.c` and `read*.c` are C89-lint-exempt
   (`docs/agent/build.md:74-77`, `scripts/c89-lint.sh::skip_file`) but that's a *lint*
   exemption, not a compiler-flag one — they compile through the same generic
   `%.o: %.c` rule (`Makefile:998-999`) with the same `$(CFLAGS)` as every other file,
   confirmed by reading that rule directly. No stray `-O0`/`-fno-*` applied only to
   this directory.

6. **One real per-access cost worth naming, not fixing:** `M68K_BUS_CHARGE`
   (`src/core/jaguar.c:447-451`) calls `bus_arbiter_m68k_access()` — a real function
   call — on every memory access when `busArbiter.enabled`, for DRAM-timing accuracy
   (the #337 cycle-domain contract). This is a deliberate accuracy feature, already
   investigated and exonerated as a stutter cause per project memory
   (`project_bus_contention_merge_plan`), and is explicitly out of scope here (not a
   "dumb win," it's a correctness/perf tradeoff already made on purpose). Not proposing
   a change; noting it exists so it isn't rediscovered as if new.

7. **`GET16`/`GET32`/`SET16`/`SET32`** (`src/core/vjag_memory.h:75-79`): byte-by-byte
   shift/OR composition from a `uint8_t*`, e.g. `GET32(r,a) = ((r[a]<<24)|(r[a+1]<<16)|
   (r[a+2]<<8)|r[a+3])`. This is the standard portable big-endian-from-byte-array
   idiom; modern GCC/Clang at `-O2`+ generally recognize it as a byte-swap-load and fold
   it to a single load+`bswap`/`rev` (this pattern is explicitly idiom-matched by both
   compilers' tree optimizers), and `unsigned char*` accesses are exempt from strict-
   aliasing concerns, so A3's finding doesn't interact with this. **Not flagging as an
   issue** — I have no evidence it doesn't fold correctly, and 68K's frame-time share is
   too small for this to be worth an unverified change. If it ever mattered, the way to
   check is `objdump -d` on `m68k_read_memory_32`'s ROM branch and see whether GCC
   emitted a fused load+bswap or four separate byte loads+shifts — I did not do this
   (no build allowed), so treat this item as "check before touching," not a finding.

---

## Summary of proposed changes by impact/effort

| # | Change | Platforms | Impact | Effort | Risk | Who |
|---|---|---|---|---|---|---|
| A1-1 | `-fno-semantic-interposition` | unix, rpi*, arm64-generic, android, armv*-generic | MED-HIGH (unmeasured) | S | LOW | sonnet (mechanical), needs on-device A/B |
| A1-2 | `-fvisibility=hidden` gated on `!TEST_EXPORTS` | same as above | MED-HIGH (unmeasured) | M | LOW-MED | design review, then sonnet |
| A2 | Pin `-O3 -DNDEBUG -fomit-frame-pointer` (+per-ABI `-march`) in `jni/Android.mk` | android | MED-HIGH (estimate) | S-M | LOW | sonnet, ABI-tuning part needs review |
| A5 | PGO via existing `make benchmark` harness | unix, rpi*, android (any GCC/Clang ELF target) | MED-HIGH (generic prior, unmeasured) | M | MED (build complexity) | needs design (corpus, staleness), not sonnet-alone |
| A6 | Add generic `arm64`/`armv*` to `OPT_O3_PLATFORMS` | generic arm64/armv builds only | LOW (narrow surface) | S | LOW | sonnet |
| A7 | `-fno-stack-protector`/unwind trimming, RPi-scoped only | rpi* only, deliberately not global | LOW-MED (unmeasured) | S | MED (security tradeoff — needs an explicit yes/no, not silent) | needs a decision, then sonnet |
| A1b | Doc comment: classic_armv7_a7's existing LTO already gets most of A1's benefit | docs only | n/a | S | none | sonnet |

Nothing in Part B is being proposed — the 68K interpreter's dispatch, memory-access
chain, and per-instruction loop are all structurally sound for a table-dispatch
interpreter, and its 0.7-2.6% frame-time share means even a real inefficiency there
wouldn't move the needle versus GPU/DSP/blitter work. Already-shipped/decided-against
items **not** re-proposed here: `-ffast-math`/`-Ofast` globally (#517, rejected,
measured ~0%), SIMD/NEON selection (#560/#562, fixed), per-SoC `-mcpu`/`-O3` for named
RPi targets (#567, merged), linker dead-code elimination for the unused 68K "fast" table
(#321, shipped), `-Wno-strict-aliasing` "fix" (A3 above — not a real issue).

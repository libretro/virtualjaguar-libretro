# Build & C89 detail

Detail for the compact rules in [`CLAUDE.md`](../../CLAUDE.md). LLM-oriented shorthand.

## Commands

```bash
make -j$(getconf _NPROCESSORS_ONLN)          # build, auto-detects platform
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1  # debug: -O0 -g
make clean
make platform=ios-arm64                        # cross-compile
```

Output: `virtualjaguar_libretro.{dylib,so,dll}`. CI: `make -j4` on Ubuntu (GCC) + macOS
(Clang) + `test/regression_test.sh`. `Makefile` = 30+ targets, auto-detected via `uname`
or `platform=`; `Makefile.common` lists sources. Flags: `-D__LIBRETRO__`, `-DMSB_FIRST`
(big-endian).

## ARM / Raspberry Pi targets

The `rpi*` platform names identify the silicon exactly, so they carry real per-SoC
tuning (issue #560). Everything is resolved in the Makefile's `rpi*` platform block —
**never re-specify these on the command line**, and never append an `-O` in a platform
block (that is the #516 bug: the last `-O` wins and a later `-O2` silently beat
`-Ofast` for years).

| platform | SoC | core | blitter | flags on top of `-O3` |
| --- | --- | --- | --- | --- |
| `rpi0` `rpi1` | BCM2835 | ARM1176JZF-S (ARMv6) | **scalar** | `-marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard` |
| `rpi2` | BCM2836 | Cortex-A7 | neon | `-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard` |
| `rpi3` / `rpi3_64` | BCM2837 | Cortex-A53 | neon | `-mcpu=cortex-a53` (+`-mfpu=neon-fp-armv8 -mfloat-abi=hard` on 32-bit) |
| `rpi4` / `rpi4_64` | BCM2711 | Cortex-A72 | neon | `-mcpu=cortex-a72` (same 32-bit addition) |
| `rpi5` / `rpi5_64` | BCM2712 | Cortex-A76 | neon | `-mcpu=cortex-a76` (same 32-bit addition) |

Three traps, all verified in an `arm-linux-gnueabihf` container:

- **`-mfpu` / `-mfloat-abi` are 32-bit-ARM only.** `aarch64-linux-gnu-gcc` hard-errors
  (`unrecognized command-line option '-mfpu=neon-fp-armv8'`), so a `*_64` row that
  acquires one does not build at all.
- **`rpi0`/`rpi1` need `-marm`.** Debian's armhf cross toolchain is
  `--with-arch=armv7-a+fp` and defaults to Thumb; ARMv6 only has Thumb-1, and GCC
  cannot pair Thumb-1 with hard-float (`sorry, unimplemented: Thumb-1 'hard-float' VFP
  ABI`). A native Pi 1 compiler defaults to ARM mode, so this bites cross-builds only.
- **`rpi1` is scalar on purpose.** ARM1176 has no NEON. Do not "fix" it.

`-Ofast` is deliberately **not** used: it implies `-ffast-math`, which #517 measured at
~0% here and removed globally as a determinism hazard (run-ahead #400, netplay,
savestate compat all depend on determinism). `make OPT_LEVEL=-Ofast platform=rpi4`
still works if you want to measure it.

`test/tools/simd_matrix_check.sh` (in `make test`) asserts all of the above — blitter,
the *last* `-O`, the expected `-mcpu`, and that 32-bit-only flags stay off 64-bit rows.
Release/nightly build all eight via `release.yml`'s matrix, cross-compiled on x86_64
(`-mcpu` names a core to generate code *for*; it does not require running on one).

> The libretro GitLab buildbot cannot do this: `libretro-infrastructure/ci-templates`
> has no `rpi*` template (only `linux-aarch64.yml` for ARM Linux), so per-chipset Pi
> cores would need a template added upstream.

**Host builds: prefix `DEVELOPER_DIR=/Library/Developer/CommandLineTools`.** `xcode-select`
points at Xcode.app, so bare `make`/`cc` resolve inside an app bundle → macOS raises an App
Management prompt on every invocation (dozens per multi-agent run). Same Apple clang, no
bundle, no prompts. Do NOT `xcode-select --switch` globally — full Xcode is needed for iOS
cross-builds.

## C89 / GNU89 — strict

libretro buildbot uses MSVC on Windows; CI has a `c89-lint` job. Run
`bash scripts/c89-lint.sh src/YOURFILE.c` before pushing.

- **No mid-block declarations** — all vars at top of block, before any statement. Most common violation.
- `//` allowed (GNU89) but prefer `/* */` for new code.
- No C99: no `for (int i…)`, no compound literals, no designated initializers, no VLAs.
- Exempt (see `scripts/c89-lint.sh::skip_file`): `src/m68000/cpu*.c`, `src/m68000/read*.c`
  (UAE 68K); `src/bios/jag*bios*.c` (bin2c hex tables); `src/tom/blitter_simd_{sse2,neon}.c`
  (intrinsics); `test/tools/test_rcheevos_e2e.c`; `test/tools/flicker_detect.c`;
  `deps/libchdr/*` and `tools/jagcd/*` (vendored libchdr = C99 unity TU, see `unity.o` rule).

## Test ABI & re-linking

`make` links production-slim ABI (`retro_*` only). `make test` needs the wide test ABI so
harnesses can `dlsym` internals. `TEST_EXPORTS=1` selects wide **and** adds `-DVJ_TRACE` →
changes object *content*, not just export list.

Switching either direction is automatic: Makefile stamps build config into `.build-config`;
when it differs it deletes the library **and every object** before building. Both
`make` → `make TEST_EXPORTS=1 test` and the reverse work with no `make clean`. Cost: one
full rebuild per flip (~21s at `-j8` without ccache).

Stamp covers **every** compile-affecting switch (`BUILD_AXES`: `TEST_EXPORTS BENCH_PROFILE
DEBUG BLITTER_TRACE COVERAGE RELEASE_DEBUG_INFO DEBUG_PRESENTATION STATIC_LINKING platform
OPT_LEVEL LTO`).
**Adding a new CFLAGS-affecting switch means adding it to that list** — forgetting = silent
chimera binary, not a build error. It stamps variable *names*, not `$(CFLAGS)` (`DEBUG=1`
injects a per-second `-DBUILD_TIMESTAMP`, so stamping flags would flush every build).

The flush is unconditional, not a curated "objects that use vjtrace" list — that list is what
broke before (omitted `src/tom/blit_memo.o` → `make TEST_EXPORTS=1` then plain `make` died on
undefined `_vjtrace_emit` from `BlitMemoLaunch`; reverse linked clean but silently kept no-op
macros). Do not reintroduce a curated/grepped/`-MD`-derived list; a file can pick up `VJT_*`
via indirect include. `make -n` is exempt from the flush (dry run costs nothing). CI gates both
directions on every host row of `c-cpp.yml`.

History: before v2.3.2 the switch didn't relink at all (`Missing: m68k_execute`). Before #457
the stamp tracked `TEST_EXPORTS` alone → `make DEBUG=1` after a release recompiled zero objects
(a "debug build" that was all `-O2`, no debug info); toggling `BENCH_PROFILE` recompiled nothing
so `timing_probe` reported `timing_halfline_callbacks counter not found` (reads as broken tool,
not ignored flag). `VJ_EXPECT_BUILD` can't catch either — git rev is identical across the flip.

## ELF visibility / interposition / LTO (issue #569)

Every ELF/GNU-ld release build (`unix`, `rpi*`, generic `arm64`/`aarch64`/`armv*`, Android
via `jni/Android.mk`) now compiles with `-fno-semantic-interposition` and, for
non-`TEST_EXPORTS` builds, `-fvisibility=hidden`. Mach-O targets (`osx`, `ios*`, `tvos*`)
are untouched — Apple ld64's two-level namespace already resolves same-image symbols
directly, so GCC's ELF-only interposition assumption never applied there. Gated on
`GC_STYLE=gnu` in the `Makefile`, the same variable that already marks the GNU-ld targets
for `--gc-sections` — but `GC_STYLE=gnu` is a slightly *wider* set than "ELF", so two
platforms are explicitly carved back out:

- **`qnx`** also has `GC_STYLE=gnu` and keeps `-fvisibility=hidden` (GCC ≥ 4.0), but is
  excluded from `-fno-semantic-interposition` (needs GCC ≥ 5.1): no CI or buildbot job
  builds `qnx` in this repo, so its toolchain floor is unverified, and QNX SDP 6.x's `qcc`
  is known to wrap an old enough GCC that the flag can hard-error instead of no-op.
- **`win`** (native MinGW) also sets `GC_STYLE=gnu` purely because MinGW's `ld` honours
  `--version-script`/`--gc-sections` too — not because its output is ELF. `win`'s `TARGET`
  is a PE/COFF `.dll`, and `libretro.h`'s `RETRO_API` expands to
  `__attribute__((__dllexport__))` there, not the visibility attribute, so this whole
  block's rationale (ELF-style GOT/PLT interposition, `RETRO_API` surviving
  `-fvisibility=hidden` via inherited visibility) doesn't apply — and `win` *is* built in
  CI (`c-cpp.yml`'s MSYS2 job, `release.yml`). It is excluded outright from all three of
  `-fno-semantic-interposition`, `-fvisibility=hidden`, and the `LTO=1` knob below.

- **Why:** without these flags, GCC treats every cross-file global (`gpu_reg`, `dsp_pc`,
  the flag/counter externs the hot interpreter headers share) and every cross-file call
  (`JaguarReadLong` &c.) as potentially interposable, forcing GOT/PLT indirection per
  emulated instruction. This project already pins its entire exported surface via
  `$(LINK_SCRIPT)` (`link.T`/`link-test.T`), so nothing outside `libretro.c`'s `retro_*`
  functions is meant to be interposable in the first place.
- **`TEST_EXPORTS` is exempt from `-fvisibility=hidden`:** the white-box harnesses
  `dlsym()` internal symbols (see Test ABI section above); hiding them at compile time
  would break that even though the version script still lists them.
  `-fno-semantic-interposition` still applies under `TEST_EXPORTS=1` — it only affects
  codegen, not the symbol table.
- **`retro_*` stays exported under `-fvisibility=hidden`:** `libretro.h` declares every
  entry point `RETRO_API`, which expands to
  `__attribute__((visibility("default")))` on non-Windows GCC/Clang ≥ 4. A visibility
  attribute already present on an earlier declaration governs the later definition, so
  `libretro.c`'s definitions (which never repeat `RETRO_API`) inherit it from the header
  they include.
- **`LTO=1` opt-in knob:** `make LTO=1` (combine with `platform=` as usual) appends
  `-flto` to both compile and link lines for every `GC_STYLE=gnu` target except `win`
  (see above). Deliberately **not** default-on — it needs an A/B on real Pi hardware first
  (`test/tools/rpi_perf.sh`), the same caution the `-O3` rollout used (#515/#516).
  `classic_armv7_a7` is unaffected either way: it already runs its own
  `-flto=4 -fwhole-program` pipeline in its platform block, independent of this knob.
  `LTO` is in `BUILD_AXES` since it changes object content.
- **Android (`jni/Android.mk`):** `ndk-build` never includes `Makefile`, only
  `Makefile.common`, so it inherited none of the desktop optimisation flags. `COREFLAGS`
  now also carries `-O3 -DNDEBUG -fvisibility=hidden -fno-stack-protector
  -fomit-frame-pointer -fno-semantic-interposition` by hand.
- **Issue #516 does not apply here:** neither `-fvisibility=hidden` nor
  `-fno-semantic-interposition` is an `-O`, so neither can silently shift a platform's
  resolved optimisation level.

## Build-identity guard (stale-binary protection)

Every harness that dlopens the core prints the binary's embedded version (`vX.Y.Z
<gitrev>[-dirty]`, also logged at `retro_init`). If `VJ_EXPECT_BUILD` is set (as `make test`
and `cd_boot_matrix.sh` do, via `scripts/build-id.sh`), a core whose version doesn't
token-match fails the load loudly instead of silently testing stale code.

```bash
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/your_harness ...
```

`scripts/build-id.sh` prints short git rev + `-dirty` when tracked files are modified;
`scripts/gen-version-h.sh` stamps that same string into the core, so both sides agree. Exists
because `make` can skip a rebuild when mtimes are second-identical. Manual fallback:
`nm -gU <dylib> | grep <newsymbol>`.

**Stale .o hazard:** `git stash` cycles can build chimera binaries `VJ_EXPECT_BUILD` cannot
catch (git rev identical). Baseline measurements from a pristine worktree.

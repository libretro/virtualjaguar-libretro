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
DEBUG BLITTER_TRACE COVERAGE RELEASE_DEBUG_INFO DEBUG_PRESENTATION STATIC_LINKING platform`).
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

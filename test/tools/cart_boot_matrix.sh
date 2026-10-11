#!/usr/bin/env bash
#
# test/tools/cart_boot_matrix.sh — cartridge boot matrix for every ROM in the
# local private corpus, in both boot modes (HLE and real BIOS).
#
# The cartridge counterpart of cd_boot_matrix.sh: runs each ROM headlessly
# through test/tools/cart_boot_probe for a fixed frame budget and writes one
# markdown row per title to docs/cart-boot-matrix.md.  The site generator
# (scripts/build_site.py) renders that file; nothing on the site is typed by
# hand.
#
# BIOS-mode runs are scored from the boot-ROM -> cart handoff onward, and a
# cart the boot ROM rejects is BIOS_REJECT; see cart_classify.sh (issue #852).
#
# Honesty rules (mirrors the CD matrix):
#   - "GAME_CODE" means the final 68K PC sits in game-owned RAM or cart space
#     after the frame budget.  It is NOT a completed-the-game certificate.
#   - Menu vs in-game is not distinguished headlessly (see "Headless
#     framebuffer caveat" in CLAUDE.md).
#   - A black-video row is reported as undetermined evidence, not as broken:
#     a handful of titles under-render through the headless read path.
#   - Rows are stamped with the core build id; resuming skips only rows from
#     the SAME build and re-runs rows recorded by any other build, so an OUT
#     file can never resurrect ancient results as fresh (the phantom Battle
#     Morph lesson — see cd_boot_matrix.sh).
#
# Env knobs:
#   CART_MATRIX_ROMS_ROOT  ROM directory     (default test/roms/private/ROMS)
#   CART_MATRIX_OUT        output markdown   (default docs/cart-boot-matrix.md)
#   CART_MATRIX_LOGDIR     logs + row cache  (default /tmp/cart-matrix-logs)
#   CART_MATRIX_FRAMES     frames per run    (default 600).  In BIOS mode: frames
#                          scored AFTER the boot-ROM -> cart handoff
#   CART_MATRIX_BIOS_BOOT_FRAMES  extra frame cap BIOS runs get on top of
#                          FRAMES for the boot animation (default 700; the
#                          handoff lands at ~frame 492)
#   CART_MATRIX_TIMEOUT    seconds per run   (default 90)
#   CART_MATRIX_JOBS       parallel workers  (default 4)
#   CART_MATRIX_MAX_RUNS   stop after N fresh titles this invocation (chunking)
#   CART_MATRIX_PROBE_ARGS extra cart_boot_probe arguments, appended to every
#                          probe run (word-split), e.g.
#                          "--option virtualjaguar_usefastblitter=disabled"
#                          to sweep the ACCURATE blitter -- the shipped
#                          default.  Without it the probe runs the harness
#                          default, the FAST blitter, which is blind to
#                          accurate-only hangs (issue #800).  Folded into the
#                          row-cache identity, kept out of the default
#                          LOGDIR, and refused without an explicit
#                          CART_MATRIX_OUT, so an Accurate sweep can never
#                          reuse, overwrite or publish over Fast rows.
#
# Requires the wide test ABI:  make TEST_EXPORTS=1
# and the probe:               see cart_boot_probe.c header for the cc line.

set -u

ROMS_ROOT="${CART_MATRIX_ROMS_ROOT:-test/roms/private/ROMS}"
PROBE_ARGS="${CART_MATRIX_PROBE_ARGS:-}"
if [ -n "$PROBE_ARGS" ] && [ -z "${CART_MATRIX_OUT:-}" ]; then
    echo "error: CART_MATRIX_PROBE_ARGS is set; also set CART_MATRIX_OUT" >&2
    echo "  (the default OUT is the published Fast-blitter matrix)" >&2
    exit 1
fi
PROBE_ARGS_TAG=""
if [ -n "$PROBE_ARGS" ]; then
    PROBE_ARGS_TAG="-$(printf '%s' "$PROBE_ARGS" | { shasum 2>/dev/null || sha1sum; } | cut -c1-8)"
fi
OUT="${CART_MATRIX_OUT:-docs/cart-boot-matrix.md}"
LOGDIR="${CART_MATRIX_LOGDIR:-/tmp/cart-matrix-logs$PROBE_ARGS_TAG}"
FRAMES="${CART_MATRIX_FRAMES:-600}"
BIOS_BOOT_FRAMES="${CART_MATRIX_BIOS_BOOT_FRAMES:-700}"
# BIOS runs are ~1.8x longer than HLE ones (boot animation + FRAMES scored).
TIMEOUT_SECS="${CART_MATRIX_TIMEOUT:-150}"
JOBS="${CART_MATRIX_JOBS:-4}"
MAX_RUNS="${CART_MATRIX_MAX_RUNS:-0}"

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/matrix_common.sh"

# Core discovery, the test-export check and the build-identity guard are shared
# with the CD sweep (matrix_common.sh).  Refusing to start without a core is the
# point: the old fallback silently selected a nonexistent .so when the dylib was
# missing (an ABI-mode switch deletes it, and a stray iOS-built .o makes the
# relink fail), every worker then dlopen-failed, and the classifier read that as
# "the ROM would not load" -- 123 false LOAD_FAIL rows.
if [ -n "${CART_MATRIX_CORE:-}" ]; then
    CORE="$CART_MATRIX_CORE"
    if [ ! -f "$CORE" ]; then
        echo "error: CART_MATRIX_CORE=$CORE does not exist" >&2
        exit 1
    fi
else
    matrix_find_core || exit 1
    CORE="./$MATRIX_CORE"
fi
PROBE=./test/tools/cart_boot_probe

BUILD_ID="$(matrix_build_id)"
export VJ_EXPECT_BUILD="$BUILD_ID"

# Row-cache identity (#440): scoped to inputs that can change a verdict, so a
# docs-only commit no longer invalidates all 154 rows.  See matrix_common.sh.
CACHE_ID="$(matrix_cache_id)$PROBE_ARGS_TAG"

ROWDIR="$LOGDIR/rows"
mkdir -p "$LOGDIR" "$ROWDIR"

if [ ! -x "$PROBE" ]; then
    echo "error: $PROBE not built (see cart_boot_probe.c header)" >&2
    exit 1
fi
if [ ! -d "$ROMS_ROOT" ]; then
    echo "error: ROM root '$ROMS_ROOT' not found" >&2
    exit 1
fi

run_bounded() { matrix_run_bounded "$@"; }

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/cart_classify.sh"
process_one() {
    rom="$1"
    base="$(basename "$rom")"
    title="${base%.*}"
    slug="$(printf '%s' "$base" | tr -c 'A-Za-z0-9._-' '_')"
    rowfile="$ROWDIR/$slug.row"

    if [ -f "$rowfile" ] && grep -q "build:$CACHE_ID -->" "$rowfile"; then
        return 0
    fi

    hle_log="$LOGDIR/hle-$slug.log"
    bios_log="$LOGDIR/bios-$slug.log"

    DYLD_LIBRARY_PATH=. LD_LIBRARY_PATH=. \
        run_bounded "$TIMEOUT_SECS" "$hle_log" \
        "$PROBE" "$CORE" "$rom" --frames "$FRAMES" $PROBE_ARGS
    hle_rc=$?
    hle="$(cart_classify_mode "$hle_rc" "$hle_log" hle)"

    DYLD_LIBRARY_PATH=. LD_LIBRARY_PATH=. \
        run_bounded "$TIMEOUT_SECS" "$bios_log" \
        "$PROBE" "$CORE" "$rom" --frames "$((FRAMES + BIOS_BOOT_FRAMES))" \
        --post-handoff "$FRAMES" --bios $PROBE_ARGS
    bios_rc=$?
    bios="$(cart_classify_mode "$bios_rc" "$bios_log" bios)"

    printf '| %s | %s | %s | %s | %s |<!-- build:%s -->\n' \
        "$title" \
        "${hle%%|*}" "${hle#*|}" \
        "${bios%%|*}" "${bios#*|}" \
        "$CACHE_ID" > "$rowfile"
    echo "done: $title  [hle: ${hle%%|*}]  [bios: ${bios%%|*}]"
}
export -f process_one run_bounded cart_classify_mode field
export -f matrix_core_error matrix_run_bounded
export ROWDIR LOGDIR FRAMES BIOS_BOOT_FRAMES TIMEOUT_SECS MATRIX_TIMEOUT_BIN PROBE CORE BUILD_ID CACHE_ID VJ_EXPECT_BUILD PROBE_ARGS

# ---------------------------------------------------------------------------
# ROM list -> fresh work list (respecting MAX_RUNS) -> parallel workers
# ---------------------------------------------------------------------------

LIST="$LOGDIR/roms.list"
find -L "$ROMS_ROOT" -maxdepth 1 -type f \
     \( -name '*.j64' -o -name '*.jag' -o -name '*.rom' -o -name '*.abs' \
        -o -name '*.cof' -o -name '*.bin' -o -name '*.prg' \) \
     ! -name '\[BIOS\]*' \
    | sort > "$LIST"

TOTAL="$(wc -l < "$LIST" | tr -d ' ')"
FRESH="$LOGDIR/fresh.list"
: > "$FRESH"
count=0
while IFS= read -r rom; do
    base="$(basename "$rom")"
    slug="$(printf '%s' "$base" | tr -c 'A-Za-z0-9._-' '_')"
    if [ -f "$ROWDIR/$slug.row" ] && grep -q "build:$CACHE_ID -->" "$ROWDIR/$slug.row"; then
        continue
    fi
    printf '%s\n' "$rom" >> "$FRESH"
    count=$((count + 1))
    if [ "$MAX_RUNS" -gt 0 ] && [ "$count" -ge "$MAX_RUNS" ]; then break; fi
done < "$LIST"

echo "corpus: $TOTAL ROMs; fresh this invocation: $count; build: $BUILD_ID; cache: $CACHE_ID; jobs: $JOBS"

# Preflight: the build-identity guard must pass BEFORE any worker writes a
# row.  A stale or mismatched core once turned an entire sweep into 153
# LOAD_FAIL rows — fail loudly and write nothing instead.
if [ "$count" -gt 0 ]; then
    first_rom="$(head -1 "$FRESH")"
    preflight_log="$LOGDIR/preflight.log"
    DYLD_LIBRARY_PATH=. LD_LIBRARY_PATH=. \
        run_bounded "$TIMEOUT_SECS" "$preflight_log" \
        "$PROBE" "$CORE" "$first_rom" --frames 1
    if grep -q 'FATAL build mismatch' "$preflight_log"; then
        echo "error: core build does not match VJ_EXPECT_BUILD=$BUILD_ID" >&2
        grep 'FATAL build mismatch' "$preflight_log" >&2
        echo "rebuild with:  make TEST_EXPORTS=1   (and rebuild the probe if it changed)" >&2
        exit 1
    fi
    # The core must actually load.  This check used to be gated behind
    # "if a CARTPROBE line exists", so a first ROM that legitimately fails to
    # load (e.g. an alpha dump) skipped the whole preflight and let a broken
    # core through -- exactly how the 123-LOAD_FAIL sweep got started.
    preflight_err="$(matrix_core_error "$preflight_log")"
    if [ -n "$preflight_err" ]; then
        echo "error: $preflight_err" >&2
        echo "  core: $CORE" >&2
        grep -m1 'dlopen(' "$preflight_log" >&2 2>/dev/null
        echo "rebuild with:  make clean && make TEST_EXPORTS=1" >&2
        exit 1
    fi
    # A first ROM that cannot load is not itself an error, but it means the
    # preflight proved nothing -- walk forward until one loads, so the sweep is
    # never started on an unverified core.
    if ! grep -q '^CARTPROBE ' "$preflight_log"; then
        probe_ok=0
        while read -r cand; do
            run_bounded "$TIMEOUT_SECS" "$preflight_log" \
                "$PROBE" "$CORE" "$cand" --frames 1
            preflight_err="$(matrix_core_error "$preflight_log")"
            if [ -n "$preflight_err" ]; then
                echo "error: $preflight_err (core: $CORE)" >&2
                exit 1
            fi
            if grep -q '^CARTPROBE ' "$preflight_log"; then probe_ok=1; break; fi
        done < "$FRESH"
        if [ "$probe_ok" -eq 0 ]; then
            echo "error: no ROM in the corpus produced a CARTPROBE line" >&2
            echo "the core or the probe is broken -- refusing to write a matrix" >&2
            exit 1
        fi
    fi
    if grep -q '^CARTPROBE ' "$preflight_log" &&
       ! grep -q ' pc_valid=1 ' "$preflight_log"; then
        echo "error: probe could not read m68k_get_reg from the core" >&2
        echo "rebuild with:  make TEST_EXPORTS=1   (and rebuild the probe if it changed)" >&2
        exit 1
    fi
fi

if [ "$count" -gt 0 ]; then
    tr '\n' '\0' < "$FRESH" | xargs -0 -n1 -P "$JOBS" bash -c 'process_one "$1"' _
fi

# ---------------------------------------------------------------------------
# Assemble OUT from row cache (all builds; stale-build rows only survive
# until a fresh run replaces them, and each row carries its stamp).
# ---------------------------------------------------------------------------

DONE="$(ls "$ROWDIR" 2>/dev/null | wc -l | tr -d ' ')"
{
    printf '# Cartridge boot matrix\n\n'
    printf 'Generated by `test/tools/cart_boot_matrix.sh` — do not edit rows by hand.\n\n'
    printf 'Each ROM in the local private corpus runs headlessly for %s frames in\n' "$FRAMES"
    printf 'both boot modes.  `GAME_CODE` = final 68K PC in game-owned RAM/cart space\n'
    printf '— it means "boots and executes", not "completed the game".  Menu vs\n'
    printf 'in-game is not distinguished headlessly.  A "black video" note is\n'
    printf 'undetermined evidence (headless read-path caveat), not a verdict.\n'
    printf 'Rows are stamped with the core build that produced them.\n\n'
    printf '**Real BIOS rows are scored from the boot-ROM handoff.** The boot ROM plays a\n'
    printf '~490-frame logo animation and jingle before it hands the 68K to the cart, so\n'
    printf 'a BIOS run keeps going until %s frames after the handoff (cap: %s frames)\n' "$FRAMES" "$((FRAMES + BIOS_BOOT_FRAMES))"
    printf 'and only counts video/audio from there.  `BIOS_REJECT` = the boot ROM ran,\n'
    printf 'never reached the cartridge and halted in its own `BRA.S *` loop (the red\n'
    printf '"Jaguar" reject screen: the dump fails the header/encryption check), which\n'
    printf 'is not "BIOS works".  `? (bios_trap)` = the cart got control, then the 68K\n'
    printf 'fell back into the boot ROM (an exception through its vectors).\n\n'
    printf '**Never backward.** Every release candidate'"'"'s regenerated matrix is diffed\n'
    printf 'against the previous tag'"'"'s with `test/tools/matrix_diff.py OLD.md NEW.md`.\n'
    printf 'A row that moves backward (`LOAD_FAIL` < `?` < `GAME_CODE` = `BIOS_REJECT`, per boot mode), or\n'
    printf 'whose notes gain a crash-watchdog signature (`gpu_wedge`, `dsp_wedge`,\n'
    printf '`inframe_hang`, `video_stall`, `gpu_pc_escape`, `dsp_pc_escape`), blocks the\n'
    printf 'tag until it has a ticket and an explicit deferral.  Checklist:\n'
    printf '`docs/release-process.md`.\n\n'
    if [ -n "$PROBE_ARGS" ]; then
        printf 'Probe arguments for every run: `%s`\n\n' "$PROBE_ARGS"
    fi
    printf '| Title | HLE | HLE notes | Real BIOS | BIOS notes |\n'
    printf '|---|---|---|---|---|\n'
    # Only rows for ROMs in the CURRENT list: a row cached for a file that
    # was later excluded (or renamed) must not resurrect into the table.
    while IFS= read -r rom; do
        base="$(basename "$rom")"
        slug="$(printf '%s' "$base" | tr -c 'A-Za-z0-9._-' '_')"
        [ -f "$ROWDIR/$slug.row" ] && cat "$ROWDIR/$slug.row"
    done < "$LIST" | sort -f
} > "$OUT"

DONE="$(grep -c '^|' "$OUT")"
DONE=$((DONE - 2))
echo "wrote $OUT ($DONE of $TOTAL titles have rows)"

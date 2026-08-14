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
#   CART_MATRIX_FRAMES     frames per run    (default 600)
#   CART_MATRIX_TIMEOUT    seconds per run   (default 90)
#   CART_MATRIX_JOBS       parallel workers  (default 4)
#   CART_MATRIX_MAX_RUNS   stop after N fresh titles this invocation (chunking)
#
# Requires the wide test ABI:  make TEST_EXPORTS=1
# and the probe:               see cart_boot_probe.c header for the cc line.

set -u

ROMS_ROOT="${CART_MATRIX_ROMS_ROOT:-test/roms/private/ROMS}"
OUT="${CART_MATRIX_OUT:-docs/cart-boot-matrix.md}"
LOGDIR="${CART_MATRIX_LOGDIR:-/tmp/cart-matrix-logs}"
FRAMES="${CART_MATRIX_FRAMES:-600}"
TIMEOUT_SECS="${CART_MATRIX_TIMEOUT:-90}"
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
CACHE_ID="$(matrix_cache_id)"

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

field() {
    # usage: field <name> <probe-line>   (numeric / $hex fields)
    printf '%s' "$2" | grep -oE "$1=[^ ]+" | head -1 | cut -d= -f2
}

classify_mode() {
    # usage: classify_mode <rc> <logfile>
    # echoes "STAGE|notes"
    rc="$1"; logfile="$2"
    line="$(grep -m1 '^CARTPROBE ' "$logfile" 2>/dev/null || true)"

    sigs="$(grep -oE 'gpu_pc_escape|dsp_pc_escape|gpu_wedge|dsp_wedge|video_stall' \
                 "$logfile" 2>/dev/null | sort -u | paste -sd, - || true)"

    if grep -q 'FATAL build mismatch' "$logfile" 2>/dev/null; then
        # Never classify a guard refusal as a title result.  The preflight
        # should catch this before any worker runs; this is belt-and-braces
        # for a core swapped mid-sweep.
        echo "? (build_mismatch)|core does not match VJ_EXPECT_BUILD — row invalid"
        return
    fi
    if [ "$rc" -eq 124 ]; then
        echo "? (timeout)|no probe line within ${TIMEOUT_SECS}s${sigs:+; $sigs}"
        return
    fi
    # A dlopen failure is a HARNESS fault, not a title result.  Writing it as
    # LOAD_FAIL is what let a missing core masquerade as 123 unloadable ROMs,
    # and because rows are cached, the bad rows were then reused by the next
    # invocation.  Shared with the CD sweep so one fix covers both.
    core_err="$(matrix_core_error "$logfile")"
    if [ -n "$core_err" ]; then
        echo "? (core_error)|$core_err"
        return
    fi
    if [ -z "$line" ] || printf '%s' "$line" | grep -q 'load_fail=1'; then
        echo "LOAD_FAIL|probe could not load the ROM"
        return
    fi

    frames="$(field frames "$line")"; frames="${frames:-0}"
    pc_valid="$(field pc_valid "$line")"; pc_valid="${pc_valid:-0}"
    pc_hex="$(printf '%s' "$line" | grep -oE 'pc=\$[0-9A-Fa-f]+' | grep -oE '[0-9A-Fa-f]+$')"
    lit="$(field lit_frames "$line")"; lit="${lit:-0}"
    motion="$(field motion "$line")"; motion="${motion:-0}"
    audio="$(field audio_nonsilent "$line")"; audio="${audio:-0}"

    if [ -z "$pc_hex" ] || [ "$frames" -eq 0 ]; then
        echo "LOAD_FAIL|no frames rendered"
        return
    fi
    if [ "$pc_valid" -ne 1 ]; then
        echo "? (no_reg)|probe missing m68k_get_reg — rebuild core with TEST_EXPORTS=1"
        return
    fi

    pc=$((16#$pc_hex))
    # Valid 68K execute bands: main RAM (mirrors) < $200000, cart $800000-
    # $DFFFFF, boot ROM $E00000-$E1FFFF.  Anything else is a crash, not a
    # reached stage.
    if ! { [ "$pc" -lt $((0x200000)) ] || \
           { [ "$pc" -ge $((0x800000)) ] && [ "$pc" -le $((0xDFFFFF)) ]; } || \
           { [ "$pc" -ge $((0xE00000)) ] && [ "$pc" -le $((0xE1FFFF)) ]; }; }; then
        echo "? (pc_escape)|final_pc=\$$pc_hex${sigs:+; $sigs}"
        return
    fi

    notes=""
    if [ "$lit" -ge 30 ]; then
        if [ "$motion" -ge 30 ]; then notes="video"; else notes="static video"; fi
    else
        notes="black video (headless — undetermined)"
    fi
    if [ "$audio" -ge 4800 ]; then notes="$notes, audio"; else notes="$notes, silent"; fi
    [ -n "$sigs" ] && notes="$notes; $sigs"
    echo "GAME_CODE|$notes"
}

process_one() {
    rom="$1"
    base="$(basename "$rom")"
    title="${base%.*}"
    slug="$(printf '%s' "$base" | tr -c 'A-Za-z0-9._-' '_')"
    rowfile="$ROWDIR/$slug.row"

    if [ -f "$rowfile" ] && grep -q "build:$CACHE_ID" "$rowfile"; then
        return 0
    fi

    hle_log="$LOGDIR/hle-$slug.log"
    bios_log="$LOGDIR/bios-$slug.log"

    DYLD_LIBRARY_PATH=. LD_LIBRARY_PATH=. \
        run_bounded "$TIMEOUT_SECS" "$hle_log" \
        "$PROBE" "$CORE" "$rom" --frames "$FRAMES"
    hle_rc=$?
    hle="$(classify_mode "$hle_rc" "$hle_log")"

    DYLD_LIBRARY_PATH=. LD_LIBRARY_PATH=. \
        run_bounded "$TIMEOUT_SECS" "$bios_log" \
        "$PROBE" "$CORE" "$rom" --frames "$FRAMES" --bios
    bios_rc=$?
    bios="$(classify_mode "$bios_rc" "$bios_log")"

    printf '| %s | %s | %s | %s | %s |<!-- build:%s -->\n' \
        "$title" \
        "${hle%%|*}" "${hle#*|}" \
        "${bios%%|*}" "${bios#*|}" \
        "$CACHE_ID" > "$rowfile"
    echo "done: $title  [hle: ${hle%%|*}]  [bios: ${bios%%|*}]"
}
export -f process_one run_bounded classify_mode field
export -f matrix_core_error matrix_run_bounded
export ROWDIR LOGDIR FRAMES TIMEOUT_SECS MATRIX_TIMEOUT_BIN PROBE CORE BUILD_ID CACHE_ID VJ_EXPECT_BUILD

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
    if [ -f "$ROWDIR/$slug.row" ] && grep -q "build:$CACHE_ID" "$ROWDIR/$slug.row"; then
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

#!/usr/bin/env bash
#
# test/tools/vjtrace_selftest.sh -- vjtrace flight-recorder self-test.
#
# Three independent checks against the vjtrace facility (Tasks 1-6 of the
# 2026-08-11 vjtrace-flight-recorder plan), using ONLY the in-tree
# test/roms/yarc.j64 ROM -- never test/roms/private, so this never skips.
#
#   (a) DETERMINISM -- two identical 300-frame runs with --field-csv and
#       --snap 200 must produce a byte-identical field CSV (field_diff exit
#       0) and a byte-identical frame-200 state snapshot (trace_memdiff exit
#       0). This is the gate every future vjtrace-based A/B measurement
#       depends on: if the emulator is nondeterministic frame-for-frame,
#       every downstream trace_diff/field_diff comparison is meaningless.
#       If this fails, the correct response is to REPORT IT and investigate,
#       not to relax the check.
#
#   (b) WATCH ATTRIBUTION -- a --watch 0x0:0x200000:w run (main RAM writes)
#       must produce watch_wr hits in the field CSV AND, in the WATCH_WR ring
#       records, BOTH a who=M68K record AND a who=GPU or who=DSP record, each
#       carrying a genuine (nonzero) originating PC.
#
#       NOTE ON who=M68K (Task 7.5, closing the Task 7 gap): watch hooks
#       live in JaguarReadByte/Word/Long and JaguarWriteByte/Word/Long
#       (src/core/jaguar.c) for GPU/DSP/OP/blitter/CDROM callers, AND, as of
#       Task 7.5, directly in the 68K's own bus-access fast path --
#       m68k_read_memory_8/16/32 and m68k_write_memory_8/16/32 (jaguar.c,
#       ~425-810) -- which never routes through JaguarRead*/JaguarWrite* (it
#       writes/reads jaguarMainRAM[] directly and dispatches TOM/JERRY/CDROM
#       itself). Task 7 confirmed the gap empirically (a VJ_TRACE_RING=
#       25000000 run, large enough that nothing is evicted, showed zero
#       who=M68K WATCH_WR records); Task 7.5 hooked only the non-decomposing
#       entry points of that fast path (see the "Terminal branch" comments
#       at each hook site in jaguar.c) so a single 68K access produces the
#       natural record(s) and never double-counts. GPU/DSP-attributed watch
#       counts are unchanged by this change (verified with a run large
#       enough to avoid the default ring's wraparound -- the default 1M-slot
#       ring wraps many times over during a 300-frame run, so a naive
#       before/after diff of the *default-ring* tail window looks like it
#       shifts BLITTER counts; that is a ring-eviction artifact of adding a
#       new event type into a shared fixed-size ring, not a behavior change
#       -- see task-7.5-report.md for the full before/after comparison with
#       VJ_TRACE_RING set high enough to hold the whole run).
#
#   (c) RING WRAP -- a VJ_TRACE_RING=1024 run of 300 frames must produce
#       EXACTLY 1024 dumped records (proving vjtrace_dump()'s
#       min(ring_head, ring_cap) window is correct once the ring has
#       wrapped) with strictly increasing `seq` values across the dump
#       (proving no duplicate or out-of-order slot after wrap).
#
# Each check prints its own "PASS"/"FAIL" line with a one-line reason.
# Exit: 0 if all three checks PASS, 1 if any FAILS.
#
# Usage: vjtrace_selftest.sh [core_path]
#   core_path defaults to a platform-appropriate
#   ./virtualjaguar_libretro.{dylib,so} in the repo root (the brief's
#   example used the macOS .dylib name literally; this script auto-detects
#   by `uname -s` instead so the same script also runs Linux CI, same as
#   test/sram_test.sh's platform switch).
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

# ---- resolve core -------------------------------------------------------
CORE="${1:-}"
if [ -z "$CORE" ]; then
    case "$(uname -s)" in
        Darwin) CORE="./virtualjaguar_libretro.dylib" ;;
        *)      CORE="./virtualjaguar_libretro.so" ;;
    esac
fi
if [ ! -f "$CORE" ]; then
    echo "vjtrace_selftest: core not found at '$CORE' -- build with 'make TEST_EXPORTS=1' first" >&2
    exit 1
fi
CORE="$(cd "$(dirname "$CORE")" && pwd)/$(basename "$CORE")"

ROM="$ROOT/test/roms/yarc.j64"
if [ ! -f "$ROM" ]; then
    echo "vjtrace_selftest: ROM not found at '$ROM'" >&2
    exit 1
fi

# Honor an already-exported VJ_EXPECT_BUILD (make test sets this); compute
# it ourselves for a standalone run so a stale core still fails loudly.
export VJ_EXPECT_BUILD="${VJ_EXPECT_BUILD:-$("$ROOT/scripts/build-id.sh")}"

# ---- build the analyzer/smoke tools if missing ---------------------------
# `make test` builds nothing extra for this script (none of these four are
# in the TEST_EXPORTS prerequisite list) -- they are hand-built tools per
# their own header comments. Build once into test/tools/ (untracked, like
# the other non-test_-prefixed tool binaries there) so repeat runs of this
# script and of `make test` reuse them instead of rebuilding every time.
BUILD_CC="${CC:-cc}"
TOOLS_DIR="$ROOT/test/tools"

build_if_missing() {
    local bin="$1"; shift
    if [ ! -x "$TOOLS_DIR/$bin" ]; then
        echo "vjtrace_selftest: building $bin"
        "$BUILD_CC" -O2 -Wall -std=c99 "$@" -o "$TOOLS_DIR/$bin"
    fi
}

build_if_missing vjtrace_smoke -I"$ROOT" -I"$ROOT/libretro-common/include" \
    "$ROOT/test/tools/vjtrace_smoke.c" \
    "$ROOT/test/harness/harness.c" "$ROOT/test/harness/trace_probe.c" \
    -ldl -lm
build_if_missing trace_dump    -I"$ROOT" "$ROOT/test/tools/trace_dump.c"
build_if_missing field_diff    -I"$ROOT" "$ROOT/test/tools/field_diff.c"
build_if_missing trace_memdiff -I"$ROOT" "$ROOT/test/tools/trace_memdiff.c"

SMOKE="$TOOLS_DIR/vjtrace_smoke"
TRACE_DUMP="$TOOLS_DIR/trace_dump"
FIELD_DIFF="$TOOLS_DIR/field_diff"
TRACE_MEMDIFF="$TOOLS_DIR/trace_memdiff"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/vjtrace_selftest.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

RESULT=0
pass() { echo "vjtrace_selftest: PASS -- $1: $2"; }
fail() { echo "vjtrace_selftest: FAIL -- $1: $2" >&2; RESULT=1; }

# ==========================================================================
# (a) determinism
# ==========================================================================
DIR_A="$WORK_DIR/a"
DIR_B="$WORK_DIR/b"
mkdir -p "$DIR_A" "$DIR_B"

RUN_A_OK=1
"$SMOKE" "$CORE" "$ROM" --frames 300 \
    --field-csv "$DIR_A/field.csv" --snap 200 --snap-prefix "$DIR_A/snap" \
    >"$DIR_A/log.txt" 2>&1 || RUN_A_OK=0

RUN_B_OK=1
"$SMOKE" "$CORE" "$ROM" --frames 300 \
    --field-csv "$DIR_B/field.csv" --snap 200 --snap-prefix "$DIR_B/snap" \
    >"$DIR_B/log.txt" 2>&1 || RUN_B_OK=0

if [ "$RUN_A_OK" -eq 0 ] || [ "$RUN_B_OK" -eq 0 ]; then
    fail determinism "smoke run failed (run_a_ok=$RUN_A_OK run_b_ok=$RUN_B_OK) -- see $DIR_A/log.txt / $DIR_B/log.txt"
    cat "$DIR_A/log.txt" >&2 || true
    cat "$DIR_B/log.txt" >&2 || true
else
    FIELD_RC=0
    "$FIELD_DIFF" "$DIR_A/field.csv" "$DIR_B/field.csv" >"$WORK_DIR/field_diff.out" 2>&1 || FIELD_RC=$?
    MEM_RC=0
    "$TRACE_MEMDIFF" "$DIR_A/snap_f000200.vjsn" "$DIR_B/snap_f000200.vjsn" >"$WORK_DIR/memdiff.out" 2>&1 || MEM_RC=$?

    if [ "$FIELD_RC" -eq 0 ] && [ "$MEM_RC" -eq 0 ]; then
        pass determinism "field_diff exit 0 ($(cat "$WORK_DIR/field_diff.out")), trace_memdiff exit 0 ($(cat "$WORK_DIR/memdiff.out"))"
    else
        fail determinism "BLOCKED -- emulator is nondeterministic across two identical 300-frame runs of the same ROM (field_diff rc=$FIELD_RC, trace_memdiff rc=$MEM_RC). Do NOT relax this check -- every future vjtrace-based A/B measurement depends on it. Investigate before proceeding."
        echo "----- field_diff output -----" >&2
        cat "$WORK_DIR/field_diff.out" >&2 || true
        echo "----- trace_memdiff output -----" >&2
        cat "$WORK_DIR/memdiff.out" >&2 || true
    fi
fi

# ==========================================================================
# (b) watch attribution
# ==========================================================================
DIR_W="$WORK_DIR/watch"
mkdir -p "$DIR_W"

RUN_W_OK=1
"$SMOKE" "$CORE" "$ROM" --frames 300 \
    --field-csv "$DIR_W/field.csv" --trace-out "$DIR_W/trace.vjtr" \
    --watch 0x0:0x200000:w \
    >"$DIR_W/log.txt" 2>&1 || RUN_W_OK=0

if [ "$RUN_W_OK" -eq 0 ]; then
    fail watch-attribution "smoke run failed -- see $DIR_W/log.txt"
    cat "$DIR_W/log.txt" >&2 || true
else
    WATCH_WR_SUM=$(awk -F, '
        NR==1 { for (i=1;i<=NF;i++) if ($i=="watch_wr") col=i; next }
        { s += $col } END { print s+0 }' "$DIR_W/field.csv")

    find_nonzero_pc() {
        # $1 = who; prints the first nonzero-pc WATCH_WR line for it, or
        # nothing.
        "$TRACE_DUMP" "$DIR_W/trace.vjtr" --type WATCH_WR --who "$1" \
            >"$WORK_DIR/watch_${1}.txt" 2>"$WORK_DIR/watch_${1}.err" || true
        awk '{
            for (i=1;i<=NF;i++) {
                if ($i ~ /^pc=/) {
                    split($i,a,"=")
                    if (a[2] != "0") { print; exit }
                }
            }
        }' "$WORK_DIR/watch_${1}.txt"
    }

    # M68K: must be present since Task 7.5 hooked the 68K bus fast path
    # directly (see the NOTE ON who=M68K above).
    M68K_LINE=$(find_nonzero_pc M68K)
    M68K_OK=0
    [ -n "$M68K_LINE" ] && M68K_OK=1

    # GPU/DSP: at least one of the two RISC processors must also show a
    # nonzero-pc record, proving the pre-existing JaguarWrite*-routed
    # attribution path (GPU/DSP local-store writes still bypass it -- see
    # the coverage note in src/core/vjtrace.h) still works.
    RISC_OK=0
    RISC_WHO=""
    RISC_LINE=""
    for who in GPU DSP; do
        LINE=$(find_nonzero_pc "$who")
        if [ -n "$LINE" ]; then
            RISC_OK=1
            RISC_WHO="$who"
            RISC_LINE="$LINE"
            break
        fi
    done

    if [ "$WATCH_WR_SUM" -gt 0 ] && [ "$M68K_OK" -eq 1 ] && [ "$RISC_OK" -eq 1 ]; then
        pass watch-attribution "watch_wr CSV sum=$WATCH_WR_SUM, who=M68K nonzero pc ($M68K_LINE), who=$RISC_WHO nonzero pc ($RISC_LINE)"
    else
        fail watch-attribution "watch_wr CSV sum=$WATCH_WR_SUM (want >0), who=M68K pc-bearing record found=$M68K_OK, who=GPU/DSP pc-bearing record found=$RISC_OK"
    fi
fi

# ==========================================================================
# (c) ring wrap
# ==========================================================================
DIR_R="$WORK_DIR/wrap"
mkdir -p "$DIR_R"

RUN_R_OK=1
VJ_TRACE_RING=1024 "$SMOKE" "$CORE" "$ROM" --frames 300 \
    --trace-out "$DIR_R/trace.vjtr" \
    >"$DIR_R/log.txt" 2>&1 || RUN_R_OK=0

if [ "$RUN_R_OK" -eq 0 ]; then
    fail ring-wrap "smoke run failed -- see $DIR_R/log.txt"
    cat "$DIR_R/log.txt" >&2 || true
else
    "$TRACE_DUMP" "$DIR_R/trace.vjtr" >"$DIR_R/dump.txt"
    COUNT=$(awk 'END{print NR+0}' "$DIR_R/dump.txt")
    SEQ_OK=$(awk '
        {
            split($1,a,"=")
            s = a[2] + 0
            if (NR > 1 && s <= prev) bad = 1
            prev = s
        }
        END { print (bad == 1) ? "no" : "yes" }' "$DIR_R/dump.txt")

    if [ "$COUNT" -eq 1024 ] && [ "$SEQ_OK" = "yes" ]; then
        pass ring-wrap "trace_dump reports exactly 1024 records, seq strictly increasing"
    else
        fail ring-wrap "record count=$COUNT (want 1024), seq strictly increasing=$SEQ_OK"
    fi
fi

# ==========================================================================
echo
if [ "$RESULT" -eq 0 ]; then
    echo "vjtrace_selftest: all checks PASS"
else
    echo "vjtrace_selftest: one or more checks FAILED" >&2
fi
exit "$RESULT"

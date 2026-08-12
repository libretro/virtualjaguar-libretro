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
#       For who=M68K specifically, "nonzero pc" alone is not a real test (a
#       constant wrong value would still be nonzero), so this check also
#       asserts two cheap sanity properties: the pc is EVEN (68K
#       instructions are always fetched from even addresses) and at least
#       two distinct WATCH_WR/M68K records share an IDENTICAL pc at
#       ADJACENT addresses (addr and addr+-2) -- the signature of a single
#       multi-word store (e.g. a MOVEM.L push) attributed to one
#       instruction. NEITHER of these discriminates the specific bug Task
#       7.5's post-review fix corrected -- a re-review caught this:
#       m68k_incpc() for a MOVEM handler runs ONCE, before its whole write
#       loop (src/m68000/cpuemu.c, op_4890_5 and friends), so every word of
#       one MOVEM.L push shares ONE constant regs.pc value under BOTH the
#       OLD buggy code (m68k_get_reg(M68K_REG_PC) at access time) and the
#       NEW fixed code (pcQueue[(pcQPtr-1)&(VJT_PCHIST_CAP-1)]) -- evenness
#       and adjacent-pc-pairing pass identically either way. They are kept
#       as cheap, generically-useful sanity checks (they DO catch e.g. an
#       uninitialized/garbage pc, or one processor's writes misattributed
#       to another), but are NOT the regression guard for this specific bug.
#
#       THE ACTUAL DISCRIMINATOR runs a second, short (30-frame) smoke pass
#       with TWO simultaneous watches: the usual RAM range for writes, PLUS
#       the cart ROM code range (0x800000:0xDFFEFF) for READS -- which
#       means every 68K instruction fetch (get_iword() in
#       src/m68000/inlines.h, called from the M68K_HOOK_FUNCTION call site
#       in src/m68000/m68kinterface.c right before each opcode dispatch)
#       now also produces a WATCH_RD/M68K record. That read hook exists for
#       exactly this reason (see the read-side hooks in
#       m68k_read_memory_8/16/32, src/core/jaguar.c) -- it is what makes a
#       black-box, no-new-C-tool discriminator possible at all: it lets
#       this script observe the REAL 68K fetch stream, not just the
#       ring's own pc metadata.
#
#       The discriminating fact: the address of the FIRST code-region READ
#       that follows a given WATCH_WR in seq order reflects a REAL 68K
#       fetch -- that fetch happens in the actual UAE core regardless of
#       what our own pc bookkeeping claims, so it is an INDEPENDENT ground
#       truth the recorded pc can be checked against. When execution falls
#       straight through from the writing instruction to the next one (no
#       intervening branch/JSR/interrupt), that next fetch address is
#       exactly pc+len of the writing instruction:
#         - Under the OLD buggy code, the recorded pc for a write IS
#           regs.pc read at access time, which m68k_incpc() has already
#           advanced to pc+len -- so for a straight-through write,
#           `write.pc == next_read.addr` holds by construction, not by
#           coincidence.
#         - Under the NEW fixed code, the recorded pc is the CURRENT
#           instruction's start address, which cannot equal pc+len (68K
#           instructions are at least 2 bytes; the MOVEM.L this ROM
#           exercises is 4) -- so `write.pc == next_read.addr` should not
#           hold for a straight-through write, and for a write whose very
#           next fetch is instead redirected by a branch/JSR/interrupt (this
#           ROM has plenty -- IRQ_DISPATCH events interleave with these
#           writes in the captured stream) it holds even less: the buggy
#           pc+len guess is now wrong too, since real execution didn't go
#           there either, so `write.pc == next_read.addr` cannot hold under
#           EITHER source in that case, straight-through or not, matching
#           is possible only under the bug.
#       Net effect: `write.pc == next_read.addr` is not expected to hold
#       for every resolved write even under the bug (control-flow
#       redirection is common in this ROM and defeats it exactly as it
#       would defeat the buggy pc), but it is a POSITIVE, purely
#       bug-driven signal whenever it does hold, and it must NEVER hold
#       under the fix.
#
#       As a companion positive-evidence check (not the discriminator, but
#       corroboration that pc names a real, already-fetched address rather
#       than nothing at all): pc must also equal the addr of SOME earlier
#       code-region read within a short lookback window -- true whenever
#       the recorded pc is a real 68K instruction-start address that this
#       run actually executed, which holds under both old and new code (an
#       address that was fetched "one instruction late" is still a real,
#       previously-fetched address), so this alone would not catch the
#       regression -- it is included only to guard against a DIFFERENT
#       failure mode (pc resolving to something that was never fetched at
#       all, e.g. garbage or a wrong array).
#
#       Verified empirically (not just by this argument) during Task 7.5's
#       second re-review round: temporarily reverting vjt_pc_of() to
#       `return m68k_get_reg(NULL, M68K_REG_PC);` and rebuilding made this
#       exact check FAIL (write.pc == next_read.addr held for 256 of 1200
#       resolved writes -- 0 is required); restoring the fix and rebuilding
#       made it PASS again (0 of 1200, same ROM, same watch config, same
#       1200-sample denominator -- only the pc source changed). Both raw
#       outputs are in
#       task-7.5-report.md.
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
    # directly (see the NOTE ON who=M68K above). find_nonzero_pc() also
    # leaves the FULL who=M68K WATCH_WR dump at $WORK_DIR/watch_M68K.txt,
    # which the stronger PC-attribution checks below reuse.
    M68K_LINE=$(find_nonzero_pc M68K)
    M68K_OK=0
    [ -n "$M68K_LINE" ] && M68K_OK=1

    # Stronger PC-attribution checks (post-review, see the long comment
    # on this check above): (1) every recorded pc must be EVEN -- 68K
    # instructions only ever start at even addresses, so an odd pc is
    # unambiguous evidence of a wrong-PC bug; (2) at least one pair of
    # CONSECUTIVE who=M68K WATCH_WR records (filtering to who=M68K
    # guarantees "consecutive in this file" also means "consecutive in
    # time for the 68K", since no other processor's record can appear
    # between two writes made by one atomic 68K instruction) must share
    # an identical pc with addr values 2 apart -- the multi-word-store
    # signature (e.g. MOVEM.L) that a correct instruction-start PC
    # produces.
    PC_ATTR_OK=0
    PC_ATTR_DETAIL=""
    if [ "$M68K_OK" -eq 1 ]; then
        PC_ATTR_RESULT=$(awk '
            function hex2dec(h,    i,c,v,idx) {
                v = 0
                for (i = 1; i <= length(h); i++) {
                    c = toupper(substr(h, i, 1))
                    idx = index("0123456789ABCDEF", c) - 1
                    v = v * 16 + idx
                }
                return v
            }
            {
                pc = ""; addr = ""
                for (i = 1; i <= NF; i++) {
                    if ($i ~ /^pc=/)   { split($i, a, "="); pc = a[2] }
                    if ($i ~ /^addr=/) { split($i, a, "="); addr = a[2] }
                }
                if (pc == "" || addr == "") next
                last = substr(pc, length(pc), 1)
                if (last !~ /[02468ACEace]/) {
                    oddcount++
                }
                d = hex2dec(addr)
                if (pc == prev_pc) {
                    diff = d - prev_addr
                    if (diff < 0) diff = -diff
                    if (diff == 2 && !found) {
                        found = 1
                        line1 = prevline
                        line2 = $0
                    }
                }
                prev_pc = pc; prev_addr = d; prevline = $0
            }
            END {
                print "ODD_COUNT=" (oddcount + 0)
                print "ADJPAIR_FOUND=" (found + 0)
                print "LINE1=" line1
                print "LINE2=" line2
            }
        ' "$WORK_DIR/watch_M68K.txt")

        ODD_COUNT=$(echo "$PC_ATTR_RESULT" | awk -F= '/^ODD_COUNT=/{print $2}')
        ADJPAIR_FOUND=$(echo "$PC_ATTR_RESULT" | awk -F= '/^ADJPAIR_FOUND=/{print $2}')
        ADJ_LINE1=$(echo "$PC_ATTR_RESULT" | grep '^LINE1=' | cut -d= -f2-)
        ADJ_LINE2=$(echo "$PC_ATTR_RESULT" | grep '^LINE2=' | cut -d= -f2-)

        if [ "${ODD_COUNT:-1}" -eq 0 ] && [ "${ADJPAIR_FOUND:-0}" -eq 1 ]; then
            PC_ATTR_OK=1
            PC_ATTR_DETAIL="all pc even, adjacent-pc pair: [$ADJ_LINE1] / [$ADJ_LINE2]"
        else
            PC_ATTR_DETAIL="odd-pc records=$ODD_COUNT (want 0), adjacent-pc pair found=$ADJPAIR_FOUND (want 1)"
        fi
    fi

    # THE ACTUAL DISCRIMINATOR (see the long comment at the top of this
    # check): a second, short smoke pass with a code-region READ watch
    # added, so the real 68K fetch stream is observable and can be
    # checked against the recorded pc as independent ground truth.
    DIR_W2="$WORK_DIR/watch_pc_discrim"
    mkdir -p "$DIR_W2"
    PC_DISCRIM_OK=0
    PC_DISCRIM_DETAIL=""
    RUN_W2_OK=1
    "$SMOKE" "$CORE" "$ROM" --frames 30 \
        --watch 0x0:0x200000:w --watch 0x800000:0xDFFEFF:r \
        --trace-out "$DIR_W2/trace.vjtr" \
        >"$DIR_W2/log.txt" 2>&1 || RUN_W2_OK=0

    if [ "$RUN_W2_OK" -eq 0 ]; then
        PC_DISCRIM_DETAIL="smoke run failed -- see $DIR_W2/log.txt"
        cat "$DIR_W2/log.txt" >&2 || true
    else
        "$TRACE_DUMP" "$DIR_W2/trace.vjtr" --who M68K >"$DIR_W2/m68k_all.txt"
        DISCRIM_RESULT=$(awk '
            {
                type = $4
                if (type != "WATCH_RD" && type != "WATCH_WR") next
                pc = ""; addr = ""
                for (i = 1; i <= NF; i++) {
                    if ($i ~ /^pc=/)   { split($i, a, "="); pc = a[2] }
                    if ($i ~ /^addr=/) { split($i, a, "="); addr = a[2] }
                }
                if (pc == "" || addr == "") next
                if (type == "WATCH_WR") {
                    pending_pc = pc
                    have_pending = 1
                } else {
                    if (have_pending) {
                        resolved++
                        if (addr == pending_pc) {
                            fwd_eq++
                            if (fwd_eq == 1) { ex_pc = pending_pc; ex_addr = addr }
                        }
                        have_pending = 0
                    }
                }
            }
            END {
                print "RESOLVED=" (resolved + 0)
                print "FWD_EQ=" (fwd_eq + 0)
                print "EX_PC=" ex_pc
                print "EX_ADDR=" ex_addr
            }
        ' "$DIR_W2/m68k_all.txt")

        DISCRIM_RESOLVED=$(echo "$DISCRIM_RESULT" | awk -F= '/^RESOLVED=/{print $2}')
        DISCRIM_FWD_EQ=$(echo "$DISCRIM_RESULT" | awk -F= '/^FWD_EQ=/{print $2}')

        # A near-zero resolved count means the discriminator watch config
        # itself didn't produce enough M68K read/write traffic to mean
        # anything -- that is a setup failure, not evidence of correctness,
        # so require a real sample size before trusting FWD_EQ==0.
        if [ "${DISCRIM_RESOLVED:-0}" -ge 100 ] && [ "${DISCRIM_FWD_EQ:-1}" -eq 0 ]; then
            PC_DISCRIM_OK=1
            PC_DISCRIM_DETAIL="resolved=$DISCRIM_RESOLVED, write.pc==next_read.addr count=$DISCRIM_FWD_EQ (want 0)"
        else
            PC_DISCRIM_DETAIL="resolved=$DISCRIM_RESOLVED (want >=100), write.pc==next_read.addr count=$DISCRIM_FWD_EQ (want 0 -- nonzero means the pc source regressed to m68k_get_reg(M68K_REG_PC))"
        fi
    fi

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

    if [ "$WATCH_WR_SUM" -gt 0 ] && [ "$M68K_OK" -eq 1 ] && [ "$PC_ATTR_OK" -eq 1 ] \
        && [ "$PC_DISCRIM_OK" -eq 1 ] && [ "$RISC_OK" -eq 1 ]; then
        pass watch-attribution "watch_wr CSV sum=$WATCH_WR_SUM, who=M68K nonzero pc ($M68K_LINE), $PC_ATTR_DETAIL, pc-discriminator: $PC_DISCRIM_DETAIL, who=$RISC_WHO nonzero pc ($RISC_LINE)"
    else
        fail watch-attribution "watch_wr CSV sum=$WATCH_WR_SUM (want >0), who=M68K pc-bearing record found=$M68K_OK, pc-attribution check=$PC_ATTR_OK ($PC_ATTR_DETAIL), pc-discriminator check=$PC_DISCRIM_OK ($PC_DISCRIM_DETAIL), who=GPU/DSP pc-bearing record found=$RISC_OK"
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

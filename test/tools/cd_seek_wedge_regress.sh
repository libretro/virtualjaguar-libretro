#!/usr/bin/env bash
# cd_seek_wedge_regress.sh -- issue #741 regression: bios-mode titles whose
# CD transfers legitimately finish must NOT log cd_seek_wedge.
#
# Before #741 the watchdog counted any frame with frozen FIFO drains, and
# fired on every one of these titles while they went on to run (15 fires,
# BUTCH low byte $02 each time: the game had cleared the master interrupt
# enable to signal completion).  Asserts on the log line itself, not the
# harness [PASS]: test_cd_bios_boot also reports [PASS] on a real
# IMASK-stuck wedge, so [PASS] proves nothing here.  The other side (a real
# wedge must still fire) is pinned without a disc by
# test/test_crash_detect_cd_wedge.
#
# Needs the private corpus; SKIPs (exit 0) without it.
#
# Usage: bash test/tools/cd_seek_wedge_regress.sh
#   VJ_TEST_CD_ROOT   corpus root (default test/roms/private)
#   CD_WEDGE_FRAMES   frames per title (default 3000; the latest pre-#741
#                     fire was frame 2716)
set -u

ROOT="${VJ_TEST_CD_ROOT:-test/roms/private}"
FRAMES="${CD_WEDGE_FRAMES:-3000}"
HARNESS=./test/test_cd_bios_boot

# Focus substrings for test_cd_bios_boot (VJ_TEST_CD_FOCUS).  Frog Feast
# was in the original report but stopped firing on develop before #741; it
# stays as a guard.  Philia surfaced while fixing it: it ends its transfer
# by clearing I2CNTRL bit 2 (FIFO data enable) rather than BUTCH bit 0.
TITLES=(
    "Baldies (USA) (Rev 1)"
    "Battle Morph"
    "BrainDead 13"
    "Highlander"
    "Primal Rage"
    "Myst (USA)"
    "Frog Feast"
    "Philia"
)

if [ ! -d "$ROOT" ]; then
    echo "SKIP: cd_seek_wedge_regress: no CD corpus at $ROOT"
    exit 0
fi
if [ ! -x "$HARNESS" ]; then
    echo "FAIL: $HARNESS not built (make TEST_EXPORTS=1 $HARNESS)" >&2
    exit 1
fi

fails=0
ran=0
for t in "${TITLES[@]}"; do
    log="$(DYLD_LIBRARY_PATH=. LD_LIBRARY_PATH=. \
        VJ_TEST_CD_ROOT="$ROOT" VJ_TEST_CD_FOCUS="$t" \
        VJ_TEST_CD_EXTS=cue VJ_TEST_CD_FRAMES="$FRAMES" \
        "$HARNESS" 2>&1)"
    rc=$?
    # [RUN] = the harness found the disc.  Only its absence is a SKIP; a
    # crash, a nonzero exit or a missing [PASS] after [RUN] is a FAIL, or a
    # broken image / dead harness would pass green without ever reaching
    # the false-positive window.
    if ! printf '%s\n' "$log" | grep -q '^ *\[RUN\]'; then
        echo "SKIP: $t (not in corpus)"
        continue
    fi
    ran=$((ran + 1))
    if [ "$rc" -ne 0 ] || ! printf '%s\n' "$log" | grep -q '^ *\[PASS\]'; then
        echo "FAIL: $t did not run to a [PASS] (exit $rc):" >&2
        printf '%s\n' "$log" | grep -E '^ *\[(PASS|FAIL|CRASH)\]' >&2
        fails=$((fails + 1))
        continue
    fi
    hits="$(printf '%s\n' "$log" | grep '\[CRASH-DETECT\] cd_seek_wedge')"
    if [ -n "$hits" ]; then
        echo "FAIL: $t logged cd_seek_wedge:" >&2
        printf '%s\n' "$hits" >&2
        fails=$((fails + 1))
    else
        echo "ok:   $t"
    fi
done

if [ "$ran" -eq 0 ]; then
    echo "SKIP: cd_seek_wedge_regress: none of the titles are in $ROOT"
    exit 0
fi
if [ "$fails" -ne 0 ]; then
    echo "cd_seek_wedge_regress: $fails of $ran title(s) logged a false cd_seek_wedge" >&2
    exit 1
fi
echo "cd_seek_wedge_regress: $ran title(s) clean"

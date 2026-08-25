#!/usr/bin/env bash
# Expand test/fixtures/dragons_lair_death_branch[_bios].press into
# fmv_seek_probe --press args and assert that a scene BRANCH actually fired.
#
#   bash test/tools/run_dl_branch_fixture.sh <core> [hle|bios] [extra args...]
#
# The gate is the CD trace ring, not pixels: a branch is a CD read whose LBA
# delta from the previous read exceeds BRANCH_MIN_DELTA (default 100000)
# sectors.  Steady-state streaming reads step 339-428 sectors and the
# attract-loop restart steps 5171, so the classes do not overlap.
#
# In HLE mode the event is HLE_READ; in BIOS mode it is SEEK_START.  The
# `seeks`/`seekstarts` CSV columns are incremented only by the BUTCH $12xx
# register path (src/cd/cdrom.c) which HLE bypasses, so they are structurally
# 0 in HLE regardless of whether a branch happened.  Do not gate HLE on them.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CORE=${1:?usage: run_dl_branch_fixture.sh <core> [hle|bios] [extra args...]}
shift || true
MODE=hle
case "${1:-}" in
  hle|bios) MODE=$1; shift ;;
esac

PROBE=${PROBE:-"$ROOT/test/tools/fmv_seek_probe"}
# Disc images nest inconsistently ("<Title>/<Title>/*.cue" after the iCloud
# restore, one level shallower before it), so resolve through find-rom.sh
# rather than pinning one layout.  An explicit DISC= still wins.
DISC=${DISC:-}
if [ -z "$DISC" ]; then
  DISC="$ROOT/test/roms/private/Jaguar CD/BinCue/Dragon's Lair (USA)/Dragon's Lair (USA).cue"
  if [ ! -f "$DISC" ]; then
    DISC=$( cd "$ROOT" && bash scripts/find-rom.sh \
              "Dragon's Lair (USA).cue" "Dragon's Lair*.cue" 2>/dev/null ) || DISC=""
    case "$DISC" in ""|/*) ;; *) DISC="$ROOT/$DISC" ;; esac
  fi
fi
SYSTEM_DIR=${SYSTEM_DIR:-"$ROOT/test/roms/private"}
BRANCH_MIN_DELTA=${BRANCH_MIN_DELTA:-100000}
OUTDIR=${OUTDIR:-/tmp/dl_branch_$$}

if [ "$MODE" = bios ]; then
  PRESS_FILE=${PRESS_FILE:-"$ROOT/test/fixtures/dragons_lair_death_branch_bios.press"}
  FRAMES=${FRAMES:-1400}
  MODE_ARGS=(--option virtualjaguar_cd_boot_mode=bios)
  EXPECT_MODE='mode=BIOS'
else
  PRESS_FILE=${PRESS_FILE:-"$ROOT/test/fixtures/dragons_lair_death_branch.press"}
  FRAMES=${FRAMES:-960}
  MODE_ARGS=()
  EXPECT_MODE='mode=HLE'
fi

if [ ! -x "$PROBE" ]; then
  echo "run_dl_branch_fixture: build the probe first:" >&2
  echo "  cc -O2 -Wall -std=c99 -I. -I./test -I./libretro-common/include \\" >&2
  echo "     -o test/tools/fmv_seek_probe test/tools/fmv_seek_probe.c \\" >&2
  echo "     test/harness/harness.c -ldl -lm" >&2
  exit 1
fi
if [ ! -r "$DISC" ]; then
  echo "run_dl_branch_fixture: disc image missing: $DISC" >&2
  exit 1
fi
if [ ! -r "$PRESS_FILE" ]; then
  echo "run_dl_branch_fixture: press fixture not readable: $PRESS_FILE" >&2
  exit 1
fi

press_args=()
while IFS= read -r line || [ -n "$line" ]; do
  case "$line" in
    ''|\#*) continue ;;
  esac
  press_args+=(--press "$line")
done < "$PRESS_FILE"

mkdir -p "$OUTDIR"
log="$OUTDIR/trace.log"

set +e
VJ_CD_TRACE=1 VJ_CD_TRACE_LIVE=1 VJ_HARNESS_LOG_INFO=1 \
FMV_CSV="$OUTDIR/fields.csv" \
  "$PROBE" "$CORE" "$DISC" \
  --frames "$FRAMES" \
  --system-dir "$SYSTEM_DIR" \
  "${MODE_ARGS[@]}" \
  "${press_args[@]}" \
  "$@" > "$log" 2>&1
rc=$?
set -e

# Boot mode must be confirmed from the core's own log line, never the flag.
if ! awk -v want="$EXPECT_MODE" 'index($0, "CD game, ") && index($0, want) {f=1} END{exit !f}' "$log"; then
  echo "run_dl_branch_fixture: FAIL -- core did not report $EXPECT_MODE" >&2
  if awk '/CD game, mode=/{f=1} END{exit !f}' "$log"; then
    awk '/CD game, mode=/' "$log" >&2
  else
    # No boot line at all: usually the VJ_EXPECT_BUILD guard rejecting a core
    # built at a different git rev, or a load failure.  Show the tail.
    echo "run_dl_branch_fixture: no [BOOT] line -- tail of $log:" >&2
    tail -12 "$log" >&2
  fi
  exit 1
fi

awk -v min="$BRANCH_MIN_DELTA" '
  /kind=(SEEK_START|HLE_READ)/ {
    split($2, a, "="); split($5, b, "=");
    tick = a[2] + 0; lba = b[2] + 0;
    if (have) {
      d = lba - prev; if (d < 0) d = -d;
      if (d >= min) {
        n++;
        printf "  BRANCH  field=%.1f  LBA %d -> %d  (delta %d)\n",
               tick / 524.0, prev, lba, lba - prev;
      }
    }
    prev = lba; have = 1;
  }
  END { print "branches=" n+0 > "/dev/stderr"; exit (n+0) ? 0 : 1 }
' "$log"
gate=$?

echo "run_dl_branch_fixture: mode=$MODE frames=$FRAMES log=$log"
if [ $gate -ne 0 ]; then
  echo "run_dl_branch_fixture: FAIL -- no branch-magnitude CD read (>= $BRANCH_MIN_DELTA sectors)" >&2
  exit 1
fi
if [ $rc -ne 0 ]; then
  echo "run_dl_branch_fixture: FAIL -- probe exited $rc" >&2
  exit 1
fi
echo "run_dl_branch_fixture: PASS"

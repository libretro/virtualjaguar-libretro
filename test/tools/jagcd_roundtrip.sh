#!/bin/sh
# Optional round-trip: synth CUE -> CHD (CHSE-capable chdman) -> jagcd-chd-check.
# Exit 77 if no suitable chdman.
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CHDMAN=${JAGCD_CHDMAN:-}
if [ -z "$CHDMAN" ]; then
   if [ -x "$ROOT/tools/jagcd/chdman" ]; then
      CHDMAN=$ROOT/tools/jagcd/chdman
   else
      CHDMAN=$(command -v chdman || true)
   fi
fi
[ -n "$CHDMAN" ] || { echo "SKIP: no chdman"; exit 77; }

TMP=$(mktemp -d /tmp/jagcd-roundtrip.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
dd if=/dev/zero of="$TMP/t1.bin" bs=2352 count=4 status=none 2>/dev/null || dd if=/dev/zero of="$TMP/t1.bin" bs=2352 count=4
dd if=/dev/zero of="$TMP/t2.bin" bs=2352 count=8 status=none 2>/dev/null || dd if=/dev/zero of="$TMP/t2.bin" bs=2352 count=8
printf '%s\n' \
  'REM SESSION 01' \
  'FILE "t1.bin" BINARY' \
  '  TRACK 01 AUDIO' \
  '    INDEX 01 00:00:00' \
  'REM SESSION 02' \
  'FILE "t2.bin" BINARY' \
  '  TRACK 02 AUDIO' \
  '    INDEX 01 00:00:00' > "$TMP/disc.cue"

export JAGCD_CHDMAN=$CHDMAN
set +e
out=$(sh "$ROOT/tools/jagcd/jagcd-to-chd" "$TMP/disc.cue" "$TMP/disc.chd" 2>&1)
rc=$?
set -e
if [ "$rc" -eq 0 ]; then
   echo "round-trip ok: $TMP/disc.chd"
   exit 0
fi
printf '%s\n' "$out"
case "$out" in
   *too\ old*|*did\ not\ write\ CHSE*)
      echo "SKIP: chdman could not write CHSE (too old)"
      exit 77
      ;;
esac
exit "$rc"

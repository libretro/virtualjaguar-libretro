#!/usr/bin/env bash
# Expand test/fixtures/avp_reach_gameplay.press into cd_visual_verify --press
# args and enforce a mechanical gameplay gate.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PRESS_FILE=${PRESS_FILE:-"$ROOT/test/fixtures/avp_reach_gameplay.press"}
VERIFY=${VERIFY:-"$ROOT/test/tools/cd_visual_verify"}
AVP=${AVP:-"$ROOT/test/roms/private/ROMS/Alien vs Predator (1994).jag"}
# cd_visual_verify defaults system_dir to the CWD-relative "test/roms/private",
# so pass an absolute one to keep this script runnable from anywhere.  It is
# placed before "$@" and the harness takes the last --system-dir it sees, so a
# caller-supplied --system-dir still wins.
SYSTEM_DIR=${SYSTEM_DIR:-"$ROOT/test/roms/private"}
FRAMES=${FRAMES:-3000}
OUTDIR=${OUTDIR:-/tmp/avp_fixture_$$}
CORE=${1:?usage: run_avp_fixture.sh <core> [extra args...]}
shift || true

if [ ! -f "$AVP" ]; then
  AVP=$(find -L "$ROOT/test/roms/private" -iname 'Alien vs Predator (1994).jag' | head -1 || true)
fi
if [ ! -f "$AVP" ]; then
  echo "run_avp_fixture: AvP ROM missing" >&2
  exit 1
fi
if [ ! -x "$VERIFY" ]; then
  echo "run_avp_fixture: build cd_visual_verify first" >&2
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
log="$OUTDIR/timeline.log"

set +e
"$VERIFY" "$CORE" "$AVP" \
  --frames "$FRAMES" \
  "${press_args[@]}" \
  --outdir "$OUTDIR" \
  --shot-every "${SHOT_EVERY:-0}" \
  --system-dir "$SYSTEM_DIR" \
  --quiet \
  "$@" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
set -e

# Gate: in windows covering the last 300 frames, require
#   sum(moving_frames) >= 40  (AvP first-person tops out ~12/60 per window)
#   max nonblack >= 5000
python3 - "$log" "$FRAMES" <<'PY'
import re, sys
log, frames = sys.argv[1], int(sys.argv[2])
start = frames - 300
wins = []
for line in open(log, encoding='utf-8', errors='replace'):
    m = re.search(
        r'\[win\s+(\d+)\] frames\s+(\d+)-\s*(\d+): motion\s+(\d+)/(\d+)'
        r'.*max nonblack\s+(\d+)/',
        line,
    )
    if m:
        wins.append(tuple(map(int, m.groups())))
if not wins:
    print('GATE FAIL: no timeline windows parsed', file=sys.stderr)
    sys.exit(1)
mov_sum = 0
max_nb = 0
for wid, a, b, mov, tot, nb in wins:
    if b < start:
        continue
    mov_sum += mov
    if nb > max_nb:
        max_nb = nb
    print(f'late win {wid} {a}-{b} motion={mov}/{tot} nonblack={nb}')
print(f'late_moving_sum={mov_sum} max_nonblack={max_nb}')
if mov_sum >= 40 and max_nb >= 5000:
    print('GATE PASS')
    sys.exit(0)
print('GATE FAIL', file=sys.stderr)
sys.exit(1)
PY
gate=$?
exit $(( rc != 0 ? rc : gate ))

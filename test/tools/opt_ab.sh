#!/usr/bin/env bash
# Interleaved A/B benchmark for compile-flag changes.
#
# Why this exists: a sequential A/B (all of arm A, then all of arm B) measures
# host drift, not your change.  Evaluating -O3 in issue #515, the sequential
# protocol reported +12.5% median / +40.5% best-run; re-running THE SAME TWO
# BINARIES interleaved gave -11.7% at p=0.55.  The effect reversed sign.
#
# This runs A/B/B/A so each arm occupies both the earliest and the latest slot
# of every quartet, cancelling linear drift, and reports a Mann-Whitney p so a
# result under the noise floor cannot be mistaken for a win.
#
# Usage:
#   test/tools/opt_ab.sh 'OPT_LEVEL=-O2' 'OPT_LEVEL=-O3' [quartets]
#   test/tools/opt_ab.sh 'FASTMATH=1'    'FASTMATH=0'    8
#
# Each argument is passed verbatim to make.  A full `make clean` runs between
# arms: BUILD_AXES does not cover every flag, so an incremental rebuild would
# silently mix objects (the #457 chimera class).
set -euo pipefail

ARM_A="${1:?usage: opt_ab.sh <make-args-A> <make-args-B> [quartets]}"
ARM_B="${2:?usage: opt_ab.sh <make-args-A> <make-args-B> [quartets]}"
QUARTETS="${3:-8}"
ROM="${BENCH_ROM:-test/roms/yarc.j64}"
FRAMES="${BENCH_FRAMES:-600}"

cd "$(dirname "$0")/../.."
[ -f "$ROM" ] || { echo "no ROM at $ROM (set BENCH_ROM)" >&2; exit 1; }

CORES=$(getconf _NPROCESSORS_ONLN)
LOAD=$(uptime | sed 's/.*load averages*: *//' | awk '{print int($1)}')
if [ "$LOAD" -gt "$CORES" ]; then
  echo "REFUSING: load average ${LOAD} exceeds ${CORES} cores." >&2
  echo "Any result would measure contention, not your change. Wait for the box to settle." >&2
  exit 2
fi

case "$(uname -s)" in
  Darwin) LIB=virtualjaguar_libretro.dylib ;;
  *)      LIB=virtualjaguar_libretro.so ;;
esac
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

build() {  # build <tag> <make-args>
  make clean >/dev/null 2>&1
  # shellcheck disable=SC2086
  make -j"$CORES" $2 >/dev/null 2>&1 || { echo "build failed: $2" >&2; exit 1; }
  cp "$LIB" "$WORK/$1.lib"
  printf '%-6s %s  %s\n' "$1" "$(shasum -a 256 "$WORK/$1.lib" | cut -c1-12)" "$2"
}
build a "$ARM_A"
build b "$ARM_B"

# Build the harness once. Do NOT drive this through `make benchmark`: it
# re-invokes make and can rebuild between reps, measuring a different binary
# than the one you think you built.
cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
   -o "$WORK/bench" test/tools/test_benchmark.c $([ "$(uname -s)" = Linux ] && echo -ldl) 2>/dev/null

run() { "$WORK/bench" "$WORK/$1.lib" "$ROM" "$FRAMES" --warmup 60 --blitter fast 2>/dev/null \
        | grep -oE 'Frames/sec:[[:space:]]+[0-9.]+' | grep -oE '[0-9.]+$'; }

: > "$WORK/a.txt"; : > "$WORK/b.txt"
for _ in $(seq 1 "$QUARTETS"); do
  run a >> "$WORK/a.txt"; run b >> "$WORK/b.txt"
  run b >> "$WORK/b.txt"; run a >> "$WORK/a.txt"
  printf '.'
done
echo

A="$ARM_A" B="$ARM_B" python3 - "$WORK/a.txt" "$WORK/b.txt" <<'PY'
import sys, os, math, statistics as st
L = lambda p: [float(x) for x in open(p) if x.strip()]
a, b = L(sys.argv[1]), L(sys.argv[2])
for name, v in ((os.environ['A'], a), (os.environ['B'], b)):
    print(f"{name:28} n={len(v):3d} min={min(v):7.1f} median={st.median(v):7.1f} max={max(v):7.1f}")
print(f"\nmedian delta B vs A: {(st.median(b)/st.median(a)-1)*100:+.1f}%"
      f"    best-run: {(max(b)/max(a)-1)*100:+.1f}%")
c = sorted([(v, 0) for v in a] + [(v, 1) for v in b]); R = [0.0, 0.0]; i = 0
while i < len(c):
    j = i
    while j + 1 < len(c) and c[j+1][0] == c[i][0]: j += 1
    for k in range(i, j + 1): R[c[k][1]] += (i + j) / 2 + 1
    i = j + 1
n1, n2 = len(a), len(b)
z = (R[0] - n1 * (n1 + 1) / 2 - n1 * n2 / 2) / math.sqrt(n1 * n2 * (n1 + n2 + 1) / 12)
p = math.erfc(abs(z) / math.sqrt(2))
print(f"Mann-Whitney: z={z:+.2f} p={p:.4f} -> "
      + ("B faster" if p < .05 and z < 0 else "A faster" if p < .05 else
         "NOT significant at p<0.05 -- under the noise floor, report no effect"))
PY

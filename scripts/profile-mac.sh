#!/usr/bin/env bash
#
# profile-mac.sh -- Run test_benchmark under Xcode Instruments on Apple Silicon
# (or any Mac with Xcode CLT).
#
# Usage:
#   scripts/profile-mac.sh [--template NAME] [--frames N] [--warmup N]
#                          [--blitter fast|accurate] [--rom PATH] [--open]
#
# Defaults:
#   template = "Time Profiler"
#   frames   = 600  warmup = 60  blitter = accurate
#   rom      = test/roms/yarc.j64
#   --open   = open the .trace bundle in Instruments when finished
#
# Common templates:
#   "Time Profiler"   -- where time is being spent (call tree / flame)
#   "CPU Counters"    -- Apple Silicon PMU (cycles, instr, branches, misses)
#   "System Trace"    -- syscalls, scheduler, VM events
#
set -euo pipefail

TEMPLATE="Time Profiler"
FRAMES=600
WARMUP=60
BLITTER=accurate
ROM="test/roms/yarc.j64"
OPEN_TRACE=0

while [ $# -gt 0 ]; do
   case "$1" in
      --template) TEMPLATE="$2"; shift 2 ;;
      --frames)   FRAMES="$2"; shift 2 ;;
      --warmup)   WARMUP="$2"; shift 2 ;;
      --blitter)  BLITTER="$2"; shift 2 ;;
      --rom)      ROM="$2"; shift 2 ;;
      --open)     OPEN_TRACE=1; shift ;;
      -h|--help)
         sed -n '2,20p' "$0"
         exit 0 ;;
      *)
         echo "Unknown arg: $1" >&2
         exit 2 ;;
   esac
done

if ! command -v xctrace >/dev/null 2>&1; then
   echo "xctrace not found. Install Xcode Command Line Tools: xcode-select --install" >&2
   exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p build
TRACE="build/profile-$(date +%Y%m%d-%H%M%S).trace"

# Make sure the core + harness are built (no BENCH_PROFILE; profiling
# instrumentation skews sampling results).
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
   -o test/tools/test_benchmark test/tools/test_benchmark.c

CORE="./virtualjaguar_libretro.dylib"
HARNESS="./test/tools/test_benchmark"

echo ">>> xctrace template:   $TEMPLATE"
echo ">>> trace output:       $TRACE"
echo ">>> rom / blitter:      $ROM / $BLITTER"
echo ">>> frames (+warmup):   $FRAMES (+$WARMUP)"

xctrace record \
   --template "$TEMPLATE" \
   --output "$TRACE" \
   --launch -- "$HARNESS" "$CORE" "$ROM" "$FRAMES" \
                          --warmup "$WARMUP" --blitter "$BLITTER"

echo ">>> trace written to $TRACE"
if [ "$OPEN_TRACE" = "1" ]; then
   open "$TRACE"
fi

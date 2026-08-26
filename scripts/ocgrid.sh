#!/bin/bash
# Overclock gate-1 grid: does a clock scale actually lift the INTERNAL frame rate?
# Measures framebuffer TRANSITIONS (hash != previous hash) from a gameplay savestate.
# Trap guard: transitions == window length means the counter SATURATED -> UNMEASURED.
set -u
CORE=./virtualjaguar_libretro.dylib
OUT=$1; shift
ROM=$1; shift
STATE=$1; shift
FRAMES=${FRAMES:-900}
TMP=$(mktemp -d)
printf '%-22s %-10s %s\n' "cell" "transit" "verdict"
base=""
for cell in "risc=1x,m68k=1x" "risc=1.5x,m68k=1x" "risc=2x,m68k=1x" \
            "risc=1x,m68k=1.5x" "risc=1.5x,m68k=1.5x" "risc=2x,m68k=1.5x"; do
  r=${cell#risc=}; r=${r%%,*}
  m=${cell##*m68k=}
  csv=$TMP/$(echo "$cell" | tr ',=' '__').csv
  ./test/tools/frame_hash_ab "$CORE" "$ROM" --csv "$csv" \
      --load-state "$STATE" --frames "$FRAMES" \
      --option "virtualjaguar_risc_clock_scale=$r" \
      --option "virtualjaguar_m68k_clock_scale=$m" >/dev/null 2>&1
  # count hash transitions
  t=$(awk -F, 'NR>1{if(prev!="" && $6!=prev) n++; prev=$6} END{print n+0}' "$csv")
  n=$(awk 'NR>1' "$csv" | wc -l | tr -d ' ')
  [ -z "$base" ] && base=$t
  if [ "$t" -ge "$((n-1))" ]; then v="SATURATED (unmeasured)"
  elif [ "$base" -eq 0 ]; then v="baseline 0 (bad state?)"
  else
    pct=$(awk -v a="$t" -v b="$base" 'BEGIN{printf "%+.1f%%", (a-b)*100.0/b}')
    v="$pct vs stock"
  fi
  printf '%-22s %-10s %s\n' "$cell" "$t/$n" "$v"
done
rm -rf "$TMP"

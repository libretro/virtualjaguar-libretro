#!/usr/bin/env bash
# Windowed check of the emulation thread (#967).  It OPENS A WINDOW: run it on the Mac Studio (vjr.sh run), never on
# a machine someone is using.  Interactive runs with --emu-thread on and off: each must measure 600 UI frames, show a
# new frame in at least 250 of them (a 60 fps game on a 120 Hz display gives one per two UI frames), log no owner
# violation, and (on) really start the thread.  Then a paused threaded run
# (the UI draws every tool provider while the emulation is parked) and a screenshot with the Settings window open (the
# option table read every frame) must log no owner violation either.
#
# usage: VJ_WINDOWED_OK=1 emu_thread_windowed_check.sh <virtualjaguar> <rom> <out-dir>
set -uo pipefail
[ "${VJ_WINDOWED_OK:-}" = 1 ] || { echo "refusing: this opens a window; set VJ_WINDOWED_OK=1 on the measurement machine"; exit 2; }
exe="$1"; rom="$2"; out="$3"
mkdir -p "$out"
fail=0
for mode in on off; do
  log="$out/windowed-$mode.log"
  csv="$out/windowed-$mode.csv"
  VJ_PERF_CSV="$csv" "$exe" --emu-thread "$mode" --perf-frames 600 --perf-input --show-window settings-interface "$rom" \
    >"$log" 2>&1 || { echo "FAIL $mode: exit status $?"; fail=1; }
  if grep -q "owner violation" "$log"; then echo "FAIL $mode: owner violation"; grep "owner violation" "$log"; fail=1; fi
  if [ "$mode" = on ] && ! grep -q "\[emu\] emulation thread on" "$log"; then
    echo "FAIL on: the emulation thread did not start"; fail=1
  fi
  rows=$(( $(wc -l < "$csv" 2>/dev/null || echo 1) - 1 ))
  [ "$rows" -ge 590 ] || { echo "FAIL $mode: $rows CSV rows, expected 600"; fail=1; }
  awk -F, 'NR > 1 && $2 > 0 { n++ } END { exit !(n > 250) }' "$csv" 2>/dev/null \
    || { echo "FAIL $mode: fewer than 250 of 600 UI frames showed a new emulated frame"; fail=1; }
  grep -E "^PERF|\[emu\]" "$log"
done
# Paused (at UI frame 30): the UI keeps drawing while the emulation thread is parked; nothing may reach into the core.
"$exe" --emu-thread on --perf-frames 300 --perf-pause --show-window settings "$rom" >"$out/windowed-paused.log" 2>&1 \
  || { echo "FAIL paused run: exit status $?"; fail=1; }
if grep -q "owner violation" "$out/windowed-paused.log"; then echo "FAIL paused run: owner violation"; fail=1; fi
"$exe" --emu-thread on --show-window settings --screenshot "$out/windowed-on.png" --screenshot-frame 240 "$rom" \
  >"$out/windowed-shot.log" 2>&1 || { echo "FAIL screenshot run: exit status $?"; fail=1; }
[ -s "$out/windowed-on.png" ] || { echo "FAIL: no screenshot written"; fail=1; }
if grep -q "owner violation" "$out/windowed-shot.log"; then echo "FAIL screenshot run: owner violation"; fail=1; fi
[ "$fail" = 0 ] && echo "emu_thread_windowed_check: OK"
exit "$fail"

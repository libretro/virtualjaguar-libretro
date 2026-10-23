#!/usr/bin/env bash
# The Phase 3 Studio gate (#967, tracking #1079): --emu-thread on with EVERY feature on at once -- all eight Tools windows,
# an F9 halt, a gdb halt, a Neuro client.  The window must keep presenting while halted, nothing may reach into the core
# from the UI thread, nothing is refused.  It OPENS WINDOWS: Mac Studio only (studio.sh sh), never the maintainer's Mac.
#   usage: VJ_WINDOWED_OK=1 emu_thread_gate_check.sh <virtualjaguar> <rom> <out-dir>
#   env:   GATE_MEDIAN_MS (17.5) GATE_P99_MS (25)  bounds on iter_ms while halted (60 Hz display; loosen for a TSan build)
#          GATE_WD_A (60) GATE_WD_B (75) GATE_WD_C (45)  per-run watchdog seconds
#   exit:  0 = OK, 1 = a check failed (a hang included), 3 = every check that ran passed but a run was skipped because the
#          display was asleep (an environment failure, not a hang: a window created on a sleeping display blocks forever in
#          the GL swap, threaded or inline -- soak-results.md in vj-eval/remote/emu-p3d)
set -uo pipefail
[ "${VJ_WINDOWED_OK:-}" = 1 ] || { echo "refusing: this opens a window; set VJ_WINDOWED_OK=1 on the measurement machine"; exit 2; }
exe="$1"; rom="$2"; out="$3"
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$out"; out="$(cd "$out" && pwd)"
MED="${GATE_MEDIAN_MS:-17.5}"; P99="${GATE_P99_MS:-25}"
WD_A="${GATE_WD_A:-60}"; WD_B="${GATE_WD_B:-75}"; WD_C="${GATE_WD_C:-45}"
fail=0; envfail=0
f() { echo "FAIL $1"; fail=1; }
ids="memory clut scanner cpu objects blitter textures scope"
free_port() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()'; }
scan() { [ -f "$1" ] && /usr/bin/grep -qE 'owner violation|ThreadSanitizer' "$1" && f "$2: $(/usr/bin/grep -m1 -E 'owner violation|ThreadSanitizer' "$1")"; }
# stats <csv> <first> <last> -> "median p99 zero nonzero n" of iter_ms / emu_frames over data rows first..last (1-based)
#   A missing or unreadable CSV (a killed run) is "-1 -1 0 0 0", never a traceback.
stats() { python3 - "$1" "$2" "$3" <<'PY'
import csv, sys
try:
    rows = list(csv.DictReader(open(sys.argv[1])))[int(sys.argv[2]) - 1:int(sys.argv[3])]
    ms = sorted(float(r["iter_ms"]) for r in rows)
    z = sum(1 for r in rows if int(r["emu_frames"]) == 0)
except (OSError, KeyError, ValueError):
    print("-1 -1 0 0 0")
    sys.exit(0)
print("%.2f %.2f %d %d %d" % (ms[len(ms) // 2] if ms else -1, ms[max(0, int(len(ms) * 0.99) - 1)] if ms else -1, z, len(rows) - z, len(rows)))
PY
}
longest_zero_run() { python3 - "$1" <<'PY'
import csv, sys
best = (0, 0, 0); cur = None
try:
    for i, r in enumerate(csv.DictReader(open(sys.argv[1])), 1):
        if int(r["emu_frames"]) == 0:
            cur = cur or i
            if i - cur + 1 > best[0]: best = (i - cur + 1, cur, i)
        else:
            cur = None
except (OSError, KeyError, ValueError):
    best = (0, 0, 0)
print(best[1], best[2])
PY
}
csv_rows() { local n=0; [ -f "$1" ] && n=$(( $(wc -l < "$1") - 1 )); [ "$n" -gt 0 ] && echo "$n" || echo 0; }   # data rows, 0 when missing
# watchdog <pid> <seconds> <flag-file>: kills the app at the deadline and leaves the flag file (a kill is a FAIL).  stop_wd ends it.
wd=0
watchdog() {
  ( sleep "$2"; if kill -0 "$1" 2>/dev/null; then : >"$3"; kill "$1" 2>/dev/null; sleep 2; kill -9 "$1" 2>/dev/null; fi ) >/dev/null 2>&1 &
  wd=$!
}
stop_wd() { [ "$wd" = 0 ] || { pkill -P "$wd" 2>/dev/null; kill "$wd" 2>/dev/null; wait "$wd" 2>/dev/null; }; wd=0; }
# thread_on <log> <run>: the emulation thread really started (a failed start silently runs inline)
thread_on() {
  /usr/bin/grep -q '\[emu\] emulation thread on' "$1" || f "$2: the emulation thread did not start"
  /usr/bin/grep -q 'could not start the emulation thread' "$1" && f "$2: could not start the emulation thread (ran inline)"
}
le() { python3 -c 'import sys
try: sys.exit(0 if float(sys.argv[1]) <= float(sys.argv[2]) else 1)
except (ValueError, IndexError): sys.exit(1)' "${1:-}" "${2:-}"; }
# uint <value> -> the value when it is a non-negative integer, else 0 (an empty read from a killed run must not reach [ -gt ])
uint() { case "${1:-}" in ''|*[!0-9]*) echo 0 ;; *) echo "$1" ;; esac; }

# ---- the display: a window created while it sleeps blocks forever in the GL swap (threaded and inline alike), so every run wakes it
# first, holds it awake for the run, and re-checks.  Still asleep = the run is SKIPPED and reported as an environment failure
# ("display asleep, run skipped"), which is not a hang and not an emulation-thread failure.  No caffeinate / system_profiler
# (Linux): nothing to do.
caf=0
display_asleep() { command -v system_profiler >/dev/null 2>&1 && system_profiler SPDisplaysDataType 2>/dev/null | /usr/bin/grep -q "Display Asleep: Yes"; }
release_display() { [ "$caf" = 0 ] || { kill "$caf" 2>/dev/null; { wait "$caf"; } 2>/dev/null; }; caf=0; }
# wake_display <run> <budget-seconds>: 0 = go, 1 = skip the run
wake_display() {
  command -v caffeinate >/dev/null 2>&1 || return 0
  caffeinate -u -t 3
  caffeinate -u -d -i -t "$2" >/dev/null 2>&1 & caf=$!
  if display_asleep; then
    echo "ENV $1: display asleep, run skipped (environment, not a hang: a window created on a sleeping display blocks in the GL swap)"
    envfail=$((envfail + 1)); release_display; return 1
  fi
  return 0
}
cleanup() { stop_wd; release_display; [ -z "${srv:-}" ] || kill "$srv" 2>/dev/null; }
trap cleanup EXIT

# ---- Run A: every tool open, F9's halt at CSV row ~120 and continue at ~420, a Neuro client pressing once a second ----
run_a() {
  wake_display A $((WD_A + 30)) || return 0
  local res="$out/neuro.txt" nport presses med p99 zero nonzero n
  command rm -f "$res" "$res.port"
  python3 "$here/neuro_gate_server.py" "$res" >"$out/neuro-server.log" 2>&1 & srv=$!
  for _ in $(seq 50); do [ -s "$res.port" ] && break; sleep 0.1; done
  [ -s "$res.port" ] || f "A: the Neuro server did not report its port"
  nport=$(cat "$res.port" 2>/dev/null || echo 9)
  command rm -rf "$out/data-a" "$out/a.killed"
  VJ_PERF_CSV="$out/a.csv" "$exe" --emu-thread on --perf-frames 720 --perf-input --perf-halt --show-window tools \
    --data-dir "$out/data-a" --neuro "ws://127.0.0.1:$nport" "$rom" >"$out/a.log" 2>&1 & app=$!
  watchdog "$app" "$WD_A" "$out/a.killed"
  { wait "$app"; } 2>/dev/null; rc=$?; stop_wd; release_display
  [ ! -e "$out/a.killed" ] || kill "$srv" 2>/dev/null     # a hung app never connects: do not wait out the server's 60 s accept
  { wait "$srv"; } 2>/dev/null; srv=
  scan "$out/a.log" A
  if [ -e "$out/a.killed" ]; then f "A: killed by the watchdog after $WD_A s (hung)"; return 0; fi
  [ "$rc" = 0 ] || f "A: exit status $rc"
  thread_on "$out/a.log" A
  for id in $ids; do
    /usr/bin/grep -q "^\[app\] tool $id open=1\$" "$out/a.log" || f "A: $id not open at start"
    /usr/bin/grep -q "^\[app\] tool $id open=1 (halted)\$" "$out/a.log" || f "A: $id not open while halted"
  done
  /usr/bin/grep -q '^\[perf\] halted=1$' "$out/a.log" || f "A: F9 did not halt the core ([perf] halted=1 missing)"
  /usr/bin/grep -q '^\[perf\] resumed=1$' "$out/a.log" || f "A: Continue did not resume a halted core ([perf] resumed=1 missing)"
  /usr/bin/grep -q 'halt unavailable' "$out/a.log" && f "A: halt unavailable (debug.enabled() false with a game loaded)"
  read -r med p99 zero nonzero n <<<"$(stats "$out/a.csv" 130 410)"
  zero=$(uint "$zero"); nonzero=$(uint "$nonzero"); n=$(uint "$n")
  echo "A halted rows 130-410: n=$n parked=$zero iter_ms median $med p99 $p99"
  [ "$n" -gt 0 ] || f "A: no CSV rows 130-410 (the run ended early)"
  [ "$nonzero" = 0 ] || f "A: $nonzero of $n halted rows ran an emulated frame (the halt did not hold)"
  le "$med" "$MED" || f "A: halted median iter_ms $med > $MED (the window is not presenting at display rate while halted)"
  le "$p99" "$P99" || f "A: halted p99 iter_ms $p99 > $P99"
  read -r _ _ _ nonzero n <<<"$(stats "$out/a.csv" 440 720)"
  nonzero=$(uint "$nonzero"); n=$(uint "$n")
  [ "$nonzero" -ge 200 ] || f "A: only $nonzero of $n rows after Continue ran a frame (expected >= 200 of 280)"
  presses=$(sed -n '1s/^[A-Za-z]* presses=\([0-9]*\).*$/\1/p' "$res" 2>/dev/null); presses=$(uint "$presses")   # ok or FAIL line: the count is what counts
  echo "A Neuro: $(head -1 "$res" 2>/dev/null || echo missing)"
  [ "$presses" -ge 5 ] || f "A: Neuro answered $presses presses (expected >= 5): $(tail -3 "$out/neuro-server.log" 2>/dev/null | tr '\n' ' ')"
}

# ---- Run B: gdb halts the boot (gdb_wait) with every tool open; the probe holds the halt 6 s, then continues ----
run_b() {
  wake_display B $((WD_B + 30)) || return 0
  local gport med p99 zs ze len rows nonzero n
  gport=$(free_port)
  command rm -rf "$out/data-b" "$out/b.killed"
  VJ_PERF_CSV="$out/b.csv" "$exe" --emu-thread on --perf-frames 900 --perf-input --show-window tools --data-dir "$out/data-b" \
    --option virtualjaguar_gdb_stub=enabled --option virtualjaguar_gdb_wait=enabled --option "virtualjaguar_gdb_port=$gport" \
    "$rom" >"$out/b.log" 2>&1 & app=$!
  watchdog "$app" "$WD_B" "$out/b.killed"      # probe (<= 10 s to connect + 6 s hold) + 25 s to finish + margin; SIGTERM alone can be ignored by a hung UI thread
  python3 "$here/emu_thread_gate_gdb.py" "$gport" 6.0 >"$out/b-probe.log" 2>&1 || f "B: gdb probe failed: $(tail -2 "$out/b-probe.log" | tr '\n' ' ')"
  for _ in $(seq 250); do kill -0 "$app" 2>/dev/null || break; sleep 0.1; done     # <= 25 s for the run to finish after the continue
  kill -0 "$app" 2>/dev/null && [ ! -e "$out/b.killed" ] && f "B: the app did not exit within 25 s of the probe"
  { wait "$app"; } 2>/dev/null; rc=$?; stop_wd; release_display
  scan "$out/b.log" B
  if [ -e "$out/b.killed" ]; then f "B: killed by the watchdog after $WD_B s (hung)"; return 0; fi
  [ "$rc" = 0 ] || f "B: exit status $rc"
  thread_on "$out/b.log" B
  for id in $ids; do /usr/bin/grep -q "^\[app\] tool $id open=1\$" "$out/b.log" || f "B: $id not open"; done
  read -r zs ze <<<"$(longest_zero_run "$out/b.csv")"
  zs=$(uint "$zs"); ze=$(uint "$ze")
  len=$(( ze - zs + 1 ))
  echo "B longest parked run: rows $zs-$ze ($len rows)"
  [ "$len" -ge 200 ] || f "B: longest parked run is $len rows (expected >= 200 for a 6 s hold at 60 Hz)"
  read -r med p99 _ _ n <<<"$(stats "$out/b.csv" $(( zs + 30 )) "$ze")"
  n=$(uint "$n")
  echo "B halted rows $(( zs + 30 ))-$ze: n=$n iter_ms median $med p99 $p99"
  [ "$n" -gt 0 ] || f "B: no halted rows to measure"
  le "$med" "$MED" || f "B: halted median iter_ms $med > $MED"
  le "$p99" "$P99" || f "B: halted p99 iter_ms $p99 > $P99"
  rows=$(csv_rows "$out/b.csv")
  read -r _ _ _ nonzero _ <<<"$(stats "$out/b.csv" $(( ze + 1 )) "$rows")"
  nonzero=$(uint "$nonzero")
  [ "$nonzero" -ge 100 ] || f "B: only $nonzero rows ran a frame after the gdb continue (expected >= 100)"
}

# ---- Run C: the plain default path, every tool open, nothing halted ----
run_c() {
  wake_display C $((WD_C + 30)) || return 0
  local rows nonzero n
  command rm -rf "$out/data-c" "$out/c.killed"
  VJ_PERF_CSV="$out/c.csv" "$exe" --emu-thread on --perf-frames 600 --perf-input --show-window tools --data-dir "$out/data-c" "$rom" >"$out/c.log" 2>&1 & app=$!
  watchdog "$app" "$WD_C" "$out/c.killed"
  { wait "$app"; } 2>/dev/null; rc=$?; stop_wd; release_display
  scan "$out/c.log" C
  if [ -e "$out/c.killed" ]; then f "C: killed by the watchdog after $WD_C s (hung)"; return 0; fi
  [ "$rc" = 0 ] || f "C: exit status $rc"
  thread_on "$out/c.log" C
  rows=$(csv_rows "$out/c.csv")
  read -r _ _ _ nonzero n <<<"$(stats "$out/c.csv" 1 "$rows")"
  nonzero=$(uint "$nonzero"); n=$(uint "$n")
  [ "$nonzero" -ge 250 ] || f "C: only $nonzero of $n UI frames showed an emulated frame (expected >= 250 of 600)"
  /usr/bin/grep -E '^PERF|\[emu\]' "$out/c.log"
}

run_a
run_b
run_c
if [ "$fail" != 0 ]; then
  [ "$envfail" = 0 ] || echo "emu_thread_gate_check: $envfail run(s) also skipped (display asleep)"
  exit 1
fi
if [ "$envfail" != 0 ]; then
  echo "emu_thread_gate_check: INCOMPLETE -- $envfail run(s) skipped, display asleep (environment failure; re-run with the display awake)"
  exit 3
fi
echo "emu_thread_gate_check: OK"
exit 0

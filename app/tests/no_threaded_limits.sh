#!/usr/bin/env bash
# The Phase 3 bar (#967; tracking #1079): with Settings > Interface > Emulation thread on, NOTHING is switched off.
#   usage: no_threaded_limits.sh <repo-root>          (ctest: no_threaded_limits, label emu_thread)
# 1 the old gates and their symbols are gone              2 the user-facing "off while threaded" texts are gone
# 3 the old limit tests are gone and their replacements exist   4 the GDB stub is locked to "disabled" by hardcore only
# 5 only listed files ask "am I threaded?" (threaded_branch_allowlist.txt: every hit picks HOW, never WHETHER)
# 6 tsan.supp did not grow
set -u
root="${1:?usage: no_threaded_limits.sh <repo-root>}"
cd "$root" || exit 2
G="${GREP:-/usr/bin/grep}"; [ -x "$G" ] || G=grep      # never a shell wrapper (one here drops matches on big files)
fail=0
bad() { echo "FAIL no_threaded_limits: $1"; shift; [ $# -eq 0 ] || printf '    %s\n' "$@"; fail=1; }
X=(--exclude=no_threaded_limits.sh --exclude=threaded_branch_allowlist.txt)

# 1  the gates
h=$("$G" -rnE 'limitFeaturesForEmuThread|liftFeatureLimits|setThreadBlocked|threadBlocked|setHaltAvailable|haltAvailable\(|haltDisabledReason' app/src app/tests app/cmake "${X[@]}")
[ -z "$h" ] || bad "a removed gate is referenced (delete it):" "$h"

# 2  the texts.  A FAIL_REGULAR_EXPRESSION line in the test cmake is a test that FORBIDS the text, not the text.
docs=(); for d in app/README.md docs/neuro.md docs/app-debug-tools.md; do [ -f "$d" ] && docs+=("$d"); done
h=$("$G" -rnE 'are off while the emulation thread is on|back on \(the emulation thread did not start\)|off for this run: the emulation thread|[Oo]ff while the emulation thread is on|[Nn]ot available while Settings > Interface > Emulation thread|this version (turns|switches) off the Tools|Halting is not available with the emulation thread' app/src app/tests app/cmake "${docs[@]}" "${X[@]}" | "$G" -v 'FAIL_REGULAR_EXPRESSION')
[ -z "$h" ] || bad "an 'off while threaded' text survives:" "$h"

# 3  the limit tests, and what 3a (A9), 3b (B4, B5) and 3c (C4) registered in their place (literal add_test lines; two are
#    registered in a foreach over _m, so the literal text is the one with ${_m})
h=$("$G" -rnE 'emu_thread_limits|gdb_locked_threaded|neuro_off_threaded' app/cmake app/tests docs/agent docs/neuro.md "${X[@]}" 2>/dev/null)
[ -z "$h" ] || bad "a limit test is still named (delete or rename it):" "$h"
for t in debug_views_threaded debug_e2e_threaded gdb_threaded_halt debug_on_threaded gdb_enabled_threaded \
         'gdb_hardcore_locked_${_m}' 'gdb_hardcore_flip_${_m}' neuro_on_threaded neuro_loopback_threaded neuro_watch_threaded; do
  "$G" -rqF "add_test(NAME $t" app/cmake || bad "replacement test not registered: $t"
done

# 4  the GDB stub option is locked to "disabled" by RetroAchievements hardcore only (Decision 2), never by the thread
while IFS=: read -r f n _; do
  lo=$(( n > 4 ? n - 4 : 1 )); w=$(sed -n "${lo},$((n + 4))p" "$f")
  printf '%s\n' "$w" | "$G" -qE '=[[:space:]]*"disabled"' || continue
  printf '%s\n' "$w" | "$G" -qi 'hardcore' || bad "$f:$n locks virtualjaguar_gdb_stub to disabled for a reason other than hardcore:" "$w"
  ! printf '%s\n' "$w" | "$G" -qE 'threaded\(\)|emuThreadThisRun' || bad "$f:$n locks virtualjaguar_gdb_stub by the thread:" "$w"
done < <("$G" -rn 'virtualjaguar_gdb_stub' app/src)

# 5  who asks "am I threaded?"
P='emuThreadThisRun|(\.|->)threaded\(\)'
A=app/tests/threaded_branch_allowlist.txt
while read -r n f; do
  a=$(awk -v f="$f" '$1 == f && $1 !~ /^#/ { print $2 }' "$A"); a=${a:-0}
  if [ "$n" -gt "$a" ]; then
    bad "$f asks 'am I threaded?' $n times, allowed $a (a feature must not be disabled by it; if it only picks HOW, raise the count with a reason):" \
        "$("$G" -nE "$P" "$f")"
  elif [ "$n" -lt "$a" ]; then echo "note: $f allows $a but has $n; tighten the allowlist"; fi
done < <("$G" -rcE "$P" app/src --exclude='emu_thread.*' | awk -F: '$NF > 0 { print $NF, $1 }')

# 6  tsan.supp
n=$(awk '!/^[[:space:]]*(#|$)/' app/tests/tsan.supp | wc -l | tr -d ' ')
[ "$n" -le 1 ] || bad "app/tests/tsan.supp grew to $n entries (route the reader through a published value instead)"

[ "$fail" = 0 ] && echo "no_threaded_limits: OK"
exit "$fail"

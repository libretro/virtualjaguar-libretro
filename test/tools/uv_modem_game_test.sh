#!/usr/bin/env bash
# uv_modem_game_test.sh — full-game Ultra Vortek Voice Modem netplay test.
#
# Boots two core instances with the retail Ultra Vortek ROM, types the
# 911 modem code on both title screens, has one side pick ANSWER PHONE
# and the other DIAL OPPONENT (number "1"), and asserts that both sides
# get past the whole modem choreography into the in-game lockstep data
# phase (pad packets as $F0xx words with $F301 framing, visible in the
# VJ_VM_TRACE log).
#
# Usage: uv_modem_game_test.sh <core> [port]
# Exit codes: 0 pass, 1 fail, 77 = private ROM absent (skip).
# Runtime: ~95 s (two --realtime instances of 5400 frames).
set -u

CORE="${1:?usage: uv_modem_game_test.sh <core> [port]}"
PORT="${2:-$(( 25000 + ($$ % 4000) ))}"
# Locate the ROM through find-rom.sh rather than one hardcoded spelling:
# the corpus holds BOTH a top-level "Ultra Vortek (1995).jag" and a deeper
# copy under "Atari Jaguar Rom Collection/", and a retail-vs-beta pair.
# Hardcoding one path is how the Skyhammer sentinel went inert -- see the
# header of scripts/find-rom.sh.  Exact retail spelling first so the Beta
# is only ever a last resort; VJ_UV_ROM overrides everything.
ROM="${VJ_UV_ROM:-$(bash "$(dirname "$0")/../../scripts/find-rom.sh" \
    'Ultra Vortek (1995).jag' 'Ultra Vortek (1995).*' 'Ultra Vortek*.jag')}"
BIN="$(dirname "$0")/netlink_game"
OUT="${TMPDIR:-/tmp}/uv_modem_game_$$"

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "uv_modem_game_test: SKIP (private ROM absent)" >&2
    exit 77
fi
if [ ! -x "$BIN" ]; then
    echo "uv_modem_game_test: $BIN not built" >&2
    exit 1
fi
# A dylib passes -x but dies at exec with "cannot execute binary file".
# That happens for real: the test-tool link rules live inside the
# TEST_EXPORTS=1 branch of the Makefile, so `make test/tools/netlink_game`
# without it silently falls through to make's built-in %:%.c rule and
# links a shared library (LDFLAGS carries -dynamiclib/-shared).  Fail with
# the fix instead of two bogus "sent 0 data words" lines.
case "$(file -b "$BIN" 2>/dev/null)" in
    *"shared library"*|*dylib*|*"dynamically linked library"*)
        echo "uv_modem_game_test: $BIN is a shared library, not an executable." >&2
        echo "  Rebuild it with:  make TEST_EXPORTS=1 $BIN" >&2
        exit 1
        ;;
esac
mkdir -p "$OUT"

MAPOPTS=(--option virtualjaguar_uart_device=voicemodem
  --option virtualjaguar_alt_inputs=enabled
  --option virtualjaguar_p1_retropad_up=up
  --option virtualjaguar_p1_retropad_down=down
  --option virtualjaguar_p1_retropad_left=left
  --option virtualjaguar_p1_retropad_right=right
  --option virtualjaguar_p1_retropad_a=btn_a
  --option virtualjaguar_p1_retropad_b=btn_b
  --option virtualjaguar_p1_retropad_y=btn_c
  --option virtualjaguar_p1_retropad_x=num_0
  --option virtualjaguar_p1_retropad_l1=num_9
  --option virtualjaguar_p1_retropad_r1=num_1)

# Wire-speed enhancement (#498), per side, so this script doubles as the
# safety gate for it: the whole point of the feature is that it may not
# change anything the GAME can see, and the modem choreography plus the
# pad-word counts below are the only end-to-end check that holds.
# "disabled" is the option's own default, so an unset run (the one
# `make test` actually exercises) is the stock baseline this has always
# measured -- these two env vars are for a human to override manually.
#
# #552 replaced the option's 2x/4x values with a negotiated "auto":
# VJ_UV_SPEED_SERVER/CLIENT=auto sets real intent on that side, but
# whether it actually accelerates now depends on jlink.c's discovery-port
# negotiation confirming with the peer -- which, for two real processes
# on ONE machine sharing 127.0.0.1's discovery port, is not reliable (see
# the SO_REUSEPORT hazard documented on jlink.c's JLinkNegEligible()).
# There is no longer a numeric value this script can pass to force a
# specific side's timing the way "2"/"4" used to -- the old "one side
# faster, does the choreography still complete" question is what #552's
# negotiation is designed to make structurally impossible to create by
# accident in the first place, not a case to keep exercising here.
SPEED_SRV="${VJ_UV_SPEED_SERVER:-disabled}"
SPEED_CLI="${VJ_UV_SPEED_CLIENT:-disabled}"

# netlink_game putenv()s VJ_NETLINK_PORT itself from --port (default
# 42171), overriding any inherited value -- the port MUST go on the
# command line or concurrent pairs cross-connect on the default.
export VJ_VM_TRACE=1

"$BIN" "$CORE" "$ROM" --role server --port "$PORT" --frames 5400 --realtime \
  "${MAPOPTS[@]}" --option virtualjaguar_netlink_speed="$SPEED_SRV" \
  --press 905:1:6 --press 918:2:6 --press 931:2:6 \
  --press 1250:up:6 --press 1290:up:6 --press 1360:a:6 \
  > "$OUT/answer.log" 2>&1 &
SRV=$!
sleep 2
"$BIN" "$CORE" "$ROM" --role client --port "$PORT" --frames 5400 --realtime \
  "${MAPOPTS[@]}" --option virtualjaguar_netlink_speed="$SPEED_CLI" \
  --press 905:1:6 --press 918:2:6 --press 931:2:6 \
  --press 1250:up:6 --press 1350:a:6 --press 1450:2:6 --press 1530:a:6 \
  > "$OUT/dial.log" 2>&1 &
CLI=$!
wait "$SRV"
wait "$CLI"

rc=0
for side in answer dial; do
    n=$(grep -c "cmd F0" "$OUT/$side.log" || true)
    if [ "${n:-0}" -lt 10 ]; then
        echo "uv_modem_game_test: FAIL ($side sent only ${n:-0} data words;" \
             "log: $OUT/$side.log)" >&2
        rc=1
    else
        echo "uv_modem_game_test: $side side in data phase ($n pad words sent)"
    fi
done
if [ "$rc" -eq 0 ]; then
    echo "uv_modem_game_test: PASS (port $PORT, wire speed" \
         "server=$SPEED_SRV client=$SPEED_CLI)"
    command rm -rf "$OUT"
fi
exit "$rc"

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
ROM="test/roms/private/ROMS/Atari Jaguar Rom Collection/ROMS/Ultra Vortek (1995).jag"
BIN="$(dirname "$0")/netlink_game"
OUT="${TMPDIR:-/tmp}/uv_modem_game_$$"

if [ ! -f "$ROM" ]; then
    echo "uv_modem_game_test: SKIP (private ROM absent)" >&2
    exit 77
fi
if [ ! -x "$BIN" ]; then
    echo "uv_modem_game_test: $BIN not built" >&2
    exit 1
fi
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

# netlink_game putenv()s VJ_NETLINK_PORT itself from --port (default
# 42171), overriding any inherited value -- the port MUST go on the
# command line or concurrent pairs cross-connect on the default.
export VJ_VM_TRACE=1

"$BIN" "$CORE" "$ROM" --role server --port "$PORT" --frames 5400 --realtime \
  "${MAPOPTS[@]}" \
  --press 905:1:6 --press 918:2:6 --press 931:2:6 \
  --press 1250:up:6 --press 1290:up:6 --press 1360:a:6 \
  > "$OUT/answer.log" 2>&1 &
SRV=$!
sleep 2
"$BIN" "$CORE" "$ROM" --role client --port "$PORT" --frames 5400 --realtime \
  "${MAPOPTS[@]}" \
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
    echo "uv_modem_game_test: PASS (port $PORT)"
    command rm -rf "$OUT"
fi
exit "$rc"

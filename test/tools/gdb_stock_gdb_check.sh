#!/bin/sh
# gdb_stock_gdb_check.sh -- drives a REAL, stock gdb (built with m68k) against a
# headless core with NO workaround settings (issue #936). Skips (exit 0) when
# no m68k-capable gdb is installed (Homebrew `gdb` and Debian `gdb-multiarch`
# have it; plain Debian `gdb` does not).
#
#   make && cc ... test/tools/gdb_determinism_probe  (build line in its header)
#   sh test/tools/gdb_stock_gdb_check.sh
#
# Asserts: target remote succeeds with only `set endian big`; registers and
# memory read right; a breakpoint hits; a watchpoint triggers; the long
# monitor reply arrives whole; detach leaves the emulator running and a second
# `target remote` reconnects.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PORT=22355
GDB=
for c in gdb-multiarch gdb; do
  if command -v "$c" >/dev/null 2>&1 &&
     "$c" -nx -batch -ex 'set architecture m68k' 2>&1 | grep -q 'set to "m68k"'; then
    GDB=$c; break
  fi
done
[ -n "$GDB" ] || { echo "SKIP: no gdb with m68k support"; exit 0; }
case $(uname) in Darwin) CORE=virtualjaguar_libretro.dylib ;; *) CORE=virtualjaguar_libretro.so ;; esac
[ -f "$ROOT/$CORE" ] && [ -x "$ROOT/test/tools/gdb_determinism_probe" ] || { echo "SKIP: build the core and gdb_determinism_probe first"; exit 0; }

TMP=$(mktemp -d)
cat > "$TMP/s.gdb" <<GDBEOF
set pagination off
set endian big
target remote 127.0.0.1:$PORT
info registers pc
x/2i \$pc
break *0x802008
continue
info registers pc
delete
watch *(int*)0xf0210c
continue
info registers pc
delete
monitor regs gpu
monitor trace 100
detach
target remote 127.0.0.1:$PORT
info registers pc
disconnect
GDBEOF

"$ROOT/test/tools/gdb_determinism_probe" "$ROOT/$CORE" "$ROOT/test/roms/yarc.j64" --frames 100000 \
  --option virtualjaguar_gdb_stub=enabled --option virtualjaguar_gdb_port=$PORT \
  --option virtualjaguar_gdb_wait=enabled >"$TMP/host.log" 2>&1 &
HOST=$!
sleep 3
perl -e 'alarm 90; exec @ARGV' "$GDB" -nx -batch -x "$TMP/s.gdb" >"$TMP/out.txt" 2>&1
RUNNING=0; kill -0 $HOST 2>/dev/null && RUNNING=1
kill $HOST 2>/dev/null; wait $HOST 2>/dev/null

fail=0
need() { grep -q -- "$1" "$TMP/out.txt" || { echo "FAIL: missing: $1"; fail=1; }; }
need 'pc             0x802000'
need 'movew #9984,%sr'
need 'Breakpoint 1, 0x00802008'
need 'SIGTRAP'
need 'GPU PC='
need 'r31=\$'
need 'oldest first'
need 'Remote target) detached'
[ "$RUNNING" = 1 ] || { echo "FAIL: emulator stopped after detach"; fail=1; }
grep -q 'conflicting enabled responses\|too long\|doesn.t know how to detach' "$TMP/out.txt" &&
  { echo "FAIL: known #936 error present"; fail=1; }
[ $fail = 0 ] && echo "PASS: stock $GDB works out of the box (with set endian big)" || sed 's/^/  | /' "$TMP/out.txt"
trash "$TMP" 2>/dev/null || command rm -rf "$TMP"
exit $fail

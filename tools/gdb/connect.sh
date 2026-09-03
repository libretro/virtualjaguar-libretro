#!/bin/sh
#
# tools/gdb/connect.sh -- launch a real gdb against a running Virtual
# Jaguar core's GDB remote stub (issue #652).
#
#   https://github.com/libretro/virtualjaguar-libretro
#
# docs/gdb-stub-guide.md is the full reference for what is on the other
# end of the socket; this script only launches your debugger at it and
# loads tools/gdb/jaguar.gdbinit (settings + convenience aliases) and
# tools/gdb/jaguar_gdb.py (richer `jag-*` commands, if your gdb has
# Python).
#
# POSIX sh.
#
# Usage:
#   tools/gdb/connect.sh                       127.0.0.1:2345 (the core's default)
#   tools/gdb/connect.sh --port 3333
#   tools/gdb/connect.sh --host 127.0.0.1 --port 2346
#   tools/gdb/connect.sh --gdb /path/to/m68k-elf-gdb
#   tools/gdb/connect.sh --no-python
#   tools/gdb/connect.sh -- -tui               extra args are passed to gdb verbatim
#
# THIS PROJECT SHIPS NO GDB. tools/jaguar-toolchain pins exactly five
# tools -- rmac, rln, lyxass, pc_jagcrypt, new_bjl -- and none of them is
# a debugger. Bring your own `gdb`. A stock host build talks to the
# GPU/DSP threads (2/3) just fine; only the 68K thread's (1) native
# disassembly and register names need a gdb built with m68k support,
# which most distro/Homebrew gdb builds do NOT include -- see
# docs/gdb-stub-guide.md, "No shipped m68k-elf-gdb".
#
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
GDBINIT="$SCRIPT_DIR/jaguar.gdbinit"
PYPLUGIN="$SCRIPT_DIR/jaguar_gdb.py"

HOST=127.0.0.1
PORT=2345
GDB_BIN="${GDB:-}"
USE_PYTHON=1

die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
note() { printf '%s\n' "$*" >&2; }

usage() {
  cat <<'USAGE'
Usage: tools/gdb/connect.sh [options] [-- extra gdb args]

  --host HOST       stub host (default: 127.0.0.1 -- the stub never
                     listens on anything else, see the design doc)
  --port PORT       stub port (default: 2345, matching the core's
                     virtualjaguar_gdb_port default)
  --gdb PATH        use this gdb binary instead of auto-detecting one
                     (or set $GDB)
  --no-python       skip loading tools/gdb/jaguar_gdb.py
  -h, --help        this text

This project ships no gdb -- see the comment block at the top of this
script, or docs/gdb-stub-guide.md, "No shipped m68k-elf-gdb".
USAGE
  exit 0
}

while [ $# -gt 0 ]; do
  case "$1" in
    --host)       HOST="${2:-}"; [ -n "$HOST" ] || die "--host needs a value"; shift 2 ;;
    --port)       PORT="${2:-}"; [ -n "$PORT" ] || die "--port needs a value"; shift 2 ;;
    --gdb)        GDB_BIN="${2:-}"; [ -n "$GDB_BIN" ] || die "--gdb needs a path"; shift 2 ;;
    --no-python)  USE_PYTHON=0; shift ;;
    -h|--help)    usage ;;
    --)           shift; break ;;
    *)            break ;;
  esac
done

[ -f "$GDBINIT" ] || die "missing $GDBINIT -- run this from a checkout of the repo."

# --------------------------------------------------------------------
# find a gdb
# --------------------------------------------------------------------
GDB_EXPLICIT=0
[ -n "$GDB_BIN" ] && GDB_EXPLICIT=1

if [ -z "$GDB_BIN" ]; then
  for _c in m68k-elf-gdb m68k-unknown-elf-gdb gdb-multiarch gdb; do
    if command -v "$_c" >/dev/null 2>&1; then GDB_BIN="$_c"; break; fi
  done
fi

if [ -n "$GDB_BIN" ] && [ "$GDB_EXPLICIT" -eq 1 ] && ! command -v "$GDB_BIN" >/dev/null 2>&1; then
  die "'$GDB_BIN' (from --gdb or \$GDB) does not exist or is not executable."
fi

if [ -z "$GDB_BIN" ] || ! command -v "$GDB_BIN" >/dev/null 2>&1; then
  cat >&2 <<EOF
ERROR: no gdb found.

This project's toolchain ships no debugger (rmac/rln/lyxass/pc_jagcrypt/
new_bjl only) -- you bring your own. Install one, e.g.:

  macOS (Homebrew):   brew install gdb
  Debian/Ubuntu:      sudo apt install gdb-multiarch
  Fedora:             sudo dnf install gdb

Or point this script at one you already have:

  tools/gdb/connect.sh --gdb /path/to/gdb
  GDB=/path/to/gdb tools/gdb/connect.sh

A stock gdb works fine for the GPU/DSP threads. Only the 68K thread's
native disassembly needs a gdb built with m68k support, which most of
the above do NOT include by default -- see docs/gdb-stub-guide.md,
"No shipped m68k-elf-gdb".

On macOS, a Homebrew gdb also needs its own code-signing step before it
can attach to anything -- see Homebrew's own post-install caveats
(\`brew info gdb\`) if it fails to attach even after this script finds it.
EOF
  exit 1
fi

if ! "$GDB_BIN" --version >/dev/null 2>&1; then
  die "'$GDB_BIN' does not run (tried '$GDB_BIN --version')."
fi

note "Connecting: $GDB_BIN -> $HOST:$PORT"
note "(a halt on the other end freezes the whole frontend -- see docs/gdb-stub-guide.md if that surprises you)"

if [ "$USE_PYTHON" -eq 1 ] && [ -f "$PYPLUGIN" ]; then
  exec "$GDB_BIN" -q -x "$GDBINIT" -x "$PYPLUGIN" -ex "jconnect $HOST $PORT" "$@"
else
  exec "$GDB_BIN" -q -x "$GDBINIT" -ex "jconnect $HOST $PORT" "$@"
fi

#!/bin/sh
#
# tools/gdb/install.sh -- install the Virtual Jaguar GDB/LLDB convenience
# tooling into your personal ~/.gdbinit and ~/.lldbinit, so it is
# available in every debugging session -- not just ones launched through
# tools/gdb/connect.sh.
#
#   https://github.com/libretro/virtualjaguar-libretro
#   docs/gdb-stub-guide.md is the feature this tooling sits on top of.
#
# POSIX sh. Appends one clearly-marked block to each file and never
# touches anything else already in it. Safe to re-run: it recognizes its
# own block and does nothing the second time. `--uninstall` removes
# exactly that block, byte for byte, and nothing else.
#
# Usage:
#   tools/gdb/install.sh                  install into ~/.gdbinit and ~/.lldbinit
#   tools/gdb/install.sh --gdbinit-only
#   tools/gdb/install.sh --lldbinit-only
#   tools/gdb/install.sh --uninstall
#   tools/gdb/install.sh --dry-run
#
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
GDBINIT_SRC="$SCRIPT_DIR/jaguar.gdbinit"
GDB_PY_SRC="$SCRIPT_DIR/jaguar_gdb.py"
LLDB_PY_SRC="$SCRIPT_DIR/jaguar_lldb.py"

BEGIN_MARK="# >>> virtualjaguar-libretro gdb-stub tooling (tools/gdb) >>>"
END_MARK="# <<< virtualjaguar-libretro gdb-stub tooling (tools/gdb) <<<"

DO_GDB=1
DO_LLDB=1
UNINSTALL=0
DRY_RUN=0

say()  { printf '%s\n' "$*"; }
note() { printf '  - %s\n' "$*"; }
ok()   { printf '  + %s\n' "$*"; }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# Run a file-mutating command, bypassing any shell alias/function named
# rm/cp/mv. Mirrors tools/install.sh's as_root() exactly (same name, same
# reasoning) for anyone auditing both: this user's shell aliases those
# three to their `-i` (interactive) forms, and a confirmation prompt with
# no TTY attached to answer it blocks forever rather than failing. Unlike
# tools/install.sh, everything this script writes lives under $HOME, so
# there is no sudo branch to gate -- `command` alone is always enough.
as_root() { command "$@"; }

usage() {
  cat <<'USAGE'
Usage: tools/gdb/install.sh [options]

  --gdbinit-only    only touch ~/.gdbinit
  --lldbinit-only   only touch ~/.lldbinit
  --uninstall       remove the block this script previously added
  --dry-run         print what would change, change nothing
  -h, --help        this text
USAGE
  exit 0
}

while [ $# -gt 0 ]; do
  case "$1" in
    --gdbinit-only)  DO_LLDB=0; shift ;;
    --lldbinit-only) DO_GDB=0; shift ;;
    --uninstall)     UNINSTALL=1; shift ;;
    --dry-run)       DRY_RUN=1; shift ;;
    -h|--help)       usage ;;
    *)               die "unknown option: $1 (try --help)" ;;
  esac
done

[ -f "$GDBINIT_SRC" ] || die "missing $GDBINIT_SRC -- run this from a checkout of the repo."
[ -f "$GDB_PY_SRC" ]  || die "missing $GDB_PY_SRC"
[ -f "$LLDB_PY_SRC" ] || die "missing $LLDB_PY_SRC"

# block_for <gdb|lldb> -- the exact text appended/removed for each file.
block_for() {
  printf '%s\n' "$BEGIN_MARK"
  printf '%s\n' "# Added by tools/gdb/install.sh -- https://github.com/libretro/virtualjaguar-libretro"
  printf '%s\n' "# Remove with: sh '$SCRIPT_DIR/install.sh' --uninstall"
  if [ "$1" = gdb ]; then
    printf 'source %s\n' "$GDBINIT_SRC"
    printf 'source %s\n' "$GDB_PY_SRC"
  else
    printf 'command script import %s\n' "$LLDB_PY_SRC"
  fi
  printf '%s\n' "$END_MARK"
}

has_block() {
  [ -f "$1" ] && grep -qF "$BEGIN_MARK" "$1" 2>/dev/null
}

remove_block() {
  _f="$1"
  if [ ! -f "$_f" ] || ! has_block "$_f"; then
    note "nothing to remove from $_f"
    return 0
  fi
  if [ "$DRY_RUN" -eq 1 ]; then
    note "would remove the block from $_f"
    return 0
  fi
  _tmp="$_f.vjgdb.tmp.$$"
  awk -v b="$BEGIN_MARK" -v e="$END_MARK" '
    $0 == b { skip = 1; next }
    $0 == e { skip = 0; next }
    !skip { print }
  ' "$_f" > "$_tmp"
  as_root mv "$_tmp" "$_f"
  ok "removed block from $_f"
}

install_block() {
  _f="$1"; _kind="$2"
  if has_block "$_f"; then
    note "already installed in $_f"
    return 0
  fi
  if [ "$DRY_RUN" -eq 1 ]; then
    note "would append to $_f:"
    block_for "$_kind" | sed 's/^/      /'
    return 0
  fi
  # A plain redirect is a shell builtin, not an external rm/cp/mv target,
  # so it needs no as_root guard -- `>>` creates the file if absent and
  # never truncates it if present.
  block_for "$_kind" >> "$_f"
  ok "installed -> $_f"
}

if [ "$UNINSTALL" -eq 1 ]; then
  say "Removing Virtual Jaguar gdb-stub tooling"
  [ "$DO_GDB" -eq 1 ]  && remove_block "$HOME/.gdbinit"
  [ "$DO_LLDB" -eq 1 ] && remove_block "$HOME/.lldbinit"
  exit 0
fi

say "Installing Virtual Jaguar gdb-stub tooling"
[ "$DO_GDB" -eq 1 ]  && install_block "$HOME/.gdbinit"  gdb
[ "$DO_LLDB" -eq 1 ] && install_block "$HOME/.lldbinit" lldb

if [ "$DRY_RUN" -eq 0 ]; then
  say ""
  say "New gdb sessions get:  jconnect, jdisasm, jregs, jhalt, jtrace, jwatch"
  say "                       (macros) and jag-disasm, jag-regs, jag-trace,"
  say "                       jag-halt, jag-watch, jag-info-registers (Python)."
  say "New lldb sessions get: jag-disasm, jag-regs, jag-trace, jag-halt,"
  say "                       jag-watch, jag-info-registers (Python)."
  say ""
  say "Nothing here connects automatically -- run \`jconnect\` (gdb) or"
  say "\`gdb-remote 127.0.0.1:2345\` (lldb), or just use tools/gdb/connect.sh,"
  say "which needs none of this installed."
  say ""
  say "Remove later with:  $0 --uninstall"
fi

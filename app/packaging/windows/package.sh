#!/bin/bash
# Stage the Windows packages from an install tree (run in an MSYS2/bash shell).
#
#   package.sh <install-prefix> <version-label> <out-dir> [--installer <ISCC.exe>]
#
# Produces, in <out-dir>:
#   VirtualJaguar-<label>-windows-x64.zip           portable folder (portable.txt inside)
#   VirtualJaguar-<label>-windows-x64-setup.exe     Inno Setup installer (with --installer)
set -euo pipefail
prefix="$(cd "$1" && pwd)"; label="$2"; out="$3"; shift 3
iscc=""
if [ "${1:-}" = "--installer" ]; then iscc="$2"; fi
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$out"; out="$(cd "$out" && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

# One tree both packages share: exe + data beside it + licence/notices.
tree="$work/tree/VirtualJaguar"
mkdir -p "$tree"
cp -a "$prefix/bin/." "$tree/"
cp "$prefix/share/doc/virtualjaguar/LICENSE.txt" \
   "$prefix/share/doc/virtualjaguar/THIRD-PARTY-NOTICES.txt" "$tree/"
cp "$here/../icons/virtualjaguar.ico" "$work/tree/virtualjaguar.ico"
[ -f "$tree/virtualjaguar.exe" ] || { echo "no virtualjaguar.exe in the install tree" >&2; exit 1; }

# Optional local signing hook (local-package.sh --sign sets VJ_SIGN_CMD; CI leaves it unset).
sign_files() { if [ -n "${VJ_SIGN_CMD:-}" ]; then "$VJ_SIGN_CMD" "$@"; fi; }
if [ -n "${VJ_SIGN_CMD:-}" ]; then
  sign_list=()
  while IFS= read -r -d '' f; do sign_list+=("$f"); done < <(find "$tree" -type f \( -name '*.exe' -o -name '*.dll' \) -print0)
  sign_files "${sign_list[@]}"
fi

# The installer is built from the tree WITHOUT portable.txt.
if [ -n "$iscc" ]; then
  MSYS2_ARG_CONV_EXCL='*' "$iscc" \
    "/DAppVersion=${label}" \
    "/DSourceDir=$(cygpath -w "$tree")" \
    "/DOutputDir=$(cygpath -w "$out")" \
    "/DOutputName=VirtualJaguar-${label}-windows-x64-setup" \
    "$(cygpath -w "$here/virtualjaguar.iss")"
  sign_files "$out/VirtualJaguar-${label}-windows-x64-setup.exe"
fi

# Portable: the same tree plus portable.txt (keeps settings in .\data).
cat > "$tree/portable.txt" <<'PORT'
This file makes Virtual Jaguar portable: settings, saves and save states are kept in the
"data" folder next to virtualjaguar.exe instead of %APPDATA%\VirtualJaguar.
Delete this file to use the per-user folder again.
PORT
zipname="VirtualJaguar-${label}-windows-x64.zip"
rm -f "$out/$zipname"
(cd "$work/tree" && zip -qr -9 "$out/$zipname" VirtualJaguar)
ls -la "$out"

#!/bin/bash
# Stage the Windows packages from an install tree (run in an MSYS2/bash shell).
#
#   package.sh <install-prefix> <version-label> <out-dir> [--arch x64|arm64] [--installer <ISCC.exe>]
#
# Produces, in <out-dir> (<arch> defaults to x64):
#   VirtualJaguar-<label>-windows-<arch>.zip           portable folder (portable.txt inside)
#   VirtualJaguar-<label>-windows-<arch>-setup.exe     Inno Setup installer (with --installer)
set -euo pipefail
prefix="$(cd "$1" && pwd)"; label="$2"; out="$3"; shift 3
iscc=""; arch="x64"
while [ $# -gt 0 ]; do
  case "$1" in
    --installer) iscc="$2"; shift 2 ;;
    --arch) arch="$2"; shift 2 ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
done
case "$arch" in x64|arm64) ;; *) echo "--arch takes x64 or arm64 (got '$arch')" >&2; exit 2 ;; esac
arm64_def=""; [ "$arch" = arm64 ] && arm64_def="/DArm64"
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

# The installer is built from the tree WITHOUT portable.txt.
if [ -n "$iscc" ]; then
  MSYS2_ARG_CONV_EXCL='*' "$iscc" \
    "/DAppVersion=${label}" \
    "/DSourceDir=$(cygpath -w "$tree")" \
    "/DOutputDir=$(cygpath -w "$out")" \
    ${arm64_def:+"$arm64_def"} \
    "/DOutputName=VirtualJaguar-${label}-windows-${arch}-setup" \
    "$(cygpath -w "$here/virtualjaguar.iss")"
fi

# Portable: the same tree plus portable.txt (keeps settings in .\data).
cat > "$tree/portable.txt" <<'PORT'
This file makes Virtual Jaguar portable: settings, saves and save states are kept in the
"data" folder next to virtualjaguar.exe instead of %APPDATA%\VirtualJaguar.
Delete this file to use the per-user folder again.
PORT
zipname="VirtualJaguar-${label}-windows-${arch}.zip"
rm -f "$out/$zipname"
(cd "$work/tree" && zip -qr -9 "$out/$zipname" VirtualJaguar)
ls -la "$out"

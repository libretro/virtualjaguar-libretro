#!/bin/bash
# Stage the Windows packages from an install tree (run in an MSYS2/bash shell).
#
#   package.sh <install-prefix> <version-label> <out-dir> [--arch x64|arm64] [--installer <ISCC.exe>] [--direct-installer]
#
# Produces, in <out-dir> (<arch> defaults to x64):
#   VirtualJaguar-<label>-windows-<arch>.zip           portable folder (portable.txt inside)
#   VirtualJaguar-<label>-windows-<arch>-setup.exe     Inno Setup installer (with --installer)
# With --direct-installer, emits only VirtualJaguar-<channel>-<build>-windows-<arch>-direct-setup.exe.
set -euo pipefail
prefix="$(cd "$1" && pwd)"; label="$2"; out="$3"; shift 3
iscc=""; arch="x64"; direct=0
while [ $# -gt 0 ]; do
  case "$1" in
    --installer) iscc="$2"; shift 2 ;;
    --direct-installer) direct=1; shift ;;
    --arch) arch="$2"; shift 2 ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
done
case "$arch" in x64|arm64) ;; *) echo "--arch takes x64 or arm64 (got '$arch')" >&2; exit 2 ;; esac
installer_defs=()
if [ "$arch" = arm64 ]; then installer_defs+=(/DArm64); fi
setup_name="VirtualJaguar-${label}-windows-${arch}-setup"
build_info="$prefix/share/doc/virtualjaguar/winsparkle-build.txt"
if [ "$direct" = 1 ]; then
  [ -n "$iscc" ] && [ -f "$build_info" ] && [ -f "$prefix/bin/WinSparkle.dll" ] || {
    echo "Direct packaging needs ISCC and a WinSparkle install tree" >&2; exit 1;
  }
  IFS= read -r build_line < "$build_info"
  read -r channel sdk_arch build_version extra <<< "${build_line%$'\r'}"
  [ "$sdk_arch" = "$arch" ] && [ -z "$extra" ] || {
    echo "WinSparkle build metadata does not match package architecture" >&2; exit 1;
  }
  case "$channel" in
    stable) [[ "$build_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ && "$label" = "$build_version" ]] || { echo "Invalid stable WinSparkle build version" >&2; exit 2; } ;;
    nightly) [[ "$build_version" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid nightly WinSparkle build sequence" >&2; exit 2; } ;;
    *) echo "Unknown update channel: $channel" >&2; exit 2 ;;
  esac
  installer_defs+=(/DDirectUpdates)
  if [ "$channel" = nightly ]; then installer_defs+=(/DDirectNightly); fi
  setup_name="VirtualJaguar-${channel}-${build_version}-windows-${arch}-direct-setup"
elif [ -e "$build_info" ] || [ -e "$prefix/bin/WinSparkle.dll" ]; then
  echo "WinSparkle builds must use --direct-installer" >&2; exit 1
fi
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
if [ "$direct" = 1 ] && { [ -e "$tree/portable.txt" ] || [ -e "$tree/direct-install.ini" ]; }; then
  echo "Direct installer requires a fresh staging tree" >&2; exit 1
fi

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
    "${installer_defs[@]}" \
    "/DOutputName=$setup_name" \
    "$(cygpath -w "$here/virtualjaguar.iss")"
  sign_files "$out/$setup_name.exe"
fi

if [ "$direct" = 1 ]; then exit 0; fi

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

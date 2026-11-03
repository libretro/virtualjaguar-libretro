#!/bin/bash
# Assert a Windows package tree ships a working librashader runtime (#1153).  The Windows
# counterpart of linux/check-librashader.sh; run it in the MSYS2 shell CI builds in.
#
#   check-librashader.sh <dir-holding-virtualjaguar.exe> <x64|arm64> <vj_shader_tests.exe>
#
# <dir> is the unpacked portable zip's VirtualJaguar folder, the Inno Setup install folder
# (C:\vjtest in CI) or an MSIX layout.  Checks, in order:
#   1. check-librashader-dll.py: librashader.dll is there, a PE image for <arch>, imports only
#      Windows' own DLLs (no D3DX9_43 / VC++ redistributable: it must load on a stock install),
#      exports the C API, and -- loaded by this machine's python -- reports ABI 2.
#   2. vj_shader_tests probe: the APP's own lookup finds it, i.e. beside the executable with
#      VJ_LIBRASHADER unset (the test binary is COPIED into <dir> so its base path is the package).
#   3. vj_shader_tests library: the packaged DLL parses the bundled presets; VJ_REQUIRE_LIBRASHADER
#      makes a runtime that does not load a FAILURE, never a skip.
# The software-opengl/ sub-tree (stage-software-gl.py) carries a second copy of the whole app;
# its DLL gets check 1 as well.
set -euo pipefail
dir="$(cd "$1" && pwd -P)"; arch="$2"; tests="$3"
here="$(cd "$(dirname "$0")" && pwd)"
case "$arch" in x64|arm64) ;; *) echo "arch must be x64 or arm64 (got '$arch')" >&2; exit 2 ;; esac
lib="$dir/librashader.dll"
[ -f "$lib" ] && [ ! -L "$lib" ] || { echo "::error::$lib is missing (or a symlink)"; exit 1; }
ls -la "$lib"
# MSYS2's python is a native Windows build of this machine's CPU: the load check applies.
python "$here/check-librashader-dll.py" "$(cygpath -m "$lib")" --arch "$arch" --require-load
if [ -f "$dir/software-opengl/librashader.dll" ]; then
  echo "--- software-opengl/ copy"
  python "$here/check-librashader-dll.py" "$(cygpath -m "$dir/software-opengl/librashader.dll")" --arch "$arch" --require-load
fi
# Run the test binary from the package's own folder, the way the app runs: its base path is
# then the package's folder, which is where the probe looks first.
t="$dir/vj_shader_tests_probe.exe"
command cp -f "$tests" "$t"
trap 'command rm -f "$t"' EXIT
echo "--- probe (the app's own lookup, VJ_LIBRASHADER unset)"
env -u VJ_LIBRASHADER "$t" probe "$(cygpath -m "$dir")"
echo "--- library (parse the bundled presets through the packaged DLL)"
VJ_REQUIRE_LIBRASHADER=1 VJ_LIBRASHADER="$(cygpath -m "$lib")" "$t" library

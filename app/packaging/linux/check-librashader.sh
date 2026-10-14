#!/bin/bash
# Assert a Linux package ships a working librashader runtime.
#
#   check-librashader.sh <dir-holding-the-executable> <x86_64|aarch64> <vj_shader_tests>
#
# <dir> is squashfs-root/usr/bin (AppImage) or /usr/lib/virtualjaguar (.deb).  Checks that
# librashader.so is there, is a regular, world-readable file for the right CPU, has no
# unresolved dependency, and that the app's own lookup finds it: vj_shader_tests is COPIED
# beside the package's executable and run with VJ_LIBRASHADER unset (`probe`), then the library
# parses the bundled presets (`library`, which fails rather than skips: VJ_REQUIRE_LIBRASHADER).
set -euo pipefail
dir="$(cd "$1" && pwd -P)"; arch="$2"; tests="$3"
lib="$dir/librashader.so"
[ -f "$lib" ] && [ ! -L "$lib" ] || { echo "::error::$lib is missing (or a symlink)"; exit 1; }
ls -la "$lib"
perm="$(stat -c %a "$lib")"
case "$perm" in 644|755) ;; *) echo "::error::$lib has mode $perm"; exit 1 ;; esac
desc="$(file -b "$lib")"; echo "$desc"
case "$desc" in *'ELF 64-bit LSB shared object'*) ;; *) echo "::error::$lib is not an ELF shared object"; exit 1 ;; esac
case "$arch" in
  x86_64)  want='x86-64' ;;
  aarch64) want='ARM aarch64' ;;
  *) echo "unknown arch $arch" >&2; exit 2 ;;
esac
case "$desc" in *"$want"*) ;; *) echo "::error::$lib is not $arch"; exit 1 ;; esac
deps="$(ldd "$lib")"; echo "$deps"
case "$deps" in *'not found'*) echo "::error::unresolved libraries in $lib"; exit 1 ;; esac
# Run the test binary from the package's own folder, the way the app runs: its base path is
# then the package's folder, which is where the probe looks first.
t="$dir/vj_shader_tests_probe"
cp "$tests" "$t" 2>/dev/null || sudo cp "$tests" "$t"
trap 'rm -f "$t" 2>/dev/null || sudo rm -f "$t"' EXIT
env -u VJ_LIBRASHADER "$t" probe "$dir"
VJ_REQUIRE_LIBRASHADER=1 VJ_LIBRASHADER="$lib" "$t" library

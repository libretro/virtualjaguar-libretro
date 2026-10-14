#!/bin/bash
# Build the librashader C API (the shader runtime the app dlopen()s) from the pinned source.
#
#   build-from-source.sh <rust-target-triple> <out-dir> [<work-dir>]
#
# Used by .github/workflows/librashader.yml; usable by hand (and by distro packagers who
# would rather not take upstream's binary).  Needs a stable Rust >= 1.88 (rustup/cargo on
# PATH, the target installed: `rustup target add <triple>`), curl, tar, a C/C++ compiler.
#
# Fetches https://github.com/SnowflakePowered/librashader/archive/<commit>.tar.gz, checks
# its SHA-256 against pin.env, runs
#     cargo xtask build --profile optimized --target <triple> -- --locked
# and copies the result to <out-dir>/librashader.{so,dylib,dll} (librashader's own build
# script renames liblibrashader_capi.*).  macOS libraries get the install name
# @rpath/librashader.dylib and MACOSX_DEPLOYMENT_TARGET from pin.env.
set -euo pipefail
target="${1:?usage: build-from-source.sh <rust-target-triple> <out-dir> [<work-dir>]}"
out="${2:?usage: build-from-source.sh <rust-target-triple> <out-dir> [<work-dir>]}"
here="$(cd "$(dirname "$0")" && pwd)"
work="${3:-$(mktemp -d)}"

# pin.env is plain KEY=VALUE lines (no quoting, no spaces).
eval "$(grep -E '^[A-Z0-9_]+=' "$here/pin.env")"

case "$target" in
  *-apple-darwin)  lib=librashader.dylib ;;
  *-linux-*)       lib=librashader.so ;;
  *-windows-*)     lib=librashader.dll ;;
  *) echo "unsupported target $target" >&2; exit 2 ;;
esac

mkdir -p "$work" "$out"
arc="$work/librashader-$LIBRASHADER_COMMIT.tar.gz"
if [ ! -f "$arc" ]; then
  curl -fsSL --retry 3 -o "$arc" "https://github.com/SnowflakePowered/librashader/archive/$LIBRASHADER_COMMIT.tar.gz"
fi
if command -v sha256sum >/dev/null; then got="$(sha256sum "$arc" | cut -d' ' -f1)"; else got="$(shasum -a 256 "$arc" | cut -d' ' -f1)"; fi
if [ "$got" != "$LIBRASHADER_SHA256" ]; then
  echo "librashader archive digest mismatch: $got != $LIBRASHADER_SHA256" >&2
  rm -f "$arc"; exit 1
fi
src="$work/src-$LIBRASHADER_COMMIT"
if [ ! -d "$src" ]; then
  mkdir -p "$src.tmp"; tar -xzf "$arc" -C "$src.tmp" --strip-components=1; mv "$src.tmp" "$src"
fi

cd "$src"
case "$target" in
  *-apple-darwin)
    export MACOSX_DEPLOYMENT_TARGET="$LIBRASHADER_MACOS_MIN"
    # Cargo applies RUSTFLAGS to the target artifacts only (not build scripts) when --target is given.
    export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-Wl,-install_name,@rpath/librashader.dylib" ;;
esac
echo "building $LIBRASHADER_TAG ($LIBRASHADER_COMMIT) for $target with $(rustc --version)"
cargo xtask build --profile optimized --target "$target" -- --locked
test -f "target/$target/optimized/$lib"
cp -f "target/$target/optimized/$lib" "$out/$lib"
ls -la "$out/$lib"

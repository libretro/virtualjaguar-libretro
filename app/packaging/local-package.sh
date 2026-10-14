#!/bin/bash
# Package (and optionally sign / notarize) the app on a developer machine, after a normal build.
#
#   bash app/packaging/local-package.sh [--sign] [--notarize] [--build-dir DIR] [--out DIR]
#   bash app/packaging/local-package.sh --sign-exe <path>     dev-build helper (macOS, CMake)
#
#   (none)       assemble only: dist/Virtual Jaguar.app (macOS) or the Windows zip (+ installer
#                when Inno Setup's ISCC is found).  macOS ad-hoc signs, as Apple Silicon requires.
#   --sign       sign with a certificate found on this machine (see below)
#   --notarize   macOS only: submit to Apple, wait, staple, check Gatekeeper (implies --sign;
#                needs a Developer ID Application identity and notary credentials)
#
# Settings come from the environment, then from the repo-root `.env` (see env.example):
#   macOS    VJ_SIGN_IDENTITY (name, SHA-1 or "-") > newest "Developer ID Application" in the
#            keychain > "Apple Development" (runs locally, cannot be notarized) > ad hoc.
#            Notarization: VJ_NOTARY_PROFILE (notarytool keychain profile) or
#            APPLE_NOTARY_KEY_ID + APPLE_NOTARY_ISSUER_ID + APPLE_NOTARY_KEY_PATH.
#   Windows  VJ_SIGN_PFX (+ VJ_SIGN_PFX_PASSWORD) > VJ_SIGN_CERT_SHA1 (CurrentUser\My) >
#            signtool's automatic pick > unsigned.  Run from Git Bash / MSYS2.
#
# Prerequisite: a build, e.g. `cmake -S app -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
sign=0; notarize=0; build="$root/build"; out="$root/dist"; sign_exe=""
while [ $# -gt 0 ]; do
  case "$1" in
    --sign) sign=1 ;;
    --notarize) sign=1; notarize=1 ;;
    --build-dir) build="$(cd "$2" && pwd)"; shift ;;
    --out) out="$2"; shift ;;
    --sign-exe) sign_exe="$2"; shift ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
  esac
  shift
done

# shellcheck source=load-env.sh
. "$here/load-env.sh"
trap vj_env_cleanup EXIT
vj_load_env "$root"

os="$(uname -s)"
case "$os" in
  Darwin) ;;
  MINGW*|MSYS*|CYGWIN*) ;;
  *) echo "local signing supports macOS and Windows (Git Bash); on $os there is nothing to sign here" >&2; exit 2 ;;
esac

# ---- macOS ---------------------------------------------------------------------------------
announce_identity() {
  # shellcheck source=macos/local-identity.sh
  . "$here/macos/local-identity.sh"
  vj_pick_identity
  case "$VJ_ID_KIND" in
    explicit)       echo "[sign] identity (VJ_SIGN_IDENTITY): $VJ_ID_NAME" ;;
    developer-id)   echo "[sign] identity: $VJ_ID_NAME, expires $VJ_ID_EXPIRY" ;;
    development)    echo "[sign] identity: $VJ_ID_NAME, expires $VJ_ID_EXPIRY"
                    echo "[sign] warning: no 'Developer ID Application' identity found; an Apple Development" \
                         "signature runs on this Mac but cannot be notarized or distributed" ;;
    adhoc)          [ -z "${VJ_SIGN_IDENTITY:-}" ] || echo "[sign] VJ_SIGN_IDENTITY=-: signing ad hoc"
                    [ -n "${VJ_SIGN_IDENTITY:-}" ] || echo "[sign] no signing identity: signing ad hoc (runs here; Gatekeeper refuses it elsewhere)." \
                         "List yours with: security find-identity -v -p codesigning" ;;
  esac
}

if [ -n "$sign_exe" ]; then
  # CMake POST_BUILD (-DVJ_APP_SIGN_DEV_BUILD=ON): never fail the build over a signature.
  if [ "$os" = Darwin ]; then
    announce_identity
    codesign --force --sign "$VJ_ID" "$sign_exe" || echo "[sign] warning: could not sign $sign_exe"
  fi
  exit 0
fi

[ -f "$build/CMakeCache.txt" ] || { echo "no CMake build in $build (build first, or pass --build-dir)" >&2; exit 1; }
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt" | head -1)"; : "${version:=0.0.0}"
work="$(mktemp -d)"
trap 'vj_env_cleanup; rm -rf "$work"' EXIT
echo "[pkg] installing the build into a stage ..."
cmake --install "$build" --prefix "$work/stage" >/dev/null

if [ "$os" = Darwin ]; then
  app="$out/Virtual Jaguar.app"
  mkdir -p "$out"
  bash "$here/macos/make-app.sh" "$work/stage" "$app" "$version" "$(date +%Y%m%d%H%M)" 11.0
  cs="$here/macos/codesign.sh"
  if [ "$sign" = 1 ]; then
    announce_identity
    if [ "$notarize" = 1 ] && [ "$VJ_ID_KIND" != developer-id ] && [ "$VJ_ID_KIND" != explicit ]; then
      echo "--notarize needs a 'Developer ID Application' identity (found: $VJ_ID_NAME)" >&2; exit 1
    fi
    VJ_SIGN_IDENTITY="$VJ_ID" bash "$cs" sign-app "$app"
  else
    bash "$cs" sign-app "$app" adhoc
  fi
  if [ "$notarize" = 1 ]; then
    if [ -z "${VJ_NOTARY_PROFILE:-}" ] && { [ -z "${APPLE_NOTARY_KEY_ID:-}" ] || [ -z "${APPLE_NOTARY_ISSUER_ID:-}" ] \
        || [ -z "${APPLE_NOTARY_KEY_PATH:-}" ]; }; then
      echo "--notarize needs VJ_NOTARY_PROFILE, or APPLE_NOTARY_KEY_ID + APPLE_NOTARY_ISSUER_ID +" \
           "APPLE_NOTARY_KEY_PATH (environment or .env; see app/packaging/env.example)" >&2
      exit 1
    fi
    ditto -c -k --keepParent "$app" "$work/notarize.zip"
    echo "[notarize] submitting (this uploads the app to Apple and waits) ..."
    RUNNER_TEMP="$work" bash "$cs" notarize "$work/notarize.zip"
    bash "$cs" staple "$app"
    spctl -a -t exec -vv "$app"
  fi
  # Always leave a distributable zip: the ticket and signature travel inside the app.
  zip="$out/VirtualJaguar-$version-macos-local.zip"
  command rm -f "$zip"
  ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
  echo "[pkg] $app"
  echo "[pkg] $zip"
else
  # ---- Windows (Git Bash / MSYS2) -------------------------------------------------------------
  iscc=""
  if command -v iscc >/dev/null 2>&1; then iscc="$(command -v iscc)"
  elif [ -x "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" ]; then iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
  elif [ -x "/c/Program Files/Inno Setup 6/ISCC.exe" ]; then iscc="/c/Program Files/Inno Setup 6/ISCC.exe"
  fi
  [ -n "$iscc" ] || echo "[pkg] Inno Setup (ISCC) not found: building the portable zip only"
  if [ "$sign" = 1 ]; then export VJ_SIGN_CMD="$here/windows/sign.sh"; fi
  args=("$work/stage" "$version-local" "$out")
  [ -z "$iscc" ] || args+=(--installer "$iscc")
  bash "$here/windows/package.sh" "${args[@]}"
fi

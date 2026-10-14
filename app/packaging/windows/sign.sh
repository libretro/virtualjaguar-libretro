#!/bin/bash
# Authenticode-sign files with signtool (Git Bash / MSYS2).  Used by local-package.sh --sign
# (package.sh runs it via VJ_SIGN_CMD); never by CI, which signs through SignPath.
#
#   sign.sh <file>...
#
# Certificate, first match wins:
#   VJ_SIGN_PFX (+ VJ_SIGN_PFX_PASSWORD)   a .pfx / .p12 file
#   VJ_SIGN_CERT_SHA1                      thumbprint of a cert in the CurrentUser\My store
#   (otherwise)                            signtool's automatic choice; none found = unsigned
# VJ_SIGN_TIMESTAMP_URL (default http://timestamp.digicert.com), VJ_SIGNTOOL (path override).
set -euo pipefail
[ $# -gt 0 ] || { sed -n '2,12p' "$0"; exit 2; }
export MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1   # keep signtool's /switches from becoming paths

tool="${VJ_SIGNTOOL:-}"
if [ -z "$tool" ]; then
  tool="$(command -v signtool.exe || true)"
  # shellcheck disable=SC2012
  [ -n "$tool" ] || tool="$(ls -d "/c/Program Files (x86)/Windows Kits/10/bin/"*/x64/signtool.exe 2>/dev/null | sort -V | tail -1 || true)"
fi
if [ -z "$tool" ] || [ ! -x "$tool" ]; then
  echo "[sign] signtool.exe not found (install the Windows SDK or set VJ_SIGNTOOL): files stay UNSIGNED" >&2
  exit 0
fi

files=()
for f in "$@"; do files+=("$(cygpath -w "$f")"); done
args=(sign /fd sha256 /tr "${VJ_SIGN_TIMESTAMP_URL:-http://timestamp.digicert.com}" /td sha256)
if [ -n "${VJ_SIGN_PFX:-}" ]; then
  [ -f "$VJ_SIGN_PFX" ] || { echo "[sign] VJ_SIGN_PFX is not a file" >&2; exit 1; }
  args+=(/f "$(cygpath -w "$VJ_SIGN_PFX")")
  [ -z "${VJ_SIGN_PFX_PASSWORD:-}" ] || args+=(/p "$VJ_SIGN_PFX_PASSWORD")
  echo "[sign] certificate: VJ_SIGN_PFX"
elif [ -n "${VJ_SIGN_CERT_SHA1:-}" ]; then
  args+=(/sha1 "$VJ_SIGN_CERT_SHA1")
  echo "[sign] certificate: thumbprint $VJ_SIGN_CERT_SHA1 (CurrentUser\\My)"
else
  args+=(/a)
  echo "[sign] certificate: automatic selection"
  if ! "$tool" "${args[@]}" "${files[@]}"; then
    echo "[sign] no usable code-signing certificate found: files stay UNSIGNED" >&2
    exit 0
  fi
  exit 0
fi
"$tool" "${args[@]}" "${files[@]}"

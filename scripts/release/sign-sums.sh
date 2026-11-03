#!/usr/bin/env bash
# Detached minisign signature on SHA256SUMS.txt (issue #1151).
#
#   scripts/release/sign-sums.sh <release-dir>
#
# Environment:
#   MINISIGN_SECRET_KEY   the project release key: the CONTENTS of the
#                         minisign secret-key file (repository secret).
#                         Absent -> nothing is signed and the step prints a
#                         loud warning; the verify-signatures gate turns that
#                         into a failure on tags and release/** once the public
#                         key is committed (docs/release-signing.pub).
#   MINISIGN_PASSWORD     its password (empty for a key generated with -W)
#   MINISIGN_PUBKEY_FILE  the committed public key, default docs/release-signing.pub.
#                         The fresh signature is checked against it: a secret
#                         that does not match the published key is a
#                         configuration error, so the signature is dropped and,
#                         on a tag or release/** push, the step fails.
#   LANE, GITHUB_SHA      go into the signed trusted comment
#
# Output: <release-dir>/SHA256SUMS.txt.minisig, and `signed=true|false` in
# $GITHUB_OUTPUT.  Verify:  minisign -Vm SHA256SUMS.txt -p docs/release-signing.pub
set -euo pipefail
rel=${1:?release dir}
sums="$rel/SHA256SUMS.txt"
pub=${MINISIGN_PUBKEY_FILE:-docs/release-signing.pub}
out=${GITHUB_OUTPUT:-/dev/null}
strict=false
case "${GITHUB_REF:-}" in refs/tags/*|refs/heads/release/*) strict=true ;; esac

[ -f "$sums" ] || { echo "::error::$sums does not exist (run stage.sh first)"; exit 1; }
command rm -f "$sums.minisig"

if [ -z "${MINISIGN_SECRET_KEY:-}" ]; then
  echo "::warning title=SHA256SUMS.txt is NOT signed::The MINISIGN_SECRET_KEY secret is not configured," \
    "so this run ships an unsigned checksum list. Generate the release key and add the secret as" \
    "described in app/packaging/README.md (Signed checksums)."
  echo "signed=false" >> "$out"
  exit 0
fi

if ! command -v minisign >/dev/null 2>&1; then
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update -qq && sudo apt-get install -y -qq minisign
  elif command -v brew >/dev/null 2>&1; then
    brew install minisign
  fi
fi
command -v minisign >/dev/null 2>&1 || { echo "::error::minisign is not installed and could not be installed"; exit 1; }

umask 077
key=$(mktemp)
trap 'command rm -f "$key"' EXIT
printf '%s\n' "$MINISIGN_SECRET_KEY" > "$key"

trusted="virtualjaguar-libretro ${LANE:-release} ${GITHUB_SHA:-} $(date -u +%Y-%m-%dT%H:%M:%SZ)"
# minisign reads the password from stdin when it is not a terminal.
printf '%s\n' "${MINISIGN_PASSWORD:-}" | minisign -S -s "$key" -m "$sums" \
  -t "$trusted" -c "SHA256SUMS.txt of https://github.com/libretro/virtualjaguar-libretro" >/dev/null
echo "==> signed: $sums.minisig"
cat "$sums.minisig"

if [ -f "$pub" ]; then
  if minisign -Vm "$sums" -p "$pub" -q; then
    echo "==> verifies with the committed public key $pub"
    echo "signed=true" >> "$out"
  else
    msg="SHA256SUMS.txt.minisig does NOT verify with the committed public key ($pub): the MINISIGN_SECRET_KEY secret and the published key are not a pair. The signature was removed."
    command rm -f "$sums.minisig"
    echo "signed=false" >> "$out"
    if [ "$strict" = true ]; then echo "::error::$msg"; exit 1; else echo "::warning::$msg"; fi
  fi
else
  echo "::warning::$pub is not committed, so the new signature could not be cross-checked; commit the public key (see app/packaging/README.md)."
  echo "signed=true" >> "$out"
fi

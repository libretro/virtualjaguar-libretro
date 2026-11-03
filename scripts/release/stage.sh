#!/usr/bin/env bash
# Flatten the workflow artifacts of a release.yml run into the files a release
# ships, and write SHA256SUMS.txt (issues #1151 / #1152).
#
#   scripts/release/stage.sh <artifacts-dir> <release-dir> <status-dir>
#
#   artifacts-dir  what actions/download-artifact put there (one directory per
#                  artifact).  librashader-* and ci-core-* are build
#                  intermediates and signing-status-* are the signing sidecars
#                  (see signing-status.sh): none of them is a release asset.
#   release-dir    every shipped file, flat; plus SHA256SUMS.txt
#   status-dir     the signing sidecars, for the verify-signatures gate
#
# Environment:
#   LANE        release | nightly | prerelease | dry-run.  Anything but `release`
#               rewrites display_version in the .info file to the build's real
#               identity (a nightly .info must not claim to be the last tag).
#   GITHUB_SHA  used in that rewrite
#
# One asset name must map to one file: the release attestation and the checksum
# list are keyed by name, so a second artifact carrying the same basename with
# DIFFERENT bytes would silently replace a shipped file.  That is an error here.
# Byte-identical copies are fine: every build job uploads its dist/ tree, and
# dist/info/virtualjaguar_libretro.info is in the repository, so each artifact
# carries the same .info file (the final one is written below anyway).
set -euo pipefail
art=${1:?artifacts dir}; rel=${2:?release dir}; st=${3:?status dir}
lane=${LANE:-release}
mkdir -p "$rel" "$st"

while IFS= read -r f; do
  n=$(basename "$f")
  if [ -e "$rel/$n" ]; then
    cmp -s "$f" "$rel/$n" && continue
    echo "::error::two artifacts ship different files named $n ($f and an earlier one)"; exit 1
  fi
  cp "$f" "$rel/$n"
done < <(find "$art" -mindepth 2 -type f \
           -not -path "$art/librashader-*" -not -path "$art/ci-core-*" \
           -not -path "$art/signing-status-*" | LC_ALL=C sort)

find "$art" -mindepth 2 -type f -path "$art/signing-status-*" -exec cp {} "$st/" \;
# The gate downloads this directory as an artifact; an empty artifact cannot be
# uploaded, so there is always one file in it.
printf 'signing sidecars written by scripts/release/signing-status.sh (lane %s)\n' "$lane" > "$st/README.txt"

# The RetroArch core .info file, so frontend maintainers / users can drop it
# into RetroArch's info/ dir; on a tag it is the file PR'd to libretro-super.
if [ -f dist/info/virtualjaguar_libretro.info ]; then
  if [ "$lane" = release ]; then
    cp dist/info/virtualjaguar_libretro.info "$rel/"
  else
    # display_version names the last TAGGED release, so shipping it verbatim
    # would label a nightly "v2.3.2".  Rewrite it to the build's actual
    # identity, which also makes it obvious that a nightly .info is not the
    # file to PR to libretro-super.
    base=$(sed -n 's/^CORE_BASE_VERSION[[:space:]]*:*=[[:space:]]*\(.*\)/\1/p' Makefile | head -1)
    sed "s/^display_version = \".*\"/display_version = \"${base}-${lane}-${GITHUB_SHA:0:7}\"/" \
      dist/info/virtualjaguar_libretro.info > "$rel/virtualjaguar_libretro.info"
  fi
fi

# Deterministic ordering, two-space format compatible with
# `sha256sum -c SHA256SUMS.txt` on Linux + Mac (with coreutils).  Built OUTSIDE
# the directory and moved in: redirecting straight to ./SHA256SUMS.txt races
# (the shell can create the empty file before the glob expands, and the list
# then contains itself with the empty-file hash).  v2.3.2 won that race once.
if command -v sha256sum >/dev/null 2>&1; then SUM="sha256sum"; else SUM="shasum -a 256"; fi
tmp=$(mktemp)
( cd "$rel" && $SUM -- * | LC_ALL=C sort -k 2 ) > "$tmp"
mv "$tmp" "$rel/SHA256SUMS.txt"

echo "==> Staged ($lane):"
ls -la "$rel"
echo "==> SHA256SUMS.txt:"
cat "$rel/SHA256SUMS.txt"

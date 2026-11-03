#!/usr/bin/env bash
# Record what a signing step did to one release asset, for the
# verify-signatures gate (issue #1152).
#
#   scripts/release/signing-status.sh <asset-name> <signed> <notarized> <method> <dir> [detail]
#
# Writes <dir>/<asset-name>.status.  The job uploads <dir> as an artifact
# named signing-status-<something>; release.yml's stage job collects them and
# the gate reads them for the facts it cannot re-derive on its own runner:
# whether Apple actually issued a notarization ticket for a bare Mach-O file
# (not stapleable, checked online by Gatekeeper) and whether the AppImage
# signing step ran.  Everything the gate CAN check itself (codesign authority,
# Authenticode, the embedded AppImage signature, attestations) it re-checks
# from the file, never from this sidecar.
set -eu
asset=${1:?asset name}; signed=${2:-false}; notarized=${3:-false}; method=${4:-none}; dir=${5:?dir}
detail=${6:-}
mkdir -p "$dir"
{
  echo "asset=$asset"
  echo "signed=$signed"
  echo "notarized=$notarized"
  echo "method=$method"
  echo "detail=$detail"
  echo "job=${GITHUB_JOB:-local}"
  echo "run=${GITHUB_RUN_ID:-0}"
} > "$dir/$asset.status"
echo "signing status: $asset signed=$signed notarized=$notarized ($method)"

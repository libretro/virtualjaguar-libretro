#!/usr/bin/env bash
# The "Signatures, provenance and checksums" section of a release body
# (issues #1151 / #1152): the gate's per-artifact table, how to verify, and
# the SHA-256 list.  Printed to stdout; release.yml appends it to the curated
# notes (tag) or the generated rolling body (nightly / prerelease).
#
#   scripts/release/body-signing.sh <release-dir> <table.md|""> <owner/repo> <default-branch> <download-url>
set -euo pipefail
rel=${1:?release dir}; table=${2:-}; repo=${3:?owner/repo}; branch=${4:-master}
download_url=${5:?download URL}
here=$(cd "$(dirname "$0")/../.." && pwd)
minipub="$here/docs/release-signing.pub"
server=${GITHUB_SERVER_URL:-https://github.com}

echo "## Signatures, provenance and checksums"
echo
echo '<details><summary>All files and signing status</summary>'
echo
if [ -n "$table" ] && [ -s "$table" ]; then
  sed -E "s@^\| \`([A-Za-z0-9_.-]+)\` \|@| [\`\1\`]($download_url/\1) |@" "$table"
else
  echo "_The signing gate did not produce a table for this run (see the workflow log)._"
  echo
  for file in "$rel"/*; do
    [ -f "$file" ] || continue
    name=${file##*/}
    printf -- '- [`%s`](%s/%s)\n' "$name" "$download_url" "$name"
  done
fi
echo
echo '</details>'
echo
echo "Windows builds use Azure Artifact Signing when configured. See the table above for the Authenticode verification result for this build."
echo
echo "Every asset above carries a GitHub build-provenance attestation that names the commit and the workflow run that built it."
echo
echo '```sh'
echo "gh attestation verify <file> --repo $repo        # provenance: who built it, from which commit (GitHub CLI 2.49+)"
if [ -f "$minipub" ]; then
  echo "minisign -Vm SHA256SUMS.txt -P $(sed -n 2p "$minipub")"
  echo "                                                  # the checksum list is signed with the project release key"
fi
echo "sha256sum --check SHA256SUMS.txt                  # Linux; macOS: shasum -a 256 --check SHA256SUMS.txt"
echo '```'
echo
echo "AppImages: \`python3 app/packaging/linux/verify-appimage.py <file>.AppImage\` checks the embedded GPG signature. Full instructions and antivirus false-positive guidance: [SECURITY.md]($server/$repo/blob/$branch/SECURITY.md)."
echo
echo "<details><summary>SHA-256 checksums</summary>"
echo
echo '```'
cat "$rel/SHA256SUMS.txt"
echo '```'
echo
echo "</details>"

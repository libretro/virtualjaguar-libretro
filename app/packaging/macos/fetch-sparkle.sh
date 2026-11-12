#!/bin/bash
# Download the pinned universal Sparkle SDK into a new directory.
set -euo pipefail
[ "$#" -eq 1 ]
version=2.10.1
sha=1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27
archive="$(mktemp)"
trap 'rm -f -- "$archive"' EXIT
curl --proto '=https' --proto-redir '=https' -fsSL \
  "https://github.com/sparkle-project/Sparkle/releases/download/$version/Sparkle-$version.tar.xz" \
  -o "$archive"
printf '%s  %s\n' "$sha" "$archive" | shasum -a 256 -c
mkdir "$1"
tar -xf "$archive" -C "$1"

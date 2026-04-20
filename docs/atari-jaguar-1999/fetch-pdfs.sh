#!/usr/bin/env bash
# Re-download the source PDFs from cubanismo/jaguar-sdk into this directory.
# The PDFs themselves are .gitignored (they're ~73 MB); only the converted
# Markdown is checked in. Run this script if you need the originals locally
# (e.g. to re-run .convert.py after improving the conversion settings, or to
# verify a passage that the OCR garbled).
set -euo pipefail

cd "$(dirname "$0")"

echo ">> fetching PDF list from cubanismo/jaguar-sdk@master..."
urls=$(curl -sfL \
  "https://api.github.com/repos/cubanismo/jaguar-sdk/contents/jaguar/docs/dev" \
  | python3 -c "import json,sys; [print(e['download_url']) for e in json.load(sys.stdin)]")

count=$(printf '%s\n' "$urls" | wc -l | tr -d ' ')
echo ">> downloading $count files in parallel..."
printf '%s\n' "$urls" | xargs -P 8 -n 1 curl -sfLO

echo ">> URL-decoding filenames..."
python3 - <<'PY'
import os, urllib.parse
for f in os.listdir('.'):
    if '%' in f:
        new = urllib.parse.unquote(f)
        os.rename(f, new)
        print(f'    {f} -> {new}')
PY

echo ">> done. $(ls -1 *.pdf | wc -l | tr -d ' ') PDFs downloaded."

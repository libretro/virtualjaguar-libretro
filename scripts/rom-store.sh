#!/usr/bin/env bash
# rom-store.sh -- publish / fetch the Tier-1 private test corpus from an
# S3-compatible object store (issue #534 follow-up).
#
# WHY THIS EXISTS
# ---------------
# Almost every meaningful check in this repo needs a commercial ROM that
# cannot be committed: the audio pair, the #406 DRAM-scale gate, blit
# memoisation, the lightgun probe, and any cachegrind A/B on a REAL game
# rather than a GPU-heavy demo.  Without them CI runs a suite that reports
# green while skipping the parts that catch regressions -- and a skip that
# reads as a pass is this project's most expensive recurring failure mode.
#
# The full corpus is ~24 GB.  The subset the automated suite actually asks
# for is ~34 MB (see test/roms/tier1.patterns, whose entries are copied from
# real find-rom.sh call sites, so it cannot drift from what the tests want).
# 34 MB fits GitHub's 10 GB Actions cache with room to spare, so steady-state
# egress is ~zero and only a cache miss touches the store at all.
#
# STORE CHOICE IS NOT BAKED IN.  Anything with an S3 API works.  Cloudflare
# R2 is the recommended default for one reason: zero egress fees.  CI is an
# egress-shaped workload -- it downloads far more than it stores -- and that
# is exactly where per-GB bandwidth pricing (GitHub LFS packs, S3) bills you
# for a corpus that costs cents to store.
#
# THE ROMS ARE NOT PUBLIC AND MUST NOT BE.  Use a private bucket. This script
# never creates one, never sets an ACL, and never prints a credential.
#
# USAGE
#   scripts/rom-store.sh manifest   # regenerate test/roms/tier1.manifest
#                                   #   from the local private corpus
#   scripts/rom-store.sh publish    # upload the manifest's files to the store
#   scripts/rom-store.sh fetch      # download + verify into $VJ_ROM_DEST
#   scripts/rom-store.sh verify     # hash-check what is already on disk
#
# BACKENDS
#   VJ_ROM_BACKEND=github  (default when VJ_ROM_GH_REPO is set)
#       One zip attached to a release in a PRIVATE GitHub repo, moved with the
#       `gh` CLI.  Free, one secret, and `gh` is already on every runner.
#       A release ASSET rather than files committed to the repo: git keeps
#       every version of a binary forever, so re-dumping a 4 MB ROM would add
#       4 MB to every clone permanently, while an asset is replaced in place.
#       Right for Tier 1 (22 MB).  Wrong for Tier 2: CD images are GB-scale,
#       and asset/repo ceilings bite long before R2's economics do.
#
#   VJ_ROM_BACKEND=s3      (default otherwise)
#       Any S3-compatible store.  R2 recommended for zero egress.
#
#   Hosting commercial ROMs plus a BIOS image on GitHub is a takedown surface,
#   and a strike lands on the ACCOUNT, not the file.  Prefer a personal
#   account over an org that hosts the emulator itself.
#
# ENV (github backend)
#   VJ_ROM_GH_REPO   owner/repo of the private ROM repo
#   VJ_ROM_GH_TAG    release tag holding the asset   (default: tier1)
#   VJ_ROM_GH_ASSET  asset filename                  (default: tier1.zip)
#   GH_TOKEN         token with read access (write, to publish)
#
# ENV (s3 backend)
#   VJ_ROM_ENDPOINT  S3 endpoint URL. R2: https://<account>.r2.cloudflarestorage.com
#   VJ_ROM_BUCKET    bucket name
#   VJ_ROM_KEY       access key id      (GitHub secret)
#   VJ_ROM_SECRET    secret access key  (GitHub secret)
#   VJ_ROM_PREFIX    key prefix in the bucket        (default: tier1)
#   VJ_ROM_DEST      where fetch writes              (default: test/roms/tier1)
#   VJ_ROM_REGION    S3 region; R2 wants "auto"      (default: auto)
#   ROMS_PRIVATE_ROOT  source corpus for manifest/publish (find-rom.sh honours it)
#
# EXIT CODES
#   0   success
#   77  SKIP -- store not configured (no endpoint/bucket/credentials).  A
#       distinct code on purpose: callers must be able to tell "no store
#       configured" from "the store is broken".  Never silently 0.
#   1   real failure -- download error, hash mismatch, missing source ROM
set -uo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd -P)
PATTERNS="$REPO/test/roms/tier1.patterns"
MANIFEST="$REPO/test/roms/tier1.manifest"
DEST="${VJ_ROM_DEST:-$REPO/test/roms/tier1}"
case "$DEST" in /*) ;; *) DEST="$REPO/$DEST" ;; esac
PREFIX="${VJ_ROM_PREFIX:-tier1}"
REGION="${VJ_ROM_REGION:-auto}"
GH_REPO="${VJ_ROM_GH_REPO:-}"
GH_TAG="${VJ_ROM_GH_TAG:-tier1}"
GH_ASSET="${VJ_ROM_GH_ASSET:-tier1.zip}"
# Infer the backend so a caller that sets only the GitHub vars does not also
# have to remember to say so.
BACKEND="${VJ_ROM_BACKEND:-}"
if [ -z "$BACKEND" ]; then
  if [ -n "$GH_REPO" ]; then BACKEND=github; else BACKEND=s3; fi
fi

die()  { echo "rom-store: $*" >&2; exit 1; }
note() { echo "rom-store: $*"; }

# sha256: coreutils on Linux, shasum on macOS.  Same digest, different tool.
if command -v sha256sum >/dev/null 2>&1; then
  sha256() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then
  sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
  die "no sha256sum or shasum on PATH"
fi

filesize() { wc -c < "$1" | tr -d ' '; }

# ------------------------------------------------------------------ store ---
store_configured() {
  case "$BACKEND" in
    github) [ -n "$GH_REPO" ] && [ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ] ;;
    *)      [ -n "${VJ_ROM_ENDPOINT:-}" ] && [ -n "${VJ_ROM_BUCKET:-}" ] &&
            [ -n "${VJ_ROM_KEY:-}" ] && [ -n "${VJ_ROM_SECRET:-}" ] ;;
  esac
}

gh_cli() {
  command -v gh >/dev/null 2>&1 || die "gh CLI not found (brew install gh)"
  GH_TOKEN="${GH_TOKEN:-${GITHUB_TOKEN:-}}" gh "$@"
}

# The whole Tier-1 set travels as ONE zip: nine separate release assets would
# be nine round trips and nine chances to end up half-updated, and at 22 MB
# there is nothing to gain from partial transfers.
gh_publish() {
  local staging zip src rel n=0
  staging=$(mktemp -d "${TMPDIR:-/tmp}/vjroms.XXXXXX")
  zip="$staging/$GH_ASSET"
  mkdir -p "$staging/pack"
  while IFS=$'\t' read -r src rel; do
    [ -n "${rel:-}" ] || continue
    mkdir -p "$staging/pack/$(dirname "$rel")"
    cp "$src" "$staging/pack/$rel"
    n=$((n + 1))
  done < <(resolve_patterns) || { command rm -rf "$staging"; die "local corpus is incomplete; cannot publish"; }
  ( cd "$staging/pack" && zip -q -r -X "$zip" . ) || { command rm -rf "$staging"; die "zip failed"; }
  note "packed $n file(s) -> $GH_ASSET ($(filesize "$zip") bytes)"
  # Create the release on first publish; --clobber replaces the asset after.
  gh_cli release view "$GH_TAG" --repo "$GH_REPO" >/dev/null 2>&1 || {
    note "creating release $GH_TAG in $GH_REPO"
    gh_cli release create "$GH_TAG" --repo "$GH_REPO" \
        --title "Tier-1 test ROMs" \
        --notes "Tier-1 private test corpus. Contents and hashes: test/roms/tier1.manifest" \
      >/dev/null || { command rm -rf "$staging"; die "could not create release $GH_TAG"; }
  }
  gh_cli release upload "$GH_TAG" "$zip" --repo "$GH_REPO" --clobber >/dev/null \
    || { command rm -rf "$staging"; die "asset upload failed"; }
  command rm -rf "$staging"
  note "published $n file(s) to $GH_REPO release $GH_TAG ($GH_ASSET)"
}

gh_fetch() {
  local staging
  staging=$(mktemp -d "${TMPDIR:-/tmp}/vjroms.XXXXXX")
  note "get $GH_REPO release $GH_TAG :: $GH_ASSET"
  gh_cli release download "$GH_TAG" --repo "$GH_REPO" --pattern "$GH_ASSET" \
      --dir "$staging" --clobber >/dev/null \
    || { command rm -rf "$staging"; die "release download failed ($GH_REPO $GH_TAG)"; }
  command -v unzip >/dev/null 2>&1 || { command rm -rf "$staging"; die "unzip not found"; }
  # Unpack to a staging tree, NOT straight over DEST: the per-file hash check
  # in cmd_verify below is what decides these are good, and a half-extracted
  # archive must never be able to masquerade as a verified corpus.
  unzip -qo "$staging/$GH_ASSET" -d "$staging/pack" \
    || { command rm -rf "$staging"; die "unzip failed"; }
  mkdir -p "$DEST"
  ( cd "$staging/pack" && find . -type f -print0 | while IFS= read -r -d '' f; do
      mkdir -p "$DEST/$(dirname "${f#./}")"
      mv -f "$f" "$DEST/${f#./}"
    done )
  command rm -rf "$staging"
}

# aws CLI is the transport: it is the one S3 client already present on GitHub
# runners, so CI needs no install step.  Credentials go in the environment of
# the child only -- never written to ~/.aws, never echoed.
aws_s3() {
  command -v aws >/dev/null 2>&1 || die "aws CLI not found (brew install awscli / apt-get install awscli)"
  AWS_ACCESS_KEY_ID="$VJ_ROM_KEY" \
  AWS_SECRET_ACCESS_KEY="$VJ_ROM_SECRET" \
  AWS_DEFAULT_REGION="$REGION" \
  AWS_EC2_METADATA_DISABLED=true \
  AWS_REQUEST_CHECKSUM_CALCULATION=when_required \
  AWS_RESPONSE_CHECKSUM_VALIDATION=when_required \
    aws s3 --endpoint-url "$VJ_ROM_ENDPOINT" "$@"
}

# --------------------------------------------------------------- manifest ---
# Format: "<sha256>  <size>  <path-under-DEST>", sorted by path so the file is
# diffable and its own hash is stable.  Not sha256sum(1) format, because the
# size column is what lets fetch detect a truncated download before hashing.
# Resolve every pattern line against the local corpus, emitting
#   "<local-abs-path><TAB><path-under-DEST>"
# Both `manifest` and `publish` go through this, so they cannot disagree
# about which local file an entry means.  Re-deriving the source by globbing
# the manifest's basename is what the first version did, and it broke on
# "[BIOS] Atari Jaguar (World).j64": `find -iname` reads "[BIOS]" as a
# CHARACTER CLASS matching one of B/I/O/S, so the file was unfindable by its
# own name.
resolve_patterns() {  # -> lines on stdout; returns 1 if anything is missing
  local line pats f missing=0
  while IFS= read -r line; do
    case "$line" in ''|'#'*) continue ;; esac
    IFS='|' read -r -a pats <<< "$line"
    f=$( cd "$REPO" && bash scripts/find-rom.sh "${pats[@]}" 2>/dev/null ) || f=""
    if [ -z "$f" ]; then
      echo "rom-store: MISSING from local corpus: ${pats[0]}" >&2
      missing=$((missing + 1)); continue
    fi
    case "$f" in /*) ;; *) f="$REPO/$f" ;; esac
    printf '%s\t%s\n' "$f" "ROMS/$(basename "$f")"
  done < "$PATTERNS"
  [ "$missing" -eq 0 ]
}

cmd_manifest() {
  [ -f "$PATTERNS" ] || die "no pattern list at $PATTERNS"
  local src rel n=0 complete=1
  : > "$MANIFEST.tmp"
  while IFS=$'\t' read -r src rel; do
    [ -n "${rel:-}" ] || continue
    printf '%s  %s  %s\n' "$(sha256 "$src")" "$(filesize "$src")" "$rel" >> "$MANIFEST.tmp"
    n=$((n + 1))
  done < <(resolve_patterns) || complete=0
  LC_ALL=C sort -k3 "$MANIFEST.tmp" > "$MANIFEST"
  command rm -f "$MANIFEST.tmp"
  note "manifest: $n entries -> $MANIFEST"
  [ "$complete" -eq 1 ] || die "pattern(s) matched nothing; manifest is INCOMPLETE"
}

# ---------------------------------------------------------------- publish ---
cmd_publish() {
  [ -f "$MANIFEST" ] || die "no manifest -- run: $0 manifest"
  store_configured || { note "SKIP: store not configured (see ENV in $0)"; exit 77; }
  if [ "$BACKEND" = github ]; then gh_publish; return; fi
  local src rel n=0 want
  while IFS=$'\t' read -r src rel; do
    [ -n "${rel:-}" ] || continue
    # Cross-check against the manifest before uploading: publishing a file
    # whose hash the manifest does not name would give every later fetch a
    # mismatch that looks like a corrupt transfer.
    want=$(awk -v r="$rel" '$0 ~ /  /{ i=index($0,"  "); h=substr($0,1,i-1); rest=substr($0,i+2); j=index(rest,"  "); p=substr(rest,j+2); if (p==r) print h }' "$MANIFEST")
    [ -n "$want" ] || die "$rel is not in the manifest -- run: $0 manifest"
    [ "$(sha256 "$src")" = "$want" ] || die "$rel: local copy does not match the manifest (regenerate it)"
    note "put $rel"
    aws_s3 cp "$src" "s3://$VJ_ROM_BUCKET/$PREFIX/$rel" >/dev/null || die "upload failed: $rel"
    n=$((n + 1))
  done < <(resolve_patterns) || die "local corpus is incomplete; cannot publish"
  note "published $n file(s) to s3://$VJ_ROM_BUCKET/$PREFIX/"
}

# ------------------------------------------------------------------ fetch ---
cmd_fetch() {
  [ -f "$MANIFEST" ] || die "no manifest at $MANIFEST"
  local hash size rel dst tmp have=0 got=0
  # A pre-pass: if every file is already present and correct, the store is
  # never contacted at all.  This is what makes an Actions cache hit free.
  if cmd_verify --quiet; then
    note "all $(grep -cv '^$' "$MANIFEST") file(s) already present and verified in $DEST"
    return 0
  fi
  store_configured || {
    # Name the variables for the backend actually in play. Telling a GitHub
    # user to set VJ_ROM_KEY sends them looking for a credential that has no
    # bearing on why their fetch skipped.
    if [ "$BACKEND" = github ]; then
      note "SKIP: store not configured -- set VJ_ROM_GH_REPO and GH_TOKEN."
    else
      note "SKIP: store not configured -- set VJ_ROM_ENDPOINT / VJ_ROM_BUCKET /"
      note "      VJ_ROM_KEY / VJ_ROM_SECRET."
    fi
    note "      Local devs with their own corpus should point"
    note "      ROMS_PRIVATE_ROOT at it and skip this entirely."
    exit 77
  }
  if [ "$BACKEND" = github ]; then
    gh_fetch
    if cmd_verify; then
      note "fetch complete -> $DEST"
      note "point the suite at it with:  ROMS_PRIVATE_ROOT=$DEST"
      return 0
    fi
    die "fetched archive does not match the manifest"
  fi
  while read -r hash size rel; do
    [ -n "${rel:-}" ] || continue
    dst="$DEST/$rel"
    if [ -f "$dst" ] && [ "$(filesize "$dst")" = "$size" ] && [ "$(sha256 "$dst")" = "$hash" ]; then
      have=$((have + 1)); continue
    fi
    mkdir -p "$(dirname "$dst")"
    tmp="$dst.part"
    note "get $rel"
    aws_s3 cp "s3://$VJ_ROM_BUCKET/$PREFIX/$rel" "$tmp" >/dev/null || {
      command rm -f "$tmp"; die "download failed: $rel"; }
    # Verify BEFORE it lands at the real name, so a corrupt transfer can never
    # be picked up by a later run as a valid cached copy.
    [ "$(filesize "$tmp")" = "$size" ] || { command rm -f "$tmp"; die "$rel: size mismatch"; }
    [ "$(sha256 "$tmp")" = "$hash" ]  || { command rm -f "$tmp"; die "$rel: SHA-256 mismatch"; }
    mv -f "$tmp" "$dst"
    got=$((got + 1))
  done < "$MANIFEST"
  note "fetch complete: $got downloaded, $have already present -> $DEST"
  note "point the suite at it with:  ROMS_PRIVATE_ROOT=$DEST"
}

# ----------------------------------------------------------------- verify ---
cmd_verify() {
  local quiet=0; [ "${1:-}" = --quiet ] && quiet=1
  [ -f "$MANIFEST" ] || { [ "$quiet" = 1 ] && return 1; die "no manifest at $MANIFEST"; }
  local hash size rel dst bad=0 ok=0
  while read -r hash size rel; do
    [ -n "${rel:-}" ] || continue
    dst="$DEST/$rel"
    if [ ! -f "$dst" ]; then
      [ "$quiet" = 1 ] || echo "  MISSING  $rel"; bad=$((bad + 1)); continue
    fi
    if [ "$(filesize "$dst")" != "$size" ] || [ "$(sha256 "$dst")" != "$hash" ]; then
      [ "$quiet" = 1 ] || echo "  CORRUPT  $rel"; bad=$((bad + 1)); continue
    fi
    [ "$quiet" = 1 ] || echo "  ok       $rel"; ok=$((ok + 1))
  done < "$MANIFEST"
  [ "$quiet" = 1 ] || note "verify: $ok ok, $bad bad (in $DEST)"
  [ "$bad" -eq 0 ]
}

case "${1:-}" in
  manifest) cmd_manifest ;;
  publish)  cmd_publish ;;
  fetch)    cmd_fetch ;;
  verify)   cmd_verify ;;
  *) sed -n '2,60p' "$0"; exit 2 ;;
esac

#!/bin/bash
# Body of the macos-sign composite action (see action.yml).  One step does everything so a single
# EXIT trap can delete the temporary keychain whatever happens.  No `set -x`: the environment holds
# the signing secrets.
set -euo pipefail

cs="$GITHUB_ACTION_PATH/../../../app/packaging/macos/codesign.sh"
tmp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
out="${GITHUB_OUTPUT:-/dev/null}"

if [ -z "$APPLE_DEVELOPER_ID_P12_BASE64" ] || [ -z "$APPLE_DEVELOPER_ID_P12_PASSWORD" ] \
    || [ -z "$APPLE_NOTARY_KEY_ID" ] || [ -z "$APPLE_NOTARY_ISSUER_ID" ] || [ -z "$APPLE_NOTARY_KEY_P8_BASE64" ]; then
  echo "::warning::macOS build is NOT Developer ID signed or notarized" \
    "(the APPLE_* secrets are missing: fork pull requests never receive them)." \
    "It is ad-hoc signed or unsigned; Gatekeeper refuses it until right-click > Open."
  { echo "signed=false"; echo "notarized=false"; } >> "$out"
  exit 0
fi

# A tag is a release: a notarization failure must stop it.  Anywhere else (pull request, manual run,
# push to develop / release/**) it only warns and the signed build is kept.
strict=false
[ "${GITHUB_REF_TYPE:-}" = tag ] && strict=true
if [ "$strict" = true ]; then
  export VJ_NOTARY_FAIL_LEVEL=error VJ_NOTARY_TIMEOUT=45m
else
  export VJ_NOTARY_FAIL_LEVEL=warning VJ_NOTARY_TIMEOUT=25m
fi

targets=()
while IFS= read -r line; do
  line="${line#"${line%%[![:space:]]*}"}"; line="${line%"${line##*[![:space:]]}"}"
  [ -n "$line" ] && targets+=("$line")
done <<< "$VJ_PATHS"
[ "${#targets[@]}" -gt 0 ] || { echo "::error::macos-sign: no paths given"; exit 1; }
for t in "${targets[@]}"; do
  [ -e "$t" ] || { echo "::error::macos-sign: $t does not exist"; exit 1; }
done

stage="$(mktemp -d "$tmp/macos-sign.XXXXXX")"
cleanup() {
  security delete-keychain vj-signing.keychain-db >/dev/null 2>&1 || true
  rm -rf "$stage" "$tmp/devid.p12" "$tmp/notary.p8"
}
trap cleanup EXIT

bash "$cs" import-cert

for t in "${targets[@]}"; do
  case "$t" in
    *.app) bash "$cs" sign-app "$t" ;;
    *)     bash "$cs" sign-file "$t" "$VJ_ENTITLEMENTS" ;;
  esac
done
echo "signed=true" >> "$out"

notarized=false
if [ "$VJ_NOTARIZE" = true ]; then
  # One submission for everything: copies in a staging dir, zipped.  The staged copies are only
  # shipped to Apple; the ticket is looked up by code-directory hash, so stapling the originals works.
  for t in "${targets[@]}"; do
    n="$(basename "$t")"
    [ ! -e "$stage/$n" ] || { echo "::error::macos-sign: two paths named $n"; exit 1; }
    ditto "$t" "$stage/$n"
  done
  zip="$tmp/macos-sign-notarize.zip"
  command rm -f "$zip"
  ditto -c -k --sequesterRsrc "$stage" "$zip"
  if bash "$cs" notarize "$zip"; then
    notarized=true
  elif [ "$strict" = true ]; then
    echo "::error::notarization failed on a release tag"
    exit 1
  else
    echo "::warning::notarization did not complete (Apple notary outage, timeout or rejection);" \
      "keeping the Developer ID signed but NOT notarized build"
  fi
  command rm -f "$zip"
  if [ "$notarized" = true ]; then
    for t in "${targets[@]}"; do
      case "$t" in
        *.app) bash "$cs" staple "$t"; spctl -a -t exec -vv "$t" ;;
        *)     echo "notarized (cannot be stapled; Gatekeeper checks online): $t" ;;
      esac
    done
  fi
fi
echo "notarized=$notarized" >> "$out"

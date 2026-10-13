#!/bin/bash
# Code signing + notarization helpers (used by .github/workflows/app-package.yml).
#
#   codesign.sh import-cert        create a temp keychain from the P12 secrets
#   codesign.sh sign-app <app> [adhoc]   sign inside-out (dylibs, then the bundle)
#   codesign.sh notarize <file>    submit (zip/pkg), wait, fail on Invalid
#   codesign.sh staple <file>      staple the ticket
#
# Environment (GitHub secrets mapped by the workflow; see app/packaging/README.md):
#   APPLE_DEVELOPER_ID_P12_BASE64, APPLE_DEVELOPER_ID_P12_PASSWORD   Developer ID Application cert
#   APPLE_NOTARY_KEY_ID, APPLE_NOTARY_ISSUER_ID, APPLE_NOTARY_KEY_P8_BASE64   App Store Connect API key
# Local use (see local-package.sh): VJ_SIGN_IDENTITY (name or SHA-1), and for notarize either
#   VJ_NOTARY_PROFILE or APPLE_NOTARY_KEY_ID + APPLE_NOTARY_ISSUER_ID + APPLE_NOTARY_KEY_PATH.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
kc_name=vj-signing.keychain-db

# VJ_SIGN_IDENTITY=-  signs ad hoc through the SAME code path (hardened runtime, nested code
# first, entitlements) -- used to exercise the script without a certificate.
identity() {
  [ -n "${VJ_SIGN_IDENTITY:-}" ] && { echo "$VJ_SIGN_IDENTITY"; return; }
  security find-identity -v -p codesigning "$kc_name" | sed -n 's/.*"\(Developer ID Application:.*\)"$/\1/p' | head -1
}

case "${1:-}" in
  import-cert)
    : "${APPLE_DEVELOPER_ID_P12_BASE64:?}" "${APPLE_DEVELOPER_ID_P12_PASSWORD:?}"
    tmp="${RUNNER_TEMP:-/tmp}"
    pw="$(uuidgen)"
    echo "$APPLE_DEVELOPER_ID_P12_BASE64" | base64 --decode > "$tmp/devid.p12"
    security create-keychain -p "$pw" "$kc_name"
    security set-keychain-settings -lut 21600 "$kc_name"
    security unlock-keychain -p "$pw" "$kc_name"
    security import "$tmp/devid.p12" -k "$kc_name" -P "$APPLE_DEVELOPER_ID_P12_PASSWORD" \
        -T /usr/bin/codesign -T /usr/bin/security
    security set-key-partition-list -S apple-tool:,apple: -s -k "$pw" "$kc_name" >/dev/null
    security list-keychains -d user -s "$kc_name" login.keychain-db
    rm -f "$tmp/devid.p12"
    id="$(identity)"
    [ -n "$id" ] || { echo "no 'Developer ID Application' identity in the P12" >&2; exit 1; }
    echo "imported identity: $id"
    ;;
  sign-app)
    app="$2"; mode="${3:-}"
    if [ "$mode" = adhoc ]; then
      # Unsigned-by-us build (forks, PRs): an ad-hoc signature is still REQUIRED on
      # Apple Silicon, or the app reports "damaged".  Gatekeeper still refuses it
      # (unidentified developer) until the user opens it via right-click > Open.
      codesign --force --deep -s - "$app"
      codesign --verify --deep --strict "$app"
      exit 0
    fi
    id="$(identity)"; [ -n "$id" ] || { echo "no identity; run import-cert" >&2; exit 1; }
    ts=--timestamp; [ "$id" = - ] && ts=--timestamp=none
    # Inside-out: every nested Mach-O first, then the bundle with the entitlements.
    while IFS= read -r -d '' f; do
      if file -b "$f" | grep -q 'Mach-O' && [ "$f" != "$app/Contents/MacOS/virtualjaguar" ]; then
        codesign --force --options runtime "$ts" --sign "$id" "$f"
      fi
    done < <(find "$app/Contents" -type f -print0)
    codesign --force --options runtime "$ts" --entitlements "$here/entitlements.plist" \
        --sign "$id" "$app"
    codesign --verify --deep --strict --verbose=2 "$app"
    codesign -dv --entitlements - "$app" 2>&1 | sed -n '1,25p'
    ;;
  notarize)
    # Credentials, first match wins (CI uses the third; local builds the first two):
    #   VJ_NOTARY_PROFILE                       a `xcrun notarytool store-credentials` profile
    #   APPLE_NOTARY_KEY_ID/_ISSUER_ID/_KEY_PATH  App Store Connect API key, .p8 on disk
    #   APPLE_NOTARY_KEY_ID/_ISSUER_ID/_KEY_P8_BASE64  the same key, base64 (GitHub secret)
    tmp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
    p8=""; made_p8=""
    if [ -n "${VJ_NOTARY_PROFILE:-}" ]; then
      auth=(--keychain-profile "$VJ_NOTARY_PROFILE")
    else
      : "${APPLE_NOTARY_KEY_ID:?}" "${APPLE_NOTARY_ISSUER_ID:?}"
      if [ -n "${APPLE_NOTARY_KEY_PATH:-}" ]; then
        p8="$APPLE_NOTARY_KEY_PATH"
        [ -f "$p8" ] || { echo "APPLE_NOTARY_KEY_PATH is not a file" >&2; exit 1; }
      else
        : "${APPLE_NOTARY_KEY_P8_BASE64:?}"
        p8="$tmp/notary.p8"; made_p8=1
        echo "$APPLE_NOTARY_KEY_P8_BASE64" | base64 --decode > "$p8"
      fi
      auth=(--key "$p8" --key-id "$APPLE_NOTARY_KEY_ID" --issuer "$APPLE_NOTARY_ISSUER_ID")
    fi
    log="$tmp/notarytool.json"
    set +e
    xcrun notarytool submit "$2" "${auth[@]}" --wait --timeout 45m --output-format json > "$log"
    rc=$?
    set -e
    cat "$log"
    status="$(sed -n 's/.*"status" *: *"\([^"]*\)".*/\1/p' "$log" | head -1)"
    if [ "$rc" -ne 0 ] || [ "$status" != "Accepted" ]; then
      subid="$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' "$log" | head -1)"
      [ -n "$subid" ] && xcrun notarytool log "$subid" "${auth[@]}" || true
      [ -z "$made_p8" ] || rm -f "$p8"
      echo "::error::notarization of $2 ended '$status'"
      exit 1
    fi
    [ -z "$made_p8" ] || rm -f "$p8"
    ;;
  staple)
    xcrun stapler staple "$2"
    xcrun stapler validate "$2"
    ;;
  *)
    sed -n '2,12p' "$0"; exit 2 ;;
esac

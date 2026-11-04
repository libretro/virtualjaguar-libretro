#!/usr/bin/env bash
# The release signing gate (issue #1152): for every staged release asset,
# compare its actual signing state with what its asset class is expected to
# have, and write one per-artifact table for the job summary and the release
# body.
#
#   scripts/release/verify.sh <release-dir> <status-dir> <strict|warn> <table.md>
#
#   release-dir   the flattened assets + SHA256SUMS.txt (scripts/release/stage.sh)
#   status-dir    the signing sidecars (scripts/release/signing-status.sh)
#   strict        a failed expectation fails the script (tags, release/**)
#   warn          failed expectations are ::warning:: annotations and the exit
#                 status is 0 (develop nightlies, dry runs): a signing-service
#                 outage must never withhold the core nightly
#   table.md      where the markdown table goes (also appended to
#                 $GITHUB_STEP_SUMMARY when that is set)
#
# Environment:
#   REPO                   owner/name for `gh attestation verify` (default libretro/virtualjaguar-libretro)
#   GH_TOKEN               for gh
#   SKIP_ATTEST=1          do not call gh (hermetic tests); the column says "skipped"
#   WINDOWS_EXPECT         see the expectation table below (Windows cores and tools)
#   WINDOWS_APP_EXPECT     same, for the Windows app zips and installers (#1150)
#   MINISIGN_PUBKEY_FILE   default docs/release-signing.pub
#   APPIMAGE_PUBKEY_FILE   default docs/release-signing-appimage.asc
#
# Needs a macOS runner for the Apple checks (codesign, spctl, stapler); on
# another host those rows report "unknown" and fail in strict mode.
# Optional tools: minisign (checksum signature), osslsigncode (Authenticode),
# gpg + python3 (AppImage signature, app/packaging/linux/verify-appimage.py).
set -euo pipefail

# ---------------------------------------------------------------------------
# Expected platform-signature state per asset class.  Change ONE line here when
# a class gains a signature; the gate then enforces it on tags and release/**.
# ---------------------------------------------------------------------------
EXPECT_MACOS="signed-notarized"                # Developer ID Application + Apple notarization ticket (#989)
EXPECT_WINDOWS=${WINDOWS_EXPECT:-unsigned}    # core DLLs, cue2cdi, jagcd tools: release.yml passes "signed" when the Azure Artifact Signing secrets exist (#1150)
EXPECT_WINDOWS_APP=${WINDOWS_APP_EXPECT:-unsigned}  # app zips + installers: same; a separate flag so the two classes can be gated apart
EXPECT_IOS=unsigned                           # Developer ID does not apply; Provenance signs them when it embeds them
EXPECT_APPIMAGE=auto                          # "signed" once docs/release-signing-appimage.asc is committed (#1151)
EXPECT_SUMS_SIG=auto                          # required once docs/release-signing.pub is committed (#1151)
EXPECT_ATTEST=required                        # every asset carries a GitHub build-provenance attestation (#1151)
# ---------------------------------------------------------------------------

rel=${1:?release dir}; st=${2:?status dir}; mode=${3:?strict|warn}; table=${4:?table.md}
case "$mode" in strict|warn) ;; *) echo "mode must be strict or warn" >&2; exit 2 ;; esac
REPO=${REPO:-libretro/virtualjaguar-libretro}
here=$(cd "$(dirname "$0")/../.." && pwd)
minipub=${MINISIGN_PUBKEY_FILE:-$here/docs/release-signing.pub}
apppub=${APPIMAGE_PUBKEY_FILE:-$here/docs/release-signing-appimage.asc}
verify_appimage="$here/app/packaging/linux/verify-appimage.py"

[ "$EXPECT_APPIMAGE" != auto ] || { if [ -f "$apppub" ]; then EXPECT_APPIMAGE=signed; else EXPECT_APPIMAGE=unsigned; fi; }
[ "$EXPECT_SUMS_SIG" != auto ] || { if [ -f "$minipub" ]; then EXPECT_SUMS_SIG=required; else EXPECT_SUMS_SIG=optional; fi; }

tmp=$(mktemp -d)
trap 'command rm -rf "$tmp"' EXIT
rows=()
fails=()
have() { command -v "$1" >/dev/null 2>&1; }

# ---- sidecars -------------------------------------------------------------
sc() { # sc <asset> <key> -> value or ""
  [ -f "$st/$1.status" ] && sed -n "s/^$2=//p" "$st/$1.status" | head -1 || true
}

# ---- Apple ----------------------------------------------------------------
macho_signature() { # <file> -> "Developer ID (TEAM)" | "ad-hoc" | "none" | "other (...)" | "unknown (...)"
  have codesign || { echo "unknown (no codesign on this host)"; return; }
  local info auth team
  if ! info=$(codesign -dvv "$1" 2>&1); then echo "none"; return; fi
  if printf '%s\n' "$info" | grep -q '^Signature=adhoc'; then echo "ad-hoc"; return; fi
  auth=$(printf '%s\n' "$info" | grep -c '^Authority=Developer ID Application:' || true)
  team=$(printf '%s\n' "$info" | sed -n 's/^TeamIdentifier=//p' | head -1)
  if [ "$auth" -gt 0 ] && codesign --verify --strict "$1" >/dev/null 2>&1; then
    echo "Developer ID (${team:-?})"
  else
    echo "other ($(printf '%s\n' "$info" | sed -n 's/^Authority=//p' | head -1))"
  fi
}
spctl_notarized() { # <file-or-app> -> 0 when Gatekeeper reports a notarization ticket
  have spctl || return 1
  spctl -a -t exec -vv "$1" 2>&1 | grep -q 'source=Notarized Developer ID'
}
stapled() { have xcrun && xcrun stapler validate "$1" >/dev/null 2>&1; }

# ---- Windows --------------------------------------------------------------
pe_signature() { # <file> -> "Authenticode (CN)" | "none" | "invalid (...)" | "unknown (...)"
  have osslsigncode || { echo "unknown (osslsigncode missing)"; return; }
  local out cn
  out=$(osslsigncode verify -in "$1" 2>&1 || true)
  if printf '%s\n' "$out" | grep -qi 'No signature found'; then echo "none"; return; fi
  cn=$(printf '%s\n' "$out" | sed -n '/Signer.s certificate/,/Subject/{s/.*Subject: .*CN=\([^/,]*\).*/\1/p;}' | head -1)
  if printf '%s\n' "$out" | grep -q 'Signature verification: ok'; then
    echo "Authenticode (${cn:-?})"
  elif printf '%s\n' "$out" | grep -qi 'Subject:'; then
    echo "invalid (${cn:-?})"
  else
    echo "none"
  fi
}

# ---- AppImage -------------------------------------------------------------
appimage_signature() { # <file> -> "GPG <fpr>" | "GPG <fpr> (NOT the project key)" | "bad" | "none" | "unknown (...)"
  have python3 || { echo "unknown (no python3)"; return; }
  local args=("$verify_appimage" "$1" --json) out rc
  [ ! -f "$apppub" ] || args+=(--pubkey "$apppub")
  out=$(python3 -I "${args[@]}" 2>/dev/null) && rc=0 || rc=$?
  case "$rc" in
    0) echo "GPG $(printf '%s' "$out" | sed -n 's/.*"fingerprint": "\([0-9A-F]*\)".*/\1/p')" ;;
    1) echo "none" ;;
    2) echo "bad" ;;
    3) echo "GPG $(printf '%s' "$out" | sed -n 's/.*"fingerprint": "\([0-9A-F]*\)".*/\1/p') (NOT the project key)" ;;
    *) echo "unknown ($(printf '%s' "$out" | sed -n 's/.*"detail": "\([^"]*\)".*/\1/p'))" ;;
  esac
}

# ---- attestation ----------------------------------------------------------
attested() { # <file> -> "yes" | "no (reason)" | "skipped"
  if [ "${SKIP_ATTEST:-0}" = 1 ]; then echo "skipped"; return; fi
  have gh || { echo "no (gh missing)"; return; }
  local out
  if out=$(gh attestation verify "$1" --repo "$REPO" 2>&1); then echo "yes"
  else echo "no ($(printf '%s\n' "$out" | grep -v '^$' | tail -1 | cut -c1-80))"; fi
}

# ---- rows -----------------------------------------------------------------
# row <asset> <class> <signed> <notarized> <attested> <ok|FAIL reason>
row() {
  local res="✅"
  if [ "$6" != ok ]; then res="❌ $6"; fails+=("$1: $6"); fi
  rows+=("| \`$1\` | $2 | $3 | $4 | $5 | $res |")
}
classify() {
  case "$1" in
    virtualjaguar_libretro-macos-*.dylib) echo macos-core ;;
    cue2cdi-macos*) echo macos-tool ;;
    jagcd-tools-macos*.zip) echo macos-tools-zip ;;
    VirtualJaguar-*-macos-*.zip) echo macos-app-zip ;;
    virtualjaguar_libretro-ios-*.dylib|virtualjaguar_libretro-tvos-*.dylib) echo ios ;;
    VirtualJaguar-*-windows-*-setup.exe) echo windows-installer ;;
    virtualjaguar_libretro-windows-*.dll|cue2cdi-windows-*.exe) echo windows-pe ;;
    VirtualJaguar-*-windows-*.zip) echo windows-app-zip ;;
    jagcd-tools-windows*.zip) echo windows-tools-zip ;;
    *.AppImage) echo appimage ;;
    *.deb) echo deb ;;
    *.flatpak) echo flatpak ;;
    *-debug.tar.gz) echo debug-symbols ;;
    virtualjaguar_libretro.info) echo core-info ;;
    SHA256SUMS.txt) echo checksums ;;
    SHA256SUMS.txt.minisig) echo checksum-signature ;;
    virtualjaguar_libretro-*.so|virtualjaguar_libretro-*.bc|virtualjaguar_libretro-*.a|cue2cdi-linux*|jagcd-tools-linux*.zip) echo core-or-tool ;;
    *) echo other ;;
  esac
}
want_macos() { # <signed-desc> <notarized yes|no> -> ok|reason
  case "$1" in "Developer ID"*) ;; *) echo "not Developer ID signed ($1)"; return ;; esac
  [ "$2" = yes ] || { echo "not notarized"; return; }
  echo ok
}
want_windows() { # <signed-desc> <expectation> -> ok|reason
  case "$2" in
    signed) case "$1" in Authenticode*) echo ok ;; *) echo "no Authenticode signature ($1)" ;; esac ;;
    *)      echo ok ;;
  esac
}
extract_zip() { # <zip> <dir>
  mkdir -p "$2"
  if have ditto; then ditto -x -k "$1" "$2"; else unzip -q "$1" -d "$2"; fi
}

files=()
while IFS= read -r f; do files+=("$f"); done < <(cd "$rel" && find . -maxdepth 1 -type f | sed 's|^\./||' | LC_ALL=C sort)
[ "${#files[@]}" -gt 0 ] || { echo "::error::$rel is empty"; exit 1; }

echo "==> verifying ${#files[@]} assets in $rel (mode $mode; windows app expected: $EXPECT_WINDOWS_APP, windows cores/tools: $EXPECT_WINDOWS, appimage: $EXPECT_APPIMAGE, checksum signature: $EXPECT_SUMS_SIG)"

# Checksum list consistency first: everything else is keyed by it.
sums_ok=yes
if [ -f "$rel/SHA256SUMS.txt" ]; then
  if have sha256sum; then ( cd "$rel" && sha256sum --check --quiet SHA256SUMS.txt ) || sums_ok=no
  else ( cd "$rel" && shasum -a 256 --check --quiet SHA256SUMS.txt ) || sums_ok=no; fi
else
  sums_ok=missing
fi

for f in "${files[@]}"; do
  p="$rel/$f"; cls=$(classify "$f"); att=$(attested "$p")
  attres=ok
  case "$att" in yes|skipped) ;; *) [ "$EXPECT_ATTEST" != required ] || attres="no attestation" ;; esac
  signed="n/a"; notar="n/a"; res=ok
  case "$cls" in
    macos-core)
      signed=$(macho_signature "$p")
      notar=no; [ "$(sc "$f" notarized)" != true ] || notar=yes
      res=$(want_macos "$signed" "$notar") ;;
    macos-tool)
      signed=$(macho_signature "$p")
      notar=no; { [ "$(sc "$f" notarized)" = true ] || spctl_notarized "$p"; } && notar=yes
      res=$(want_macos "$signed" "$notar") ;;
    macos-tools-zip)
      d="$tmp/$f.d"; extract_zip "$p" "$d"
      signed=""; notar=yes; res=ok
      for exe in chdman jagcd-chd-check; do
        x=$(find "$d" -type f -name "$exe" | head -1)
        [ -n "$x" ] || { res="$exe missing from the zip"; continue; }
        s=$(macho_signature "$x"); signed="${signed:+$signed; }$exe: $s"
        n=no; { [ "$(sc "$f" notarized)" = true ] || spctl_notarized "$x"; } && n=yes
        r=$(want_macos "$s" "$n"); [ "$r" = ok ] || res="$exe: $r"; [ "$n" = yes ] || notar=no
      done ;;
    macos-app-zip)
      d="$tmp/$f.d"; extract_zip "$p" "$d"
      app=$(find "$d" -maxdepth 1 -name '*.app' | head -1)
      if [ -z "$app" ]; then signed="no .app in the zip"; notar=no; res="no .app in the zip"
      else
        signed=$(macho_signature "$app")
        if codesign --verify --deep --strict "$app" >/dev/null 2>&1; then :; else signed="$signed (deep verify FAILED)"; fi
        notar=no
        if spctl_notarized "$app"; then notar=yes; stapled "$app" && notar="yes (stapled)"; fi
        res=$(want_macos "$signed" "${notar%% *}")
      fi ;;
    ios)
      signed="n/a (signed by the app that embeds it)" ;;
    windows-pe)
      signed=$(pe_signature "$p"); res=$(want_windows "$signed" "$EXPECT_WINDOWS") ;;
    windows-installer)
      signed=$(pe_signature "$p"); res=$(want_windows "$signed" "$EXPECT_WINDOWS_APP") ;;
    windows-app-zip|windows-tools-zip)
      # Every PE file in the zip, not just the executables: the app zip ships librashader.dll,
      # a software-opengl/ copy of the app and the bundled Mesa DLLs, and app-package.yml signs
      # them all (#1150).  One cell summarises the states; the first offender names the failure.
      exp=$EXPECT_WINDOWS; [ "$cls" != windows-app-zip ] || exp=$EXPECT_WINDOWS_APP
      d="$tmp/$f.d"; extract_zip "$p" "$d"
      signed=""; res=ok; n=0; bad=0; first=""; states="|"
      while IFS= read -r x; do
        b=${x#"$d/"}; s=$(pe_signature "$x"); n=$((n + 1))
        case "$states" in *"|$s|"*) ;; *) states="$states$s|" ;; esac
        r=$(want_windows "$s" "$exp")
        if [ "$r" != ok ]; then bad=$((bad + 1)); [ -n "$first" ] || first="$b: $r"; fi
      done < <(find "$d" -type f \( -iname '*.exe' -o -iname '*.dll' \) | LC_ALL=C sort)
      if [ "$n" -eq 0 ]; then signed="no PE files in the zip"; res="no PE files in the zip"
      else
        signed="$(printf '%s' "$states" | sed 's/^|//; s/|$//; s/|/; /g') ($n PE files)"
        [ "$bad" -eq 0 ] || res="$bad of $n PE files unsigned: $first"
      fi ;;
    appimage)
      signed=$(appimage_signature "$p")
      if [ "$EXPECT_APPIMAGE" = signed ]; then
        case "$signed" in "GPG "*" (NOT the project key)") res="signed by another key" ;; GPG*) ;; *) res="no embedded GPG signature ($signed)" ;; esac
      fi ;;
    checksums)
      case "$sums_ok" in yes) ;; *) res="SHA256SUMS.txt does not match the staged files" ;; esac
      if [ -f "$p.minisig" ]; then
        if [ -f "$minipub" ] && have minisign; then
          if minisign -Vm "$p" -p "$minipub" -q >/dev/null 2>&1; then signed="minisign (verified)"; else signed="minisign (BAD signature)"; res="checksum signature does not verify"; fi
        elif [ -f "$minipub" ]; then signed="minisign (present; minisign not installed to verify)"; [ "$EXPECT_SUMS_SIG" != required ] || res="cannot verify the checksum signature (minisign missing)"
        else signed="minisign (present; no public key committed to check it)"; fi
      else
        signed="none"
        [ "$EXPECT_SUMS_SIG" != required ] || res="SHA256SUMS.txt.minisig is missing"
      fi ;;
    checksum-signature) signed="(the detached signature of SHA256SUMS.txt)" ;;
    *) ;;
  esac
  [ "$res" != ok ] || res=$attres
  row "$f" "$cls" "$signed" "$notar" "$att" "$res"
done

# ---- output ---------------------------------------------------------------
{
  echo "| Asset | Class | Platform signature | Notarized | Attested | Result |"
  echo "|---|---|---|---|---|---|"
  printf '%s\n' "${rows[@]}"
  echo
  n=${#files[@]}; bad=${#fails[@]}
  if [ "$bad" -eq 0 ]; then echo "All $n assets meet their expected signing state."
  else echo "**$bad of $n assets do not meet their expected signing state** (mode: $mode)."; fi
  echo
  echo "Expectations: macOS \`$EXPECT_MACOS\`, Windows app \`$EXPECT_WINDOWS_APP\`, Windows cores/tools \`$EXPECT_WINDOWS\`, iOS/tvOS \`$EXPECT_IOS\`, AppImage \`$EXPECT_APPIMAGE\`, checksum signature \`$EXPECT_SUMS_SIG\`, attestation \`$EXPECT_ATTEST\` (scripts/release/verify.sh)."
} > "$table"
cat "$table"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then { echo "## Release signing gate ($mode)"; echo; cat "$table"; } >> "$GITHUB_STEP_SUMMARY"; fi

if [ "${#fails[@]}" -gt 0 ]; then
  for m in "${fails[@]}"; do
    if [ "$mode" = strict ]; then echo "::error title=release signing gate::$m"; else echo "::warning title=release signing gate::$m"; fi
  done
  [ "$mode" != strict ] || exit 1
fi
exit 0

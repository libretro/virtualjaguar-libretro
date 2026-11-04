#!/usr/bin/env bash
#
# Hermetic tests for the release signing tooling (issues #1151 / #1152):
#   scripts/release/stage.sh, sign-sums.sh, verify.sh and
#   app/packaging/linux/verify-appimage.py.
#
#   bash test/release/test_release_scripts.sh
#
# Nothing talks to GitHub (SKIP_ATTEST=1) and no real key is used: minisign
# and GPG keys are generated into a temp dir when the tools exist and those
# tests are skipped when they do not.  The macOS codesign rows run on macOS.
# VJ_TEST_REAL_APPIMAGE=1 (Linux, network) additionally downloads the pinned
# appimagetool and signs a real AppImage, which proves the gpgme passphrase
# path make-appimage.sh relies on.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../.." && pwd)
S="$ROOT/scripts/release"; VA="$ROOT/app/packaging/linux/verify-appimage.py"
T=$(mktemp -d 2>/dev/null || mktemp -d -t vjrel); trap 'command rm -rf "$T"' EXIT
PASS=0; FAIL=0
pass() { PASS=$((PASS + 1)); printf '  ok   %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL %s\n' "$1"; [ -z "${2:-}" ] || printf '%s\n' "$2" | sed 's/^/         | /'; }
check_rc() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1 (exit $2, wanted $3)" "${4:-}"; fi; }
check_has() { case "$2" in *"$3"*) pass "$1" ;; *) fail "$1 (missing: $3)" "$2" ;; esac; }
check_lacks() { case "$2" in *"$3"*) fail "$1 (unexpected: $3)" "$2" ;; *) pass "$1" ;; esac; }
have() { command -v "$1" >/dev/null 2>&1; }
OS=$(uname -s)
export SKIP_ATTEST=1

# ---- fixtures: a fake artifacts tree --------------------------------------
A="$T/artifacts"; mkdir -p "$A"
mk() { mkdir -p "$A/$1"; head -c "${3:-3000}" /dev/urandom > "$A/$1/$2"; }
mk virtualjaguar_libretro-linux-x86_64 virtualjaguar_libretro-linux-x86_64.so
mk virtualjaguar_libretro-linux-x86_64 virtualjaguar_libretro-linux-x86_64-debug.tar.gz
mk virtualjaguar_libretro-ios-arm64 virtualjaguar_libretro-ios-arm64.dylib
mk virtualjaguar_libretro-windows-x86_64 virtualjaguar_libretro-windows-x86_64.dll
mk app-package-windows-x64 VirtualJaguar-dev-windows-x64-setup.exe
# The app zip holds more PE files than the executable (#1150): the gate scans them all.
mkdir -p "$T/zip/VirtualJaguar/software-opengl"
head -c 3000 /dev/urandom > "$T/zip/VirtualJaguar/virtualjaguar.exe"
head -c 3000 /dev/urandom > "$T/zip/VirtualJaguar/librashader.dll"
head -c 3000 /dev/urandom > "$T/zip/VirtualJaguar/software-opengl/opengl32.dll"
printf 'portable\n' > "$T/zip/VirtualJaguar/portable.txt"
(cd "$T/zip" && zip -qr "$A/app-package-windows-x64/VirtualJaguar-dev-windows-x64.zip" VirtualJaguar)
mk windows-stage-x64 virtualjaguar.exe
mk app-package-linux-x86_64 virtualjaguar_dev_amd64.deb
mk librashader-x86_64-unknown-linux-gnu librashader.so
mk ci-core-vita BUILD.json 100
if [ "$OS" = Darwin ]; then
  mkdir -p "$A/virtualjaguar_libretro-macos-arm64"
  printf 'int vj_t(void){return 1;}\n' > "$T/f.c"
  DEVELOPER_DIR=${DEVELOPER_DIR:-/Library/Developer/CommandLineTools} cc -shared -o "$A/virtualjaguar_libretro-macos-arm64/virtualjaguar_libretro-macos-arm64.dylib" "$T/f.c"
  codesign -s - "$A/virtualjaguar_libretro-macos-arm64/virtualjaguar_libretro-macos-arm64.dylib"
fi
bash "$S/signing-status.sh" virtualjaguar_libretro-macos-arm64.dylib true true developer-id "$A/signing-status-core-macos-arm64" >/dev/null

printf '\n== stage.sh ==\n'
cd "$ROOT" || exit 1
OUT=$(LANE=nightly GITHUB_SHA=0123456789abcdef bash "$S/stage.sh" "$A" "$T/rel" "$T/status" 2>&1); RC=$?
check_rc "stage exits 0" "$RC" 0 "$OUT"
check_has "shipped file staged" "$(ls "$T/rel")" "virtualjaguar_libretro-linux-x86_64.so"
check_lacks "librashader intermediate excluded" "$(ls "$T/rel")" "librashader"
check_lacks "ci-core intermediate excluded" "$(ls "$T/rel")" "BUILD.json"
check_lacks "windows-stage intermediate excluded" "$(ls "$T/rel")" "virtualjaguar.exe"
check_lacks "signing sidecar is not a release asset" "$(ls "$T/rel")" ".status"
check_has "sidecar collected for the gate" "$(ls "$T/status")" "virtualjaguar_libretro-macos-arm64.dylib.status"
check_has "nightly .info is relabelled" "$(cat "$T/rel/virtualjaguar_libretro.info")" "-nightly-0123456"
if have sha256sum; then (cd "$T/rel" && sha256sum --check --quiet SHA256SUMS.txt) && pass "SHA256SUMS.txt checks" || fail "SHA256SUMS.txt checks"
else (cd "$T/rel" && shasum -a 256 --check --quiet SHA256SUMS.txt) && pass "SHA256SUMS.txt checks" || fail "SHA256SUMS.txt checks"; fi
check_lacks "SHA256SUMS.txt does not list itself" "$(cat "$T/rel/SHA256SUMS.txt")" " SHA256SUMS.txt"
OUT=$(LANE=release bash "$S/stage.sh" "$A" "$T/rel-tag" "$T/status-tag" 2>&1)
check_has "release .info kept verbatim" "$(cat "$T/rel-tag/virtualjaguar_libretro.info")" "$(sed -n 's/^display_version = //p' dist/info/virtualjaguar_libretro.info)"
# Every build artifact carries the repository's dist/info/*.info: identical copies are fine.
mkdir -p "$A/virtualjaguar_libretro-linux-x86_64/info" "$A/virtualjaguar_libretro-ios-arm64/info"
cp dist/info/virtualjaguar_libretro.info "$A/virtualjaguar_libretro-linux-x86_64/info/"
sed 's/$/\r/' dist/info/virtualjaguar_libretro.info > "$A/virtualjaguar_libretro-ios-arm64/info/virtualjaguar_libretro.info"   # a CRLF (Windows checkout) copy
OUT=$(LANE=nightly GITHUB_SHA=0123456789abcdef bash "$S/stage.sh" "$A" "$T/rel-same" "$T/status-same" 2>&1); RC=$?
check_rc "the .info copies in every dist/ (even a CRLF one) are ignored" "$RC" 0 "$OUT"
check_has "... and the lane's .info still wins" "$(cat "$T/rel-same/virtualjaguar_libretro.info")" "-nightly-0123456"
mk dup virtualjaguar_libretro-linux-x86_64.so
OUT=$(LANE=nightly bash "$S/stage.sh" "$A" "$T/rel-dup" "$T/status-dup" 2>&1); RC=$?
check_rc "differing files under one asset name are an error" "$RC" 1 "$OUT"
check_has "duplicate named" "$OUT" "two artifacts ship different files named virtualjaguar_libretro-linux-x86_64.so"
command rm -rf "$A/dup"

printf '\n== sign-sums.sh ==\n'
OUT=$(MINISIGN_SECRET_KEY='' GITHUB_OUTPUT="$T/out1" bash "$S/sign-sums.sh" "$T/rel" 2>&1); RC=$?
check_rc "no secret: exits 0" "$RC" 0 "$OUT"
check_has "no secret: loud warning" "$OUT" "::warning title=SHA256SUMS.txt is NOT signed::"
check_has "no secret: signed=false" "$(cat "$T/out1")" "signed=false"
[ ! -e "$T/rel/SHA256SUMS.txt.minisig" ] && pass "no secret: no .minisig" || fail "no secret: no .minisig"
if have minisign; then
  printf 'pw\npw\n' | minisign -G -f -p "$T/k.pub" -s "$T/k.key" >/dev/null 2>&1
  printf 'pw\npw\n' | minisign -G -f -p "$T/other.pub" -s "$T/other.key" >/dev/null 2>&1
  OUT=$(MINISIGN_SECRET_KEY="$(cat "$T/k.key")" MINISIGN_PASSWORD=pw MINISIGN_PUBKEY_FILE="$T/k.pub" LANE=nightly GITHUB_OUTPUT="$T/out2" bash "$S/sign-sums.sh" "$T/rel" 2>&1); RC=$?
  check_rc "signs with a key" "$RC" 0 "$OUT"
  check_has "signed=true" "$(cat "$T/out2")" "signed=true"
  minisign -Vm "$T/rel/SHA256SUMS.txt" -p "$T/k.pub" -q >/dev/null 2>&1 && pass ".minisig verifies with the public key" || fail ".minisig verifies with the public key"
  check_has "trusted comment names the lane" "$(cat "$T/rel/SHA256SUMS.txt.minisig")" "virtualjaguar-libretro nightly"
  OUT=$(MINISIGN_SECRET_KEY="$(cat "$T/k.key")" MINISIGN_PASSWORD=pw MINISIGN_PUBKEY_FILE="$T/other.pub" GITHUB_REF=refs/heads/develop GITHUB_OUTPUT="$T/out3" bash "$S/sign-sums.sh" "$T/rel" 2>&1); RC=$?
  check_rc "key/pubkey mismatch on develop: warns" "$RC" 0 "$OUT"
  check_has "mismatch message" "$OUT" "not a pair"
  [ ! -e "$T/rel/SHA256SUMS.txt.minisig" ] && pass "mismatch: signature dropped" || fail "mismatch: signature dropped"
  OUT=$(MINISIGN_SECRET_KEY="$(cat "$T/k.key")" MINISIGN_PASSWORD=pw MINISIGN_PUBKEY_FILE="$T/other.pub" GITHUB_REF=refs/tags/v9.9.9 GITHUB_OUTPUT="$T/out4" bash "$S/sign-sums.sh" "$T/rel" 2>&1); RC=$?
  check_rc "key/pubkey mismatch on a tag: fails" "$RC" 1 "$OUT"
  # leave a good signature in place for the gate tests
  MINISIGN_SECRET_KEY="$(cat "$T/k.key")" MINISIGN_PASSWORD=pw MINISIGN_PUBKEY_FILE="$T/k.pub" GITHUB_OUTPUT=/dev/null bash "$S/sign-sums.sh" "$T/rel" >/dev/null 2>&1
else
  echo "  skip minisign tests (minisign not installed)"
fi

printf '\n== verify.sh ==\n'
gate() { # mode [env...]  -> OUT RC, table in $T/table.md
  _m=$1; shift
  OUT=$(env "$@" bash "$S/verify.sh" "$T/rel" "$T/status" "$_m" "$T/table.md" 2>&1); RC=$?
}
NOPUB="MINISIGN_PUBKEY_FILE=$T/none.pub APPIMAGE_PUBKEY_FILE=$T/none.asc"
# shellcheck disable=SC2086
gate warn $NOPUB
check_rc "warn mode exits 0" "$RC" 0 "$OUT"
check_has "table header" "$OUT" "| Asset | Class | Platform signature | Notarized | Attested | Result |"
check_has "attestation column skipped in tests" "$OUT" "| skipped |"
check_has "ios row is n/a" "$OUT" "| \`virtualjaguar_libretro-ios-arm64.dylib\` | ios | n/a (signed by the app that embeds it)"
check_has "windows unsigned is expected today" "$OUT" "| \`virtualjaguar_libretro-windows-x86_64.dll\` | windows-pe |"
check_lacks "windows row does not fail while expected unsigned" "$OUT" "no Authenticode signature"
check_has "app zip row counts every PE file inside" "$OUT" "| \`VirtualJaguar-dev-windows-x64.zip\` | windows-app-zip |"
check_has "app zip row summarises the PE files" "$OUT" "(3 PE files) |"
# shellcheck disable=SC2086
gate strict $NOPUB WINDOWS_APP_EXPECT=signed
check_rc "flipping the Windows app expectation makes the unsigned installer and zip fail" "$RC" 1 "$OUT"
check_has "names the unsigned installer" "$OUT" "::error title=release signing gate::VirtualJaguar-dev-windows-x64-setup.exe:"
check_has "names the first unsigned file inside the zip" "$OUT" "::error title=release signing gate::VirtualJaguar-dev-windows-x64.zip: 3 of 3 PE files unsigned: VirtualJaguar/librashader.dll:"
check_lacks "the core DLL keeps its own expectation" "$OUT" "::error title=release signing gate::virtualjaguar_libretro-windows-x86_64.dll"
# shellcheck disable=SC2086
gate strict $NOPUB WINDOWS_EXPECT=signed
check_rc "flipping the Windows core/tool expectation makes the unsigned core DLL fail" "$RC" 1 "$OUT"
check_has "names the unsigned core DLL" "$OUT" "::error title=release signing gate::virtualjaguar_libretro-windows-x86_64.dll:"
check_lacks "the app packages keep their own expectation" "$OUT" "::error title=release signing gate::VirtualJaguar-dev-windows-x64"
if [ "$OS" = Darwin ]; then
  # shellcheck disable=SC2086
  gate strict $NOPUB
  check_rc "strict: ad-hoc dylib fails" "$RC" 1 "$OUT"
  check_has "ad-hoc reported" "$OUT" "| macos-core | ad-hoc | yes | skipped | ❌ not Developer ID signed (ad-hoc) |"
  # shellcheck disable=SC2086
  gate warn $NOPUB
  check_has "warn: same finding as a warning" "$OUT" "::warning title=release signing gate::virtualjaguar_libretro-macos-arm64.dylib: not Developer ID signed (ad-hoc)"
fi
if have minisign && [ -f "$T/rel/SHA256SUMS.txt.minisig" ]; then
  gate strict "MINISIGN_PUBKEY_FILE=$T/k.pub" "APPIMAGE_PUBKEY_FILE=$T/none.asc" WINDOWS_EXPECT=unsigned WINDOWS_APP_EXPECT=unsigned
  check_has "checksum signature verified with the committed key" "$OUT" "| \`SHA256SUMS.txt\` | checksums | minisign (verified) |"
  mv "$T/rel/SHA256SUMS.txt.minisig" "$T/sig.bak"
  gate strict "MINISIGN_PUBKEY_FILE=$T/k.pub" "APPIMAGE_PUBKEY_FILE=$T/none.asc"
  check_rc "public key committed + no .minisig: strict fails" "$RC" 1 "$OUT"
  check_has "missing signature named" "$OUT" "SHA256SUMS.txt.minisig is missing"
  mv "$T/sig.bak" "$T/rel/SHA256SUMS.txt.minisig"
  gate strict "MINISIGN_PUBKEY_FILE=$T/other.pub" "APPIMAGE_PUBKEY_FILE=$T/none.asc"
  check_rc "signature by another key: strict fails" "$RC" 1 "$OUT"
  check_has "bad signature reported" "$OUT" "minisign (BAD signature)"
fi
# tampered checksum list
cp "$T/rel/SHA256SUMS.txt" "$T/sums.bak"
sed -i.orig '1s/^[0-9a-f]\{64\}/0000000000000000000000000000000000000000000000000000000000000000/' "$T/rel/SHA256SUMS.txt"
# shellcheck disable=SC2086
gate strict $NOPUB
check_has "tampered SHA256SUMS.txt fails" "$OUT" "SHA256SUMS.txt does not match the staged files"
cp "$T/sums.bak" "$T/rel/SHA256SUMS.txt"; command rm -f "$T/rel/SHA256SUMS.txt.orig"

printf '\n== verify-appimage.py ==\n'
python3 -I "$HERE/fake_appimage.py" "$T/unsigned.AppImage"
OUT=$(python3 -I "$VA" "$T/unsigned.AppImage" 2>&1); RC=$?
check_rc "unsigned image: exit 1" "$RC" 1 "$OUT"
check_has "unsigned message" "$OUT" "not signed (empty .sha256_sig section)"
OUT=$(python3 -I "$VA" "$T/sums.bak" 2>&1); RC=$?
check_rc "not an ELF: exit 4" "$RC" 4 "$OUT"
if have gpg; then
  export GNUPGHOME="$T/gnupg"; mkdir -p "$GNUPGHOME"; chmod 700 "$GNUPGHOME"
  gpg --batch --quiet --pinentry-mode loopback --passphrase '' --quick-gen-key 'VJ test key <t@example.invalid>' ed25519 sign never 2>/dev/null
  gpg --batch --quiet --pinentry-mode loopback --passphrase '' --quick-gen-key 'VJ other key <o@example.invalid>' ed25519 sign never 2>/dev/null
  FPR=$(gpg --batch --with-colons --list-keys 't@example.invalid' | awk -F: '$1=="fpr"{print $10; exit}')
  gpg --batch --armor --export "$FPR" > "$T/pub.asc"
  gpg --batch --armor --export 'o@example.invalid' > "$T/otherpub.asc"
  DIG=$(python3 -I "$HERE/fake_appimage.py" x --digest-only)
  printf '%s' "$DIG" > "$T/digest.txt"
  gpg --batch --quiet --pinentry-mode loopback --passphrase '' --local-user "$FPR" --armor --textmode --detach-sign --output "$T/sig.asc" "$T/digest.txt"
  python3 -I "$HERE/fake_appimage.py" "$T/signed.AppImage" --sig "$T/sig.asc" --key "$T/pub.asc"
  unset GNUPGHOME
  OUT=$(python3 -I "$VA" "$T/signed.AppImage" 2>&1); RC=$?
  check_rc "signed image verifies with the embedded key" "$RC" 0 "$OUT"
  check_has "fingerprint reported" "$OUT" "good signature from $FPR"
  OUT=$(python3 -I "$VA" "$T/signed.AppImage" --pubkey "$T/pub.asc" --json 2>&1); RC=$?
  check_rc "verifies against the project key" "$RC" 0 "$OUT"
  check_has "json says matches_pubkey" "$OUT" '"matches_pubkey": true'
  OUT=$(python3 -I "$VA" "$T/signed.AppImage" --pubkey "$T/otherpub.asc" 2>&1); RC=$?
  check_rc "another project key: exit 3" "$RC" 3 "$OUT"
  python3 -I "$HERE/fake_appimage.py" "$T/tampered.AppImage" --sig "$T/sig.asc" --key "$T/pub.asc" --payload-byte 66
  OUT=$(python3 -I "$VA" "$T/tampered.AppImage" 2>&1); RC=$?
  check_rc "tampered payload: exit 2" "$RC" 2 "$OUT"
  check_has "bad signature message" "$OUT" "BAD signature"
  # the gate's AppImage row
  cp "$T/signed.AppImage" "$T/rel/VirtualJaguar-dev-linux-x86_64.AppImage"
  cp "$T/unsigned.AppImage" "$T/rel/VirtualJaguar-dev-linux-aarch64.AppImage"
  ( cd "$T/rel" && command rm -f SHA256SUMS.txt && { sha256sum -- * 2>/dev/null || shasum -a 256 -- *; } > "$T/s.tmp" && mv "$T/s.tmp" SHA256SUMS.txt )
  command rm -f "$T/rel/SHA256SUMS.txt.minisig"
  gate strict "MINISIGN_PUBKEY_FILE=$T/none.pub" "APPIMAGE_PUBKEY_FILE=$T/pub.asc"
  check_rc "appimage key committed: the unsigned one fails strict" "$RC" 1 "$OUT"
  check_has "signed AppImage row" "$OUT" "| \`VirtualJaguar-dev-linux-x86_64.AppImage\` | appimage | GPG $FPR | n/a | skipped | ✅ |"
  check_has "unsigned AppImage row" "$OUT" "VirtualJaguar-dev-linux-aarch64.AppImage: no embedded GPG signature (none)"
  gate strict "MINISIGN_PUBKEY_FILE=$T/none.pub" "APPIMAGE_PUBKEY_FILE=$T/otherpub.asc"
  check_has "AppImage signed by another key fails" "$OUT" "VirtualJaguar-dev-linux-x86_64.AppImage: signed by another key"
else
  echo "  skip gpg tests (gpg not installed)"
fi

if [ "${VJ_TEST_REAL_APPIMAGE:-0}" = 1 ] && [ "$OS" = Linux ]; then
  printf '\n== real appimagetool --sign (network) ==\n'
  arch=$(uname -m)
  case "$arch" in x86_64) sha=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 ;; aarch64) sha=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158 ;; *) sha= ;; esac
  if [ -n "$sha" ] && have gpg; then
    curl -fsSL --retry 3 -o "$T/appimagetool" "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-$arch.AppImage"
    echo "$sha  $T/appimagetool" | sha256sum -c - >/dev/null && chmod +x "$T/appimagetool"
    export GNUPGHOME="$T/gnupg2"; mkdir -p "$GNUPGHOME"; chmod 700 "$GNUPGHOME"
    printf 'allow-loopback-pinentry\n' > "$GNUPGHOME/gpg-agent.conf"
    gpg --batch --quiet --pinentry-mode loopback --passphrase 'secret' --quick-gen-key 'VJ ci key <ci@example.invalid>' ed25519 sign never 2>/dev/null
    FPR2=$(gpg --batch --with-colons --list-keys 'ci@example.invalid' | awk -F: '$1=="fpr"{print $10; exit}')
    gpg --batch --armor --export "$FPR2" > "$T/cipub.asc"
    d="$T/AppDir"; mkdir -p "$d/usr/bin" "$d/usr/share/applications" "$d/usr/share/icons/hicolor/256x256/apps"
    printf '#!/bin/sh\necho hi\n' > "$d/usr/bin/hello"; chmod +x "$d/usr/bin/hello"
    printf '#!/bin/sh\nexec "$(dirname "$0")/usr/bin/hello" "$@"\n' > "$d/AppRun"; chmod +x "$d/AppRun"
    printf '[Desktop Entry]\nType=Application\nName=hello\nExec=hello\nIcon=hello\nCategories=Utility;\n' > "$d/hello.desktop"
    printf '\211PNG\r\n\032\n' > "$d/hello.png"; cp "$d/hello.png" "$d/usr/share/icons/hicolor/256x256/apps/hello.png"
    OUT=$(APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$arch" APPIMAGETOOL_SIGN_PASSPHRASE=secret "$T/appimagetool" --no-appstream --sign --sign-key "$FPR2" "$d" "$T/hello.AppImage" 2>&1); RC=$?
    check_rc "appimagetool --sign succeeds with a passphrase from the environment" "$RC" 0 "$OUT"
    unset GNUPGHOME
    OUT=$(python3 -I "$VA" "$T/hello.AppImage" --pubkey "$T/cipub.asc" 2>&1); RC=$?
    check_rc "real signed AppImage verifies" "$RC" 0 "$OUT"
    check_has "real fingerprint" "$OUT" "good signature from $FPR2"
  else
    echo "  skip (unsupported arch or no gpg)"
  fi
fi

printf '\n%s passed, %s failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]

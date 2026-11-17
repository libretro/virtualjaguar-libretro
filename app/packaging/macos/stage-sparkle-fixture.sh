#!/bin/bash
# Stage a CI-only ZIP from a universal install tree. No keychain or network access.
# stage-sparkle-fixture.sh PREFIX SDK CHANNEL VERSION RUN_NUMBER NEW_OUTPUT_DIR
set -euo pipefail
[ "$#" -eq 6 ]
prefix="$1"; sdk="$2"; channel="$3"; version="$4"; run="$5"; out="$6"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
case "$channel" in
  stable) sequence="$version"; tag="v$version" ;;
  nightly) sequence="$run"; tag="macos-nightly-$run" ;;
  *) echo "Expected stable or nightly" >&2; exit 1 ;;
esac
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
app="$tmp/Virtual Jaguar.app"

# Public test vectors, deliberately unrelated to production keys. Give the channels
# different fixture trust roots. The seed exists only inside this test program.
cat > "$tmp/fixture.swift" <<'SWIFT'
import CryptoKit
import Foundation

let channel = CommandLine.arguments[1]
let key = try Curve25519.Signing.PrivateKey(
    rawRepresentation: Data(repeating: channel == "stable" ? 42 : 43, count: 32))
if CommandLine.arguments.count == 3 {
    let bytes = try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[2]), options: .mappedIfSafe)
    print(try key.signature(for: bytes).base64EncodedString())
} else {
    print(key.publicKey.rawRepresentation.base64EncodedString())
}
SWIFT
swiftc "$tmp/fixture.swift" -o "$tmp/fixture"
key="$("$tmp/fixture" "$channel")"
bash "$here/sparkle-app.sh" "$prefix" "$app" "$version" "$channel" "$sequence" 12.0 "$sdk" "$key"
plist="$app/Contents/Info.plist"
plutil -replace CFBundleIdentifier -string "io.github.libretro.VirtualJaguar.SparkleFixture.$channel" "$plist"
plutil -replace SUFeedURL -string "https://sparkle-fixture.invalid/macos/$channel/appcast.xml" "$plist"
VJ_SIGN_IDENTITY=- bash "$here/codesign.sh" sign-app "$app"

zip="$tmp/VirtualJaguar-$tag-macos-universal-direct-fixture.zip"
ditto -c -k --keepParent "$app" "$zip"
signature="$("$tmp/fixture" "$channel" "$zip")"
swiftc "$here/verify-sparkle-archive.swift" -o "$tmp/verify"
"$tmp/verify" "$zip" "$key" "$signature"
other=nightly
if [ "$channel" = nightly ]; then other=stable; fi
if "$tmp/verify" "$zip" "$("$tmp/fixture" "$other")" "$signature"; then
  echo "Archive accepted with the other channel's public key" >&2
  exit 1
fi
cat "$zip" > "$tmp/tampered.zip"
printf 'tampered' >> "$tmp/tampered.zip"
if "$tmp/verify" "$tmp/tampered.zip" "$key" "$signature"; then
  echo "Tampered archive accepted" >&2
  exit 1
fi

ditto -x -k "$zip" "$tmp/unpacked"
app="$tmp/unpacked/Virtual Jaguar.app"
codesign --verify --deep --strict "$app"
plist="$app/Contents/Info.plist"
read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist"; }
[ "$(read_plist CFBundleIdentifier)" = "io.github.libretro.VirtualJaguar.SparkleFixture.$channel" ]
[ "$(read_plist CFBundleVersion)" = "$sequence" ]
[ "$(read_plist SUPublicEDKey)" = "$key" ]
[ "$(read_plist SUFeedURL)" = "https://sparkle-fixture.invalid/macos/$channel/appcast.xml" ]
for setting in SUEnableAutomaticChecks SUAutomaticallyUpdate SUAllowsAutomaticUpdates; do
  [ "$(read_plist "$setting")" = false ]
done
exe="$app/Contents/MacOS/virtualjaguar"
smoked=false
for arch in arm64 x86_64; do
  if ! arch "-$arch" /usr/bin/true 2>/dev/null; then
    echo "::warning::Cannot execute $arch on this host; its slice was verified during assembly"
    continue
  fi
  arch "-$arch" "$exe" --headless --frames 300 --expect-hash e33e2c61 "$root/test/roms/yarc.j64"
  smoked=true
done
[ "$smoked" = true ]

mkdir "$out"
mv "$zip" "$out/"
printf '%s\n' "$signature" > "$out/fixture-ed25519-signature.txt"

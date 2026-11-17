#!/bin/bash
# Assemble the direct distribution from a universal install tree linked with Sparkle.
# sparkle-app.sh PREFIX APP DISPLAY_VERSION CHANNEL SEQUENCE MIN_MACOS SDK PUBLIC_KEY
set -euo pipefail
[ "$#" -eq 8 ]
prefix="$1"; app="$2"; display="$3"; channel="$4"; sequence="$5"; minmac="$6"
sdk="$7"; public_key="$8"
here="$(cd "$(dirname "$0")" && pwd)"

[[ "$app" = *.app && ! -e "$app" && ! -L "$app" ]]
[[ "$display" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
case "$channel" in stable|nightly) ;; *) echo "Invalid Sparkle channel" >&2; exit 1 ;; esac
# Stable uses the release semver. Nightly uses a strictly increasing release run number.
if [ "$channel" = stable ]; then
  [ "$sequence" = "$display" ]
else
  [[ "$sequence" =~ ^[1-9][0-9]*$ ]]
fi
[[ "$public_key" =~ ^[A-Za-z0-9+/]{43}=$ ]]
[ -d "$sdk/Sparkle.framework" ]
otool -L "$prefix/bin/virtualjaguar" | awk '
  $1 == "@rpath/Sparkle.framework/Versions/B/Sparkle" { found = 1 }
  END { exit !found }
'

bash "$here/make-app.sh" "$prefix" "$app" "$display" "$sequence" "$minmac"
framework="$app/Contents/Frameworks/Sparkle.framework"
ditto "$sdk/Sparkle.framework" "$framework"
ditto "$sdk/LICENSE" "$app/Contents/Resources/Sparkle-LICENSE.txt"
plist="$app/Contents/Info.plist"
# Keep the app identity so the direct edition sees the same saves and settings.
plutil -insert VJSparkleChannel -string "$channel" "$plist"
plutil -insert SUFeedURL -string "https://jaguar.provenance-emu.com/updates/macos/$channel/appcast.xml" "$plist"
plutil -insert SUPublicEDKey -string "$public_key" "$plist"
plutil -insert SUEnableAutomaticChecks -bool NO "$plist"
plutil -insert SUAutomaticallyUpdate -bool NO "$plist"
plutil -insert SUAllowsAutomaticUpdates -bool NO "$plist"
plutil -lint "$plist"
bash "$here/check-universal.sh" "$app" "$minmac"

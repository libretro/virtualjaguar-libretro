#!/bin/bash
# Produce one appcast candidate on stdout from an already public, immutable GitHub release.
# sparkle-appcast.sh REPO CHANNEL TAG ASSET ED_SIGNATURE TRUSTED_PUBLIC_KEY PREVIOUS_VERSION
set -euo pipefail
[ "$#" -eq 7 ]
repo="$1"; channel="$2"; tag="$3"; asset="$4"; signature="$5"; key="$6"; previous="$7"
here="$(cd "$(dirname "$0")" && pwd)"
[[ "$repo" =~ ^[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+$ ]]
case "$channel" in
  stable) [[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ && "$previous" =~ ^(0|[0-9]+\.[0-9]+\.[0-9]+)$ ]] ;;
  nightly) [[ "$tag" =~ ^macos-nightly-[1-9][0-9]*$ && "$previous" =~ ^(0|[1-9][0-9]*)$ ]] ;;
  *) exit 1 ;;
esac
[[ "$asset" = "VirtualJaguar-$tag-macos-universal-direct.zip" ]]
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT

# Refuse the rolling nightly and any mutable or draft release. Verify the bytes served
# at the public download URL against GitHub's asset digest and the embedded trust key.
digest="$(curl -fsS "https://api.github.com/repos/$repo/releases/tags/$tag" |
  jq -er --arg asset "$asset" '
    select(.immutable == true and .draft == false)
    | .assets[] | select(.name == $asset)
    | .digest | select(test("^sha256:[0-9a-f]{64}$"))')"
url="https://github.com/$repo/releases/download/$tag/$asset"
archive="$tmp/$asset"
curl --proto '=https' --proto-redir '=https' -fsSL "$url" -o "$archive"
actual="$(shasum -a 256 "$archive" | cut -d ' ' -f1)"
[ "$digest" = "sha256:$actual" ]
swift "$here/verify-sparkle-archive.swift" "$archive" "$key" "$signature"
ditto -x -k "$archive" "$tmp/extracted"
app="$tmp/extracted/Virtual Jaguar.app"
codesign --verify --deep --strict -R='anchor apple generic and certificate leaf[field.1.2.840.113635.100.6.1.13] exists' "$app"
xcrun stapler validate "$app" >&2
plist="$app/Contents/Info.plist"
read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist"; }
[ "$(read_plist VJSparkleChannel)" = "$channel" ]
[ "$(read_plist SUPublicEDKey)" = "$key" ]
for setting in SUEnableAutomaticChecks SUAutomaticallyUpdate SUAllowsAutomaticUpdates; do
  [ "$(read_plist "$setting")" = false ]
done
[ "$(read_plist SUFeedURL)" = "https://jaguar.provenance-emu.com/updates/macos/$channel/appcast.xml" ]
version="$(read_plist CFBundleVersion)"
display="$(read_plist CFBundleShortVersionString)"
minimum="$(read_plist LSMinimumSystemVersion)"
bash "$here/check-universal.sh" "$app" "$minimum" >&2
length="$(stat -f %z "$archive")"
if [ "$channel" = stable ]; then
  [ "$version" = "${tag#v}" ] && [ "$display" = "$version" ]
else
  [ "$version" = "${tag#macos-nightly-}" ]
fi
python3 - "$url" "$version" "$display" "$minimum" "$length" "$signature" "$previous" <<'PY'
import sys
import xml.etree.ElementTree as ET

url, version, display, minimum, length, signature, previous = sys.argv[1:]
if tuple(map(int, version.split("."))) <= tuple(map(int, previous.split("."))):
    sys.exit("Sparkle version must increase")

sparkle = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", sparkle)
rss = ET.Element("rss", version="2.0")
channel = ET.SubElement(rss, "channel")
ET.SubElement(channel, "title").text = "Virtual Jaguar"
item = ET.SubElement(channel, "item")
ET.SubElement(item, "title").text = f"Virtual Jaguar {display}"
for name, value in (
    ("version", version),
    ("shortVersionString", display),
    ("minimumSystemVersion", minimum),
):
    ET.SubElement(item, f"{{{sparkle}}}{name}").text = value
ET.SubElement(item, "enclosure", {
    "url": url,
    "length": length,
    "type": "application/octet-stream",
    f"{{{sparkle}}}edSignature": signature,
})
ET.indent(rss)
ET.ElementTree(rss).write(sys.stdout.buffer, encoding="utf-8", xml_declaration=True)
PY

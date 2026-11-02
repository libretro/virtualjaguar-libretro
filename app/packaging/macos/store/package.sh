#!/bin/bash
# Sign a separately assembled Store-mode app, then make its App Store installer.
# package.sh <app> <profile> <application-identity> <installer-identity> <new.pkg>
# Local sandbox qualification: package.sh <app> --test <Developer-ID-or-dash>
set -euo pipefail
usage() {
    echo "Usage: $0 <app> <profile> <application-identity> <installer-identity> <new.pkg>" >&2
    echo "       $0 <app> --test <Developer-ID-or-dash>" >&2
    exit 1
}
if [ "${2:-}" = --test ]; then
    [ "$#" -eq 3 ] || usage
else
    [ "$#" -eq 5 ] || usage
fi
here="$(cd "$(dirname "$0")" && pwd)"
app="$1"
[ -d "$app/Contents" ] || { echo 'Expected an assembled .app' >&2; exit 1; }
python3 - "$app" <<'PY'
import pathlib, plistlib, sys
app = pathlib.Path(sys.argv[1])
info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
assert info['CFBundleIdentifier'] == 'io.github.libretro.VirtualJaguar', 'Wrong bundle ID'
exe = app / 'Contents/MacOS/virtualjaguar'
assert b'VJStoreFileBookmarksV1' in exe.read_bytes(), 'Executable was not built with VJ_MAC_APP_STORE=ON'
assert not list(app.rglob('portable.txt')), 'Portable data mode is not supported by the Store edition'
PY
ent="$(mktemp -t vj-store-entitlements)"
profile_plist="$(mktemp -t vj-store-profile)"
trap 'command rm -f "$ent" "$profile_plist"' EXIT
command cp -f "$here/entitlements.plist" "$ent"
mode="$2"
if [ "$mode" = --test ]; then
    id="$3"
    echo 'Local sandbox test signature only; this does not produce an uploadable package.'
else
    profile="$2"; id="$3"; installer="$4"; out="$5"
    archs="$(lipo -archs "$app/Contents/MacOS/virtualjaguar")"
    [[ " $archs " == *" arm64 "* && " $archs " == *" x86_64 "* ]] || {
        echo 'The Store package must contain both arm64 and x86_64.' >&2; exit 1;
    }
    [ ! -e "$out" ] || { echo 'Output already exists; choose a new package path.' >&2; exit 1; }
    case "$id" in
        'Apple Distribution:'*|'3rd Party Mac Developer Application:'*) ;;
        *) echo 'A Mac App Store application signing identity is required.' >&2; exit 1 ;;
    esac
    case "$installer" in
        '3rd Party Mac Developer Installer:'*|'Mac Installer Distribution:'*) ;;
        *) echo 'A Mac App Store installer signing identity is required.' >&2; exit 1 ;;
    esac
    security cms -D -i "$profile" > "$profile_plist"
    python3 - "$profile_plist" "$ent" <<'PY'
import datetime, plistlib, sys
p = plistlib.load(open(sys.argv[1], 'rb'))
e = plistlib.load(open(sys.argv[2], 'rb'))
assert p['ExpirationDate'] > datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None), 'Expired profile'
assert 'OSX' in p['Platform'], 'Not a macOS profile'
assert not p.get('ProvisionedDevices'), 'Use a Mac App Store distribution profile'
a = p['Entitlements']
ident = 'S32Z3HMYVQ.io.github.libretro.VirtualJaguar'
assert a.get('com.apple.application-identifier') == ident, 'Wrong profile application ID'
assert a.get('com.apple.developer.team-identifier') == 'S32Z3HMYVQ', 'Wrong team'
assert not a.get('get-task-allow', False), 'Debug profile is not distributable'
e['com.apple.application-identifier'] = ident
e['com.apple.developer.team-identifier'] = 'S32Z3HMYVQ'
plistlib.dump(e, open(sys.argv[2], 'wb'))
PY
    command cp -f "$profile" "$app/Contents/embedded.provisionprofile"
fi
command cp -f "$here/PrivacyInfo.xcprivacy" "$app/Contents/Resources/PrivacyInfo.xcprivacy"
/usr/libexec/PlistBuddy -c 'Add :ITSAppUsesNonExemptEncryption bool false' "$app/Contents/Info.plist" 2>/dev/null || /usr/libexec/PlistBuddy -c 'Set :ITSAppUsesNonExemptEncryption false' "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Add :NSBluetoothAlwaysUsageDescription string Connect Bluetooth game controllers to play Jaguar games.' "$app/Contents/Info.plist" 2>/dev/null || /usr/libexec/PlistBuddy -c 'Set :NSBluetoothAlwaysUsageDescription Connect Bluetooth game controllers to play Jaguar games.' "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Add :NSLocalNetworkUsageDescription string Connect to a Jaguar netplay peer or a local debugger when you enable those features.' "$app/Contents/Info.plist" 2>/dev/null || /usr/libexec/PlistBuddy -c 'Set :NSLocalNetworkUsageDescription Connect to a Jaguar netplay peer or a local debugger when you enable those features.' "$app/Contents/Info.plist"
ts=--timestamp; [ "$id" != - ] || ts=--timestamp=none
# All nested executable code is signed first, without executable entitlements.
while IFS= read -r -d '' file_path; do
    [ "$file_path" = "$app/Contents/MacOS/virtualjaguar" ] || codesign --force "$ts" --sign "$id" "$file_path"
done < <(python3 - "$app/Contents" <<'MACHO'
import os, sys
magic = {bytes.fromhex(s) for s in ('feedface', 'cefaedfe', 'feedfacf', 'cffaedfe', 'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
for root, dirs, names in os.walk(sys.argv[1]):
    for name in names:
        path = os.path.join(root, name)
        if os.path.islink(path):
            continue
        with open(path, 'rb') as f:
            if f.read(4) in magic:
                sys.stdout.buffer.write(os.fsencode(path) + b'\0')
MACHO
)
codesign --force "$ts" --entitlements "$ent" --sign "$id" "$app"
codesign --verify --deep --strict "$app"
if [ "$mode" != --test ]; then
    productbuild --component "$app" /Applications --sign "$installer" "$out"
    pkgutil --check-signature "$out"
fi

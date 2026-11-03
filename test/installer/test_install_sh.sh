#!/usr/bin/env bash
#
# Hermetic tests for tools/install.sh (#840).
#
#   test/installer/test_install_sh.sh [shell ...]     default: every shell found
#                                                      among sh bash dash zsh busybox
#
# Nothing here touches the real system.  Every run gets HOME in a temp dir, a fake
# RetroArch (VJ_INSTALL_RETROARCH), and a fake release tree served over file://
# (VJ_INSTALL_BASE_URL); the app goes to a temp --app-dest.  Fixtures are tiny
# fake ELF / Mach-O stubs of the HOST architecture, so the same suite runs on
# macOS and on Linux CI.
#
# The Linux .deb path needs root, so it is only exercised with --dry-run.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
SCRIPT="$HERE/../../tools/install.sh"
T=$(mktemp -d 2>/dev/null || mktemp -d -t vjinst)
trap 'command rm -rf "$T"' EXIT

PASS=0; FAIL=0
pass() { PASS=$((PASS + 1)); printf '  ok   %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL %s\n' "$1"; [ -n "${2:-}" ] && printf '%s\n' "$2" | sed 's/^/         | /'; }

contains() { case "$1" in *"$2"*) return 0 ;; *) return 1 ;; esac; }
check_has() { # name output needle
  if contains "$2" "$3"; then pass "$1"; else fail "$1 (missing: $3)" "$2"; fi
}
check_lacks() {
  if contains "$2" "$3"; then fail "$1 (unexpected: $3)" "$2"; else pass "$1"; fi
}
check_rc() { # name got want output
  if [ "$2" = "$3" ]; then pass "$1"; else fail "$1 (exit $2, wanted $3)" "$4"; fi
}
check_true() { # name command...
  _n=$1; shift
  if "$@"; then pass "$_n"; else fail "$_n"; fi
}

hash_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

OS=$(uname -s); MACHINE=$(uname -m)
case "$MACHINE" in arm64) MACHINE=aarch64 ;; amd64) MACHINE=x86_64 ;; esac

# --- fixtures ---------------------------------------------------------------
# fake_binary <path>: >100 KB stub that binary_arch() reads as the host arch.
fake_binary() {
  if [ "$OS" = Darwin ]; then
    if [ "$MACHINE" = aarch64 ]; then ct='\014\000\000\001'; else ct='\007\000\000\001'; fi
    # MH_MAGIC_64, cputype, cpusubtype 0, MH_DYLIB, ncmds 0, sizeofcmds 0, flags 0
    printf '\317\372\355\376'"$ct"'\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000' > "$1"
  else
    if [ "$MACHINE" = aarch64 ]; then em='\267'; else em='\076'; fi
    printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000\003\000'"$em"'\000' > "$1"
  fi
  head -c 130000 /dev/zero >> "$1"
  printf '%s' "${2:-}" >> "$1"
}

case "$OS:$MACHINE" in
  Darwin:aarch64) CORE_ASSET=virtualjaguar_libretro-macos-arm64.dylib ;;
  Darwin:x86_64)  CORE_ASSET=virtualjaguar_libretro-macos-x86_64.dylib ;;
  Linux:aarch64)  CORE_ASSET=virtualjaguar_libretro-linux-aarch64.so ;;
  *)              CORE_ASSET=virtualjaguar_libretro-linux-x86_64.so ;;
esac

# build_app_assets <dir> <ver-label> <marker>
build_app_assets() {
  d=$1; v=$2; mk=$3
  # macOS zip: Virtual Jaguar.app/Contents/{Info.plist,MacOS/virtualjaguar}
  w=$T/zipwork.$$; command rm -rf "$w"; mkdir -p "$w/Virtual Jaguar.app/Contents/MacOS"
  cat > "$w/Virtual Jaguar.app/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>Virtual Jaguar</string>
<key>CFBundleShortVersionString</key>
<string>$APPVER</string>
</dict></plist>
PL
  printf '#!/bin/sh\necho %s\n' "$mk" > "$w/Virtual Jaguar.app/Contents/MacOS/virtualjaguar"
  chmod +x "$w/Virtual Jaguar.app/Contents/MacOS/virtualjaguar"
  if command -v ditto >/dev/null 2>&1; then
    ditto -c -k --keepParent "$w/Virtual Jaguar.app" "$d/VirtualJaguar-$v-macos-universal.zip"
  else
    (cd "$w" && zip -qr "$d/VirtualJaguar-$v-macos-universal.zip" "Virtual Jaguar.app")
  fi
  command rm -rf "$w"
  for a in x86_64 aarch64; do
    cat > "$d/VirtualJaguar-$v-linux-$a.AppImage" <<AI
#!/bin/sh
# fake AppImage $mk $a
case "\$1" in --appimage-extract)
  mkdir -p squashfs-root/usr/share/icons/hicolor/256x256/apps
  echo png > squashfs-root/usr/share/icons/hicolor/256x256/apps/io.github.libretro.VirtualJaguar.png ;; esac
AI
  done
  for a in amd64 arm64; do
    printf 'fake deb %s %s\n' "$mk" "$a" > "$d/virtualjaguar_${v}_$a.deb"
  done
  printf 'fake setup %s\n' "$mk" > "$d/VirtualJaguar-$v-windows-x64-setup.exe"
}

write_sums() {
  ( cd "$1" && command rm -f SHA256SUMS.txt && for f in *; do printf '%s  %s\n' "$(hash_of "$f")" "$f"; done > "$T/sums.tmp" && mv "$T/sums.tmp" SHA256SUMS.txt )
}

# build_release <dir> <app-version-label|""> <marker>
build_release() {
  mkdir -p "$1"
  fake_binary "$1/$CORE_ASSET" "$3"
  if [ -n "$2" ]; then APPVER=${APPVER:-4.0.0}; build_app_assets "$1" "$2" "$3"; fi
  write_sums "$1"
}

# OLD: what the repo has today -- core only.   NEW: core + app.
OLD="$T/old"; NEW="$T/new"
APPVER=4.0.0
build_release "$OLD/latest/download" "" old-core
build_release "$OLD/download/nightly" "" old-nightly
build_release "$NEW/latest/download" 4.0.0 new-latest
build_release "$NEW/download/v4.0.0" 4.0.0 new-latest
build_release "$NEW/download/nightly" nightly nightly-one

fake_binary "$T/retroarch" ""
chmod +x "$T/retroarch"

# Fake verification tools (#1151), first on PATH for every run so the suite never
# calls the real gh / minisign (a real `gh attestation verify` would hit GitHub).
#   minisign: -Vm <file> -x <sig> -P <key>: succeeds when the key is the test key
#             and the signature file says GOOD.
#   gh:       `gh attestation verify ...` behaves as $FB/gh.mode says
#             (ok | 404 | auth | bad); 404 by default, like a release that
#             predates attestations.
FB="$T/fakebin"; mkdir -p "$FB"
cat > "$FB/minisign" <<'MS'
#!/bin/sh
key=""; sig=""
while [ $# -gt 0 ]; do
  case "$1" in -P) key=$2; shift 2 ;; -x) sig=$2; shift 2 ;; -Vm) shift 2 ;; *) shift ;; esac
done
[ "$key" = "RWTtestkey" ] || { echo "Signature verification failed" >&2; exit 1; }
grep -q '^GOOD' "$sig" 2>/dev/null
MS
cat > "$FB/gh" <<'GH'
#!/bin/sh
mode=$(cat "$(dirname "$0")/gh.mode" 2>/dev/null || echo 404)
[ "$1" = attestation ] || { echo "fake gh: unexpected $*" >&2; exit 2; }
case "$mode" in
  ok)   echo "Loaded digest sha256:0 for file://$3"; echo "✓ Verification succeeded!"; exit 0 ;;
  404)  echo "Error: HTTP 404: Not Found (https://api.github.com/repos/x/y/attestations/sha256:0)" >&2; exit 1 ;;
  auth) echo "To get started with GitHub CLI, please run:  gh auth login" >&2; exit 4 ;;
  *)    echo "Error: ✗ Verification failed: signature does not match" >&2; exit 1 ;;
esac
GH
chmod +x "$FB/minisign" "$FB/gh"
echo 404 > "$FB/gh.mode"
PUBKEY=""

# --- runner -----------------------------------------------------------------
# vj <shell> <base> <home> args...   -> sets OUT and RC
vj() {
  _sh=$1; _base=$2; _home=$3; shift 3
  mkdir -p "$_home"
  OUT=$(env -i PATH="$FB:$PATH" HOME="$_home" TERM=dumb NO_COLOR=1 \
        VJ_INSTALL_BASE_URL="file://$_base" VJ_INSTALL_RETROARCH="$T/retroarch" VJ_INSTALL_NO_TTY=1 \
        VJ_INSTALL_MINISIGN_PUBKEY="$PUBKEY" \
        $_sh "$SCRIPT" "$@" 2>&1 </dev/null)
  RC=$?
}

snapshot() { find "$@" 2>/dev/null | LC_ALL=C sort; }

run_suite() {
  SH=$1
  printf '\n== %s ==\n' "$SH"
  H="$T/home-$(basename "${SH%% *}")"; AD="$T/apps-$(basename "${SH%% *}")"; CD="$T/cores-$(basename "${SH%% *}")"
  command rm -rf "$H" "$AD" "$CD"; mkdir -p "$CD"

  # help / bad input
  vj "$SH" "$NEW" "$H" --help
  check_rc "help exits 0" "$RC" 0 "$OUT"; check_has "help mentions --what" "$OUT" "--what core|app|both"
  vj "$SH" "$NEW" "$H" --what nope
  check_rc "bad --what rejected" "$RC" 1 "$OUT"

  # --- the app channel does not exist yet (today's releases) ----------------
  vj "$SH" "$OLD" "$H" --app --dry-run --app-dest "$AD"
  check_rc "app-only on a release with no app: exit 3" "$RC" 3 "$OUT"
  check_has "no-app message is friendly" "$OUT" "No app build is published for this channel yet"
  check_lacks "no-app message has no shell noise" "$OUT" "command not found"
  vj "$SH" "$OLD" "$H" --app --channel nightly --dry-run --app-dest "$AD"
  check_rc "app-only on nightly with no app: exit 3" "$RC" 3 "$OUT"
  vj "$SH" "$OLD" "$H" --app --channel v9.9.9 --dry-run --app-dest "$AD"
  check_rc "missing release tag degrades to the same message" "$RC" 3 "$OUT"
  check_has "missing tag message" "$OUT" "No app build is published"
  vj "$SH" "$OLD" "$H" --both --dry-run --dest "$CD" --app-dest "$AD"
  check_rc "both with no app published still succeeds (core ran)" "$RC" 0 "$OUT"
  check_has "both: core planned" "$OUT" "would install $CORE_ASSET"
  check_has "both: app skipped gracefully" "$OUT" "No app build is published"

  # --- backward compat: no flags, no tty -> core only -----------------------
  S0=$(snapshot "$H" "$AD" "$CD")
  vj "$SH" "$OLD" "$H" --yes --dry-run --dest "$CD"
  check_rc "legacy invocation (--yes --dest) works" "$RC" 0 "$OUT"
  check_has "legacy: core planned" "$OUT" "would install $CORE_ASSET"
  check_lacks "legacy: app untouched" "$OUT" "Virtual Jaguar app"
  check_true "legacy dry-run changed nothing" test "$S0" = "$(snapshot "$H" "$AD" "$CD")"

  # --- app dry-run plans (nightly, v4) --------------------------------------
  vj "$SH" "$NEW" "$H" --app --channel nightly --dry-run --app-dest "$AD"
  check_rc "app dry-run exit 0" "$RC" 0 "$OUT"
  case "$OS" in
    Darwin) check_has "mac plan names the nightly zip" "$OUT" "VirtualJaguar-nightly-macos-universal.zip"
            check_has "mac plan target" "$OUT" "$AD/Virtual Jaguar.app" ;;
    Linux)  check_has "linux plan names the AppImage for this arch" "$OUT" "VirtualJaguar-nightly-linux-$MACHINE.AppImage"
            check_has "linux plan target" "$OUT" "$AD/VirtualJaguar.AppImage" ;;
  esac
  check_has "plan says dry-run" "$OUT" "--dry-run: nothing was changed (app)"
  check_true "app dry-run changed nothing" test "$S0" = "$(snapshot "$H" "$AD" "$CD")"

  vj "$SH" "$NEW" "$H" --app --dry-run --app-dest "$AD"
  check_has "latest resolves the version from SHA256SUMS" "$OUT" "(version 4.0.0)"

  if [ "$OS" = Linux ]; then
    vj "$SH" "$NEW" "$H" --app --dry-run --app-format deb --app-dest "$AD"
    case "$MACHINE" in aarch64) da=arm64 ;; *) da=amd64 ;; esac
    if command -v dpkg >/dev/null 2>&1 && [ "$(dpkg --print-architecture)" != "$da" ]; then :; else
      check_has "deb dry-run picks the matching arch" "$OUT" "virtualjaguar_4.0.0_$da.deb"
    fi
  else
    vj "$SH" "$NEW" "$H" --app --dry-run --app-format deb --app-dest "$AD"
    check_rc "linux-only format rejected on macOS" "$RC" 1 "$OUT"
  fi

  # --- real install into temp dirs, then update / up-to-date / force --------
  vj "$SH" "$NEW" "$H" --app --channel nightly --yes --app-dest "$AD"
  check_rc "app install exit 0" "$RC" 0 "$OUT"
  case "$OS" in
    Darwin) TARGET="$AD/Virtual Jaguar.app"
            check_true "app bundle installed" test -x "$TARGET/Contents/MacOS/virtualjaguar" ;;
    *)      TARGET="$AD/VirtualJaguar.AppImage"
            check_true "AppImage installed and executable" test -x "$TARGET"
            check_true "desktop entry uses the shared id" /usr/bin/grep -q '^Icon=io.github.libretro.VirtualJaguar$' "$H/.local/share/applications/io.github.libretro.VirtualJaguar.desktop"
            check_true "icon extracted from the AppImage" test -f "$H/.local/share/icons/hicolor/256x256/apps/io.github.libretro.VirtualJaguar.png" ;;
  esac
  check_has "SHA-256 verified" "$OUT" "SHA-256 matches SHA256SUMS.txt"
  RCPT=$(find "$H" -name install-receipt | head -1)
  check_true "receipt written" test -n "$RCPT"

  vj "$SH" "$NEW" "$H" --app --channel nightly --yes --app-dest "$AD"
  check_has "second run: already up to date" "$OUT" "already up to date"
  vj "$SH" "$NEW" "$H" --app --channel nightly --yes --force --app-dest "$AD"
  check_has "--force reinstalls" "$OUT" "reinstall"

  # a new nightly lands: the receipt hash differs -> update, old copy kept
  NEW2="$T/new2"; command rm -rf "$NEW2"; command cp -R "$NEW" "$NEW2"
  command rm -rf "$NEW2/download/nightly"
  build_release "$NEW2/download/nightly" nightly nightly-two
  vj "$SH" "$NEW2" "$H" --app --channel nightly --yes --app-dest "$AD"
  check_has "new nightly is an update" "$OUT" "update:"
  case "$OS" in
    Darwin) check_true "previous app kept aside" test -d "$TARGET.bak"
            check_true "new bundle in place" /usr/bin/grep -q nightly-two "$TARGET/Contents/MacOS/virtualjaguar" ;;
    *)      check_true "previous AppImage kept aside" test -f "$TARGET.bak"
            check_true "new AppImage in place" /usr/bin/grep -q nightly-two "$TARGET" ;;
  esac

  # a nightly bundle reports the project version (4.0.0) like a release: the release must
  # still be offered as an update (decided from the receipt, not Info.plist)
  vj "$SH" "$NEW2" "$H" --app --channel v4.0.0 --dry-run --app-dest "$AD"
  check_has "release offered over a nightly of the same project version" "$OUT" "update:"
  check_lacks "... and not called up to date" "$OUT" "already up to date"

  # --list reports the install
  vj "$SH" "$NEW" "$H" --list --dest "$CD" --app-dest "$AD"
  check_has "--list shows the installed app" "$OUT" "installed: "

  # unattended run with no --what: this script installed the app -> it is updated too
  vj "$SH" "$NEW" "$H" --yes --channel v4.0.0 --dest "$CD" --app-dest "$AD" --dry-run
  check_has "unattended default adds an app this script installed" "$OUT" "Virtual Jaguar app"
  # ... but a machine whose app was never installed by the script stays core-only
  vj "$SH" "$NEW" "$T/h-fresh-$$" --yes --dest "$CD" --dry-run
  check_lacks "unattended default stays core-only on a fresh machine" "$OUT" "Virtual Jaguar app"

  # --- verification fails closed ---------------------------------------------
  BAD="$T/bad"; command rm -rf "$BAD"; command cp -R "$NEW" "$BAD"
  case "$OS" in Darwin) af="VirtualJaguar-nightly-macos-universal.zip" ;; *) af="VirtualJaguar-nightly-linux-$MACHINE.AppImage" ;; esac
  printf 'tampered' >> "$BAD/download/nightly/$af"
  H2="$T/h-bad-$$"; AD2="$T/apps-bad-$$"
  vj "$SH" "$BAD" "$H2" --app --channel nightly --yes --app-dest "$AD2"
  check_rc "tampered app is rejected" "$RC" 1 "$OUT"
  check_has "mismatch reported" "$OUT" "SHA-256 mismatch"
  check_true "nothing installed after a mismatch" test ! -e "$AD2"

  printf 'tampered' >> "$BAD/latest/download/$CORE_ASSET"
  vj "$SH" "$BAD" "$H2" --core --yes --dest "$CD"
  check_rc "tampered core is rejected" "$RC" 1 "$OUT"
  check_true "core dir untouched after a mismatch" test -z "$(ls "$CD")"

  # --- the core still installs ------------------------------------------------
  vj "$SH" "$NEW" "$H" --core --yes --dest "$CD"
  check_rc "core install exit 0" "$RC" 0 "$OUT"
  check_has "core SHA-256 verified" "$OUT" "SHA-256 matches"
  check_true "core file in place" test -f "$CD/virtualjaguar_libretro.${CORE_ASSET##*.}"

  # --- macOS ships a zip only ----------------------------------------------------
  AD4="$T/apps4-$$"; H4="$T/h4-$$"
  vj "$SH" "$NEW" "$H4" --app --yes --channel v4.0.0 --app-format dmg --app-dest "$AD4"
  check_rc "--app-format dmg is rejected (there is no DMG)" "$RC" 1 "$OUT"
  if [ "$OS" = Darwin ]; then
    vj "$SH" "$NEW" "$H4" --app --yes --channel v4.0.0 --app-format zip --app-dest "$AD4"
    check_rc "zip install exit 0" "$RC" 0 "$OUT"
    check_true "zip: bundle installed" test -x "$AD4/Virtual Jaguar.app/Contents/MacOS/virtualjaguar"
    vj "$SH" "$NEW" "$H4" --app --yes --channel v4.0.0 --app-dest "$AD4"
    check_has "second run: up to date (version read from Info.plist)" "$OUT" "already up to date"
  fi

  # --- both, for real -----------------------------------------------------------
  CD3="$T/cores3-$$"; AD3="$T/apps3-$$"; H3="$T/h3-$$"; mkdir -p "$CD3"
  vj "$SH" "$NEW" "$H3" --both --yes --channel v4.0.0 --dest "$CD3" --app-dest "$AD3"
  check_rc "both installs" "$RC" 0 "$OUT"
  check_true "both: core present" test -f "$CD3/virtualjaguar_libretro.${CORE_ASSET##*.}"
  check_true "both: app present" test -n "$(ls "$AD3" 2>/dev/null)"

  # --- the checksum list's own signature / provenance (#1151) ---------------------
  # Default so far: no pinned key, fake gh answers 404 -> warned, installed anyway.
  check_has "no attestation (older release): warns and goes on" "$OUT" "no build attestation"
  _sfx=$(basename "${SH%% *}")     # per shell: the suite runs several in one process
  SIGNED="$T/signed-$_sfx"; command rm -rf "$SIGNED"; command cp -R "$NEW" "$SIGNED"
  printf 'untrusted comment: test\nGOOD\n' > "$SIGNED/download/nightly/SHA256SUMS.txt.minisig"
  printf 'untrusted comment: test\nBAD\n' > "$SIGNED/download/v4.0.0/SHA256SUMS.txt.minisig"
  PUBKEY=RWTtestkey
  H5="$T/h5-$_sfx"; AD5="$T/apps5-$_sfx"; CD5="$T/cores5-$_sfx"; mkdir -p "$CD5"
  vj "$SH" "$SIGNED" "$H5" --app --channel nightly --dry-run --app-dest "$AD5"
  check_rc "signed checksums: dry run ok" "$RC" 0 "$OUT"
  check_has "minisign signature verified" "$OUT" "SHA256SUMS.txt signature verified (project release key)"
  vj "$SH" "$SIGNED" "$H5" --core --channel nightly --yes --dest "$CD5"
  check_has "core path verifies the signature too" "$OUT" "SHA256SUMS.txt signature verified"
  check_has "... before the checksum" "$OUT" "SHA-256 matches"
  vj "$SH" "$SIGNED" "$H5" --app --dry-run --app-dest "$AD5"
  check_rc "channel without .minisig: still ok" "$RC" 0 "$OUT"
  check_has "channel without .minisig: warns" "$OUT" "has no SHA256SUMS.txt.minisig"
  vj "$SH" "$SIGNED" "$H5" --app --channel v4.0.0 --yes --app-dest "$AD5"
  check_rc "bad checksum signature: refused" "$RC" 1 "$OUT"
  check_has "bad signature message" "$OUT" "does NOT verify with the project release key"
  check_has "bad signature: points at --no-verify" "$OUT" "--no-verify skips this check"
  check_true "bad signature: nothing installed" test ! -e "$AD5"
  vj "$SH" "$SIGNED" "$H5" --app --channel v4.0.0 --yes --app-dest "$AD5" --no-verify
  check_rc "--no-verify installs despite the bad signature" "$RC" 0 "$OUT"
  check_has "--no-verify says so" "$OUT" "signature check skipped (--no-verify)"
  check_has "--no-verify keeps the checksum check" "$OUT" "SHA-256 matches SHA256SUMS.txt"
  # No pinned key -> the attestation path through gh.
  PUBKEY=""
  for mode in ok auth bad; do
    echo "$mode" > "$FB/gh.mode"
    vj "$SH" "$SIGNED" "$H5" --app --channel nightly --dry-run --app-dest "$AD5"
    case "$mode" in
      ok)   check_rc "gh attestation ok: proceeds" "$RC" 0 "$OUT"
            check_has "attestation verified" "$OUT" "provenance verified (GitHub build attestation)" ;;
      auth) check_rc "gh not logged in: proceeds" "$RC" 0 "$OUT"
            check_has "gh not logged in: notes it" "$OUT" "gh is not logged in" ;;
      bad)  check_rc "bad attestation: refused" "$RC" 1 "$OUT"
            check_has "bad attestation message" "$OUT" "did NOT verify" ;;
    esac
  done
  echo 404 > "$FB/gh.mode"
  vj "$SH" "$NEW" "$H5" --help
  check_has "help lists --no-verify" "$OUT" "--no-verify"
}

# --- pick shells ------------------------------------------------------------
if [ $# -gt 0 ]; then SHELLS="$*"; else
  SHELLS=""
  for s in sh bash dash zsh; do command -v "$s" >/dev/null 2>&1 && SHELLS="$SHELLS $s"; done
  command -v busybox >/dev/null 2>&1 && SHELLS="$SHELLS busybox_ash"
fi

for s in $SHELLS; do
  if [ "$s" = busybox_ash ]; then run_suite "busybox ash"; else run_suite "$s"; fi
done

printf '\n%s passed, %s failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]

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
  if [ "${MAKE_DMG:-0}" = 1 ] && [ "$OS" = Darwin ] && command -v hdiutil >/dev/null 2>&1; then
    hdiutil create -quiet -fs HFS+ -volname "Virtual Jaguar" -srcfolder "$w" -ov "$d/VirtualJaguar-$v-macos-universal.dmg" >/dev/null 2>&1 || true
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
MAKE_DMG=1 build_release "$NEW/download/v4.0.0" 4.0.0 new-latest
build_release "$NEW/download/nightly" nightly nightly-one

fake_binary "$T/retroarch" ""
chmod +x "$T/retroarch"

# --- runner -----------------------------------------------------------------
# vj <shell> <base> <home> args...   -> sets OUT and RC
vj() {
  _sh=$1; _base=$2; _home=$3; shift 3
  mkdir -p "$_home"
  OUT=$(env -i PATH="$PATH" HOME="$_home" TERM=dumb NO_COLOR=1 \
        VJ_INSTALL_BASE_URL="file://$_base" VJ_INSTALL_RETROARCH="$T/retroarch" VJ_INSTALL_NO_TTY=1 \
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

  # --- macOS DMG path -----------------------------------------------------------
  if [ "$OS" = Darwin ] && [ -f "$NEW/download/v4.0.0/VirtualJaguar-4.0.0-macos-universal.dmg" ]; then
    AD4="$T/apps4-$$"; H4="$T/h4-$$"
    vj "$SH" "$NEW" "$H4" --app --yes --channel v4.0.0 --app-format dmg --app-dest "$AD4"
    check_rc "dmg install exit 0" "$RC" 0 "$OUT"
    check_true "dmg: bundle installed" test -x "$AD4/Virtual Jaguar.app/Contents/MacOS/virtualjaguar"
    vj "$SH" "$NEW" "$H4" --app --yes --channel v4.0.0 --app-dest "$AD4"
    check_has "dmg then zip same version: up to date (version read from Info.plist)" "$OUT" "already up to date"
  fi

  # --- both, for real -----------------------------------------------------------
  CD3="$T/cores3-$$"; AD3="$T/apps3-$$"; H3="$T/h3-$$"; mkdir -p "$CD3"
  vj "$SH" "$NEW" "$H3" --both --yes --channel v4.0.0 --dest "$CD3" --app-dest "$AD3"
  check_rc "both installs" "$RC" 0 "$OUT"
  check_true "both: core present" test -f "$CD3/virtualjaguar_libretro.${CORE_ASSET##*.}"
  check_true "both: app present" test -n "$(ls "$AD3" 2>/dev/null)"
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

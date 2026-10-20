#!/usr/bin/env bash
# Opens windows: run only on the dedicated Studio/display host, never the maintainer's local desktop.
# VJ_WINDOWED_OK=1 fullscreen_menu_windowed_check.sh <virtualjaguar> <rom> <out-dir>
set -euo pipefail
[ "${VJ_WINDOWED_OK:-}" = 1 ] || { echo "refusing: set VJ_WINDOWED_OK=1 on the display test host"; exit 2; }
exe="$1"; rom="$2"; out="$3"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
for enabled in 0 1; do
    data="$(mktemp -d "$out/data-$enabled-XXXXXX")"
    cat > "$data/config.ini" <<'CONFIG'
ui.menubar_in_window=1
ui.viewports=0
ra.enabled=0
window.w=800
window.h=600
CONFIG
    # Missing key checks the legacy default; the enabled run checks the saved option.
    if [ "$enabled" = 1 ]; then echo 'ui.fullscreen_autohide_menu=1' >> "$data/config.ini"; fi
    hidden=$((1 - enabled))
    cat > "$data/menu.txt" <<SCRIPT
0 focus gained
100 mouse 320 240
200 expect menu-visible=1
400 key tap F11
2500 mouse 320 240
2800 expect fullscreen=1
2900 expect menu-visible=$hidden
3000 shot $out/fullscreen-$enabled.png
3200 mouse 200 1
3500 expect menu-visible=1
3700 mouse 320 240
4000 expect menu-visible=$hidden
4200 key tap F1
4500 expect menu-visible=$enabled
4700 key tap F1
5000 expect menu-visible=$hidden
5200 key tap Escape
7300 mouse 320 240
7600 expect fullscreen=0
7700 expect menu-visible=1
7800 shot $out/restored-$enabled.png
8000 quit
SCRIPT
    log="$out/menu-$enabled.log"
    "$exe" --emu-thread off --data-dir "$data" --tv-test-script "$data/menu.txt" "$rom" > "$log" 2>&1
    if grep -E 'tv: FAIL|headless: FAIL|owner violation|ThreadSanitizer' "$log"; then exit 1; fi
    grep -q "^ui.fullscreen_autohide_menu=$enabled$" "$data/config.ini"
    test -s "$out/fullscreen-$enabled.png"
    test -s "$out/restored-$enabled.png"
    echo "PASS fullscreen menu auto-hide=$enabled (hover, F1, restore, persistence)"
done
# Native-only macOS users can reveal the fullscreen bar without enabling the duplicate windowed menu.
if [ "$(uname -s)" = Darwin ]; then
    data="$(mktemp -d "$out/native-only-XXXXXX")"
    cat > "$data/config.ini" <<'CONFIG'
ui.menubar_in_window=0
ui.fullscreen_autohide_menu=1
ui.viewports=0
ra.enabled=0
window.w=800
window.h=600
CONFIG
    cat > "$data/menu.txt" <<'SCRIPT'
0 focus gained
100 mouse 320 240
200 expect menu-visible=0
400 key tap F11
2500 mouse 320 240
2800 expect fullscreen=1
2900 expect menu-visible=0
3200 key tap F1
3500 expect menu-visible=1
3700 key tap F1
4000 expect menu-visible=0
4200 key tap Escape
6300 mouse 320 240
6600 expect fullscreen=0
6700 expect menu-visible=0
6800 quit
SCRIPT
    log="$out/native-only.log"
    "$exe" --emu-thread off --data-dir "$data" --tv-test-script "$data/menu.txt" "$rom" > "$log" 2>&1
    if grep -E 'tv: FAIL|headless: FAIL|owner violation|ThreadSanitizer' "$log"; then exit 1; fi
    grep -q '^ui.menubar_in_window=0$' "$data/config.ini"
    echo 'PASS native-only preference survives fullscreen F1 reveal'
fi

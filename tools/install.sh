#!/bin/sh
#
# Virtual Jaguar libretro core -- installer / updater.
#
#   https://github.com/libretro/virtualjaguar-libretro
#
# POSIX sh.  Runs under sh, bash, zsh, dash, busybox ash.  Safe to pipe:
#
#   curl -fsSL https://raw.githubusercontent.com/libretro/virtualjaguar-libretro/develop/tools/install.sh | sh
#
# Piping into a shell means stdin is the script, so prompts read /dev/tty
# instead.  Where there is no tty (CI, a pipe with no terminal) the script goes
# non-interactive and uses defaults rather than hanging on a read.
#
# Read it before you run it -- sensible for anything off the internet, and this
# one asks for sudo.  `--dry-run` prints the whole plan and changes nothing.
#
# Usage:
#   ./install.sh                        interactive when a terminal is present
#   ./install.sh --yes                  no prompts, all defaults
#   ./install.sh --channel nightly
#   ./install.sh --channel v3.5.1
#   ./install.sh --dest /path/to/cores  skip frontend discovery
#   ./install.sh --list                 show what was found, install nothing
#   ./install.sh --dry-run
#   ./install.sh --auto-update weekly   install a background updater
#   ./install.sh --auto-update off      remove it
#
# ---------------------------------------------------------------------------
# THE MISTAKE THIS EXISTS TO PREVENT
#
# Release builds are named by board AND word size: `rpi4` is 32-bit armhf,
# `rpi4_64` is 64-bit aarch64.  Picking by `uname -m` gets this wrong on the
# many Raspberry Pi setups that run a 64-bit KERNEL with a 32-bit USERLAND --
# uname says `aarch64` while every binary is 32-bit.  Install the 64-bit core
# there and RetroArch says only "Failed to open libretro core".
#
# The core must match RETROARCH, not the kernel.  So the word size is read out
# of the RetroArch binary itself, and a mismatch aborts before anything is
# written.
# ---------------------------------------------------------------------------

set -eu

REPO="libretro/virtualjaguar-libretro"
RAW="https://raw.githubusercontent.com/$REPO/develop/tools/install.sh"
CORE_NAME="virtualjaguar_libretro"

CHANNEL="latest"
DEST=""
ASSUME_YES=0
DRY_RUN=0
LIST_ONLY=0
AUTO_UPDATE=""

# --------------------------------------------------------------------------
# output helpers
# --------------------------------------------------------------------------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  B=$(printf '\033[1m'); D=$(printf '\033[2m'); R=$(printf '\033[0m')
  GRN=$(printf '\033[32m'); YLW=$(printf '\033[33m'); RED=$(printf '\033[31m')
else
  B=''; D=''; R=''; GRN=''; YLW=''; RED=''
fi
say()  { printf '%s\n' "$*"; }
step() { printf '\n%s==>%s %s%s%s\n' "$GRN" "$R" "$B" "$*" "$R"; }
ok()   { printf '  %s+%s %s\n' "$GRN" "$R" "$*"; }
note() { printf '  %s-%s %s\n' "$D" "$R" "$*"; }
warn() { printf '  %s!%s %s\n' "$YLW" "$R" "$*" >&2; }
die()  { printf '\n%sERROR%s %s\n' "$RED" "$R" "$*" >&2; exit 1; }

# --------------------------------------------------------------------------
# interactivity
#
# When piped, stdin is the script text, so reading it would consume the script
# or block.  /dev/tty is the user's terminal regardless of redirection -- if it
# is not openable there is genuinely no one to ask.
# --------------------------------------------------------------------------
TTY=""
if [ -r /dev/tty ] && [ -w /dev/tty ] 2>/dev/null; then TTY=/dev/tty; fi
INTERACTIVE=0
[ -n "$TTY" ] && INTERACTIVE=1

ask() {
  # ask "<prompt>" "<default>"  -> echoes the answer
  _p="$1"; _d="$2"
  if [ "$INTERACTIVE" -eq 0 ] || [ "$ASSUME_YES" -eq 1 ]; then
    printf '%s' "$_d"; return 0
  fi
  printf '%s [%s]: ' "$_p" "$_d" > "$TTY"
  IFS= read -r _a < "$TTY" || _a=""
  [ -n "$_a" ] || _a="$_d"
  printf '%s' "$_a"
}

confirm() {
  # confirm "<prompt>"  -> 0 for yes
  [ "$ASSUME_YES" -eq 1 ] && return 0
  [ "$INTERACTIVE" -eq 0 ] && return 0
  printf '%s [Y/n]: ' "$1" > "$TTY"
  IFS= read -r _a < "$TTY" || _a=""
  case "$_a" in [nN]*) return 1 ;; *) return 0 ;; esac
}

# How to tell the user to re-run us.  When piped into a shell, $0 is the shell
# name and not a file, so neither `sed "$0"` nor a `$0 --flag` hint works.
if [ -r "$0" ] && [ "$0" != "sh" ] && [ "$0" != "-sh" ] && [ "$0" != "bash" ]; then
  SELF="$0"; SELF_IS_FILE=1
else
  SELF="curl -fsSL $RAW | sh -s --"; SELF_IS_FILE=0
fi

usage() {
  if [ "$SELF_IS_FILE" -eq 1 ]; then
    sed -n '3,40p' "$0" | sed 's/^#\{1,\} \{0,1\}//'
  else
    cat <<'USAGE'
Virtual Jaguar libretro core -- installer / updater.
  https://github.com/libretro/virtualjaguar-libretro

  --channel latest|nightly|<tag>   which build (default: latest)
  --dest <dir>                     install here, skip frontend discovery
  --list                           show what was found, install nothing
  --dry-run                        print the plan, change nothing
  --yes                            no prompts, take every default
  --auto-update daily|weekly|off   background updater
  --help

Piped into a shell, prompts read /dev/tty; with no tty it takes defaults.
Download it and read it first if you would rather:
  curl -fLO https://raw.githubusercontent.com/libretro/virtualjaguar-libretro/develop/tools/install.sh
USAGE
  fi
  exit 0
}

while [ $# -gt 0 ]; do
  case "$1" in
    --channel)     CHANNEL="${2:-}"; [ -n "$CHANNEL" ] || die "--channel needs a value"; shift 2 ;;
    --dest)        DEST="${2:-}";    [ -n "$DEST" ]    || die "--dest needs a path";    shift 2 ;;
    --auto-update) AUTO_UPDATE="${2:-}"; [ -n "$AUTO_UPDATE" ] || die "--auto-update needs daily|weekly|off"; shift 2 ;;
    --yes|-y)      ASSUME_YES=1; shift ;;
    --dry-run)     DRY_RUN=1; shift ;;
    --list)        LIST_ONLY=1; shift ;;
    -h|--help)     usage ;;
    *)             die "unknown option: $1   (try --help)" ;;
  esac
done

case "$AUTO_UPDATE" in ''|daily|weekly|off) ;; *) die "--auto-update takes daily, weekly or off (got '$AUTO_UPDATE')" ;; esac

command -v curl >/dev/null 2>&1 || die "curl is required.  Debian/RetroPie: sudo apt install -y curl"

# --------------------------------------------------------------------------
# binary inspection
#
# ELF: byte 4 is the class (1=32-bit, 2=64-bit), bytes 18-19 the machine.
# Mach-O: read the arch with `file`, which macOS always has.
# `od` is used rather than `file` on Linux because `file` is NOT installed on a
# stock RetroPie or Lakka image, while coreutils always is.
# --------------------------------------------------------------------------
elf_class() { od -An -t u1 -j 4  -N 1 "$1" 2>/dev/null | tr -d ' '; }
elf_machine() { od -An -t u1 -j 18 -N 1 "$1" 2>/dev/null | tr -d ' '; }

# Prints e.g. "64 aarch64" / "32 arm" / "64 x86_64" / "32 i386", or "" if unreadable.
binary_arch() {
  _f="$1"
  [ -f "$_f" ] || { printf ''; return; }
  case "$(uname -s)" in
    Darwin)
      # macOS frontends are usually UNIVERSAL binaries carrying both slices, so
      # "does it contain arm64" is the wrong question -- RetroArch.app contains
      # both, and on an Intel Mac the x86_64 slice is the one that runs. Which
      # slice runs is the host architecture, so when there is more than one,
      # that is what decides.
      _archs=""
      if command -v lipo >/dev/null 2>&1; then
        _archs=$(lipo -archs "$_f" 2>/dev/null || true)
      fi
      if [ -z "$_archs" ]; then
        # No developer tools: parse `file`, which lists every slice.
        _fo=$(file -b "$_f" 2>/dev/null || true)
        case "$_fo" in *arm64*)  _archs="arm64" ;; esac
        case "$_fo" in *x86_64*) _archs="$_archs x86_64" ;; esac
      fi
      _host=$(uname -m)
      # Substring match rather than `for _a in $_archs`: zsh does NOT word-split
      # unquoted variables, so that loop sees "x86_64 arm64" as ONE item, never
      # matches, and silently falls through to the first slice -- picking the
      # x86_64 core on an Apple Silicon Mac. zsh is macOS's default shell, so
      # this is the likely invocation, not an exotic one. `case` is split-free
      # and behaves identically in every shell.
      case " $_archs " in
        *" $_host "*) _pick="$_host" ;;
        *)            _pick=${_archs%% *} ;;
      esac
      case "$_pick" in
        arm64)  printf '64 aarch64' ;;
        x86_64) printf '64 x86_64' ;;
        *)      printf '' ;;
      esac
      ;;
    *)
      _c=$(elf_class "$_f"); _m=$(elf_machine "$_f")
      case "$_c" in 1) _bits=32 ;; 2) _bits=64 ;; *) printf ''; return ;; esac
      # EM_386=3  EM_ARM=40  EM_X86_64=62  EM_AARCH64=183
      case "$_m" in
        3)   printf '%s i386' "$_bits" ;;
        40)  printf '%s arm' "$_bits" ;;
        62)  printf '%s x86_64' "$_bits" ;;
        183) printf '%s aarch64' "$_bits" ;;
        *)   printf '' ;;
      esac
      ;;
  esac
}

# --------------------------------------------------------------------------
# 1. find frontends
#
# The core directory is read from RetroArch's OWN config (libretro_directory)
# rather than a hardcoded per-distro table.  That is authoritative on every
# distribution, including the ones with a read-only root where a guessed path
# would be wrong (Lakka, Batocera, EmuELEC, Recalbox).
# --------------------------------------------------------------------------
CANDIDATES=""   # newline-separated:  <label>|<retroarch-binary>|<core-dir>

cfg_value() {
  # cfg_value <file> <key> -- strips quotes and expands a leading ~
  _v=$(sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*//p" "$1" 2>/dev/null | head -1 | tr -d '"')
  case "$_v" in "~"*) _v="$HOME${_v#\~}" ;; esac
  printf '%s' "$_v"
}

add_candidate() {
  # add_candidate <label> <retroarch-bin> <core-dir>
  [ -n "$3" ] || return 0
  case "$CANDIDATES" in *"|$3"*) return 0 ;; esac    # already have this core dir
  CANDIDATES="$CANDIDATES$1|$2|$3
"
}

find_retroarch() {
  for _c in /opt/retropie/emulators/retroarch/bin/retroarch \
            /usr/bin/retroarch /usr/local/bin/retroarch /bin/retroarch \
            "$HOME/.local/bin/retroarch" \
            /Applications/RetroArch.app/Contents/MacOS/RetroArch \
            "$HOME/Applications/RetroArch.app/Contents/MacOS/RetroArch"; do
    [ -x "$_c" ] && { printf '%s' "$_c"; return; }
  done
  command -v retroarch 2>/dev/null || printf ''
}

RA_BIN=$(find_retroarch)

step "Looking for RetroArch and its core directory"

for _cfg in \
    "${XDG_CONFIG_HOME:-$HOME/.config}/retroarch/retroarch.cfg" \
    "$HOME/.config/retroarch/retroarch.cfg" \
    "$HOME/Library/Application Support/RetroArch/config/retroarch.cfg" \
    "$HOME/.var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg" \
    "$HOME/snap/retroarch/current/.config/retroarch/retroarch.cfg" \
    /opt/retropie/configs/all/retroarch.cfg \
    /etc/retroarch.cfg \
    /storage/.config/retroarch/retroarch.cfg \
    /userdata/system/configs/retroarch/retroarch.cfg \
    /recalbox/share/system/configs/retroarch/retroarch.cfg ; do
  [ -r "$_cfg" ] || continue
  _dir=$(cfg_value "$_cfg" libretro_directory)
  [ -n "$_dir" ] && [ -d "$_dir" ] || continue
  add_candidate "RetroArch ($_cfg)" "$RA_BIN" "$_dir"
done

# RetroPie keeps each core in its own directory, named by package, and that is
# what its emulators.cfg points at -- not libretro_directory.
if [ -r /opt/retropie/configs/atarijaguar/emulators.cfg ]; then
  _p=$(sed -n 's/.*-L[[:space:]]\{1,\}\([^[:space:]]*virtualjaguar[^[:space:]]*\.so\).*/\1/p' \
        /opt/retropie/configs/atarijaguar/emulators.cfg 2>/dev/null | head -1)
  [ -n "$_p" ] && add_candidate "RetroPie (lr-virtualjaguar)" "$RA_BIN" "$(dirname "$_p")"
fi
[ -d /opt/retropie/libretrocores/lr-virtualjaguar ] && \
  add_candidate "RetroPie (lr-virtualjaguar)" "$RA_BIN" /opt/retropie/libretrocores/lr-virtualjaguar

# Distro core directories, only when they already contain cores.
for _d in /usr/lib/libretro /usr/lib64/libretro \
          /usr/lib/x86_64-linux-gnu/libretro /usr/lib/aarch64-linux-gnu/libretro \
          /usr/lib/arm-linux-gnueabihf/libretro /usr/local/lib/libretro ; do
  [ -d "$_d" ] || continue
  set -- "$_d"/*_libretro.so
  [ -e "$1" ] || continue
  add_candidate "System cores ($_d)" "$RA_BIN" "$_d"
done

if [ -n "$DEST" ]; then
  CANDIDATES="Chosen with --dest|$RA_BIN|$DEST
"
fi

_n=$(printf '%s' "$CANDIDATES" | grep -c . || true)
if [ "${_n:-0}" -eq 0 ]; then
  warn "no RetroArch core directory found automatically."
  say ""
  say "  If RetroArch is installed, its core folder is shown under"
  say "  ${B}Settings -> Directory -> Cores${R} in the RetroArch menu."
  say "  Re-run with:  $SELF --dest /that/path"
  say ""
  [ "$INTERACTIVE" -eq 1 ] || die "nothing to install into."
  _manual=$(ask "  Core directory" "")
  [ -n "$_manual" ] || die "nothing to install into."
  [ -d "$_manual" ] || die "not a directory: $_manual"
  CANDIDATES="Entered by hand|$RA_BIN|$_manual
"
  _n=1
fi

# --------------------------------------------------------------------------
# 2. pick one
# --------------------------------------------------------------------------
if [ "$LIST_ONLY" -eq 1 ]; then
  step "Frontends found"
  printf '%s' "$CANDIDATES" | while IFS='|' read -r _l _b _d; do
    [ -n "$_l" ] || continue
    say "  $_l"
    say "      cores:     $_d"
    say "      retroarch: ${_b:-(not found)}"
  done
  say ""
  say "Install into one with:  $SELF --dest <core dir>"
  exit 0
fi

if [ "$_n" -eq 1 ]; then
  CHOICE=$(printf '%s' "$CANDIDATES" | head -1)
  ok "$(printf '%s' "$CHOICE" | cut -d'|' -f1)"
else
  step "More than one frontend found -- pick one"
  _i=0
  printf '%s' "$CANDIDATES" | while IFS='|' read -r _l _b _d; do
    [ -n "$_l" ] || continue
    _i=$((_i + 1)); say "  $_i) $_l"; say "     $D$_d$R"
  done
  say ""
  _sel=$(ask "  Number" "1")
  CHOICE=$(printf '%s' "$CANDIDATES" | sed -n "${_sel}p")
  [ -n "$CHOICE" ] || die "no such choice: $_sel"
  ok "$(printf '%s' "$CHOICE" | cut -d'|' -f1)"
fi

FE_LABEL=$(printf '%s' "$CHOICE" | cut -d'|' -f1)
RA_BIN=$(printf '%s'  "$CHOICE" | cut -d'|' -f2)
CORE_DIR=$(printf '%s' "$CHOICE" | cut -d'|' -f3)
note "cores -> $CORE_DIR"

[ -d "$CORE_DIR" ] || die "core directory does not exist: $CORE_DIR"
if [ ! -w "$CORE_DIR" ] && [ "$(id -u)" -ne 0 ]; then
  case "$(uname -s)" in
    Linux)
      if ! command -v sudo >/dev/null 2>&1; then
        die "$CORE_DIR is not writable and sudo is unavailable.

     Distributions like Lakka, Batocera, EmuELEC and Recalbox mount the root
     filesystem read-only and cores cannot be replaced in place.  Use that
     distribution's own core-update mechanism instead."
      fi
      ;;
  esac
fi

# --------------------------------------------------------------------------
# 3. word size and machine -- from the frontend binary
# --------------------------------------------------------------------------
step "Working out which build you need"

ARCHINFO=""
[ -n "$RA_BIN" ] && ARCHINFO=$(binary_arch "$RA_BIN")

if [ -z "$ARCHINFO" ]; then
  # Fall back to an existing core in the same directory -- still the frontend's
  # own ABI, which is the thing that has to match.
  for _c in "$CORE_DIR"/*_libretro.so "$CORE_DIR"/*_libretro.dylib; do
    [ -f "$_c" ] || continue
    ARCHINFO=$(binary_arch "$_c"); [ -n "$ARCHINFO" ] && { note "read from $(basename "$_c")"; break; }
  done
fi
[ -n "$ARCHINFO" ] || die "could not determine the frontend's architecture.
     Neither the RetroArch binary nor an existing core could be read."

BITS=$(printf '%s' "$ARCHINFO" | cut -d' ' -f1)
MACH=$(printf '%s' "$ARCHINFO" | cut -d' ' -f2)
ok "frontend is ${BITS}-bit $MACH"

_kern=$(uname -m)
if [ "$BITS" = 32 ] && [ "$_kern" = "aarch64" ]; then
  warn "kernel says '$_kern' but the frontend is 32-bit: 64-bit kernel, 32-bit userland."
  warn "Using the 32-bit core.  This is the case picking by \`uname -m\` gets wrong."
fi

# board, for the Pi-tuned builds
MODEL=""
[ -r /proc/device-tree/model ] && MODEL=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null)
[ -n "$MODEL" ] || MODEL=$(sed -n 's/^Model[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo 2>/dev/null | head -1)

OS=$(uname -s)
ASSET=""
case "$OS" in
  Darwin)
    case "$MACH" in
      aarch64) ASSET="$CORE_NAME-macos-arm64.dylib" ;;
      x86_64)  ASSET="$CORE_NAME-macos-x86_64.dylib" ;;
    esac
    ;;
  Linux)
    case "$MODEL" in
      *"Raspberry Pi 5"*)               PI=rpi5; SUF=cortex-a76 ;;
      *"Raspberry Pi 4"*|*"Pi 400"*)    PI=rpi4; SUF=cortex-a72 ;;
      *"Compute Module 4"*)             PI=rpi4; SUF=cortex-a72 ;;
      *"Raspberry Pi 3"*|*"Pi Zero 2"*) PI=rpi3; SUF=cortex-a53 ;;
      *"Compute Module 3"*)             PI=rpi3; SUF=cortex-a53 ;;
      *"Raspberry Pi 2"*)               PI=rpi2; SUF=cortex-a7  ;;
      *"Raspberry Pi"*)                 PI=rpi1; SUF=armv6      ;;
      *)                                PI=""; SUF="" ;;
    esac
    if [ -n "$PI" ]; then
      [ -n "$MODEL" ] && ok "board: $MODEL"
      case "$BITS:$PI" in
        64:rpi1|64:rpi2) ASSET="$CORE_NAME-linux-aarch64.so" ;;   # no 64-bit build for these parts
        64:*)            ASSET="$CORE_NAME-linux-${PI}_64-${SUF}.so" ;;
        32:*)            ASSET="$CORE_NAME-linux-${PI}-${SUF}.so" ;;
      esac
    else
      case "$BITS $MACH" in
        "64 x86_64")  ASSET="$CORE_NAME-linux-x86_64.so" ;;
        "32 i386")    ASSET="$CORE_NAME-linux-i686.so" ;;
        "64 aarch64") ASSET="$CORE_NAME-linux-aarch64.so" ;;
        "32 arm")     ASSET="$CORE_NAME-linux-rpi2-cortex-a7.so"
                      warn "generic 32-bit ARM: using the ARMv7 build (rpi2/cortex-a7)." ;;
      esac
    fi
    ;;
  *)
    die "unsupported OS '$OS'.  This installer handles Linux and macOS.
     Windows builds are on the releases page: https://github.com/$REPO/releases"
    ;;
esac
[ -n "$ASSET" ] || die "no build matches ${BITS}-bit $MACH on $OS.
     Please open an issue at https://github.com/$REPO/issues with:
       os=$OS mach=$MACH bits=$BITS model=${MODEL:-n/a} uname=$_kern"
ok "build: $ASSET"

# --------------------------------------------------------------------------
# 4. channel
# --------------------------------------------------------------------------
if [ "$CHANNEL" = "latest" ] && [ "$INTERACTIVE" -eq 1 ] && [ "$ASSUME_YES" -eq 0 ] && [ -z "$AUTO_UPDATE" ]; then
  step "Which build?"
  say "  1) latest release          ${D}stable, recommended${R}"
  say "  2) nightly                 ${D}every push to develop; gated on compiling, not on tests${R}"
  say "  3) a specific version      ${D}e.g. v3.5.1${R}"
  case "$(ask '  Number' '1')" in
    2) CHANNEL="nightly" ;;
    3) CHANNEL=$(ask "  Tag" "v3.5.1") ;;
    *) CHANNEL="latest" ;;
  esac
fi

case "$CHANNEL" in
  latest)  URL="https://github.com/$REPO/releases/latest/download/$ASSET" ;;
  nightly) URL="https://github.com/$REPO/releases/download/nightly/$ASSET" ;;
  *)       URL="https://github.com/$REPO/releases/download/$CHANNEL/$ASSET" ;;
esac
ok "channel: $CHANNEL"

# --------------------------------------------------------------------------
# 5. download and check BEFORE touching anything installed
# --------------------------------------------------------------------------
case "$ASSET" in *.dylib) EXT=dylib ;; *) EXT=so ;; esac
CORE_PATH="$CORE_DIR/$CORE_NAME.$EXT"

step "Downloading"
note "$URL"

TMP=$(mktemp -d 2>/dev/null || mktemp -d -t vjinstall)
trap 'rm -rf "$TMP"' EXIT INT TERM

curl -fsSL --retry 3 -o "$TMP/core" "$URL" || die "download failed.

     That release may not ship a build named '$ASSET'.
     What it does ship: https://github.com/$REPO/releases"

SIZE=$(wc -c < "$TMP/core" | tr -d ' ')
[ "${SIZE:-0}" -gt 100000 ] || die "downloaded $SIZE bytes -- that is not a core."
ok "$((SIZE / 1024)) KB"

step "Checking it matches your frontend"
DL=$(binary_arch "$TMP/core")
[ -n "$DL" ] || die "the downloaded file is not a shared library this script can read."
[ "$DL" = "$ARCHINFO" ] || die "architecture mismatch -- nothing was changed.
       frontend:   $ARCHINFO
       downloaded: $DL
     Please report at https://github.com/$REPO/issues with:
       asset=$ASSET model=${MODEL:-n/a} uname=$_kern"
ok "both $ARCHINFO"

if [ "$DRY_RUN" -eq 1 ]; then
  step "--dry-run: nothing was changed"
  say "  would install $ASSET"
  say "  into          $CORE_PATH"
  exit 0
fi

# --------------------------------------------------------------------------
# 6. install
# --------------------------------------------------------------------------
SUDO=""
[ "$(id -u)" -eq 0 ] || { [ -w "$CORE_DIR" ] || SUDO="sudo"; }

if [ -f "$CORE_PATH" ]; then
  confirm "  Replace the core already at $CORE_PATH?" || die "cancelled -- nothing changed."
fi

step "Installing"
BACKUP=""
if [ -f "$CORE_PATH" ]; then
  BACKUP="$CORE_PATH.bak-$(date +%Y%m%d-%H%M%S)"
  $SUDO cp -p "$CORE_PATH" "$BACKUP" || die "could not back up the existing core."
  ok "backed up -> $(basename "$BACKUP")"
fi
$SUDO mkdir -p "$CORE_DIR"
$SUDO cp "$TMP/core" "$CORE_PATH" || die "could not write $CORE_PATH"
$SUDO chmod 0644 "$CORE_PATH" 2>/dev/null || true
ok "installed -> $CORE_PATH"

if [ -n "$RA_BIN" ] && [ -x "$RA_BIN" ]; then
  step "Asking RetroArch to load it"
  if _out=$("$RA_BIN" -L "$CORE_PATH" --version 2>&1); then
    ok "loaded"
    printf '%s\n' "$_out" | sed -n '1,2p' | sed 's/^/      /'
  else
    warn "RetroArch could not load it.  Over SSH with no display this check is"
    warn "not conclusive, but if games fail too, restore the backup:"
    [ -n "$BACKUP" ] && say "      sudo cp -p '$BACKUP' '$CORE_PATH'"
    printf '%s\n' "$_out" | sed -n '1,4p' | sed 's/^/      /'
  fi
fi

# --------------------------------------------------------------------------
# 7. optional auto-update
#
# systemd user timer where available, else cron.  Both are removable with
# `--auto-update off`, and both just re-run this script with --yes.
# --------------------------------------------------------------------------
UPDATER="$HOME/.local/bin/vj-core-update"
SYSTEMD_DIR="$HOME/.config/systemd/user"

remove_auto_update() {
  _did=0
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now vj-core-update.timer >/dev/null 2>&1 && _did=1 || true
    rm -f "$SYSTEMD_DIR/vj-core-update.timer" "$SYSTEMD_DIR/vj-core-update.service" 2>/dev/null || true
    systemctl --user daemon-reload >/dev/null 2>&1 || true
  fi
  if command -v crontab >/dev/null 2>&1 && crontab -l 2>/dev/null | grep -q vj-core-update; then
    crontab -l 2>/dev/null | grep -v vj-core-update | crontab - && _did=1
  fi
  rm -f "$UPDATER" 2>/dev/null || true
  [ "$_did" -eq 1 ] && ok "auto-update removed" || note "no auto-update was installed"
}

if [ "$AUTO_UPDATE" = "off" ]; then
  step "Removing auto-update"; remove_auto_update; exit 0
fi

if [ -z "$AUTO_UPDATE" ] && [ "$INTERACTIVE" -eq 1 ] && [ "$ASSUME_YES" -eq 0 ]; then
  step "Keep it up to date automatically?"
  say "  1) no                      ${D}re-run this script when you want to update${R}"
  say "  2) weekly"
  say "  3) daily"
  case "$(ask '  Number' '1')" in
    2) AUTO_UPDATE=weekly ;;
    3) AUTO_UPDATE=daily ;;
    *) AUTO_UPDATE="" ;;
  esac
fi

if [ -n "$AUTO_UPDATE" ] && [ "$AUTO_UPDATE" != "off" ]; then
  step "Setting up $AUTO_UPDATE auto-update"
  mkdir -p "$(dirname "$UPDATER")"
  cat > "$UPDATER" <<UPD
#!/bin/sh
# Auto-generated by the Virtual Jaguar core installer.  Remove with:
#   sh <(curl -fsSL $RAW) --auto-update off
set -eu
curl -fsSL "$RAW" -o "\$0.tmp.\$\$"
sh "\$0.tmp.\$\$" --yes --channel "$CHANNEL" --dest "$CORE_DIR"
rm -f "\$0.tmp.\$\$"
UPD
  chmod +x "$UPDATER"
  ok "updater -> $UPDATER"

  if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
    mkdir -p "$SYSTEMD_DIR"
    cat > "$SYSTEMD_DIR/vj-core-update.service" <<SVC
[Unit]
Description=Update the Virtual Jaguar libretro core

[Service]
Type=oneshot
ExecStart=$UPDATER
SVC
    cat > "$SYSTEMD_DIR/vj-core-update.timer" <<TMR
[Unit]
Description=Update the Virtual Jaguar libretro core ($AUTO_UPDATE)

[Timer]
OnCalendar=$AUTO_UPDATE
Persistent=true

[Install]
WantedBy=timers.target
TMR
    systemctl --user daemon-reload >/dev/null 2>&1 || true
    if systemctl --user enable --now vj-core-update.timer >/dev/null 2>&1; then
      ok "systemd user timer enabled ($AUTO_UPDATE)"
      note "status:  systemctl --user status vj-core-update.timer"
    else
      warn "could not enable the systemd timer; falling back to cron"
      AUTO_UPDATE_CRON=1
    fi
  else
    AUTO_UPDATE_CRON=1
  fi

  if [ "${AUTO_UPDATE_CRON:-0}" = 1 ]; then
    if command -v crontab >/dev/null 2>&1; then
      case "$AUTO_UPDATE" in
        daily)  _sched="17 4 * * *" ;;
        weekly) _sched="17 4 * * 0" ;;
      esac
      { crontab -l 2>/dev/null | grep -v vj-core-update; printf '%s %s\n' "$_sched" "$UPDATER"; } | crontab -
      ok "cron entry added ($AUTO_UPDATE)"
    else
      warn "neither a systemd user session nor crontab is available."
      warn "Run $UPDATER yourself when you want to update."
    fi
  fi
  note "remove later with:  $SELF --auto-update off"
fi

step "Done"
say "  frontend: $FE_LABEL"
say "  core:     $CORE_PATH"
say "  channel:  $CHANNEL"
[ -n "$BACKUP" ] && say "  undo:     ${SUDO:+sudo }cp -p '$BACKUP' '$CORE_PATH'"
say ""
say "  Put Jaguar ROMs where your frontend scans for them, then restart it so"
say "  the new core is picked up."

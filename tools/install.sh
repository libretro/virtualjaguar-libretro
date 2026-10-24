#!/bin/sh
#
# Virtual Jaguar -- installer / updater for the RetroArch core and the app.
#
#   https://github.com/libretro/virtualjaguar-libretro
#
# POSIX sh.  Runs under sh, bash, zsh, dash, busybox ash.  Safe to pipe:
#
#   curl -fsSL https://raw.githubusercontent.com/libretro/virtualjaguar-libretro/master/tools/install.sh | sh
#
# Piping into a shell means stdin is the script, so prompts read /dev/tty
# instead.  Where there is no tty (CI, a pipe with no terminal) the script goes
# non-interactive and uses defaults rather than hanging on a read.
#
# Read it before you run it -- sensible for anything off the internet, and the
# core install may ask for sudo.  `--dry-run` prints the whole plan and changes
# nothing.
#
# Usage:
#   ./install.sh                        interactive when a terminal is present
#   ./install.sh --yes                  no prompts, all defaults
#   ./install.sh --what core|app|both   what to install or update (default: ask;
#                                       no terminal -> the core, plus the app if
#                                       this script installed it before)
#   ./install.sh --core | --app | --both        same, shorter
#   ./install.sh --channel nightly
#   ./install.sh --channel v3.5.1
#   ./install.sh --dest /path/to/cores  core: skip frontend discovery
#   ./install.sh --app-dest /path       app: Applications dir (macOS) or bin dir (Linux)
#   ./install.sh --app-format F         app: zip (macOS), appimage|deb (Linux)
#   ./install.sh --allow-unsigned       macOS: allow clearing quarantine on an
#                                       app Gatekeeper rejects (see below)
#   ./install.sh --force                reinstall even if already up to date
#   ./install.sh --list                 show what was found, install nothing
#   ./install.sh --dry-run
#   ./install.sh --auto-update weekly   install a background updater
#   ./install.sh --auto-update off      remove it
#
# Environment:
#   VJ_INSTALL_BASE_URL   release host, default https://github.com/<repo>/releases
#                         (the layout is <base>/latest/download/<file> and
#                         <base>/download/<tag>/<file>; file:// works)
#   VJ_INSTALL_RETROARCH  path to the RetroArch binary, skips the search
#   VJ_INSTALL_NO_TTY     1 = behave as if there were no terminal (tests, CI)
#
# THE APP
#   macOS    Virtual Jaguar.app from the zip into /Applications, or
#            ~/Applications when that is not writable.  Updated where it already is.
#   Linux    the AppImage into ~/.local/bin plus a .desktop entry; or the .deb
#            through apt/dpkg with --app-format deb (that needs sudo).  x86_64 and
#            aarch64 builds only, chosen from the USERLAND, not the kernel.
#   Every download is checked against the release's SHA256SUMS.txt first.
#   Windows is install.ps1.
#
#   Gatekeeper: files fetched with curl carry no quarantine flag, so a signed and
#   notarized app opens without a prompt and this script never touches its
#   protections.  The one exception: if the app is rejected by `spctl` AND carries
#   a quarantine flag, the flag is cleared only after you say yes (or pass
#   --allow-unsigned).  --yes alone never does it.
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
# written.  The app's Linux builds follow the same rule: they are matched to
# the userland (read from /bin/sh), never to the kernel.
# ---------------------------------------------------------------------------

set -eu

REPO="libretro/virtualjaguar-libretro"
# master = the released installer; --auto-update re-fetches this URL.
RAW="https://raw.githubusercontent.com/$REPO/master/tools/install.sh"
BASE="${VJ_INSTALL_BASE_URL:-https://github.com/$REPO/releases}"
BASE="${BASE%/}"
CORE_NAME="virtualjaguar_libretro"
MAC_APP_NAME="Virtual Jaguar.app"
APPIMAGE_NAME="VirtualJaguar.AppImage"
DEB_PKG="virtualjaguar"

CHANNEL="latest"
DEST=""
APP_DEST=""
APP_FORMAT=""
WHAT=""
ASSUME_YES=0
DRY_RUN=0
LIST_ONLY=0
FORCE=0
ALLOW_UNSIGNED=0
AUTO_UPDATE=""
CORE_SOFT=0

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

# Run a file-mutating command, elevating only when we actually need to.
#
# `command` on the non-sudo path bypasses any alias or shell function named
# rm/cp/mv -- the interactive `-i` variants are a common default, and a prompt
# with nothing attached to answer it blocks forever rather than failing.
# The sudo path needs no such guard (sudo execs the binary, never a shell
# alias) and could not take one anyway, since `command` is a shell builtin.
# $SUDO is set just before the install step; empty means "we can write here".
as_root() {
  if [ -n "${SUDO:-}" ]; then sudo "$@"; else command "$@"; fi
}

# --------------------------------------------------------------------------
# interactivity
#
# When piped, stdin is the script text, so reading it would consume the script
# or block.  /dev/tty is the user's terminal regardless of redirection -- if it
# is not openable there is genuinely no one to ask.
# --------------------------------------------------------------------------
TTY=""
# VJ_INSTALL_NO_TTY=1 forces the no-terminal path (the test suite runs from terminals).
if [ -z "${VJ_INSTALL_NO_TTY:-}" ] && [ -r /dev/tty ] && [ -w /dev/tty ] 2>/dev/null; then TTY=/dev/tty; fi
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

confirm_no() {
  # confirm_no "<prompt>"  -> 0 only on an explicit yes; default NO, and NO
  # whenever nobody can be asked.  For the one decision that weakens a protection.
  [ "$INTERACTIVE" -eq 0 ] && return 1
  [ "$ASSUME_YES" -eq 1 ] && return 1
  printf '%s [y/N]: ' "$1" > "$TTY"
  IFS= read -r _a < "$TTY" || _a=""
  case "$_a" in [yY]*) return 0 ;; *) return 1 ;; esac
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
    sed -n '3,/^# ---/{/^# ---/!p;}' "$SELF" | sed 's/^#\{1,\} \{0,1\}//'
  else
    cat <<'USAGE'
Virtual Jaguar -- installer / updater for the RetroArch core and the app.
  https://github.com/libretro/virtualjaguar-libretro

  --what core|app|both             what to install or update (--core --app --both)
  --channel latest|nightly|<tag>   which build (default: latest)
  --dest <dir>                     core: install here, skip frontend discovery
  --app-dest <dir>                 app: Applications dir (macOS) / bin dir (Linux)
  --app-format zip|appimage|deb   app package (default: zip / appimage)
  --allow-unsigned                 macOS: may clear quarantine on a rejected app
  --force                          reinstall even if already up to date
  --list                           show what was found, install nothing
  --dry-run                        print the plan, change nothing
  --yes                            no prompts, take every default
  --auto-update daily|weekly|off   background updater
  --help

Piped into a shell, prompts read /dev/tty; with no tty it takes defaults.
Windows: use install.ps1.  Download this script and read it first if you like:
  curl -fLO https://raw.githubusercontent.com/libretro/virtualjaguar-libretro/master/tools/install.sh
USAGE
  fi
  exit 0
}

while [ $# -gt 0 ]; do
  case "$1" in
    --channel)     CHANNEL="${2:-}"; [ -n "$CHANNEL" ] || die "--channel needs a value"; shift 2 ;;
    --dest)        DEST="${2:-}";    [ -n "$DEST" ]    || die "--dest needs a path";    shift 2 ;;
    --app-dest)    APP_DEST="${2:-}"; [ -n "$APP_DEST" ] || die "--app-dest needs a path"; shift 2 ;;
    --app-format)  APP_FORMAT="${2:-}"; [ -n "$APP_FORMAT" ] || die "--app-format needs a value"; shift 2 ;;
    --what)        WHAT="${2:-}"; [ -n "$WHAT" ] || die "--what needs core, app or both"; shift 2 ;;
    --core)        WHAT=core; shift ;;
    --app)         WHAT=app; shift ;;
    --both)        WHAT=both; shift ;;
    --auto-update) AUTO_UPDATE="${2:-}"; [ -n "$AUTO_UPDATE" ] || die "--auto-update needs daily|weekly|off"; shift 2 ;;
    --yes|-y)      ASSUME_YES=1; shift ;;
    --dry-run)     DRY_RUN=1; shift ;;
    --list)        LIST_ONLY=1; shift ;;
    --force)       FORCE=1; shift ;;
    --allow-unsigned) ALLOW_UNSIGNED=1; shift ;;
    -h|--help)     usage ;;
    *)             die "unknown option: $1   (try --help)" ;;
  esac
done

case "$AUTO_UPDATE" in ''|daily|weekly|off) ;; *) die "--auto-update takes daily, weekly or off (got '$AUTO_UPDATE')" ;; esac
case "$WHAT" in ''|core|app|both) ;; *) die "--what takes core, app or both (got '$WHAT')" ;; esac
case "$APP_FORMAT" in ''|zip|appimage|deb) ;; *) die "--app-format takes zip, appimage or deb (got '$APP_FORMAT'; macOS ships a zip only)" ;; esac

command -v curl >/dev/null 2>&1 || die "curl is required.  Debian/RetroPie: sudo apt install -y curl"

# Scratch space for the whole run.  Parts that run in a subshell (see main) share
# it through files, and one trap removes it.
WORK=$(mktemp -d 2>/dev/null || mktemp -d -t vjinstall) || die "could not create a temp directory"
trap 'command rm -rf "$WORK"' EXIT INT TERM

# --------------------------------------------------------------------------
# release access: URLs, SHA256SUMS.txt, hashing
# --------------------------------------------------------------------------
release_url() {
  # release_url <file> -> URL of that file on the selected channel
  case "$CHANNEL" in
    latest)  printf '%s/latest/download/%s' "$BASE" "$1" ;;
    nightly) printf '%s/download/nightly/%s' "$BASE" "$1" ;;
    *)       printf '%s/download/%s/%s' "$BASE" "$CHANNEL" "$1" ;;
  esac
}

# Fetch SHA256SUMS.txt once per run.  Returns 1 when the channel has none.
ensure_sums() {
  [ -s "$WORK/SHA256SUMS.txt" ] && return 0
  curl -fsSL --retry 3 -o "$WORK/SHA256SUMS.txt" "$(release_url SHA256SUMS.txt)" 2>/dev/null || {
    command rm -f "$WORK/SHA256SUMS.txt"; return 1
  }
  [ -s "$WORK/SHA256SUMS.txt" ]
}

# First SHA256SUMS entry named <prefix>*<suffix>.  Prefix/suffix are matched
# literally; a glob in a variable is NOT portable (zsh does not treat an
# unquoted expansion in a case pattern as a pattern).
sums_find() {
  while read -r _h _n; do
    _n=${_n#\*}
    case "$_n" in
      "$1"*"$2") printf '%s' "$_n"; return 0 ;;
    esac
  done < "$WORK/SHA256SUMS.txt"
  return 1
}

sums_hash() {
  while read -r _h _n; do
    _n=${_n#\*}
    [ "$_n" = "$1" ] && { printf '%s' "$_h" | tr 'A-F' 'a-f'; return 0; }
  done < "$WORK/SHA256SUMS.txt"
  return 1
}

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  elif command -v openssl >/dev/null 2>&1; then openssl dgst -sha256 "$1" | sed 's/.*= *//'
  else return 1
  fi
}

# verify_sum <file> <asset-name> -> 0 verified; 1 mismatch / unlisted (hard);
# 2 no way to check (no SHA256SUMS, or no hashing tool).  Callers decide.
verify_sum() {
  ensure_sums || return 2
  _want=$(sums_hash "$2") || return 1
  [ -n "$_want" ] || return 1
  _got=$(sha256_of "$1") || return 2
  [ "$_got" = "$_want" ] || { printf 'expected %s\n     got      %s' "$_want" "$_got" >&2; return 1; }
  return 0
}

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

# ==========================================================================
# THE CORE
#
# Runs in a subshell (see main), so a die here ends only this part and `both`
# can still go on to the app.  Shared results go through files in $WORK.
# ==========================================================================
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
  if [ -n "${VJ_INSTALL_RETROARCH:-}" ]; then
    [ -x "$VJ_INSTALL_RETROARCH" ] && printf '%s' "$VJ_INSTALL_RETROARCH"
    return 0
  fi
  for _c in /opt/retropie/emulators/retroarch/bin/retroarch \
            /usr/bin/retroarch /usr/local/bin/retroarch /bin/retroarch \
            "$HOME/.local/bin/retroarch" \
            /Applications/RetroArch.app/Contents/MacOS/RetroArch \
            "$HOME/Applications/RetroArch.app/Contents/MacOS/RetroArch"; do
    [ -x "$_c" ] && { printf '%s' "$_c"; return; }
  done
  command -v retroarch 2>/dev/null || printf ''
}

discover_frontends() {
  RA_BIN=$(find_retroarch)

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
  return 0
}

core_install() {
  # ------------------------------------------------------------------------
  # 1. find frontends
  #
  # The core directory is read from RetroArch's OWN config (libretro_directory)
  # rather than a hardcoded per-distro table.  That is authoritative on every
  # distribution, including the ones with a read-only root where a guessed path
  # would be wrong (Lakka, Batocera, EmuELEC, Recalbox).
  # ------------------------------------------------------------------------
  step "Looking for RetroArch and its core directory"
  discover_frontends

  _n=$(printf '%s' "$CANDIDATES" | grep -c . || true)
  if [ "${_n:-0}" -eq 0 ]; then
    if [ "$CORE_SOFT" -eq 1 ]; then
      note "no RetroArch core directory found; skipping the core (the app is still updated)."
      return 0
    fi
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

  # ------------------------------------------------------------------------
  # 2. pick one
  # ------------------------------------------------------------------------
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
    return 0
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
  printf '%s' "$CORE_DIR" > "$WORK/core_dir"

  # ------------------------------------------------------------------------
  # 3. word size and machine -- from the frontend binary
  # ------------------------------------------------------------------------
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
     Windows: use install.ps1 (see https://github.com/$REPO#install)."
      ;;
  esac
  [ -n "$ASSET" ] || die "no build matches ${BITS}-bit $MACH on $OS.
     Please open an issue at https://github.com/$REPO/issues with:
       os=$OS mach=$MACH bits=$BITS model=${MODEL:-n/a} uname=$_kern"
  ok "build: $ASSET"

  URL=$(release_url "$ASSET")
  ok "channel: $CHANNEL"

  # ------------------------------------------------------------------------
  # 5. download and check BEFORE touching anything installed
  # ------------------------------------------------------------------------
  case "$ASSET" in *.dylib) EXT=dylib ;; *) EXT=so ;; esac
  CORE_PATH="$CORE_DIR/$CORE_NAME.$EXT"

  step "Downloading"
  note "$URL"

  mkdir -p "$WORK/core"
  curl -fsSL --retry 3 -o "$WORK/core/core" "$URL" || die "download failed.

     That release may not ship a build named '$ASSET'.
     What it does ship: https://github.com/$REPO/releases"

  SIZE=$(wc -c < "$WORK/core/core" | tr -d ' ')
  [ "${SIZE:-0}" -gt 100000 ] || die "downloaded $SIZE bytes -- that is not a core."
  ok "$((SIZE / 1024)) KB"

  step "Checking it"
  _vout=$(verify_sum "$WORK/core/core" "$ASSET" 2>&1) && _vrc=0 || _vrc=$?
  case "$_vrc" in
    0) ok "SHA-256 matches SHA256SUMS.txt" ;;
    1) die "SHA-256 mismatch for $ASSET -- nothing was changed.
     $_vout
     Retry in a minute (a nightly may have been replaced mid-download); if it
     persists, report it at https://github.com/$REPO/issues" ;;
    *) warn "could not check SHA-256 (no SHA256SUMS.txt for this channel, or no hashing tool)." ;;
  esac

  DL=$(binary_arch "$WORK/core/core")
  [ -n "$DL" ] || die "the downloaded file is not a shared library this script can read."
  [ "$DL" = "$ARCHINFO" ] || die "architecture mismatch -- nothing was changed.
       frontend:   $ARCHINFO
       downloaded: $DL
     Please report at https://github.com/$REPO/issues with:
       asset=$ASSET model=${MODEL:-n/a} uname=$_kern"
  ok "matches your frontend ($ARCHINFO)"

  if [ "$DRY_RUN" -eq 1 ]; then
    step "--dry-run: nothing was changed (core)"
    say "  would install $ASSET"
    say "  into          $CORE_PATH"
    return 0
  fi

  # ------------------------------------------------------------------------
  # 6. install
  # ------------------------------------------------------------------------
  SUDO=""
  [ "$(id -u)" -eq 0 ] || { [ -w "$CORE_DIR" ] || SUDO="sudo"; }

  if [ -f "$CORE_PATH" ]; then
    confirm "  Replace the core already at $CORE_PATH?" || die "cancelled -- nothing changed."
  fi

  step "Installing the core"
  BACKUP=""
  if [ -f "$CORE_PATH" ]; then
    BACKUP="$CORE_PATH.bak-$(date +%Y%m%d-%H%M%S)"
    as_root cp -p "$CORE_PATH" "$BACKUP" || die "could not back up the existing core."
    ok "backed up -> $(basename "$BACKUP")"
  fi
  as_root mkdir -p "$CORE_DIR"
  as_root cp "$WORK/core/core" "$CORE_PATH" || die "could not write $CORE_PATH"
  as_root chmod 0644 "$CORE_PATH" 2>/dev/null || true
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

  step "Core installed"
  say "  frontend: $FE_LABEL"
  say "  core:     $CORE_PATH"
  say "  channel:  $CHANNEL"
  [ -n "$BACKUP" ] && say "  undo:     ${SUDO:+sudo }cp -p '$BACKUP' '$CORE_PATH'"
  say ""
  say "  Put Jaguar ROMs where your frontend scans for them, then restart it so"
  say "  the new core is picked up."
  return 0
}

# ==========================================================================
# THE APP
# ==========================================================================
INST_KIND=""; INST_PATH=""; INST_VER=""

if [ "$(uname -s)" = Darwin ]; then
  RCPT_DIR="$HOME/Library/Application Support/VirtualJaguar"
else
  RCPT_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/VirtualJaguar"
fi
RCPT="$RCPT_DIR/install-receipt"

# The receipt is a CACHE (channel, asset hash), never the source of truth for
# whether the app is installed: a drag-install or a hand-run .deb has none.
rcpt_get() { [ -r "$RCPT" ] && sed -n "s/^$1=//p" "$RCPT" 2>/dev/null | head -1; return 0; }

rcpt_write() {
  # rcpt_write <kind> <path> <version> <asset> <sha256>
  mkdir -p "$RCPT_DIR" 2>/dev/null || return 0
  {
    printf 'channel=%s\n' "$CHANNEL"
    printf 'kind=%s\n' "$1"
    printf 'path=%s\n' "$2"
    printf 'version=%s\n' "$3"
    printf 'asset=%s\n' "$4"
    printf 'sha256=%s\n' "$5"
    printf 'installed=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$RCPT" 2>/dev/null || true
}

plist_version() {
  # plist_version <Foo.app>
  _pl="$1/Contents/Info"
  _v=$(defaults read "$_pl" CFBundleShortVersionString 2>/dev/null || true)
  if [ -z "$_v" ]; then
    _v=$(sed -n '/CFBundleShortVersionString/{n;s/.*<string>\(.*\)<\/string>.*/\1/p;}' "$_pl.plist" 2>/dev/null | head -1)
  fi
  printf '%s' "$_v"
}

app_probe_mac() {
  [ -n "$INST_PATH" ] && return 0
  [ -d "$1/$MAC_APP_NAME" ] || return 0
  INST_KIND=app; INST_PATH="$1/$MAC_APP_NAME"; INST_VER=$(plist_version "$INST_PATH")
  return 0
}

app_probe_appimage() {
  _ai="${APP_DEST:-$HOME/.local/bin}/$APPIMAGE_NAME"
  [ -f "$_ai" ] || return 0
  INST_KIND=appimage; INST_PATH="$_ai"
  # An AppImage has no readable version without running it (and running it needs
  # FUSE), so the version comes from the receipt, and only if it describes THIS file.
  if [ "$(rcpt_get path)" = "$_ai" ]; then INST_VER=$(rcpt_get version); fi
  return 0
}

app_probe_deb() {
  command -v dpkg-query >/dev/null 2>&1 || return 0
  # shellcheck disable=SC2016  # ${Status} is dpkg-query's field syntax, not ours
  _st=$(dpkg-query -W -f='${Status}|${Version}' "$DEB_PKG" 2>/dev/null || true)
  case "$_st" in
    "install ok installed|"*) INST_KIND=deb; INST_PATH="dpkg:$DEB_PKG"; INST_VER=${_st#*|} ;;
  esac
  return 0
}

# Look for an installed app.  Sets INST_KIND / INST_PATH / INST_VER (empty = none).
app_detect() {
  INST_KIND=""; INST_PATH=""; INST_VER=""
  case "$(uname -s)" in
    Darwin)
      if [ -n "$APP_DEST" ]; then app_probe_mac "$APP_DEST"
      else app_probe_mac /Applications; app_probe_mac "$HOME/Applications"; fi
      ;;
    Linux)
      case "$APP_FORMAT" in
        deb)      app_probe_deb ;;
        appimage) app_probe_appimage ;;
        *)        app_probe_appimage; [ -n "$INST_KIND" ] || app_probe_deb ;;
      esac
      ;;
  esac
  # Nightlies and PR builds report the PROJECT version (4.0.0) in Info.plist / dpkg, the
  # same string a release uses, so that string cannot tell them apart.  If our receipt
  # says this install came from a nightly, it is "nightly" whatever the bundle claims.
  if [ -n "$INST_KIND" ] && [ "$(rcpt_get path)" = "$INST_PATH" ] && [ "$(rcpt_get version)" = nightly ]; then
    INST_VER=nightly
  fi
  return 0
}

# Is the app something THIS script installed (so unattended runs may update it)?
app_has_receipt() { [ -r "$RCPT" ] && [ -n "$INST_KIND" ] && [ "$(rcpt_get kind)" = "$INST_KIND" ]; }

# Strip a Debian revision / leading v so "4.0.0-1" and "v4.0.0" compare to "4.0.0".
norm_ver() { _x=${1#v}; printf '%s' "${_x%%-*}"; }

app_status() {
  step "Virtual Jaguar app"
  app_detect
  if [ -z "$INST_KIND" ]; then
    note "not installed"
  else
    ok "installed: $INST_PATH   version: ${INST_VER:-unknown}"
    [ -r "$RCPT" ] && note "channel last installed from: $(rcpt_get channel)"
  fi
}

# Maps the userland (not the kernel) to the app's Linux arch names.
linux_app_arch() {
  _ua=$(binary_arch /bin/sh)
  case "$_ua" in
    "64 x86_64")  printf 'x86_64' ;;
    "64 aarch64") printf 'aarch64' ;;
    "")           # unreadable: fall back to the kernel, which is right on every
                  # 64-bit userland and merely optimistic on a 32-bit one
                  case "$(uname -m)" in
                    x86_64|amd64)  printf 'x86_64' ;;
                    aarch64|arm64) printf 'aarch64' ;;
                  esac ;;
  esac
  return 0
}

app_no_build() {
  # app_no_build <why> -- the graceful "nothing to install" exit
  say ""
  say "  ${B}No app build is published for this channel yet.${R}"
  say "  $1"
  say "  The standalone app ships from v4.0.  To get the core now:  $SELF --core"
  [ "$CHANNEL" = nightly ] || say "  Try the nightly channel:  $SELF --app --channel nightly"
  return 3
}

app_install() {
  step "Virtual Jaguar app"
  OS=$(uname -s)
  case "$OS" in
    Darwin)
      FORMAT=${APP_FORMAT:-zip}
      case "$FORMAT" in zip) ;; *) die "--app-format $FORMAT does not apply on macOS (the app ships as a zip)." ;; esac
      PRE="VirtualJaguar-"; SUF="-macos-universal.$FORMAT"
      ;;
    Linux)
      app_detect
      FORMAT=$APP_FORMAT
      if [ -z "$FORMAT" ]; then
        if [ "$INST_KIND" = deb ]; then FORMAT=deb; else FORMAT=appimage; fi
      fi
      case "$FORMAT" in appimage|deb) ;; *) die "--app-format $FORMAT does not apply on Linux (use appimage or deb)." ;; esac
      LARCH=$(linux_app_arch)
      if [ -z "$LARCH" ]; then
        app_no_build "No app build for this machine: the app is x86_64 and aarch64 (64-bit) only, and this userland is $(binary_arch /bin/sh)."
        return 3
      fi
      if [ "$LARCH" != "$(uname -m | sed 's/amd64/x86_64/;s/arm64/aarch64/')" ]; then
        warn "kernel says '$(uname -m)' but the userland is $LARCH: matching the userland."
      fi
      if [ "$FORMAT" = deb ]; then
        case "$LARCH" in x86_64) DARCH=amd64 ;; *) DARCH=arm64 ;; esac
        if command -v dpkg >/dev/null 2>&1 && [ "$(dpkg --print-architecture 2>/dev/null || echo "$DARCH")" != "$DARCH" ]; then
          die "dpkg is configured for $(dpkg --print-architecture), not $DARCH; the .deb would not install."
        fi
        PRE="virtualjaguar_"; SUF="_$DARCH.deb"
      else
        PRE="VirtualJaguar-"; SUF="-linux-$LARCH.AppImage"
      fi
      ;;
    MINGW*|MSYS*|CYGWIN*)
      die "this is a Windows shell.  Use install.ps1 in PowerShell instead:
       irm https://raw.githubusercontent.com/$REPO/master/tools/install.ps1 | iex" ;;
    *) die "unsupported OS '$OS' for the app (macOS and Linux; Windows is install.ps1)." ;;
  esac

  app_detect
  if [ -n "$INST_KIND" ]; then
    ok "installed now: $INST_PATH   version: ${INST_VER:-unknown}"
  else
    note "no existing app install found"
  fi

  if ! ensure_sums; then
    app_no_build "Could not read $(release_url SHA256SUMS.txt) -- that release does not exist (yet), or you are offline."
    return 3
  fi
  AASSET=$(sums_find "$PRE" "$SUF") || {
    app_no_build "The '$CHANNEL' release has no file named ${PRE}*${SUF}."
    return 3
  }
  AHASH=$(sums_hash "$AASSET") || die "SHA256SUMS.txt is malformed (no hash for $AASSET)."
  case "$FORMAT" in
    deb) AVER=${AASSET#virtualjaguar_}; AVER=${AVER%_*} ;;
    *)   AVER=${AASSET#VirtualJaguar-}; AVER=${AVER%%-macos-*}; AVER=${AVER%%-linux-*} ;;
  esac
  ok "channel $CHANNEL ships $AASSET (version $AVER)"

  # --- update decision ------------------------------------------------------
  ACTION=install
  if [ -n "$INST_KIND" ]; then
    ACTION=update
    if [ "$(rcpt_get path)" = "$INST_PATH" ] && [ "$(rcpt_get sha256)" = "$AHASH" ]; then
      ACTION=current
    elif [ "$AVER" != nightly ] && [ -n "$INST_VER" ] && [ "$(norm_ver "$INST_VER")" = "$(norm_ver "$AVER")" ]; then
      ACTION=current
    fi
    [ "$FORCE" -eq 1 ] && [ "$ACTION" = current ] && ACTION=reinstall
  fi

  # --- where it goes --------------------------------------------------------
  case "$FORMAT" in
    zip)
      if [ -n "$APP_DEST" ]; then ADIR=$APP_DEST
      elif [ -n "$INST_PATH" ]; then ADIR=$(dirname "$INST_PATH")
      elif [ -w /Applications ]; then ADIR=/Applications
      else ADIR="$HOME/Applications"; fi
      ATARGET="$ADIR/$MAC_APP_NAME"
      ;;
    appimage)
      ADIR=${APP_DEST:-$HOME/.local/bin}
      ATARGET="$ADIR/$APPIMAGE_NAME"
      ;;
    deb) ATARGET="dpkg:$DEB_PKG" ;;
  esac

  case "$ACTION" in
    current) ok "already up to date ($AVER); nothing to do.  --force reinstalls." ;;
    update)  ok "update: ${INST_VER:-unknown} -> $AVER" ;;
    install) ok "fresh install" ;;
    reinstall) ok "reinstall of $AVER (--force)" ;;
  esac

  if [ "$DRY_RUN" -eq 1 ]; then
    step "--dry-run: nothing was changed (app)"
    if [ "$ACTION" = current ]; then
      say "  app is up to date: $AASSET"
    else
      say "  would $ACTION  $AASSET   (sha256 ${AHASH%"${AHASH#????????????}"}...)"
      say "  from          $(release_url "$AASSET")"
      say "  into          $ATARGET"
      if [ "$FORMAT" = deb ]; then say "  using         apt-get install / dpkg -i (root needed)"; fi
    fi
    return 0
  fi
  [ "$ACTION" = current ] && return 0

  # --- download and verify BEFORE touching anything -------------------------
  mkdir -p "$WORK/app"
  note "$(release_url "$AASSET")"
  curl -fsSL --retry 3 -o "$WORK/app/$AASSET" "$(release_url "$AASSET")" || die "download of $AASSET failed."
  _vout=$(verify_sum "$WORK/app/$AASSET" "$AASSET" 2>&1) && _vrc=0 || _vrc=$?
  case "$_vrc" in
    0) ok "SHA-256 matches SHA256SUMS.txt" ;;
    1) die "SHA-256 mismatch for $AASSET -- nothing was changed.
     $_vout" ;;
    *) die "could not verify $AASSET (no sha256sum / shasum / openssl on this machine).  Nothing was installed." ;;
  esac

  if [ "$ACTION" != install ]; then
    confirm "  Replace the app at $ATARGET?" || die "cancelled -- nothing changed."
  fi

  case "$FORMAT" in
    zip) app_install_mac ;;
    appimage) app_install_appimage ;;
    deb)     app_install_deb ;;
  esac
  rcpt_write "$INST_KIND_NEW" "$ATARGET" "$AVER" "$AASSET" "$AHASH"

  step "App installed"
  say "  app:      $ATARGET"
  say "  version:  $AVER   channel: $CHANNEL"
  [ -n "${APP_BACKUP:-}" ] && say "  undo:     replace it with $APP_BACKUP"
  return 0
}

# macOS: extract to scratch, move the old bundle aside, move the new one in.
# NEVER copy over an existing bundle: that merges, leaves stale files behind and
# breaks the code signature.
app_install_mac() {
  step "Installing the app"
  XD="$WORK/app/x"; mkdir -p "$XD"
  if command -v ditto >/dev/null 2>&1; then ditto -x -k "$WORK/app/$AASSET" "$XD" || die "could not unpack the zip."
  else unzip -q "$WORK/app/$AASSET" -d "$XD" || die "could not unpack the zip."; fi
  NEWAPP=""
  [ -d "$XD/$MAC_APP_NAME" ] && NEWAPP="$XD/$MAC_APP_NAME"
  if [ -z "$NEWAPP" ]; then
    for _a in "$XD"/*.app "$XD"/*/*.app; do [ -d "$_a" ] && { NEWAPP=$_a; break; }; done
  fi
  [ -n "$NEWAPP" ] || die "no .app inside $AASSET."

  if [ ! -d "$ADIR" ]; then mkdir -p "$ADIR" || die "could not create $ADIR"; fi
  [ -w "$ADIR" ] || die "$ADIR is not writable.  Re-run with --app-dest \"\$HOME/Applications\"."
  if pgrep -xq virtualjaguar 2>/dev/null; then warn "Virtual Jaguar looks like it is running; quit it to finish the update cleanly."; fi

  APP_BACKUP=""
  if [ -d "$ATARGET" ]; then
    APP_BACKUP="$ATARGET.bak"
    command rm -rf "$APP_BACKUP"
    command mv "$ATARGET" "$APP_BACKUP" || die "could not move the old app aside."
    ok "old app kept as $(basename "$APP_BACKUP")"
  fi
  if ! ditto "$NEWAPP" "$ATARGET"; then
    command rm -rf "$ATARGET"
    [ -n "$APP_BACKUP" ] && command mv "$APP_BACKUP" "$ATARGET"
    die "could not copy the app into $ADIR; the previous one was restored."
  fi
  INST_KIND_NEW=app
  ok "installed -> $ATARGET"

  # Gatekeeper.  curl does not set com.apple.quarantine, so a notarized build
  # opens cleanly and there is nothing to strip.  We only ever act when macOS
  # rejects the app AND it is quarantined, and then only with consent.
  if command -v spctl >/dev/null 2>&1; then
    if spctl --assess --type execute "$ATARGET" >/dev/null 2>&1; then
      ok "Gatekeeper accepts it (signed and notarized)"
    else
      warn "Gatekeeper does not accept this build (not notarized)."
      if xattr -p com.apple.quarantine "$ATARGET" >/dev/null 2>&1; then
        if [ "$ALLOW_UNSIGNED" -eq 1 ] || confirm_no "  Clear the quarantine flag so it can open?"; then
          xattr -dr com.apple.quarantine "$ATARGET" && ok "quarantine cleared"
        else
          note "left quarantined.  To open it: right-click the app, Open -- or"
          note "  xattr -dr com.apple.quarantine '$ATARGET'"
        fi
      else
        note "it carries no quarantine flag, so it should still open.  If macOS blocks it:"
        note "  right-click the app, Open -- or: xattr -dr com.apple.quarantine '$ATARGET'"
      fi
    fi
  fi
  return 0
}

app_install_appimage() {
  step "Installing the app"
  mkdir -p "$ADIR" || die "could not create $ADIR"
  APP_BACKUP=""
  if [ -f "$ATARGET" ]; then
    APP_BACKUP="$ATARGET.bak"
    command cp -pf "$ATARGET" "$APP_BACKUP" || die "could not back up the existing AppImage."
    ok "old AppImage kept as $(basename "$APP_BACKUP")"
  fi
  # copy next to the target, then rename over it: replacing a RUNNING AppImage in
  # place fails with "text file busy", a rename does not.
  command cp -f "$WORK/app/$AASSET" "$ATARGET.new" || die "could not write into $ADIR"
  chmod 0755 "$ATARGET.new"
  command mv -f "$ATARGET.new" "$ATARGET" || die "could not move the AppImage into place."
  INST_KIND_NEW=appimage
  ok "installed -> $ATARGET"

  _data="${XDG_DATA_HOME:-$HOME/.local/share}"
  _apps="$_data/applications"
  _did=io.github.libretro.VirtualJaguar    # desktop id and icon name, as the .deb uses
  mkdir -p "$_apps" 2>/dev/null || true
  cat > "$_apps/$_did.desktop" 2>/dev/null <<DESK || warn "could not write the .desktop entry"
[Desktop Entry]
Type=Application
Name=Virtual Jaguar
Comment=Atari Jaguar emulator
Exec="$ATARGET" %f
Icon=$_did
Terminal=false
Categories=Game;Emulator;
DESK
  [ -f "$_apps/$_did.desktop" ] && ok "menu entry -> $_apps/$_did.desktop"
  # Icons: the AppImage carries its hicolor tree.  --appimage-extract needs no FUSE; the
  # file has already been verified against SHA256SUMS.txt.  Best effort: a menu entry
  # without an icon is still a working entry.
  _ex="$WORK/app/ai"; mkdir -p "$_ex"
  ( cd "$_ex" && "$ATARGET" --appimage-extract 'usr/share/icons' >/dev/null 2>&1 ) || true
  if [ -d "$_ex/squashfs-root/usr/share/icons/hicolor" ]; then
    mkdir -p "$_data/icons/hicolor" 2>/dev/null || true
    if command cp -R "$_ex/squashfs-root/usr/share/icons/hicolor/." "$_data/icons/hicolor/" 2>/dev/null; then
      ok "icons -> $_data/icons/hicolor"
      command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$_data/icons/hicolor" >/dev/null 2>&1 || true
    fi
  else
    note "no icon found inside the AppImage; the menu entry will use a generic one"
  fi
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$_apps" >/dev/null 2>&1 || true

  case ":$PATH:" in *":$ADIR:"*) ;; *) note "$ADIR is not on your PATH; start it from the menu or run $ATARGET" ;; esac
  # ldconfig is in /sbin, which a non-root PATH on Debian lacks; the library sits in a
  # multiarch directory, so also look there.
  _fuse=0
  { /sbin/ldconfig -p 2>/dev/null || ldconfig -p 2>/dev/null; } | grep -q 'libfuse\.so\.2' && _fuse=1
  # (listed, not globbed: an unmatched glob is a hard error in zsh)
  for _l in /usr/lib /usr/lib64 /lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu \
            /lib/x86_64-linux-gnu /lib/aarch64-linux-gnu; do
    [ -e "$_l/libfuse.so.2" ] && _fuse=1
  done
  if [ "$_fuse" -eq 0 ]; then
    warn "AppImages need FUSE 2 (libfuse.so.2).  Debian/Ubuntu: sudo apt install libfuse2"
    warn "  (Ubuntu 24.04: libfuse2t64).  Without it:  $ATARGET --appimage-extract-and-run"
  fi
  return 0
}

app_install_deb() {
  step "Installing the app (.deb, needs root)"
  SUDO=""
  if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || die "installing a .deb needs root and sudo is not available."
    SUDO="sudo"
  fi
  chmod 0755 "$WORK" "$WORK/app" 2>/dev/null || true   # apt's sandbox user must be able to read it
  chmod 0644 "$WORK/app/$AASSET"
  if command -v apt-get >/dev/null 2>&1; then
    as_root apt-get install -y "$WORK/app/$AASSET" || die "apt-get could not install $AASSET."
  elif command -v dpkg >/dev/null 2>&1; then
    as_root dpkg -i "$WORK/app/$AASSET" || die "dpkg could not install $AASSET."
  else
    die "neither apt-get nor dpkg is available; use --app-format appimage."
  fi
  INST_KIND_NEW=deb
  APP_BACKUP=""
  ok "installed package $DEB_PKG"
  return 0
}

# ==========================================================================
# auto-update
#
# systemd user timer where available, else cron.  Both are removable with
# `--auto-update off`, and both just re-run this script with --yes.
# ==========================================================================
UPDATER="$HOME/.local/bin/vj-core-update"
SYSTEMD_DIR="$HOME/.config/systemd/user"

remove_auto_update() {
  _did=0
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now vj-core-update.timer >/dev/null 2>&1 && _did=1 || true
    command rm -f "$SYSTEMD_DIR/vj-core-update.timer" "$SYSTEMD_DIR/vj-core-update.service" 2>/dev/null || true
    systemctl --user daemon-reload >/dev/null 2>&1 || true
  fi
  if command -v crontab >/dev/null 2>&1 && crontab -l 2>/dev/null | grep -q vj-core-update; then
    crontab -l 2>/dev/null | grep -v vj-core-update | crontab - && _did=1
  fi
  command rm -f "$UPDATER" 2>/dev/null || true
  if [ "$_did" -eq 1 ]; then ok "auto-update removed"; else note "no auto-update was installed"; fi
}

setup_auto_update() {
  # setup_auto_update <what-done> <core-dir or empty>
  step "Setting up $AUTO_UPDATE auto-update"
  mkdir -p "$(dirname "$UPDATER")"
  # Every flag is passed explicitly, including --what: the updater must never
  # depend on what a future default decides to install.
  _args="--yes --channel \"$CHANNEL\" --what $1"
  [ -n "$2" ] && _args="$_args --dest \"$2\""
  [ -n "$APP_DEST" ] && _args="$_args --app-dest \"$APP_DEST\""
  [ -n "$APP_FORMAT" ] && _args="$_args --app-format $APP_FORMAT"
  cat > "$UPDATER" <<UPD
#!/bin/sh
# Auto-generated by the Virtual Jaguar installer.  Remove with:
#   sh <(curl -fsSL $RAW) --auto-update off
set -eu
curl -fsSL "$RAW" -o "\$0.tmp.\$\$"
sh "\$0.tmp.\$\$" $_args
command rm -f "\$0.tmp.\$\$"
UPD
  chmod +x "$UPDATER"
  ok "updater -> $UPDATER"

  if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
    mkdir -p "$SYSTEMD_DIR"
    cat > "$SYSTEMD_DIR/vj-core-update.service" <<SVC
[Unit]
Description=Update Virtual Jaguar (core and/or app)

[Service]
Type=oneshot
ExecStart=$UPDATER
SVC
    cat > "$SYSTEMD_DIR/vj-core-update.timer" <<TMR
[Unit]
Description=Update Virtual Jaguar ($AUTO_UPDATE)

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
}

# ==========================================================================
# main
# ==========================================================================
if [ "$AUTO_UPDATE" = "off" ]; then
  step "Removing auto-update"; remove_auto_update; exit 0
fi

# --list: report, install nothing.  The core half is allowed to find nothing.
if [ "$LIST_ONLY" -eq 1 ]; then
  set +e; ( set -e; core_install ); set -e
  app_status
  exit 0
fi

# What to install.  Unattended runs must keep doing what they always did -- the
# core -- so the app is added only when THIS script installed it before.
app_detect
if [ -z "$WHAT" ]; then
  if [ "$INTERACTIVE" -eq 1 ] && [ "$ASSUME_YES" -eq 0 ]; then
    _def=1
    [ -n "$INST_KIND" ] && _def=3
    step "What do you want to install?"
    say "  1) the RetroArch core      ${D}for RetroArch, RetroPie, Lakka ...${R}"
    say "  2) the Virtual Jaguar app  ${D}standalone, no RetroArch needed (from v4.0)${R}"
    say "  3) both"
    [ -n "$INST_KIND" ] && note "app found: $INST_PATH ${INST_VER:+(version $INST_VER)}"
    case "$(ask '  Number' "$_def")" in
      2) WHAT=app ;;
      3) WHAT=both ;;
      *) WHAT=core ;;
    esac
  else
    WHAT=core
    if app_has_receipt; then WHAT=both; CORE_SOFT=1; fi
  fi
fi

# Channel
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

CORE_RC=0; APP_RC=0
if [ "$WHAT" = core ] || [ "$WHAT" = both ]; then
  # `set +e` around the subshell: inside `cmd || ...` errexit is ignored, which
  # would let a failed step carry on silently.  The subshell turns -e back on.
  set +e; ( set -e; core_install ); CORE_RC=$?; set -e
  if [ "$CORE_RC" -ne 0 ]; then
    if [ "$WHAT" = both ]; then warn "the core step failed (see above); carrying on with the app."
    else exit "$CORE_RC"; fi
  fi
fi
if [ "$WHAT" = app ] || [ "$WHAT" = both ]; then
  set +e; ( set -e; app_install ); APP_RC=$?; set -e
  if [ "$APP_RC" -ne 0 ]; then
    # 3 = "no app build published for this channel": graceful when the core ran
    if [ "$APP_RC" -eq 3 ] && [ "$WHAT" = both ]; then APP_RC=0; fi
  fi
fi

# optional auto-update (never on a dry run)
if [ "$DRY_RUN" -eq 0 ] && [ "$CORE_RC" -eq 0 ] && [ "$APP_RC" -eq 0 ]; then
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
  if [ -n "$AUTO_UPDATE" ]; then
    _cd=""
    [ -s "$WORK/core_dir" ] && _cd=$(cat "$WORK/core_dir")
    setup_auto_update "$WHAT" "$_cd"
  fi
fi

[ "$CORE_RC" -ne 0 ] && exit "$CORE_RC"
exit "$APP_RC"

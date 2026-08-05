#!/usr/bin/env bash
# find-rom.sh -- locate a private test ROM / disc image by name pattern.
#
# The private corpus (test/roms/private, a symlink to a tree outside every
# git checkout) uses inconsistent naming: "Iron Soldier 2 (World).j64",
# "Iron Soldier (World) (v1.04).j64", "Atari Karts (1995).jag", and disc
# images nest as "<Title>/<Title>/*.cue".  Hardcoding one exact spelling in
# the Makefile turns a check into a silent SKIP the moment the corpus is
# laid out differently -- which is exactly how the Skyhammer clipping
# sentinel went inert (it looked for "Skyhammer_(1999).jag").
#
# Usage:  find-rom.sh PATTERN [PATTERN...]
#
# Patterns are shell globs matched case-insensitively against the FILE NAME
# only (not the path).  Patterns are tried in order and the first one that
# matches anything wins, so callers pass the exact expected spelling first
# and looser fallbacks after.
#
# Two filters keep the result honest:
#   * Only real loadable media are considered.  Without this, a bare
#     "Atari Karts*" matches "7zips/Atari Karts (1995).7z" and the harness is
#     handed an archive it cannot load -- a check that appears to run but
#     tests nothing.
#   * Matches are ranked by path DEPTH first, so the canonical top-level copy
#     beats a duplicate buried in "ROMS/Atari Jaguar Rom Collection/ROMS/".
#     Depth ties break by name, so the answer is deterministic across runs.
#
# Prints the path of the single best match on stdout and exits 0.
# Prints nothing and exits 1 when no pattern matches -- callers test for an
# empty result and record a skip.
#
# Honours ROMS_PRIVATE_ROOT for callers that keep the corpus elsewhere.

set -u

root="${ROMS_PRIVATE_ROOT:-test/roms/private}"

if [ "$#" -eq 0 ]; then
    echo "usage: find-rom.sh PATTERN [PATTERN...]" >&2
    exit 2
fi

# -L: test/roms/private is a SYMLINK; find does not follow it otherwise and
# would report the corpus as empty.  A missing corpus is not an error here --
# it is the normal case in CI, which has none of the private ROMs.
[ -e "$root" ] || exit 1

# Loadable media only -- never archives (.zip/.7z) or stray sidecar files.
is_loadable() {
    case "$(printf '%s' "$1" | tr 'A-Z' 'a-z')" in
    *.jag|*.j64|*.rom|*.bin|*.abs|*.cof|*.cue|*.cdi|*.chd) return 0 ;;
    *) return 1 ;;
    esac
}

for pat in "$@"; do
    match=$(
        find -L "$root" -type f -iname "$pat" 2>/dev/null |
        while IFS= read -r f; do
            is_loadable "$f" || continue
            # depth = number of path separators; shallower sorts first.
            depth=$(printf '%s' "$f" | tr -cd '/' | wc -c | tr -d ' ')
            printf '%s\t%s\n' "$depth" "$f"
        done |
        sort -t"$(printf '\t')" -k1,1n -k2,2 |
        head -1 |
        cut -f2-
    )
    if [ -n "$match" ]; then
        printf '%s\n' "$match"
        exit 0
    fi
done

exit 1

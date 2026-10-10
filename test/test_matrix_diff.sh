#!/usr/bin/env bash
# test/test_matrix_diff.sh -- regression test for test/tools/matrix_diff.py
# (issue #749, the never-backward gate for the boot matrices).
#
# Pure synthetic fixtures: no ROMs, no core, no corpus, runs in well under a
# second, so it runs everywhere `make test` does.  Each backward case gets its
# own OLD/NEW pair so one exit 1 cannot mask another.
#
# Usage: bash test/test_matrix_diff.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL="$SCRIPT_DIR/tools/matrix_diff.py"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/matrix_diff_test.XXXXXX")"
trap 'command rm -rf "$TMP"' EXIT

PASS=0
FAIL=0
ok()  { PASS=$((PASS+1)); printf '  ok   %s\n' "$1"; }
bad() { FAIL=$((FAIL+1)); printf '  FAIL %s\n     %s\n' "$1" "${2:-}"; }

STAMP='<!-- build:aaaaaaaaaaaa -->'
STAMP2='<!-- build:bbbbbbbbbbbb -->'

# cart <file> <stamp> <row>...   (row = "Title|HLE|HLE notes|BIOS|BIOS notes")
cart() {
    f="$1"; st="$2"; shift 2
    {
        printf '# Cartridge boot matrix\n\nprose line\n\n'
        printf '| Title | HLE | HLE notes | Real BIOS | BIOS notes |\n|---|---|---|---|---|\n'
        for r in "$@"; do
            IFS='|' read -r t h hn b bn <<EOR
$r
EOR
            printf '| %s | %s | %s | %s | %s |%s\n' "$t" "$h" "$hn" "$b" "$bn" "$st"
        done
    } > "$f"
}

# cd <file> <stamp> <row>...   (row = "Title|mode|stage|watchdog")
# The stamp sits INSIDE the last cell, as cd_boot_matrix.sh writes it, and the
# evidence cell carries an escaped pipe.
cd_matrix() {
    f="$1"; st="$2"; shift 2
    {
        printf '# CD boot matrix\n\n## Results\n\n'
        printf '| Title | Mode | Score | Stage | Watchdog | PC evidence |\n|---|---|---|---|---|---|\n'
        for r in "$@"; do
            IFS='|' read -r t m s w <<EOR
$r
EOR
            printf '| %s | %s | 1/1 | %s | %s | [PASS] %s : a \\| b final_pc=$0%s %s |\n' \
                "$t" "$m" "$s" "$w" "$t" "$RANDOM" "$st"
        done
        printf '\n### historical snapshot\n\n'
        printf '| Title | Mode | Score | Stage | Watchdog | PC evidence |\n|---|---|---|---|---|---|\n'
        printf '| Disc A | hle | 0/1 | LOAD_FAIL | (none) | stale |\n'
    } > "$f"
}

# run <name> <old> <new> [--json]  -> sets RC and OUT
run() {
    OUT="$(python3 -I "$TOOL" "${@:2}" 2>&1)"; RC=$?
}

expect() {
    # expect <name> <exit> [grep -F needle in OUT]...
    name="$1"; want="$2"; shift 2
    if [ "$RC" -ne "$want" ]; then bad "$name" "exit $RC, wanted $want:
$OUT"; return; fi
    for needle in "$@"; do
        if ! printf '%s\n' "$OUT" | /usr/bin/grep -qF -- "$needle"; then
            bad "$name" "output lacks: $needle
$OUT"; return
        fi
    done
    ok "$name"
}

# section_has <section> <needle>: needle appears between "<section> (" and the next blank line
section_has() {
    printf '%s\n' "$OUT" | awk -v s="$1 (" 'index($0,s)==1{on=1;next} on&&/^$/{exit} on' \
        | /usr/bin/grep -qF -- "$2"
}

echo "cart matrix"
cart "$TMP/o1" "$STAMP"  "Alpha|LOAD_FAIL|probe could not load the ROM|GAME_CODE|video, audio"
cart "$TMP/n1" "$STAMP2" "Alpha|GAME_CODE|video, audio|GAME_CODE|video, audio"
run fwd "$TMP/o1" "$TMP/n1"
expect "forward move exits 0 and is improved" 0 "RESULT: OK"
section_has improved "Alpha [HLE]" && ok "forward row listed under improved" \
    || bad "forward row listed under improved" "$OUT"

cart "$TMP/o2" "$STAMP"  "Beta|GAME_CODE|video, audio|GAME_CODE|video, audio"
cart "$TMP/n2" "$STAMP2" "Beta|GAME_CODE|video, audio|LOAD_FAIL|probe could not load the ROM"
run back "$TMP/o2" "$TMP/n2"
expect "backward stage move exits 1" 1 "RESULT: BACKWARD"
section_has regressed "Beta [BIOS]" && ok "backward row listed under regressed" \
    || bad "backward row listed under regressed" "$OUT"

cart "$TMP/o3" "$STAMP"  "Gamma|GAME_CODE|video, audio|GAME_CODE|video, audio"
cart "$TMP/n3" "$STAMP2" "Gamma|GAME_CODE|video, audio|GAME_CODE|video, audio; inframe_hang"
run sig "$TMP/o3" "$TMP/n3"
expect "notes-only crash signature (same stage) exits 1" 1 "RESULT: BACKWARD"
section_has regressed "Gamma [BIOS]" && ok "signature row listed under regressed" \
    || bad "signature row listed under regressed" "$OUT"

for s in gpu_wedge dsp_wedge video_stall gpu_pc_escape dsp_pc_escape; do
    cart "$TMP/n3$s" "$STAMP2" "Gamma|GAME_CODE|video, audio; $s|GAME_CODE|video, audio"
    run sigs "$TMP/o3" "$TMP/n3$s"
    expect "new signature $s is backward" 1 "Gamma [HLE]"
done

cart "$TMP/n3b" "$STAMP2" "Gamma|GAME_CODE|video, audio|? (pc_escape)|final_pc=\$6D2710"
run pce "$TMP/o3" "$TMP/n3b"
expect "GAME_CODE -> ? (pc_escape) exits 1" 1 "Gamma [BIOS]"

run sigfix "$TMP/n3" "$TMP/o3"
expect "signature removal at equal stage is improved, exit 0" 0 "RESULT: OK"
section_has improved "Gamma [BIOS]" && ok "removed signature listed under improved" \
    || bad "removed signature listed under improved" "$OUT"

cart "$TMP/o4" "$STAMP"  "Delta|GAME_CODE|video, audio|GAME_CODE|video, audio"
cart "$TMP/n4" "$STAMP2" "Delta|GAME_CODE|black video (headless — undetermined), silent|GAME_CODE|static video, silent"
run bv "$TMP/o4" "$TMP/n4"
expect "black-video / audio notes change is never backward" 0 "RESULT: OK" "regressed (0)"

run same "$TMP/o4" "$TMP/o4"
expect "identical file exits 0 with nothing changed" 0 "regressed (0)" "improved (0)"

cart "$TMP/n5" "$STAMP2" "Delta|GAME_CODE|video, audio|GAME_CODE|video, audio" \
                         "Epsilon|GAME_CODE|video, audio|GAME_CODE|video, audio"
run add "$TMP/o4" "$TMP/n5"
expect "added row exits 0" 0 "RESULT: OK"
section_has "added rows" "Epsilon" && ok "added row listed" || bad "added row listed" "$OUT"
run rem "$TMP/n5" "$TMP/o4"
expect "removed row exits 0" 0 "RESULT: OK"
section_has "removed rows" "Epsilon" && ok "removed row listed" || bad "removed row listed" "$OUT"

cart "$TMP/n6" "$STAMP2" "Zeta|GAME_CODE|video, audio|? (pc_escape)|final_pc=\$6D2710" \
                         "Eta|GAME_CODE|video, audio|GAME_CODE|video, audio"
cart "$TMP/o6" "$STAMP"  "Zeta|GAME_CODE|video, audio|? (pc_escape)|final_pc=\$6D2710" \
                         "Eta|GAME_CODE|video, audio|GAME_CODE|video, audio"
run asym "$TMP/o6" "$TMP/n6"
expect "asymmetric cart row reported, exit 0" 0 "RESULT: OK"
section_has still-asymmetric "Zeta" && ok "asymmetric row listed" || bad "asymmetric row listed" "$OUT"
section_has still-asymmetric "Eta" && bad "symmetric row not listed as asymmetric" "$OUT" \
    || ok "symmetric row not listed as asymmetric"

# Duplicate titles (the extension is dropped, so two dumps of a game collide):
# the regression in one copy must not be swallowed by its twin.
cart "$TMP/o7" "$STAMP"  "Twin|GAME_CODE|video, audio|GAME_CODE|video, audio" \
                         "Twin|GAME_CODE|video, audio|GAME_CODE|video, audio; inframe_hang"
cart "$TMP/n7" "$STAMP2" "Twin|GAME_CODE|video, audio|GAME_CODE|video, audio; inframe_hang" \
                         "Twin|GAME_CODE|video, audio|LOAD_FAIL|x"
run dup "$TMP/o7" "$TMP/n7"
expect "duplicate titles: a regression in one copy exits 1" 1 "RESULT: BACKWARD"
section_has regressed "Twin" && ok "duplicate-title regression listed" || bad "duplicate-title regression listed" "$OUT"
run dup0 "$TMP/o7" "$TMP/o7"
expect "duplicate titles compare equal to themselves" 0 "regressed (0)"

echo "CD matrix"
cd_matrix "$TMP/co1" "$STAMP"  "Disc A.cue|hle|GAME_CODE|(none)" "Disc A.cue|bios|BOOT_STUB|(none)" "Disc B.cue|hle|GAME_CODE|(none)" "Disc B.cue|bios|GAME_CODE|(none)"
cd_matrix "$TMP/cn1" "$STAMP2" "Disc A.cue|hle|GAME_CODE|(none)" "Disc A.cue|bios|BIOS_INTRO|(none)" "Disc B.cue|hle|GAME_CODE|(none)" "Disc B.cue|bios|GAME_CODE|(none)"
run cdback "$TMP/co1" "$TMP/cn1"
expect "CD BOOT_STUB -> BIOS_INTRO exits 1" 1 "RESULT: BACKWARD"
section_has regressed "Disc A.cue [bios]" && ok "CD backward row listed" || bad "CD backward row listed" "$OUT"
run cdfwd "$TMP/cn1" "$TMP/co1"
expect "CD BIOS_INTRO -> BOOT_STUB exits 0" 0 "RESULT: OK"
section_has improved "Disc A.cue [bios]" && ok "CD forward row listed" || bad "CD forward row listed" "$OUT"
run cdsame "$TMP/co1" "$TMP/co1"
expect "CD file vs itself exits 0 (historical tables ignored)" 0 "regressed (0)"

cd_matrix "$TMP/cn2" "$STAMP2" "Disc A.cue|hle|GAME_CODE|(none)" "Disc A.cue|bios|BOOT_STUB|[CRASH-DETECT] cd_seek_wedge frame 88" "Disc B.cue|hle|GAME_CODE|(none)" "Disc B.cue|bios|GAME_CODE|(none)"
run cdsig "$TMP/co1" "$TMP/cn2"
expect "CD new watchdog signature at equal stage exits 1" 1 "Disc A.cue [bios]"

run cdasym "$TMP/co1" "$TMP/co1"
section_has still-asymmetric "Disc A.cue" && ok "CD hle/bios stage difference listed as asymmetric" \
    || bad "CD hle/bios stage difference listed as asymmetric" "$OUT"
section_has still-asymmetric "Disc B.cue" && bad "CD symmetric title not listed" "$OUT" \
    || ok "CD symmetric title not listed"

cd_matrix "$TMP/cn3" "$STAMP2" "Disc A.cue|hle|GAME_CODE|(none)" "Disc A.cue|bios|BOOT_STUB|(none)" "Disc B.cue|hle|GAME_CODE|(none)" "Disc B.cue|bios|GAME_CODE|(none)" "Disc C.cue|hle|GAME_CODE|(none)"
run cdadd "$TMP/co1" "$TMP/cn3"
expect "CD added row exits 0" 0 "RESULT: OK"
section_has "added rows" "Disc C.cue [hle]" && ok "CD added row listed" || bad "CD added row listed" "$OUT"

echo "exit 2 and --json"
run noargs
expect "no arguments exits 2" 2 "usage"
run missing "$TMP/o1" "$TMP/does-not-exist"
expect "missing file exits 2" 2 "cannot read"
run mixed "$TMP/o1" "$TMP/co1"
expect "cart vs CD exits 2" 2 "cart matrix but NEW is a cd"
printf 'not a matrix\n' > "$TMP/junk"
run junk "$TMP/junk" "$TMP/junk"
expect "file with no table exits 2" 2 "no cart or CD matrix table"
cart "$TMP/ub" "$STAMP" "Mystery|WARP_SPEED|x|GAME_CODE|y"
run unk "$TMP/ub" "$TMP/ub"
expect "unknown stage label exits 2" 2 "unknown stage label"
run flag --bogus "$TMP/o1" "$TMP/n1"
expect "unknown option exits 2" 2 "usage"

run json --json "$TMP/o2" "$TMP/n2"
if [ "$RC" -eq 1 ] && printf '%s' "$OUT" | python3 -I -c '
import json, sys
d = json.load(sys.stdin)
assert d["kind"] == "cart" and d["exit"] == 1
assert d["regressed"][0]["row"] == "Beta [BIOS]"
'; then ok "--json emits parseable JSON and keeps the exit status"
else bad "--json emits parseable JSON and keeps the exit status" "rc=$RC $OUT"; fi

echo
echo "matrix_diff tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

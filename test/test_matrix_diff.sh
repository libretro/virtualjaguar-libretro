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

# BIOS_REJECT (#852): same rank as GAME_CODE.  An old false "BIOS works" row
# turning into a reject is a scoring correction (lateral), not a regression.
cart "$TMP/o8" "$STAMP"  "Reject|GAME_CODE|black video (headless — undetermined), silent|GAME_CODE|video, audio"
cart "$TMP/n8" "$STAMP2" "Reject|GAME_CODE|black video (headless — undetermined), silent|BIOS_REJECT|boot ROM halted at \$0050B6"
run rej "$TMP/o8" "$TMP/n8"
expect "GAME_CODE -> BIOS_REJECT is lateral, exit 0" 0 "RESULT: OK" "regressed (0)"
section_has "lateral (informational)" "Reject [BIOS]" && ok "false-pass correction listed as lateral" \
    || bad "false-pass correction listed as lateral" "$OUT"
section_has still-asymmetric "Reject" && bad "HLE GAME_CODE / BIOS_REJECT not asymmetric" "$OUT" \
    || ok "HLE GAME_CODE / BIOS_REJECT is not listed as an HLE gap"
run rejsame "$TMP/n8" "$TMP/n8"
expect "BIOS_REJECT vs itself is clean" 0 "regressed (0)" "improved (0)"
cart "$TMP/n8b" "$STAMP2" "Reject|GAME_CODE|black video (headless — undetermined), silent|? (pc_escape)|final_pc=\$6D2710"
run rejback "$TMP/n8" "$TMP/n8b"
expect "BIOS_REJECT -> ? (pc_escape) is backward" 1 "Reject [BIOS]"
cart "$TMP/n8c" "$STAMP2" "Reject|GAME_CODE|black video (headless — undetermined), silent|LOAD_FAIL|x"
run rejlf "$TMP/n8" "$TMP/n8c"
expect "BIOS_REJECT -> LOAD_FAIL is backward" 1 "Reject [BIOS]"
run rejfwd "$TMP/n8b" "$TMP/n8"
expect "? -> BIOS_REJECT is improved" 0 "RESULT: OK"
section_has improved "Reject [BIOS]" && ok "? -> BIOS_REJECT listed as improved" \
    || bad "? -> BIOS_REJECT listed as improved" "$OUT"
cart "$TMP/n8d" "$STAMP2" "Reject|GAME_CODE|black video (headless — undetermined), silent|BIOS_REJECT|boot ROM halted; gpu_wedge"
run rejsig "$TMP/n8" "$TMP/n8d"
expect "new watchdog signature on a BIOS_REJECT row is still backward" 1 "Reject [BIOS]"
# A bios_trap (cart got control, fell back into the boot ROM) is a '?' row.
cart "$TMP/n8e" "$STAMP2" "Trap|GAME_CODE|video, silent|? (bios_trap)|final_pc=\$E005DC after cart handoff at frame 492"
cart "$TMP/o8e" "$STAMP"  "Trap|GAME_CODE|video, silent|GAME_CODE|video, audio"
run trap "$TMP/o8e" "$TMP/n8e"
expect "GAME_CODE -> ? (bios_trap) is backward" 1 "Trap [BIOS]"

echo "cart classifier (cart_classify.sh)"
. "$SCRIPT_DIR/tools/matrix_common.sh"
. "$SCRIPT_DIR/tools/cart_classify.sh"
TIMEOUT_SECS=90
FRAMES=600
# probe <log> <extra fields...>: write a CARTPROBE line (plus optional core log lines on stdin)
probe_log() {
    f="$1"; shift
    { cat; printf 'CARTPROBE rom="x.jag" frames=%s w=326 h=240 pc_valid=1 %s\n' "${FRAMES_RUN:-1092}" "$*"; } > "$f"
}
cls() { # cls <log> <mode> -> CLS
    CLS="$(cart_classify_mode 0 "$1" "$2")"
}
want() { # want <name> <prefix>
    case "$CLS" in "$2"*) ok "$1";; *) bad "$1" "got: $CLS";; esac
}
# Rejected cart: boot ROM ran, no handoff, halted on $60FE for 30 frames.
FRAMES_RUN=278 probe_log "$TMP/c1" 'pc=$0050B6 nonblack_max_pct=100.0 lit_frames=230 motion=34 audio_nonsilent=105158 audio_onset=35 bios_ran=1 handoff=-1 final_op=$60FE halt_frames=30 scored_from=0 scored_frames=278 scored_lit_frames=230 scored_motion=34 scored_audio_nonsilent=105158' </dev/null
cls "$TMP/c1" bios; want "boot ROM ran + no handoff + halt loop -> BIOS_REJECT" "BIOS_REJECT|"
cls "$TMP/c1" hle;  want "same log in HLE mode is never BIOS_REJECT" "GAME_CODE|"
# Boot ROM never ran (headerless RAM-loaded .jag): not a reject, whole run scored.
probe_log "$TMP/c2" 'pc=$0086CE nonblack_max_pct=90.0 lit_frames=500 motion=100 audio_nonsilent=90000 audio_onset=35 bios_ran=0 handoff=0 final_op=$66E6 halt_frames=0 scored_from=0 scored_frames=600 scored_lit_frames=500 scored_motion=100 scored_audio_nonsilent=90000' </dev/null
cls "$TMP/c2" bios; want "bios_ran=0 never BIOS_REJECT (RAM-loaded executable)" "GAME_CODE|video, audio"
# Parked in a halt loop but fewer than 30 frames: not (yet) a reject.
probe_log "$TMP/c3" 'pc=$0050B6 lit_frames=10 motion=1 audio_nonsilent=0 bios_ran=1 handoff=-1 final_op=$60FE halt_frames=12 scored_from=0 scored_frames=600 scored_lit_frames=10 scored_motion=1 scored_audio_nonsilent=0' </dev/null
cls "$TMP/c3" bios; want "short halt (<30 frames) is not BIOS_REJECT" "GAME_CODE|"
/usr/bin/grep -q "handoff not observed" <<<"$CLS" && ok "no-handoff run says so and scores the whole run" \
    || bad "no-handoff run says so" "$CLS"
# Handed off, then moving video + audio AFTER the handoff: scored from there.
probe_log "$TMP/c4" 'pc=$00D4A4 lit_frames=950 motion=487 audio_nonsilent=551152 audio_onset=35 bios_ran=1 handoff=492 final_op=$66E6 halt_frames=0 scored_from=492 scored_frames=600 scored_lit_frames=514 scored_motion=121 scored_audio_nonsilent=262586' </dev/null
cls "$TMP/c4" bios; want "after-handoff video+audio scored" "GAME_CODE|video, audio (after handoff f492)"
# The boot animation must not count: whole-run says moving+audio, scored says static+silent.
probe_log "$TMP/c5" 'pc=$00805F46 lit_frames=1020 motion=370 audio_nonsilent=288566 audio_onset=35 bios_ran=1 handoff=492 final_op=$66E6 halt_frames=0 scored_from=492 scored_frames=600 scored_lit_frames=584 scored_motion=3 scored_audio_nonsilent=0' </dev/null
cls "$TMP/c5" bios; want "boot jingle/animation do not count: static, silent" "GAME_CODE|static video, silent (after handoff f492)"
cls "$TMP/c5" hle;  want "HLE mode still scores the whole run" "GAME_CODE|video, audio"
# Cart took control, then trapped into the boot ROM: crash, not reject, not working.
probe_log "$TMP/c6" 'pc=$E005DC lit_frames=1020 motion=370 audio_nonsilent=288566 audio_onset=35 bios_ran=1 handoff=492 final_op=$60FE halt_frames=0 scored_from=492 scored_frames=600 scored_lit_frames=584 scored_motion=3 scored_audio_nonsilent=0' </dev/null
cls "$TMP/c6" bios; want "boot-ROM PC after handoff -> ? (bios_trap)" "? (bios_trap)|"
# Watchdog signatures are never excused, boot phase included (#853).
printf '[CRASH-DETECT] dsp_pc_escape frame=5 pc=$00FFFFEF (valid)\n' | probe_log "$TMP/c7" 'pc=$0086CE lit_frames=500 motion=100 audio_nonsilent=90000 bios_ran=0 handoff=0 final_op=$66E6 halt_frames=0 scored_from=0 scored_frames=600 scored_lit_frames=500 scored_motion=100 scored_audio_nonsilent=90000'
cls "$TMP/c7" bios; want "early dsp_pc_escape still reported" "GAME_CODE|video, audio; dsp_pc_escape"
# The matrix runs the classifier under `xargs bash -c`, where only exported
# FUNCTIONS survive; a classifier that leans on a plain variable silently drops
# every signature there (it did, in the first cut of #852).
export -f cart_classify_mode field matrix_core_error
export TIMEOUT_SECS FRAMES
SUBSH="$(bash -c 'cart_classify_mode 0 "$1" bios' _ "$TMP/c7")"
case "$SUBSH" in *dsp_pc_escape*) ok "signatures survive the xargs/bash -c subshell";;
    *) bad "signatures survive the xargs/bash -c subshell" "got: $SUBSH";; esac
# Legacy probe line without the new fields still classifies as before.
probe_log "$TMP/c8" 'pc=$00803000 lit_frames=500 motion=100 audio_nonsilent=90000 audio_onset=3' </dev/null
cls "$TMP/c8" bios; want "legacy probe line (no bios_ran) scores the whole run" "GAME_CODE|video, audio"

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

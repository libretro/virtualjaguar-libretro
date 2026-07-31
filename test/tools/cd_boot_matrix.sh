#!/usr/bin/env bash
# test/tools/cd_boot_matrix.sh -- Boot-stage baseline matrix for every local
# Jaguar CD title, in both HLE and real-BIOS boot modes.
#
# This is the regression gate for src/cd/*: every future CD-boot fix must
# move rows in docs/cd-boot-matrix.md forward (LOAD_FAIL -> BIOS_INTRO ->
# BOOT_STUB -> GAME_CODE -> MENU -> IN_GAME), never backward.
#
# WHAT IT DOES
#   For each disc image under $CD_MATRIX_ROMS_ROOT (default:
#   test/roms/private), runs test/test_cd_hle_boot and/or
#   test/test_cd_bios_boot focused on that single disc, at a fixed frame
#   budget, wall-clock bounded. It then classifies the harness's own
#   structured stderr output (the "[PASS]/[FAIL] ... final_pc=$XXXXXX"
#   and "[DIAG] ..." lines already emitted by those two harnesses -- see
#   test/cd_assertions.h) into the stage taxonomy and writes one markdown
#   row per title x mode to docs/cd-boot-matrix.md.
#
#   We deliberately do NOT invoke test/test_cd_boot here: it is a
#   diagnostic memory-dump tool hand-tuned to one title's BIOS addresses
#   (see its own file header), not a per-title pass/fail harness, and has
#   no discovery/focus/JSON support worth building out for this task.
#   test_cd_hle_boot / test_cd_bios_boot already ARE our structured
#   harness (discovery, VJ_TEST_CD_FOCUS, per-disc diagnostic counters) --
#   we parse their existing stdout/stderr lines rather than inventing a
#   second output format.
#
# PREREQUISITES
#   - test/roms/private/ must contain the CUE/CDI/ISO images (gitignored,
#     commercial ROMs -- not distributed). In a git-worktree checkout that
#     doesn't share untracked files with the main checkout, symlink it:
#       ln -s /path/to/main-checkout/test/roms/private test/roms/private
#   - test/roms/private/ must also contain the real Jaguar CD BIOS file
#     for the BIOS-mode rows (this repo's copy resolves to
#     "Jaguar CD BIOS.rom"; see test/test_cd_bios_boot.c's header comment
#     for the full accepted-filename list).
#   - The core must be built with TEST_EXPORTS=1 (plain `make` strips the
#     dlsym-able export set the harnesses need -- see CLAUDE.md). This
#     script checks for the CDROMDiagGetCounters export and rebuilds if
#     it's missing.
#
# USAGE
#   bash test/tools/cd_boot_matrix.sh
#
# ENV KNOBS
#   CD_MATRIX_ROMS_ROOT   disc image root (default: test/roms/private)
#   CD_MATRIX_FRAMES      frames per title per mode (default: 3000)
#   CD_MATRIX_TIMEOUT     wall-clock seconds before a single title x mode
#                         run is killed and recorded HARNESS_HANG (default: 120)
#   CD_MATRIX_OUT         output markdown path (default: docs/cd-boot-matrix.md)
#   CD_MATRIX_LOGDIR      where per-run raw logs are kept (default: a fresh
#                         mktemp -d; NOT committed -- path is printed at the end)
#   CD_MATRIX_MAX_RUNS    max NEW title x mode runs per invocation (default 0 =
#                         unlimited). Combined with the resume guard this lets
#                         a long sweep be chunked across short invocations:
#                         each chunk skips the rows earlier chunks recorded,
#                         because the build stamp does not move when the only
#                         thing that changed is this script's own output
#                         (scripts/build-id.sh BUILD_ID_IGNORE).
set -u

# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT" || { echo "FATAL: cannot cd to repo root $REPO_ROOT" >&2; exit 1; }

ROMS_ROOT="${CD_MATRIX_ROMS_ROOT:-test/roms/private}"
FRAMES="${CD_MATRIX_FRAMES:-3000}"
TIMEOUT_SECS="${CD_MATRIX_TIMEOUT:-120}"
OUT="${CD_MATRIX_OUT:-docs/cd-boot-matrix.md}"
LOGDIR="${CD_MATRIX_LOGDIR:-$(mktemp -d "${TMPDIR:-/tmp}/cd_boot_matrix.XXXXXX")}"
MAX_RUNS="${CD_MATRIX_MAX_RUNS:-0}"   # 0 = unlimited; see the resume guard below

mkdir -p "$LOGDIR"

if [ ! -d "$ROMS_ROOT" ]; then
    echo "FATAL: $ROMS_ROOT not found." >&2
    echo "  test/roms/private is gitignored (commercial ROMs/BIOS)." >&2
    echo "  In a worktree checkout, symlink it from the main checkout:" >&2
    echo "    ln -s /path/to/main-checkout/test/roms/private test/roms/private" >&2
    exit 1
fi

DYLIB=""
for candidate in virtualjaguar_libretro.dylib virtualjaguar_libretro.so; do
    if [ -f "$candidate" ]; then DYLIB="$candidate"; break; fi
done
if [ -z "$DYLIB" ]; then
    echo "No built core found -- building with TEST_EXPORTS=1..." >&2
    make TEST_EXPORTS=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" || {
        echo "FATAL: build failed" >&2; exit 1; }
    for candidate in virtualjaguar_libretro.dylib virtualjaguar_libretro.so; do
        if [ -f "$candidate" ]; then DYLIB="$candidate"; break; fi
    done
fi

# The harnesses dlsym() CDROMDiagGetCounters; a plain `make` (no
# TEST_EXPORTS=1) relinks against the slim production export list and
# silently drops it (bit Task 2 of this plan -- see task-2-report.md).
if command -v nm >/dev/null 2>&1; then
    if ! nm -gU "$DYLIB" 2>/dev/null | grep -q CDROMDiagGetCounters; then
        echo "$DYLIB is missing test-export symbols -- rebuilding with TEST_EXPORTS=1..." >&2
        rm -f "$DYLIB"
        make TEST_EXPORTS=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" || {
            echo "FATAL: build failed" >&2; exit 1; }
    fi
fi

# Build-identity guard: every harness invocation below verifies that the
# core's embedded git rev matches the working tree (scripts/build-id.sh),
# so matrix rows can never be produced by a stale/wrong-branch binary.
#
# The id is computed ONCE per invocation and stamped into every row this
# invocation writes (see run_title).  Resuming across invocations only
# works because scripts/build-id.sh excludes the results file from its
# dirty check -- otherwise writing the first row would flip the id and the
# next invocation would re-run everything it had just recorded.  The
# default $OUT is on that ignore list; a custom tracked $OUT is not, so
# warn rather than let the sweep silently fail to converge.  (This applies
# to any re-invocation, chunked or not: an interrupted unlimited sweep
# resumes through the same guard.)
#
# The ignore list is written repo-root-relative, so an absolute $OUT has to
# be normalised before the comparison.  `git ls-files` resolves an absolute
# path inside the repo on its own, but the string compare below does not:
# without this, CD_MATRIX_OUT=/abs/path/to/docs/cd-boot-matrix.md -- a file
# that IS ignored -- fails the compare and draws a warning stating the exact
# opposite of the truth.
OUT_REL=$OUT
OUT_DIR=$(dirname "$OUT")
if OUT_PREFIX=$(cd "$OUT_DIR" 2>/dev/null && git rev-parse --show-prefix 2>/dev/null); then
    OUT_REL="${OUT_PREFIX}$(basename "$OUT")"
fi
if git ls-files --error-unmatch "$OUT" >/dev/null 2>&1 \
   && ! scripts/build-id.sh --ignores 2>/dev/null | grep -qxF "$OUT_REL"; then
    echo "WARNING: CD_MATRIX_OUT=$OUT is tracked by git but is not in" >&2
    echo "  scripts/build-id.sh's ignore list. Writing rows will flip the build" >&2
    echo "  id to '-dirty', so the next chunked invocation will treat every row" >&2
    echo "  this one records as stale and re-run it. Add the path to" >&2
    echo "  BUILD_ID_IGNORE, or point CD_MATRIX_OUT at an untracked file." >&2
fi

VJ_EXPECT_BUILD=$(scripts/build-id.sh 2>/dev/null || true)
export VJ_EXPECT_BUILD
if [ -n "$VJ_EXPECT_BUILD" ]; then
    echo "expected core build id: $VJ_EXPECT_BUILD" >&2
    if ! strings "$DYLIB" 2>/dev/null | grep -Eq "$VJ_EXPECT_BUILD( |\$)"; then
        echo "$DYLIB does not embed build id $VJ_EXPECT_BUILD -- rebuilding..." >&2
        rm -f "$DYLIB"
        make TEST_EXPORTS=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" || {
            echo "FATAL: build failed" >&2; exit 1; }
    fi
fi

# TEST_EXPORTS=1 is required: the per-binary rules only exist in that
# Makefile branch.  A bare `make test/<bin>` falls through to GNU make's
# built-in %:%.c rule, which links with the core's -dynamiclib LDFLAGS
# and produces an unexecutable shared library (every matrix row then
# reads "cannot execute binary file" -> all '?').
for bin in test/test_cd_hle_boot test/test_cd_bios_boot; do
    if [ ! -x "$bin" ] || ! file "$bin" 2>/dev/null | grep -q executable; then
        echo "Building $bin..." >&2
        rm -f "$bin"
        make TEST_EXPORTS=1 "$bin" || { echo "FATAL: failed to build $bin" >&2; exit 1; }
    fi
done

# Portable bounded-execution helper: prefer GNU coreutils `timeout`/`gtimeout`,
# fall back to a plain-bash watchdog. Returns 124 on timeout (matches GNU
# `timeout`'s convention) so callers have one thing to check either way.
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_BIN="gtimeout"
fi

run_bounded() {
    # usage: run_bounded <seconds> <logfile> -- <cmd...>
    secs="$1"; logfile="$2"; shift 2
    if [ -n "$TIMEOUT_BIN" ]; then
        "$TIMEOUT_BIN" "$secs" "$@" >"$logfile" 2>&1
        return $?
    fi
    "$@" >"$logfile" 2>&1 &
    pid=$!
    waited=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 1
        waited=$((waited + 1))
        if [ "$waited" -ge "$secs" ]; then
            kill -TERM "$pid" 2>/dev/null
            sleep 2
            kill -KILL "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            return 124
        fi
    done
    wait "$pid"
    return $?
}

# ---------------------------------------------------------------------------
# Per-title x mode runner
# ---------------------------------------------------------------------------

# Populated per-call; consumed immediately by classify_and_row().
RUN_RC=0
RUN_LOG=""

run_one() {
    # usage: run_one <mode: hle|bios> <focus-substring> <ext-filter>
    mode="$1"; focus="$2"; ext="$3"
    slug="$(printf '%s-%s' "$mode" "$focus" | tr -c 'A-Za-z0-9._-' '_')"
    RUN_LOG="$LOGDIR/$slug.log"

    harness="test/test_cd_hle_boot"
    [ "$mode" = "bios" ] && harness="test/test_cd_bios_boot"

    DYLD_LIBRARY_PATH=. LD_LIBRARY_PATH=. \
        VJ_TEST_CD_ROOT="$ROMS_ROOT" \
        VJ_TEST_CD_FOCUS="$focus" \
        VJ_TEST_CD_EXTS="$ext" \
        VJ_TEST_CD_FRAMES="$FRAMES" \
        run_bounded "$TIMEOUT_SECS" "$RUN_LOG" "./$harness"
    RUN_RC=$?
}

# ---------------------------------------------------------------------------
# Classification: harness stderr lines -> boot-stage taxonomy
#
# LOAD_FAIL -> BIOS_INTRO -> BOOT_STUB -> GAME_CODE -> MENU -> IN_GAME
#
# Evidence used, in priority order:
#   1. HARNESS_HANG   -- run_bounded returned 124 (killed at CD_MATRIX_TIMEOUT).
#   2. [CRASH] line   -- forked child died on a signal (segfault etc).
#                        Treated as a LOAD_FAIL variant: the disc never
#                        reached a stable, inspectable state.
#   3. "load failed"  -- retro_load_game() returned false -> LOAD_FAIL.
#   4. Address-band read of final_pc from the harness's own
#      "[PASS]/[FAIL] <title> : ... final_pc=$XXXXXX" line:
#        - HLE mode synthesizes past the real BIOS entirely by design (that
#          is the point of HLE) -- there is no separate BIOS_INTRO/BOOT_STUB
#          stage to observe. A stable, multi-PC run is GAME_CODE; a run that
#          never leaves a single PC (unique_pcs<=1) is left as "?" rather
#          than guessed.
#        - BIOS mode actually executes the real CD BIOS, whose code lives at
#          well-documented addresses (see test/test_cd_boot.c's hardcoded
#          dump regions, and this task's own captured evidence):
#            $E00000-$E1FFFF  boot ROM cube animation        -> BIOS_INTRO
#            $800000-$8FFFFF  CD BIOS running as cart code    -> BIOS_INTRO
#            $190000-$1AFFFF  CD BIOS code/data relocated to
#                             RAM (poll loops, data formatter) -> BIOS_INTRO
#            $004000-$007FFF  AMBIGUOUS -- gated on the handoff marker,
#                             see "the $4000-$7FFF band" below
#            $080000-$08FFFF  injected boot-stub ISR/poll loop/
#                             data (confirmed via test_cd_boot.c
#                             comments: $0803A0, $080250, $085D00) -> BOOT_STUB
#            anything else in RAM                                -> GAME_CODE
#
#   The $4000-$7FFF band: this range is used by the BIOS *and* by games,
#   so a bare address test cannot classify it. The real CD BIOS does run
#   68K code here before handoff (jagcd_bios.c hooks its GPU-auth path at
#   $005E40), but games also link code here -- Dragon's Lair's boot
#   executable loads at $004000, Myst's at $005000 (jagcd_bios.c:45), and
#   BrainDead 13 / Primal Rage load a second-stage player here after
#   their own exes land at $124000 / $080000.
#
#   So the band is gated on the BIOS handoff marker instead. The core logs
#   "[CD-BOOTSTUB] Injected $<len> bytes at $<addr>" exactly when the BIOS
#   reaches its handoff point ($050176, where it JSRs to the disc's boot
#   executable). Before that line the BIOS owns this band; after it, the
#   game does:
#            marker absent  -> BIOS_INTRO (BIOS never handed off)
#            marker present  -> falls through to GAME_CODE
#
#   History: the band used to be an unconditional -> BIOS_INTRO, sourced
#   from a single ISO row (final_pc=$0059B0). ISO loads are now refused
#   outright as unbootable (docs/cd-known-issues.md), so that provenance is
#   retired -- and the rule was producing three false BIOS_INTRO rows.
#   BrainDead 13 ($004FCA), Dragon's Lair ($004C12) and Primal Rage
#   ($0044CC) were each disassembled from a live RAM snapshot at the
#   reported PC: the first two sit in the same ReadySoft FMV player's
#   "wait for the next frame's presentation time" loop (a 30 Hz / 24 Hz
#   clock that a GPU PIT ISR advances, and that was measured still
#   advancing), and Primal Rage sits in its joypad-scan routine writing
#   $817F/$81BF/$81DF/$81EF to $F14000. All three were then confirmed by
#   cd_visual_verify to be rendering real content in bios mode. The
#   clincher is Dragon's Lair itself: its hle row reports final_pc=$004814
#   and its bios row $004C12 -- the same 2 KB of the same player -- and
#   only the bios path applied the band rule.
#   MENU vs IN_GAME is NOT distinguished: none of our harnesses read the
#   composited framebuffer the way RetroArch does (documented headless
#   framebuffer caveat in CLAUDE.md), so any GAME_CODE row is reported as
#   "GAME_CODE (menu/in-game undetermined)" rather than guessed further.
# ---------------------------------------------------------------------------

classify_stage() {
    mode="$1"; log="$2"; rc="$3"
    if [ "$rc" -eq 124 ]; then echo "HARNESS_HANG"; return; fi
    if grep -aq '\[CRASH\]' "$log" 2>/dev/null; then echo "LOAD_FAIL (harness crash)"; return; fi
    if grep -aq 'load failed' "$log" 2>/dev/null; then echo "LOAD_FAIL"; return; fi

    result_line="$(grep -aE '^\s*\[(PASS|FAIL)\]' "$log" 2>/dev/null | head -1)"
    if [ -z "$result_line" ]; then echo "?"; return; fi

    final_pc_hex="$(printf '%s' "$result_line" | grep -oE 'final_pc=\$[0-9A-Fa-f]+' | grep -oE '[0-9A-Fa-f]+$')"
    unique_pcs="$(printf '%s' "$result_line" | grep -oE 'unique_pcs=[0-9]+' | grep -oE '[0-9]+')"
    if [ -z "$final_pc_hex" ]; then echo "?"; return; fi

    pc_dec=$((16#$final_pc_hex))

    # PC outside every valid execute window (mirrors cd_pc_oob() in
    # test/cd_assertions.h: RAM <$200000, boot ROM $E00000-$E1FFFF, cart /
    # CD BIOS $800000-$8FFFFF). An escaped PC is evidence of a crash, not
    # of any boot stage having been reached -- classify "?" per the
    # never-fabricate rule, and let the pc_in_ram=0 evidence column speak.
    if [ "$pc_dec" -ge $((16#200000)) ] \
       && { [ "$pc_dec" -lt $((16#800000)) ] || [ "$pc_dec" -gt $((16#8FFFFF)) ]; } \
       && { [ "$pc_dec" -lt $((16#E00000)) ] || [ "$pc_dec" -gt $((16#E1FFFF)) ]; }; then
        echo "? (pc_escape)"
        return
    fi

    if [ "$mode" = "hle" ]; then
        if [ -n "$unique_pcs" ] && [ "$unique_pcs" -le 1 ]; then
            echo "?"
        else
            echo "GAME_CODE"
        fi
        return
    fi

    if   [ "$pc_dec" -ge $((16#E00000)) ] && [ "$pc_dec" -le $((16#E1FFFF)) ]; then echo "BIOS_INTRO"
    elif [ "$pc_dec" -ge $((16#800000)) ] && [ "$pc_dec" -le $((16#8FFFFF)) ]; then echo "BIOS_INTRO"
    elif [ "$pc_dec" -ge $((16#190000)) ] && [ "$pc_dec" -le $((16#1AFFFF)) ]; then echo "BIOS_INTRO"
    elif [ "$pc_dec" -ge $((16#004000)) ] && [ "$pc_dec" -le $((16#007FFF)) ] \
         && ! grep -aq '\[CD-BOOTSTUB\] Injected' "$log" 2>/dev/null; then
        # Shared BIOS/game band -- only BIOS_INTRO while the BIOS has not yet
        # reached its handoff point. See the taxonomy note above.
        echo "BIOS_INTRO"
    elif [ "$pc_dec" -ge $((16#080000)) ] && [ "$pc_dec" -le $((16#08FFFF)) ]; then echo "BOOT_STUB"
    elif [ "$pc_dec" -lt $((16#004000)) ]; then
        # $0000-$3FFF is ambiguous: it holds BIOS service routines (TOC at
        # $2C00, CD_read at $3610) that the BIOS uses during boot AND that
        # games call after handoff. Disambiguate with the harness's [PC-SET]
        # line: if the run also visited PCs in the game-load region above
        # the boot stub ($090000-$1FFFFF), game code is running and calling
        # BIOS services -> GAME_CODE; otherwise the evidence only supports
        # "still in BIOS service code".
        # NOTE: the harness only prints [PC-SET] when unique_pcs <= 32 and
        # the unique-PC tracker didn't overflow (test_cd_bios_boot.c).
        # Absence of the line means the game-band check NEVER RAN -- that is
        # weaker evidence than a checked negative, and the two must not be
        # conflated in the emitted stage text.
        pcset_line="$(grep -am1 '\[PC-SET\]' "$log" 2>/dev/null)"
        if [ -z "$pcset_line" ]; then
            echo "? (BIOS service band \$0-\$3FFF; PC-SET suppressed, unique_pcs>32 -- game handoff undetermined)"
        elif printf '%s' "$pcset_line" | grep -qE '\$(09|0[A-Fa-f]|1[0-9A-Fa-f])[0-9A-Fa-f]{4}'; then
            echo "GAME_CODE"
        else
            echo "? (BIOS service band \$0-\$3FFF; PC-SET showed no game-band entries)"
        fi
    else echo "GAME_CODE"
    fi
}

watchdog_of() {
    # First crash-detect watchdog line, whole line. (Do NOT use [^\n] in an
    # ERE bracket expression here -- POSIX treats it as "not backslash, not
    # the letter n", which silently truncates at the first literal 'n'.)
    log="$1"
    grep -am1 '\[CRASH-DETECT\]' "$log" 2>/dev/null
}

pc_summary_of() {
    log="$1"
    grep -aE '^\s*\[(PASS|FAIL)\]' "$log" 2>/dev/null | head -1 | sed -E 's/^\s*\[(PASS|FAIL)\]\s*//'
}

# ---------------------------------------------------------------------------
# Title list
#
# The 9 CUE titles are exactly the set test_cd_hle_boot / test_cd_bios_boot
# already discover by default (VJ_TEST_CD_EXTS defaults to "cue") -- this
# matches the Task 2 per-title baseline one-for-one. CDI (baldies.cdi) and
# one loose ISO (Primal Rage) are opt-in via VJ_TEST_CD_EXTS, per
# test/cd_assertions.h's own documented rationale (CDI parser has a known
# crasher; ISO boot is a documented, permanent limitation -- ISO images
# carry no session-2 audio pregap so the real BIOS can never validate them).
# ---------------------------------------------------------------------------

CUE_TITLES=(
    "Baldies (USA) (Rev 1).cue"
    "Battle Morph (USA).cue"
    "BrainDead 13 (USA).cue"
    "Dragon's Lair (USA).cue"
    "Highlander - The Last of the MacLeods (USA).cue"
    "Hover Strike - Unconquered Lands (USA).cue"
    "Iron Soldier 2 (USA) (Songbird).cue"
    "Myst (USA).cue"
    "Primal Rage (USA).cue"
    "Space Ace (USA).cue"
)

echo "=== CD boot matrix: $FRAMES frames/title/mode, ${TIMEOUT_SECS}s wall-clock cap, logs in $LOGDIR ===" >&2

# Resume support: rows are appended incrementally, so an interrupted sweep
# can be re-invoked and picks up where it left off (already-recorded
# title x mode rows are skipped -- see run_title). CD_MATRIX_MAX_RUNS=N
# additionally caps how many NEW runs a single invocation performs, so a
# long sweep can be chunked across several short invocations.
#
# STALE-ROW GUARD: each row is stamped with the core build id
# (`<!-- build:<rev> -->`, hidden in rendered markdown).  On resume, a row
# is skipped ONLY if its stamp matches the current build; rows recorded by
# a different build (or unstamped legacy rows) are re-run and replaced in
# place.  Without this, reusing an OUT file that predates the current build
# silently resurrects ancient results as if they were fresh -- the exact
# mechanism behind the phantom "intermittent" Battle Morph bios
# `? (pc_escape)` rows (a July-era `final_pc=$8FBFB758` FAIL row,
# byte-identical across "different" sweeps, carried forward by resume while
# every actual harness run of that title passed).  Row matching is also
# scoped to the primary results table, so rows quoted in prose notes can
# never satisfy the resume guard.
#
# The stamp stays stable across the chunks of one sweep because
# scripts/build-id.sh does not count writes to this results file as a
# source change (its BUILD_ID_IGNORE list); a real edit to emulator code
# still flips it to "-dirty" and correctly invalidates earlier rows.
# MAX_RUNS is set with the other env knobs at the top of the script.
RUNS_DONE=0

# Write the header only on a fresh file; strip any previous footer so
# appended rows stay inside the table.
if [ -f "$OUT" ] && grep -q '^| Title | Mode |' "$OUT"; then
    tmp="$(mktemp)"
    grep -v '^Raw per-run logs:' "$OUT" \
        | awk '{lines[NR]=$0} END {n=NR; while (n>0 && lines[n] ~ /^[[:space:]]*$/) n--; for (i=1;i<=n;i++) print lines[i]}' > "$tmp"
    mv "$tmp" "$OUT"
    echo "Resuming: keeping $(grep -c '^| ' "$OUT") existing table lines in $OUT" >&2
else
{
    printf '# CD boot matrix\n\n'
    printf 'Generated by `test/tools/cd_boot_matrix.sh` -- the regression gate for CD-boot\n'
    printf 'work. Every subsequent CD-boot fix must move rows forward through the stage\n'
    printf 'taxonomy below, never backward. Do not hand-edit this table; re-run the script\n'
    printf 'and commit its output instead.\n\n'
    printf '## How to re-run\n\n'
    printf '```bash\n'
    printf 'bash test/tools/cd_boot_matrix.sh\n'
    printf '```\n\n'
    printf 'Requires `test/roms/private/` populated with the CUE/CDI/ISO images and the real\n'
    printf 'Jaguar CD BIOS (`Jaguar CD BIOS.rom` in this repo'"'"'s corpus; see\n'
    printf '`test/test_cd_bios_boot.c`'"'"'s header comment for the full accepted-filename list).\n'
    printf 'In a git-worktree checkout that does not share untracked files with the main\n'
    printf 'checkout: `ln -s /path/to/main-checkout/test/roms/private test/roms/private`.\n'
    printf 'Core must be built `make TEST_EXPORTS=1` (a plain `make` strips the dlsym export\n'
    printf 'set the harnesses need -- the script auto-rebuilds if it detects this).\n'
    printf 'Env knobs: `CD_MATRIX_FRAMES` (default 3000), `CD_MATRIX_TIMEOUT` (default 120s\n'
    printf 'per title x mode), `CD_MATRIX_ROMS_ROOT` (default `test/roms/private`),\n'
    printf '`CD_MATRIX_OUT` (output path, default `docs/cd-boot-matrix.md`),\n'
    printf '`CD_MATRIX_LOGDIR` (raw per-run logs, default a fresh `mktemp -d`; pass a\n'
    printf 'fixed dir to keep logs across chunked invocations), and\n'
    printf '`CD_MATRIX_MAX_RUNS` (max NEW runs per invocation, default 0 = unlimited).\n\n'
    printf 'Rows append incrementally with a resume guard: title x mode combos already\n'
    printf 'recorded **by the same core build** (each row carries a hidden\n'
    printf '`<!-- build:<rev> -->` stamp) are skipped, so an interrupted sweep resumes\n'
    printf 'where it left off; rows recorded by a different build, or unstamped legacy\n'
    printf 'rows, are re-run and replaced in place rather than silently reused.\n'
    printf 'Recording rows does not itself move the stamp -- `scripts/build-id.sh`\n'
    printf 'excludes this results file from its dirty check -- so the chunks of one\n'
    printf 'sweep share a stamp and each chunk resumes where the last stopped. For\n'
    printf 'long sweeps (or agent sessions with per-command time limits), run chunked:\n\n'
    printf '```bash\n'
    printf '# ~3 runs x <=120s wall each per invocation; repeat until every line\n'
    printf '# reports "already recorded, skipping":\n'
    printf 'CD_MATRIX_MAX_RUNS=3 CD_MATRIX_LOGDIR=/tmp/cdmx_logs bash test/tools/cd_boot_matrix.sh\n'
    printf '```\n\n'
    printf 'To force a re-run of specific rows on the SAME build, delete those table\n'
    printf 'lines from the output file first -- the resume guard re-runs missing and\n'
    printf 'stale-build title x mode combos only.\n\n'
    printf '## Legend\n\n'
    printf '**Stage** (finest level the harness evidence supports; `?` = insufficient\n'
    printf 'evidence to classify further, never fabricated):\n\n'
    printf '| Stage | Meaning |\n|---|---|\n'
    printf '| `LOAD_FAIL` | `retro_load_game()` returned false, or the harness child crashed (segfault) before producing a usable PC trace |\n'
    printf '| `BIOS_INTRO` | Real CD BIOS executing (boot ROM cube animation, BIOS main loop/error handler, or CD-read poll code) -- has not yet handed off to the boot stub or game. In the shared `$004000`-`$007FFF` band this requires the absence of the `[CD-BOOTSTUB] Injected` handoff marker: games link code there too (Dragon'"'"'s Lair at `$004000`, Myst at `$005000`), so the address alone is not evidence |\n'
    printf '| `BOOT_STUB` | PC parked in the injected boot-stub ISR/poll-loop/data region (`$080000`-`$08FFFF`) |\n'
    printf '| `GAME_CODE` | PC in game-owned RAM outside the BIOS/boot-stub bands (HLE: any stable multi-PC run, since HLE synthesizes past BIOS/boot-stub by design) |\n'
    printf '| `MENU` / `IN_GAME` | Not distinguished by any current harness -- see "Headless framebuffer caveat" in CLAUDE.md. Reported as `GAME_CODE` with a note. |\n'
    printf '| `HARNESS_HANG` | Killed at the wall-clock timeout; the harness process itself did not exit |\n'
    printf '| `? (pc_escape)` | Final 68K PC outside every valid execute window (RAM <`$200000`, boot ROM `$E00000`-`$E1FFFF`, cart/CD BIOS `$800000`-`$8FFFFF`) -- a crash, not a reached stage |\n'
    printf '| `? (BIOS service band ...)` | Final PC in `$0`-`$3FFF` (BIOS TOC / CD_read service code, used both by the BIOS during boot and by games after handoff). Two variants, distinct evidence strength: "PC-SET showed no game-band entries" = the visited-PC dump was checked and contains no `$090000`-`$1FFFFF` addresses (checked negative); "PC-SET suppressed, unique_pcs>32" = the harness never emitted the dump, so the game-band check could not run (undetermined) |\n\n'
    printf 'Score `N/1` follows the harness'"'"'s own PASS/FAIL heuristic (PC stays in\n'
    printf 'RAM/BIOS/cart range, not self-looping, not thrashing among <=4 distinct PCs,\n'
    printf 'RAM has non-trivial non-zero payload) -- see `test/cd_assertions.h`. It is\n'
    printf 'informational, not the stage classification: a title can score FAIL yet still\n'
    printf 'classify as GAME_CODE (e.g. stuck in its own hardware-poll wait loop after\n'
    printf 'booting), or score PASS while stuck in a still-early BIOS/boot-stub band.\n\n'
    printf '## Results\n\n'
    printf '| Title | Mode | Score | Stage | Watchdog | PC evidence |\n'
    printf '|---|---|---|---|---|---|\n'
} > "$OUT"
fi

# Locate a title x mode row inside the PRIMARY results table only (the
# contiguous block of '|' lines following the first '| Title | Mode |'
# header).  Prints the 1-based line number of the first match, or nothing.
# Scoping to the primary table keeps rows quoted in prose/notes sections
# (the committed baseline doc carries several) from satisfying the resume
# guard.
find_row_lineno() {
    # usage: find_row_lineno <title> <mode>
    awk -v t="$1" -v m="$2" '
        BEGIN { needle = "| " t " | " m " |"; intab = 0 }
        !intab && index($0, "| Title | Mode |") == 1 { intab = 1; next }
        intab && /^\|---/ { next }
        intab && !/^\|/ { exit }
        intab && index($0, needle) == 1 { print NR; exit }
    ' "$OUT" 2>/dev/null
}

# Last line of the PRIMARY results table (same block find_row_lineno
# scans).  New rows are inserted here rather than appended to EOF: the
# committed doc carries several hundred lines of prose notes AFTER the
# table, so an EOF append puts a new title's row outside the table, where
# find_row_lineno cannot see it -- the title then re-runs on every
# invocation and appends another duplicate each time, forever.  (Adding a
# title to CUE_TITLES is the only way to hit this, which is why it went
# unnoticed until Myst was added.)
primary_table_last_lineno() {
    awk '
        BEGIN { intab = 0; last = 0 }
        !intab && index($0, "| Title | Mode |") == 1 { intab = 1; last = NR; next }
        intab && /^\|/ { last = NR; next }
        intab { exit }
        END { if (last) print last }
    ' "$OUT" 2>/dev/null
}

run_title() {
    # usage: run_title <display-title> <mode: hle|bios> <ext: cue|cdi|iso>
    title="$1"; mode="$2"; ext="$3"

    # Resume guard: skip a title x mode combo already recorded in $OUT --
    # but ONLY if it was recorded by the core build we are testing now.
    # A row stamped by a different build (or an unstamped legacy row) is
    # stale evidence: re-run it and replace the row in place.
    row_lineno="$(find_row_lineno "$title" "$mode")"
    if [ -n "$row_lineno" ]; then
        row_line="$(sed -n "${row_lineno}p" "$OUT")"
        if [ -z "$VJ_EXPECT_BUILD" ]; then
            # No build identity available (no git?): can't tell fresh from
            # stale -- keep legacy skip behavior, but say so.
            echo "  [$mode] $title -- already recorded, skipping (build id unavailable; provenance unverified)" >&2
            return 0
        fi
        if printf '%s' "$row_line" | grep -qF "<!-- build:$VJ_EXPECT_BUILD -->"; then
            echo "  [$mode] $title -- already recorded by this build, skipping" >&2
            return 0
        fi
        row_old_build="$(printf '%s' "$row_line" \
            | grep -oE '<!-- build:[^ ]+ -->' | head -1 \
            | sed -E 's/<!-- build:([^ ]+) -->/\1/')"
        [ -z "$row_old_build" ] && row_old_build="unstamped"
    fi
    # Chunk limit: stop starting new runs once CD_MATRIX_MAX_RUNS is reached.
    if [ "$MAX_RUNS" -gt 0 ] && [ "$RUNS_DONE" -ge "$MAX_RUNS" ]; then
        return 0
    fi
    RUNS_DONE=$((RUNS_DONE + 1))
    if [ -n "$row_lineno" ]; then
        echo "  [$mode] $title -- stale row (recorded by '$row_old_build', current '$VJ_EXPECT_BUILD'); re-running" >&2
    else
        echo "  [$mode] $title ..." >&2
    fi
    run_one "$mode" "$title" "$ext"
    log="$RUN_LOG"; rc="$RUN_RC"

    stage="$(classify_stage "$mode" "$log" "$rc")"
    watchdog="$(watchdog_of "$log")"
    [ -z "$watchdog" ] && watchdog="(none)"
    evidence="$(pc_summary_of "$log")"
    if [ -z "$evidence" ]; then
        if [ "$rc" -eq 124 ]; then evidence="killed after ${TIMEOUT_SECS}s wall-clock"
        elif grep -aq '\[CRASH\]' "$log" 2>/dev/null; then evidence="$(grep -a '\[CRASH\]' "$log" | head -1 | sed -E 's/^\s*//')"
        elif grep -aq 'load failed' "$log" 2>/dev/null; then evidence="$(grep -am1 'load failed' "$log" | sed -E 's/^\s*//')"
        else evidence="(no PASS/FAIL line captured -- see $log)"
        fi
    fi
    # Self-auditing rows: when the harness dumped a [PC-SET] containing
    # game-band PCs ($090000-$1FFFFF), quote them in the evidence column so
    # a GAME_CODE call made from them is verifiable from the row itself.
    gameband="$(grep -am1 '\[PC-SET\]' "$log" 2>/dev/null \
        | grep -oE '\$(09|0[A-Fa-f]|1[0-9A-Fa-f])[0-9A-Fa-f]{4}' \
        | tr '\n' ' ' | sed -E 's/ +$//')"
    if [ -n "$gameband" ]; then
        evidence="$evidence; PC-SET game-band: $gameband"
    fi

    score="?/1"
    if grep -aqE '^\s*\[PASS\]' "$log" 2>/dev/null; then score="1/1"; fi
    if grep -aqE '^\s*\[FAIL\]' "$log" 2>/dev/null; then score="0/1"; fi
    if [ "$rc" -eq 124 ] || grep -aq '\[CRASH\]' "$log" 2>/dev/null; then score="0/1"; fi

    # Build-id stamp (stale-row guard; see resume comment above).  Lives
    # inside the last cell as an HTML comment so rendered markdown is
    # unchanged.
    stamp=""
    [ -n "$VJ_EXPECT_BUILD" ] && stamp=" <!-- build:$VJ_EXPECT_BUILD -->"
    row="$(printf '| %s | %s | %s | %s | %s | %s%s |' \
        "$title" "$mode" "$score" "$stage" \
        "$(printf '%s' "$watchdog" | sed 's/|/\\|/g')" \
        "$(printf '%s' "$evidence" | sed 's/|/\\|/g')" \
        "$stamp")"

    if [ -n "$row_lineno" ]; then
        # Replace the stale row in place so the table keeps one row per
        # title x mode.
        tmp_row_file="$(mktemp)"
        NEWROW="$row" awk -v n="$row_lineno" \
            'NR == n { print ENVIRON["NEWROW"]; next } { print }' \
            "$OUT" > "$tmp_row_file" && mv "$tmp_row_file" "$OUT"
    else
        # New title x mode: insert at the end of the primary results table,
        # never at EOF (see primary_table_last_lineno).
        ins_lineno="$(primary_table_last_lineno)"
        if [ -n "$ins_lineno" ]; then
            tmp_row_file="$(mktemp)"
            NEWROW="$row" awk -v n="$ins_lineno" \
                '{ print } NR == n { print ENVIRON["NEWROW"] }' \
                "$OUT" > "$tmp_row_file" && mv "$tmp_row_file" "$OUT"
        else
            printf '%s\n' "$row" >> "$OUT"
        fi
    fi
}

for t in "${CUE_TITLES[@]}"; do
    run_title "$t" "hle" "cue"
    run_title "$t" "bios" "cue"
done

run_title "baldies.cdi" "hle" "cdi"
run_title "baldies.cdi" "bios" "cdi"

# Loose ISOs: support was removed entirely (CDIntf refuses .iso at load
# with a LOG_ERR explaining why -- no session/track layout means no retail
# title can boot; see docs/cd-known-issues.md).  No matrix row: there is
# nothing to measure.

echo "" >> "$OUT"
echo "Raw per-run logs: $LOGDIR (not committed; re-run to regenerate)." >> "$OUT"

echo "=== Done. Table written to $OUT ===" >&2
cat "$OUT" >&2

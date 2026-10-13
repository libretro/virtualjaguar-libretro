#!/usr/bin/env bash
# test/tools/cart_classify.sh -- the cart boot matrix's per-run classifier,
# split out of cart_boot_matrix.sh so test/test_matrix_diff.sh can feed it
# synthetic probe logs (no ROMs, no core).  Source it; do not run it.
#
# Needs from the caller: matrix_common.sh already sourced (matrix_core_error),
# and the variables TIMEOUT_SECS and FRAMES (FRAMES defaults to 600).
#
# Stages produced:
#   GAME_CODE     final 68K PC in game-owned RAM/cart space ("boots and
#                 executes", not "completed the game")
#   BIOS_REJECT   (bios mode only, issue #852) the boot ROM ran, never handed
#                 the 68K to the cartridge, and parked in its own halt loop
#                 (BRA.S *): it rejected the cart's header/encryption and shows
#                 the red "Jaguar" screen.  An authentic hardware outcome for
#                 that dump, NOT a failure of the core and NOT "BIOS works".
#   ? (bios_trap) (bios mode only) the cart WAS handed control, then the 68K
#                 ended up back in the boot ROM window ($E00000-$E3FFFF) -- a
#                 game exception through the boot-ROM vectors (Flip Out parks
#                 at $E005DC).  A crash after a successful handoff.
#   ? (...)       harness / crash verdicts as before; LOAD_FAIL
#
# BIOS-mode runs are scored from the boot-ROM -> cart handoff onward: the
# probe restarts its scored_* counters at the handoff, so the ~490-frame boot
# logo animation and jingle (identical for every title) never count as the
# title's "video" or "audio".  Crash-watchdog signatures are NOT handoff-
# relative: every one in the log counts (an early escape is a real bug, #853).
# When there is no boot phase (bios_ran=0: headerless RAM-loaded executables
# never run the boot ROM) or no handoff was observed, the whole run is scored,
# exactly as before.

field() {
    # usage: field <name> <probe-line>   (numeric / $hex fields)
    printf '%s' "$2" | grep -oE "(^| )$1=[^ ]+" | head -1 | cut -d= -f2
}

cart_classify_mode() {
    # usage: cart_classify_mode <rc> <logfile> [hle|bios]
    # echoes "STAGE|notes"
    local rc="$1" logfile="$2" mode="${3:-hle}"
    local line sigs core_err frames pc_valid pc_hex lit motion audio
    local bios_ran handoff halt_frames sf pc notes short
    # Inside the function on purpose: process_one runs under `xargs bash -c`
    # with only `export -f` functions carried over, never plain variables.
    local sig_re='gpu_pc_escape|dsp_pc_escape|gpu_wedge|dsp_wedge|video_stall|inframe_hang'

    line="$(grep -m1 '^CARTPROBE ' "$logfile" 2>/dev/null || true)"

    # Watchdog signatures: the WHOLE log, boot phase included.  A single early
    # escape is deliberately not excused -- Music Demo's frame-5 dsp_pc_escape
    # is a real DSP bug (#853), and a grace rule would hide it.
    sigs="$(grep -oE "$sig_re" "$logfile" 2>/dev/null | sort -u | paste -sd, - || true)"

    if grep -q 'FATAL build mismatch' "$logfile" 2>/dev/null; then
        # Never classify a guard refusal as a title result.  The preflight
        # should catch this before any worker runs; this is belt-and-braces
        # for a core swapped mid-sweep.
        echo "? (build_mismatch)|core does not match VJ_EXPECT_BUILD — row invalid"
        return
    fi
    if [ "$rc" -eq 124 ]; then
        echo "? (timeout)|no probe line within ${TIMEOUT_SECS}s${sigs:+; $sigs}"
        return
    fi
    # A dlopen failure is a HARNESS fault, not a title result.  Writing it as
    # LOAD_FAIL is what let a missing core masquerade as 123 unloadable ROMs,
    # and because rows are cached, the bad rows were then reused by the next
    # invocation.  Shared with the CD sweep so one fix covers both.
    core_err="$(matrix_core_error "$logfile")"
    if [ -n "$core_err" ]; then
        echo "? (core_error)|$core_err"
        return
    fi
    if [ -z "$line" ] || printf '%s' "$line" | grep -q 'load_fail=1'; then
        echo "LOAD_FAIL|probe could not load the ROM"
        return
    fi

    frames="$(field frames "$line")"; frames="${frames:-0}"
    pc_valid="$(field pc_valid "$line")"; pc_valid="${pc_valid:-0}"
    pc_hex="$(printf '%s' "$line" | grep -oE 'pc=\$[0-9A-Fa-f]+' | grep -oE '[0-9A-Fa-f]+$')"

    if [ -z "$pc_hex" ] || [ "$frames" -eq 0 ]; then
        echo "LOAD_FAIL|no frames rendered"
        return
    fi
    if [ "$pc_valid" -ne 1 ]; then
        echo "? (no_reg)|probe missing m68k_get_reg — rebuild core with TEST_EXPORTS=1"
        return
    fi
    pc=$((16#$pc_hex))

    # Boot-ROM handoff evidence (probe fields; absent from an old probe).
    bios_ran=0; handoff=0; halt_frames=0
    if [ "$mode" = "bios" ]; then
        bios_ran="$(field bios_ran "$line")";       bios_ran="${bios_ran:-0}"
        handoff="$(field handoff "$line")";         handoff="${handoff:-0}"
        halt_frames="$(field halt_frames "$line")"; halt_frames="${halt_frames:-0}"
    fi

    # Rejected cart: the boot ROM ran, never reached cartridge space, and is
    # parked in its BRA.S * halt loop.  30 = PROBE_PARK_FRAMES in the probe.
    if [ "$bios_ran" -eq 1 ] && [ "$handoff" -lt 0 ] && [ "$halt_frames" -ge 30 ]; then
        echo "BIOS_REJECT|boot ROM halted at \$$pc_hex without handing off to the cart (red \"Jaguar\" reject screen)"
        return
    fi

    # Valid 68K execute bands: main RAM (mirrors) < $200000, cart $800000-
    # $DFFFFF, boot ROM $E00000-$E1FFFF.  Anything else is a crash, not a
    # reached stage.
    if ! { [ "$pc" -lt $((0x200000)) ] || \
           { [ "$pc" -ge $((0x800000)) ] && [ "$pc" -le $((0xDFFFFF)) ]; } || \
           { [ "$pc" -ge $((0xE00000)) ] && [ "$pc" -le $((0xE1FFFF)) ]; }; }; then
        echo "? (pc_escape)|final_pc=\$$pc_hex${sigs:+; $sigs}"
        return
    fi
    # Back in the boot ROM AFTER a successful handoff = the game took an
    # exception through the boot-ROM vectors.  Not a reject (the ROM accepted
    # the cart) and not "reached game code".
    if [ "$bios_ran" -eq 1 ] && [ "$handoff" -gt 0 ] && \
       [ "$pc" -ge $((0xE00000)) ] && [ "$pc" -le $((0xE3FFFF)) ]; then
        echo "? (bios_trap)|final_pc=\$$pc_hex after cart handoff at frame $handoff${sigs:+; $sigs}"
        return
    fi

    # Score from the handoff when the probe found one (scored_* restart there);
    # otherwise the whole-run counters.
    sf="$(field scored_frames "$line")"
    if [ -n "$sf" ] && [ "$bios_ran" -eq 1 ] && [ "$handoff" -gt 0 ]; then
        lit="$(field scored_lit_frames "$line")"
        motion="$(field scored_motion "$line")"
        audio="$(field scored_audio_nonsilent "$line")"
    else
        lit="$(field lit_frames "$line")"
        motion="$(field motion "$line")"
        audio="$(field audio_nonsilent "$line")"
    fi
    lit="${lit:-0}"; motion="${motion:-0}"; audio="${audio:-0}"

    notes=""
    if [ "$lit" -ge 30 ]; then
        if [ "$motion" -ge 30 ]; then notes="video"; else notes="static video"; fi
    else
        notes="black video (headless — undetermined)"
    fi
    if [ "$audio" -ge 4800 ]; then notes="$notes, audio"; else notes="$notes, silent"; fi
    if [ "$bios_ran" -eq 1 ] && [ "$handoff" -gt 0 ]; then
        notes="$notes (after handoff f$handoff"
        short=$(( ${FRAMES:-600} / 2 ))
        [ "${sf:-0}" -lt "$short" ] && notes="$notes, only ${sf:-0} frames"
        notes="$notes)"
    elif [ "$bios_ran" -eq 1 ]; then
        notes="$notes (handoff not observed; whole run scored)"
    fi
    [ -n "$sigs" ] && notes="$notes; $sigs"
    echo "GAME_CODE|$notes"
}

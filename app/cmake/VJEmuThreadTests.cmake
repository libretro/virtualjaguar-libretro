# Emulation thread (#967, docs/app-emulation-thread-design.md, docs/app-emulation-thread-plan-phase1.md).
# Every test here carries the label `emu_thread`: the ThreadSanitizer job in .github/workflows/app.yml runs exactly
# these (ctest -L emu_thread).
#
#   frame_exchange_unit   FrameExchange: newest wins, the reader's slot is never written, no tearing, content epochs
#   emu_commands_unit     EmuCommands: FIFO per producer, the frame and debug lanes, timed waits, close-and-drain, errors
#   pacing_clock_unit     PacingClock mode A against a fake clock and a fake 512-frame-chunk audio device
#   emu_thread_unit       EmuThread: inline vs threaded, call/post/debugCall ordering, pause/advance, stop/join timeout, shutdown races
#   input_snapshot_unit   InputSnapshot: newest state, pointer motion exactly once, event timestamps, takenSince
#   audio_out_unit        AudioOut under the SDL dummy driver: the mode-A hooks and cross-thread use
#   perf_csv_unit         VJ_PERF_CSV columns: underruns and input latency
#   perf_trace_unit       VJ_PERF_TRACE -emu.csv: fill_ns is the last column and never negative
#   late_latch_unit       LateLatch: vblank tracking, budget, misses, and the event -> present model against a fake clock
#   latch_waker_unit      LatchWaker (macOS): the UI thread's timed event waits end on its wake event, on time (#1026)
#   core_events_unit      CoreEventQueue: order, bounded log lines, content switch, two threads
#   emu_thread_flag_rejects  --emu-thread takes on or off only
#   core_host_thread      CoreHost with an owner thread and the REAL core: snapshot reads, queued writes, refused calls
#   emu_thread_e2e        the REAL core inline vs threaded: identical frames and joypad matrix for one input script
#   debug_views_threaded  the Tools windows' data paths with the REAL core on the emulation thread
#   debug_e2e_threaded    the debugger halt on the emulation thread, REAL core, rig wired as App: break / step / continue / detach /
#                         hardcore / quit from a UI thread that never blocks over 50 ms; the 3 s join while halted
#   gdb_threaded_halt     a gdb client finds the threaded headless app halted at boot, reads memory, continues; SIGTERM mid-halt exits 0
#   gdb_inline_halt       the same four steps with the emulation thread off (the headless loop syncs the halt after each frame)
#   gdb_inline_halt_hardcore  inline: the client continues a boot halt, then a hardcore sign-in reloads the game (needs the halt mirror synced)
#   smoke_*_threaded      the headless smoke tests with --emu-thread on: the inline hashes, and no owner violation
#   smoke_disc_swap_threaded  File > Disc on a loaded CD through EmuThread::call, read back from the pinned snapshot
#   smoke_disc_swap_bios_threaded  the same in real-BIOS boot mode, where each insert re-stages the CD BIOS
#   smoke_disc_insert_bios_threaded  the CD BIOS with no disc: Add disc image... inserts the first disc through call()
#   smoke_m3u_playlist_threaded  an .m3u's discs registered inside EmuThread::call, read back from the pinned snapshot
#   smoke_busy_threaded  a slow load call shows the busy state (log line headless)
#   emu_thread_start_fallback  a failed thread start runs inline (nothing to lift any more)
#   debug_on_threaded / gdb_enabled(_threaded)  nothing is off while threaded; the silent gdb host attaches at load
#   gdb_hardcore_locked_* / gdb_hardcore_flip_*  hardcore locks the stub to disabled at load; a mid-game flip reloads to apply it
#   tsan_canary           (TSan builds only) a deliberate race the sanitizer must catch

function(vj_emu_thread_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

add_executable(vj_frame_exchange_test tests/frame_exchange_test.cpp)
target_include_directories(vj_frame_exchange_test PRIVATE src)
target_link_libraries(vj_frame_exchange_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_frame_exchange_test)
add_test(NAME frame_exchange_unit COMMAND vj_frame_exchange_test)
set_tests_properties(frame_exchange_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_emu_commands_test tests/emu_commands_test.cpp)
target_include_directories(vj_emu_commands_test PRIVATE src)
target_link_libraries(vj_emu_commands_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_emu_commands_test)
add_test(NAME emu_commands_unit COMMAND vj_emu_commands_test)
set_tests_properties(emu_commands_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_pacing_clock_test tests/pacing_clock_test.cpp)
target_include_directories(vj_pacing_clock_test PRIVATE src)
vj_emu_thread_warnings(vj_pacing_clock_test)
add_test(NAME pacing_clock_unit COMMAND vj_pacing_clock_test)
set_tests_properties(pacing_clock_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_emu_thread_test tests/emu_thread_test.cpp src/emu_thread.cpp)
target_include_directories(vj_emu_thread_test PRIVATE src)
target_link_libraries(vj_emu_thread_test PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(vj_emu_thread_test PRIVATE winmm)   # timeBeginPeriod (the app gets it from SDL)
endif()
vj_emu_thread_warnings(vj_emu_thread_test)
add_test(NAME emu_thread_unit COMMAND vj_emu_thread_test)
set_tests_properties(emu_thread_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_input_snapshot_test tests/input_snapshot_test.cpp src/remap.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_input_snapshot_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_input_snapshot_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
vj_emu_thread_warnings(vj_input_snapshot_test)
add_test(NAME input_snapshot_unit COMMAND vj_input_snapshot_test)
set_tests_properties(input_snapshot_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_audio_out_test tests/audio_out_test.cpp src/audio.cpp)
target_include_directories(vj_audio_out_test PRIVATE src)
target_link_libraries(vj_audio_out_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
vj_emu_thread_warnings(vj_audio_out_test)
add_test(NAME audio_out_unit COMMAND vj_audio_out_test)
set_tests_properties(audio_out_unit PROPERTIES TIMEOUT 120 LABELS emu_thread SKIP_RETURN_CODE 77
                     FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_core_host_thread_test tests/core_host_thread_test.cpp src/core_host.cpp src/emu_thread.cpp src/paths.cpp)
target_include_directories(vj_core_host_thread_test PRIVATE src)
target_link_libraries(vj_core_host_thread_test PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_core_host_thread_test PRIVATE ws2_32 iphlpapi)
endif()
vj_emu_thread_warnings(vj_core_host_thread_test)
add_test(NAME core_host_thread COMMAND vj_core_host_thread_test "${VJ_ROMS}/yarc.j64")
set_tests_properties(core_host_thread PROPERTIES TIMEOUT 300 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_emu_thread_e2e tests/emu_thread_e2e.cpp src/core_host.cpp src/emu_thread.cpp src/remap.cpp
               src/config.cpp src/paths.cpp)
target_include_directories(vj_emu_thread_e2e PRIVATE src)
target_link_libraries(vj_emu_thread_e2e PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_emu_thread_e2e PRIVATE ws2_32 iphlpapi)
endif()
vj_emu_thread_warnings(vj_emu_thread_e2e)
add_test(NAME emu_thread_e2e COMMAND vj_emu_thread_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_e2e PROPERTIES TIMEOUT 600 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

# The Tools windows' data paths with the REAL core on the emulation thread (#967 phase 3a): the session wired as App wires it.
add_executable(vj_debug_views_threaded tests/debug_views_threaded.cpp ${VJ_DEBUG_SESSION_SRCS} src/core_host.cpp
               src/emu_thread.cpp src/paths.cpp)
target_include_directories(vj_debug_views_threaded PRIVATE src)
target_link_libraries(vj_debug_views_threaded PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_debug_views_threaded PRIVATE ws2_32 iphlpapi)
endif()
vj_emu_thread_warnings(vj_debug_views_threaded)
add_test(NAME debug_views_threaded
    COMMAND vj_debug_views_threaded "${VJ_ROMS}/yarc.j64" "${VJ_ROMS}/jagniccc.j64")
set_tests_properties(debug_views_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
    FAIL_REGULAR_EXPRESSION "FAIL |owner violation|ThreadSanitizer")

# The debugger halt with the REAL core on the emulation thread (#967 phase 3b): the rig is wired the way App wires it.
add_executable(vj_debug_e2e_threaded tests/debug_e2e_threaded.cpp ${VJ_DEBUG_SESSION_SRCS} src/core_host.cpp
               src/emu_thread.cpp src/paths.cpp)
target_include_directories(vj_debug_e2e_threaded PRIVATE src src/debug)
target_link_libraries(vj_debug_e2e_threaded PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_debug_e2e_threaded PRIVATE ws2_32 iphlpapi)
endif()
vj_emu_thread_warnings(vj_debug_e2e_threaded)
add_test(NAME debug_e2e_threaded COMMAND vj_debug_e2e_threaded "${VJ_ROMS}/yarc.j64")
set_tests_properties(debug_e2e_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
                     FAIL_REGULAR_EXPRESSION "FAIL |owner violation|ThreadSanitizer")

# 3b: a gdb client finds the app halted at boot (gdb_wait) with the emulation thread on; the app's silent host saw it,
# memory is served while halted, continue finishes the run, and a SIGTERM while gdb still holds the halt exits 0 within 3 s.
if(Python3_Interpreter_FOUND)
    add_test(NAME gdb_threaded_halt
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/../tests/gdb_threaded_halt.py"
                $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64")
    set_tests_properties(gdb_threaded_halt PROPERTIES TIMEOUT 120 LABELS emu_thread
        PASS_REGULAR_EXPRESSION "PASS: gdb halt on the emulation thread"
        FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer|did not stop within 3 s|did not answer within 3 s")
    # The same client script inline: the halt sits on the UI thread inside retro_run(), the headless loop releases an app halt
    # but keeps a gdb one, and a SIGTERM mid-halt still ends the run.
    add_test(NAME gdb_inline_halt
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/../tests/gdb_threaded_halt.py"
                $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64" off)
    set_tests_properties(gdb_inline_halt PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "PASS: gdb halt on the UI thread \\(inline\\)"
        FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer|did not stop within 3 s|did not answer within 3 s")
    # A gdb halt that ended must not stay "live" in the UI mirror: Achievements::pump retries the hardcore reload only when no
    # halt holds the core (the headless inline loop closes and syncs the halt after each frame).
    add_test(NAME gdb_inline_halt_hardcore
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/../tests/gdb_threaded_halt.py"
                $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64" off "${VJ_RA_FIX}")
    set_tests_properties(gdb_inline_halt_hardcore PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "PASS: gdb halt on the UI thread \\(inline\\), continue, then the hardcore reload"
        FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer")
else()
    message(STATUS "Python3 interpreter not found: gdb_threaded_halt, gdb_inline_halt and gdb_inline_halt_hardcore are not registered "
                   "(the headless SIGINT / SIGTERM handlers and the gdb-over-socket halt paths go untested)")
endif()

add_test(NAME emu_thread_flag_rejects COMMAND virtualjaguar --headless --emu-thread maybe "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_flag_rejects PROPERTIES LABELS emu_thread PASS_REGULAR_EXPRESSION "--emu-thread wants on or off"
    FAIL_REGULAR_EXPRESSION "ThreadSanitizer|owner violation")

add_executable(vj_perf_csv_test tests/perf_csv_test.cpp)
target_include_directories(vj_perf_csv_test PRIVATE src)
target_link_libraries(vj_perf_csv_test PRIVATE ${VJ_SDL3_TARGET})
vj_emu_thread_warnings(vj_perf_csv_test)
add_test(NAME perf_csv_unit COMMAND vj_perf_csv_test "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(perf_csv_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_perf_trace_test tests/perf_trace_test.cpp)
target_include_directories(vj_perf_trace_test PRIVATE src)
vj_emu_thread_warnings(vj_perf_trace_test)
add_test(NAME perf_trace_unit COMMAND vj_perf_trace_test "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(perf_trace_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

# The headless smoke tests (VJSmokeTests.cmake) again with --emu-thread on: the same hashes as inline, nothing on the
# UI thread calling into the core, and the run really threaded (the OK line says so only then).
set(_vj_threaded_ok "headless: OK \\([0-9]+ load\\(s\\), emulation thread\\)")
add_test(NAME smoke_yarc_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames ${VJ_SMOKE_FRAMES} ${_yarc_hash_args}
            "${VJ_ROMS}/yarc.j64")
add_test(NAME smoke_reload_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames ${VJ_SMOKE_FRAMES}
            "${VJ_ROMS}/yarc.j64" "${VJ_ROMS}/jagniccc.j64" "${VJ_ROMS}/yarc.j64")
add_test(NAME smoke_state_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 240 --state-roundtrip "${VJ_ROMS}/jagniccc.j64")
add_test(NAME smoke_boot_cd_bios_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 400 --boot-bios cd --expect-bios cd:builtin
            --expect-hash b83cb3ee)
add_test(NAME cheat_apply_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 60 --cheat 001F0000:AB --expect-ram 1F0000=AB
            "${VJ_ROMS}/yarc.j64")
set_tests_properties(smoke_yarc_threaded smoke_reload_threaded smoke_state_threaded smoke_boot_cd_bios_threaded
                     cheat_apply_threaded PROPERTIES
    TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "${_vj_threaded_ok}"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")

# A thread that fails to start (VJ_TEST_EMU_START_FAIL takes the real failure branch): the run falls back inline.  Nothing
# is switched off while threaded any more, so there is nothing to give back (the old "are back on" line must be gone).
add_test(NAME emu_thread_start_fallback
    COMMAND ${CMAKE_COMMAND} -E env VJ_TEST_EMU_START_FAIL=1
            $<TARGET_FILE:virtualjaguar> --headless --emu-thread on --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_start_fallback PROPERTIES TIMEOUT 120 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "could not start the emulation thread; running emulation on the UI thread.*headless: OK"
    FAIL_REGULAR_EXPRESSION "are back on|are off while|headless: FAIL|ThreadSanitizer|owner violation")

# The disc commands threaded: every one runs inside EmuThread::call (an insert re-stages the CD BIOS, which writes the
# staged-BIOS record), and the menus read the disc list from the pinned snapshot.
add_test(NAME smoke_disc_swap_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 120 --allow-blank
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/disc_swap_threaded_data" --disc-swap "${VJ_ROMS}/synth_jagcd_multi.chd" "${VJ_ROMS}/synth_jagcd.chd")
# Real-BIOS boot: every insert re-stages the built-in CD BIOS, the staged-BIOS write the call() routing exists for.
add_test(NAME smoke_disc_swap_bios_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 120 --allow-blank
            --option virtualjaguar_cd_boot_mode=bios --expect-bios cd:builtin
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/disc_swap_bios_threaded_data" --disc-swap "${VJ_ROMS}/synth_jagcd_multi.chd" "${VJ_ROMS}/synth_jagcd.chd")
set_tests_properties(smoke_disc_swap_threaded smoke_disc_swap_bios_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "DISC swap synth_jagcd\\.chd -> synth_jagcd_multi\\.chd -> synth_jagcd\\.chd \\(emulation thread\\)"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|nothing to insert|insert failed|owner violation|ThreadSanitizer")

# File > Boot BIOS > CD BIOS (no disc), then Add disc image... (#646), threaded: the menus read the empty list from
# the pinned snapshot, and the insert runs inside EmuThread::call.
add_test(NAME smoke_disc_insert_bios_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 120 --allow-blank --boot-bios cd
            --disc-insert "${VJ_ROMS}/synth_jagcd.chd")
set_tests_properties(smoke_disc_insert_bios_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "DISC insert synth_jagcd\\.chd into the empty tray \\(emulation thread\\): disk control: inserted '[^']*synth_jagcd\\.chd'"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|nothing to insert|insert failed|owner violation|ThreadSanitizer")

# The Pro Controller (#1017) through the app itself: a config naming it on Jaguar port 2 must hand the core
# virtualjaguar_p2_device = pad_pro after the load, inline and threaded (on the emulation thread refreshLocks() runs on the owner).  The data dirs are written at configure time;
# a headless run keeps input.port.1.
foreach(_m on off)
    set(_pd "${CMAKE_CURRENT_BINARY_DIR}/pro-controller-${_m}")
    file(MAKE_DIRECTORY "${_pd}")
    file(WRITE "${_pd}/config.ini" "input.port.1=65537\n")   # devLibretroId(DevKind::ProPad)
    add_test(NAME pro_controller_option_${_m}
        COMMAND virtualjaguar --headless --emu-thread ${_m} --frames 10 --verbose --data-dir "${_pd}" "${VJ_ROMS}/yarc.j64")
endforeach()
set_tests_properties(pro_controller_option_on PROPERTIES TIMEOUT 120 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "virtualjaguar_p2_device=pad_pro \\(locked\\).*${_vj_threaded_ok}"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
set_tests_properties(pro_controller_option_off PROPERTIES TIMEOUT 120
    PASS_REGULAR_EXPRESSION "virtualjaguar_p2_device=pad_pro \\(locked\\).*headless: OK"
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

# An .m3u set threaded: loadContent registers every disc on the emulation thread; the menus read the pinned snapshot.
add_test(NAME smoke_m3u_playlist_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 120 --allow-blank --expect-discs 2
            "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/synth_jagcd_set.m3u")
set_tests_properties(smoke_m3u_playlist_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "DISC list 2: synth_jagcd\\.chd, synth_jagcd_multi\\.chd; content synth_jagcd -> synth_jagcd_multi, save RAM synth_jagcd_set \\(emulation thread\\)"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|nothing to insert|insert failed|owner violation|ThreadSanitizer")

# 3b: nothing is switched off while threaded.  The old "are off" line must be gone, the thread on, the run OK.
add_test(NAME debug_on_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(debug_on_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\\[emu\\] emulation thread on.*${_vj_threaded_ok}"
    FAIL_REGULAR_EXPRESSION "are off while the emulation thread is on|ThreadSanitizer|owner violation")
# Decision 3: whenever the stub is enabled the app attaches a silent host behind every halt, inline and threaded.  Fixed,
# distinct ports: the typed option rejects 0, and a port the core cannot open disables the stub for the session (the FAIL
# regex catches that, so a collision is a failure, not a vacuous pass).
add_test(NAME gdb_enabled
    COMMAND virtualjaguar --headless --emu-thread off --frames 5 --option virtualjaguar_gdb_stub=enabled
            --option virtualjaguar_gdb_port=23459 "${VJ_ROMS}/yarc.j64")
set_tests_properties(gdb_enabled PROPERTIES TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\\[debug\\] gdb host attached \\(silent\\).*headless: OK"
    FAIL_REGULAR_EXPRESSION "locked by the app|rejected by the core|failed to open port|headless: FAIL")
add_test(NAME gdb_enabled_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 --option virtualjaguar_gdb_stub=enabled
            --option virtualjaguar_gdb_port=23457 "${VJ_ROMS}/yarc.j64")
set_tests_properties(gdb_enabled_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\\[debug\\] gdb host attached \\(silent\\).*${_vj_threaded_ok}"
    FAIL_REGULAR_EXPRESSION "locked by the app|rejected by the core|failed to open port|headless: FAIL|ThreadSanitizer|owner violation")
# Decision 2: RetroAchievements hardcore locks the stub to disabled at load.  A SAVED setting (config.ini), not --option:
# an --option on a locked key is reported as an error by design (checkOverrides).
foreach(_m off on)
    set(_gd "${CMAKE_CURRENT_BINARY_DIR}/gdb-hardcore-${_m}")
    file(MAKE_DIRECTORY "${_gd}")
    file(WRITE "${_gd}/config.ini" "opt.virtualjaguar_gdb_stub=enabled\n")
    add_test(NAME gdb_hardcore_locked_${_m}
        COMMAND virtualjaguar --headless --emu-thread ${_m} --frames 10 --verbose --data-dir "${_gd}"
                --ra-mock "${VJ_RA_FIX}" --ra-hardcore "${VJ_ROMS}/yarc.j64")
    set_tests_properties(gdb_hardcore_locked_${_m} PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "\\[app\\] option virtualjaguar_gdb_stub=disabled \\(locked\\).*headless: OK"
        FAIL_REGULAR_EXPRESSION "gdb host attached|headless: FAIL|ThreadSanitizer|owner violation")
    # A hardcore flip mid-game (late sign-in at frame 40) with the stub OPEN: the app reloads so the lock reaches the core
    # (it reads the stub options at load only).  The attach line proves the stub was open before the flip.
    set(_gf "${CMAKE_CURRENT_BINARY_DIR}/gdb-hardcore-flip-${_m}")
    file(MAKE_DIRECTORY "${_gf}")
    if(_m STREQUAL "on")
        file(WRITE "${_gf}/config.ini" "opt.virtualjaguar_gdb_stub=enabled\nopt.virtualjaguar_gdb_port=23458\n")
    else()
        file(WRITE "${_gf}/config.ini" "opt.virtualjaguar_gdb_stub=enabled\nopt.virtualjaguar_gdb_port=23461\n")
    endif()
    add_test(NAME gdb_hardcore_flip_${_m}
        COMMAND virtualjaguar --headless --emu-thread ${_m} --frames 120 --verbose --data-dir "${_gf}"
                --ra-mock "${VJ_RA_FIX}" --ra-hardcore --ra-login-at 40 --ra-expect-reset 1 "${VJ_ROMS}/yarc.j64")
    set_tests_properties(gdb_hardcore_flip_${_m} PROPERTIES TIMEOUT 180
        PASS_REGULAR_EXPRESSION "gdb host attached \\(silent\\).*\\[ach\\] hardcore on: reloading the game to close the GDB stub.*\\[app\\] option virtualjaguar_gdb_stub=disabled \\(locked\\).*headless: OK"
        FAIL_REGULAR_EXPRESSION "headless: FAIL|failed to open port|ThreadSanitizer|owner violation")
endforeach()
set_tests_properties(gdb_hardcore_locked_on gdb_hardcore_flip_on PROPERTIES LABELS emu_thread)

# The hardcore restart that gives up (the core halted again: a gdb breakpoint still armed) must stay pending and retry, not be
# dropped with hardcore on and the stub still open.  A headless run cannot stage that re-halt (the core only runs when the loop
# advances it), so VJ_TEST_RESTART_GIVEUPS=2 makes the first two restarts report what a give-up reports.  Pinned: the retry
# line once (not per pump), the reload line once, the stub closes in the end, and --ra-expect-reset 1 (one restart counted).
foreach(_m off on)
    set(_gg "${CMAKE_CURRENT_BINARY_DIR}/gdb-hardcore-giveup-${_m}")
    file(MAKE_DIRECTORY "${_gg}")
    if(_m STREQUAL "on")
        file(WRITE "${_gg}/config.ini" "opt.virtualjaguar_gdb_stub=enabled\nopt.virtualjaguar_gdb_port=23462\n")
    else()
        file(WRITE "${_gg}/config.ini" "opt.virtualjaguar_gdb_stub=enabled\nopt.virtualjaguar_gdb_port=23463\n")
    endif()
    add_test(NAME gdb_hardcore_flip_giveup_${_m}
        COMMAND virtualjaguar --headless --emu-thread ${_m} --frames 120 --verbose --data-dir "${_gg}"
                --ra-mock "${VJ_RA_FIX}" --ra-hardcore --ra-login-at 40 --ra-expect-reset 1 "${VJ_ROMS}/yarc.j64")
    set_tests_properties(gdb_hardcore_flip_giveup_${_m} PROPERTIES TIMEOUT 180 ENVIRONMENT "VJ_TEST_RESTART_GIVEUPS=2"
        PASS_REGULAR_EXPRESSION "gdb host attached \\(silent\\).*\\[ach\\] hardcore on: reloading the game to close the GDB stub.*\\[ach\\] hardcore on: the restart gave up \\(core halted\\), retrying.*\\[app\\] option virtualjaguar_gdb_stub=disabled \\(locked\\).*headless: OK"
        FAIL_REGULAR_EXPRESSION "headless: FAIL|failed to open port|ThreadSanitizer|owner violation|close the GDB stub.*close the GDB stub|gave up \\(core halted\\), retrying.*gave up \\(core halted\\), retrying")
endforeach()
# The same without the stub: hardcore turns on late, the plain reset gives up twice, and the third try restarts the game once.
foreach(_m off on)
    add_test(NAME ra_hardcore_reset_giveup_${_m}
        COMMAND virtualjaguar --headless --emu-thread ${_m} --frames 120 --ra-mock "${VJ_RA_FIX}" --ra-hardcore --ra-login-at 40
                --ra-expect-hardcore --ra-expect-reset 1 "${VJ_ROMS}/yarc.j64")
    set_tests_properties(ra_hardcore_reset_giveup_${_m} PROPERTIES TIMEOUT 180 ENVIRONMENT "VJ_TEST_RESTART_GIVEUPS=2"
        PASS_REGULAR_EXPRESSION "\\[ach\\] hardcore on: the restart gave up \\(core halted\\), retrying.*headless: OK"
        FAIL_REGULAR_EXPRESSION "headless: FAIL|ThreadSanitizer|owner violation|close the GDB stub|gave up \\(core halted\\), retrying.*gave up \\(core halted\\), retrying")
endforeach()
set_tests_properties(gdb_hardcore_flip_giveup_on ra_hardcore_reset_giveup_on PROPERTIES LABELS emu_thread)

add_executable(vj_late_latch_test tests/late_latch_test.cpp)
target_include_directories(vj_late_latch_test PRIVATE src)
vj_emu_thread_warnings(vj_late_latch_test)
add_test(NAME late_latch_unit COMMAND vj_late_latch_test)
set_tests_properties(late_latch_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_latch_waker_test tests/latch_waker_test.cpp)
target_include_directories(vj_latch_waker_test PRIVATE src)
target_link_libraries(vj_latch_waker_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
vj_emu_thread_warnings(vj_latch_waker_test)
add_test(NAME latch_waker_unit COMMAND vj_latch_waker_test)
set_tests_properties(latch_waker_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_core_events_test tests/core_events_test.cpp)
target_include_directories(vj_core_events_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_core_events_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_core_events_test)
add_test(NAME core_events_unit COMMAND vj_core_events_test)
set_tests_properties(core_events_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

if(VJ_APP_TSAN)
    add_executable(vj_tsan_canary tests/tsan_canary.cpp)
    target_link_libraries(vj_tsan_canary PRIVATE Threads::Threads)
    # abort_on_error=0: ThreadSanitizer on macOS aborts (SIGABRT) by default, and ctest does not invert a signal death
    # for WILL_FAIL; with 0 it exits 66 like on Linux, so the canary reads the same everywhere.
    add_test(NAME tsan_canary
        COMMAND ${CMAKE_COMMAND} -E env TSAN_OPTIONS=halt_on_error=1:abort_on_error=0 $<TARGET_FILE:vj_tsan_canary>)
    set_tests_properties(tsan_canary PROPERTIES WILL_FAIL TRUE LABELS emu_thread TIMEOUT 60)
endif()

# A load call slower than 250 ms (VJ_TEST_SLOW_CALL_MS stalls it on the emulation thread) runs the wait hook on the UI
# thread; headless, that logs once (windowed, it swaps the spinner frame).
add_test(NAME smoke_busy_threaded
    COMMAND ${CMAKE_COMMAND} -E env VJ_TEST_SLOW_CALL_MS=400
            $<TARGET_FILE:virtualjaguar> --headless --emu-thread on --frames 30 "${VJ_ROMS}/yarc.j64")
set_tests_properties(smoke_busy_threaded PROPERTIES TIMEOUT 120 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "\\[emu\\] busy: a call has taken over 250 ms"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")

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
#   smoke_*_threaded      the headless smoke tests with --emu-thread on: the inline hashes, and no owner violation
#   smoke_disc_swap_threaded  File > Disc on a loaded CD through EmuThread::call, read back from the pinned snapshot
#   smoke_disc_swap_bios_threaded  the same in real-BIOS boot mode, where each insert re-stages the CD BIOS
#   smoke_disc_insert_bios_threaded  the CD BIOS with no disc: Add disc image... inserts the first disc through call()
#   smoke_m3u_playlist_threaded  an .m3u's discs registered inside EmuThread::call, read back from the pinned snapshot
#   smoke_busy_threaded  a slow load call shows the busy state (log line headless)
#   emu_thread_start_fallback  a failed thread start lifts the halt/GDB limits and runs inline
#   emu_thread_limits  the halt and the GDB stub are off while threaded; the Tools windows work
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

# A thread that fails to start (VJ_TEST_EMU_START_FAIL takes the real failure branch): the run falls back inline and the
# debugger halt and the GDB stub come back on.
add_test(NAME emu_thread_start_fallback
    COMMAND ${CMAKE_COMMAND} -E env VJ_TEST_EMU_START_FAIL=1
            $<TARGET_FILE:virtualjaguar> --headless --emu-thread on --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_start_fallback PROPERTIES TIMEOUT 120 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "the debugger halt and the GDB stub are back on .the emulation thread did not start."
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")

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
# virtualjaguar_p2_device = pad_pro after the load, inline and threaded (on the emulation thread the option lock is
# limitFeaturesForEmuThread()'s, and refreshLocks() runs on the owner).  The data dirs are written at configure time;
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

add_test(NAME emu_thread_limits
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_limits PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "the debugger halt and the GDB stub are off while the emulation thread is on \\(the Tools windows work\\)"
    FAIL_REGULAR_EXPRESSION "ThreadSanitizer|owner violation")
add_test(NAME gdb_locked_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 5 --option virtualjaguar_gdb_stub=enabled
            "${VJ_ROMS}/yarc.j64")
set_tests_properties(gdb_locked_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "managed by the app.*rejected by the core"
    FAIL_REGULAR_EXPRESSION "ThreadSanitizer|owner violation")

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

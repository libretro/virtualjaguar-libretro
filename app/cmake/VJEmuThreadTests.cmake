# Emulation thread (#967, docs/app-emulation-thread-design.md, docs/app-emulation-thread-plan-phase1.md).
# Every test here carries the label `emu_thread`: the ThreadSanitizer job in .github/workflows/app.yml runs exactly
# these (ctest -L emu_thread).
#
#   frame_exchange_unit   FrameExchange: newest wins, the reader's slot is never written, no tearing, content epochs
#   emu_commands_unit     EmuCommands: FIFO per producer, timed waits, close-and-drain, errors
#   pacing_clock_unit     PacingClock mode A against a fake clock and a fake 512-frame-chunk audio device
#   emu_thread_unit       EmuThread: inline vs threaded, call/post ordering, pause/advance, stop/join timeout, shutdown races
#   input_snapshot_unit   InputSnapshot: newest state, pointer motion exactly once, event timestamps, takenSince
#   audio_out_unit        AudioOut under the SDL dummy driver: the mode-A hooks and cross-thread use
#   core_host_thread      CoreHost with an owner thread and the REAL core: snapshot reads, queued writes, refused calls

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

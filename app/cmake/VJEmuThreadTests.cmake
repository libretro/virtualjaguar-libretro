# Emulation thread (#967, docs/app-emulation-thread-design.md, docs/app-emulation-thread-plan-phase1.md).
# Every test here carries the label `emu_thread`: the ThreadSanitizer job in .github/workflows/app.yml runs exactly
# these (ctest -L emu_thread).
#
#   frame_exchange_unit   FrameExchange: newest wins, the reader's slot is never written, no tearing, content epochs
#   emu_commands_unit     EmuCommands: FIFO per producer, timed waits, close-and-drain, errors
#   pacing_clock_unit     PacingClock mode A against a fake clock and a fake 512-frame-chunk audio device
#   emu_thread_unit       EmuThread: inline vs threaded, call/post ordering, pause/advance, stop/join timeout, shutdown races

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

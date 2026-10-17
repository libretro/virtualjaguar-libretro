# Neuro API client tests (docs/neuro.md).  No window, no GL, no internet, no Neuro.
#
#   neuro_unit      JSON, WebSocket framing / handshake / URLs, the input queue, the controller and profile
#                   actions and their validation, profile files, and the protocol against a fake server.
#   neuro_loopback  a WebSocket server on a free 127.0.0.1 port: startup, register, force, action, result,
#                   the buttons played out frame by frame, reregister_all, and the reconnect.  Skipped (77)
#                   on Windows.
#   neuro_off_threaded  --emu-thread on: Neuro stays off for the run and says so.
add_executable(vj_neuro_test tests/neuro_test.cpp ${VJ_NEURO_SOURCES})
target_include_directories(vj_neuro_test PRIVATE src src/neuro)
target_link_libraries(vj_neuro_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_neuro_test PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(vj_neuro_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_neuro_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_neuro_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME neuro_unit COMMAND vj_neuro_test unit)
add_test(NAME neuro_loopback COMMAND vj_neuro_test loopback)
set_tests_properties(neuro_unit neuro_loopback PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")
set_tests_properties(neuro_loopback PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

# #967 phase 1: with the emulation thread on, Neuro stays off (the input poll and RAM are that thread's).
add_test(NAME neuro_off_threaded
    COMMAND virtualjaguar --headless --emu-thread on --neuro ws://127.0.0.1:9 --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(neuro_off_threaded PROPERTIES LABELS emu_thread TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\\[neuro\\] off for this run: the emulation thread is on"
    FAIL_REGULAR_EXPRESSION "ThreadSanitizer|owner violation")

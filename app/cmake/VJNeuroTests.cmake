# Neuro API client tests (docs/neuro.md).  No window, no GL, no internet, no Neuro.
#
#   neuro_unit      JSON, WebSocket framing / handshake / URLs, the input queue, the controller and profile
#                   actions and their validation, profile files, and the protocol against a fake server.
#   neuro_loopback  a WebSocket server on a free 127.0.0.1 port: startup, register, force, action, result,
#                   the buttons played out frame by frame, reregister_all, and the reconnect.  Skipped (77)
#                   on Windows.
#   neuro_core_e2e  the REAL core: the replica (AgentPlayer) presses exactly N polls per step, inline, on the emulation
#                   thread in lock-step, and free-running at 600 Hz; a disabled program presses nothing.
#   neuro_app_inline  the real app, headless, --neuro against a fake server: a 30-frame press is played and reported.
#   neuro_watch_inline  a profile watch read back from RAM and printed in the run's state line.
#   With the emulation thread on (#967 phase 3c) Neuro works; the twins of the three above carry LABELS emu_thread, so the
#   ThreadSanitizer job runs them:
#   neuro_loopback_threaded  the loopback run with the input poll on its own thread; the frame-numbered press record must
#                   equal the inline run's, and a dropped link / hardcore must let go of every button.
#   neuro_on_threaded  neuro_app_inline with --emu-thread on: the same 30 frames played.
#   neuro_watch_threaded  neuro_watch_inline with --emu-thread on: the same cheat byte read back.
add_executable(vj_neuro_test tests/neuro_test.cpp src/remap.cpp src/config.cpp src/paths.cpp src/debug/debug_views.cpp
               ${VJ_NEURO_SOURCES})
target_include_directories(vj_neuro_test PRIVATE src src/neuro src/debug "${VJ_ROOT}/src/debug"
                           $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
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

# The replica against the REAL core (#967 phase 3c).  (VJEmuThreadTests.cmake, which defines vj_emu_thread_warnings, is
# included after this file, so the same flags are set here.)
add_executable(vj_neuro_core_e2e tests/neuro_core_e2e.cpp src/core_host.cpp src/emu_thread.cpp src/remap.cpp src/config.cpp
               src/paths.cpp src/neuro/neuro_actions.cpp src/neuro/neuro_json.cpp)
target_include_directories(vj_neuro_core_e2e PRIVATE src src/neuro)
target_link_libraries(vj_neuro_core_e2e PRIVATE vj_core ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_neuro_core_e2e PRIVATE ws2_32 iphlpapi)
endif()
if(MSVC)
    target_compile_options(vj_neuro_core_e2e PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_neuro_core_e2e PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_neuro_core_e2e PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME neuro_core_e2e COMMAND vj_neuro_core_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(neuro_core_e2e PROPERTIES TIMEOUT 600 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

# The real app, headless, inline, against a fake Neuro server.
add_dependencies(vj_neuro_test virtualjaguar)
add_test(NAME neuro_app_inline COMMAND vj_neuro_test app $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64" off)
set_tests_properties(neuro_app_inline PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
    FAIL_REGULAR_EXPRESSION "FAIL |ThreadSanitizer|owner violation")

# #967 phase 3c: Neuro's RAM watches reach the state text through a DebugViews request.  The two watches are 1 MiB apart,
# so this run takes the Kind::Dram path (span > 64 KiB), which also pins that no watch is dropped.  The profile
# directory is written at configure time, like the pro_controller_option_* data dirs.
set(_nw "${CMAKE_CURRENT_BINARY_DIR}/neuro-watch-data")
file(MAKE_DIRECTORY "${_nw}/neuro")
file(WRITE "${_nw}/neuro/yarc.json" "{\"game_name\":\"Yarc\",\"titles\":[\"yarc\"],\"watch\":[{\"name\":\"cheat byte\",\"address\":\"0x1F0000\",\"size\":1,\"format\":\"hex\"},{\"name\":\"far byte\",\"address\":\"0x100000\",\"size\":1}]}")
add_test(NAME neuro_watch_inline COMMAND virtualjaguar --headless --emu-thread off --frames 90 --data-dir "${_nw}"
    --cheat 001F0000:AB --neuro ws://127.0.0.1:9 "${VJ_ROMS}/yarc.j64")
set_tests_properties(neuro_watch_inline PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "\\[neuro\\] state: .*cheat byte: 0xAB"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")

# #967 phase 3c: with the emulation thread on, Neuro works.  The twins of the inline tests above, frame for frame.
add_test(NAME neuro_on_threaded COMMAND vj_neuro_test app $<TARGET_FILE:virtualjaguar> "${VJ_ROMS}/yarc.j64" on)
add_test(NAME neuro_watch_threaded COMMAND virtualjaguar --headless --emu-thread on --frames 90 --data-dir "${_nw}"
    --cheat 001F0000:AB --neuro ws://127.0.0.1:9 "${VJ_ROMS}/yarc.j64")
add_test(NAME neuro_loopback_threaded COMMAND vj_neuro_test loopback-threaded)
set_tests_properties(neuro_on_threaded neuro_loopback_threaded PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
set_tests_properties(neuro_on_threaded neuro_watch_threaded neuro_loopback_threaded PROPERTIES LABELS emu_thread
    FAIL_REGULAR_EXPRESSION "FAIL |headless: FAIL|owner violation|ThreadSanitizer|off for this run")
set_tests_properties(neuro_watch_threaded PROPERTIES TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\\[neuro\\] state: .*cheat byte: 0xAB")

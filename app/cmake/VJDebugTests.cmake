# Debug-tools tests (#652 / #833).  All headless.
#
#   debug_session_unit   DebugSession (app/src/debug) against the mock core interface
#                        (app/tests/debug_mock.h): when the tools are available, attach / halt /
#                        resume / step through the nested pump, the breakpoint list, the poke policy,
#                        the refresh cadence, the Tools registry.  No window, no core.
add_executable(vj_debug_session_test
    tests/debug_session_test.cpp src/debug/debug_session.cpp)
target_include_directories(vj_debug_session_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
if(MSVC)
    target_compile_options(vj_debug_session_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_session_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_session_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_session_unit COMMAND vj_debug_session_test)
set_tests_properties(debug_session_unit PROPERTIES TIMEOUT 60)

#   debug_e2e            the REAL core through CoreHost and DebugSession: the interface is handed over only
#                        when VJ_CAP_DEBUG is announced; "break now" parks the 68000 inside retro_run() and the
#                        session's nested UI frame runs; CoreHost refuses everything that would re-enter the
#                        core while parked (state load, reset, another load, option changes); step, continue,
#                        a breakpoint at a PC the game revisits; hardcore switched on from inside a halt
#                        resumes and detaches; a second game loads cleanly afterwards.
add_executable(vj_debug_e2e
    tests/debug_e2e.cpp src/debug/debug_session.cpp src/core_host.cpp src/paths.cpp)
target_include_directories(vj_debug_e2e PRIVATE src)
target_link_libraries(vj_debug_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_debug_e2e PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_e2e PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_e2e PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_e2e COMMAND vj_debug_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(debug_e2e PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "FAIL ")

#   debug_texture_unit   Textures window logic (texture_pack.*): manifest parse on a synthetic dump folder,
#                        pack install path + size rules, the replace flow's reload call on the mock, thumbnail LRU.
#   debug_blitter_unit   Blitter inspector + audio scope logic (blitter_log.*, audio_ring.*): B_CMD / flags /
#                        count decoding, the register table, the launch-ring tracker's wrap, the audio ring.
foreach(_t texture blitter)
    if(_t STREQUAL "texture")
        set(_srcs tests/debug_texture_test.cpp src/debug/texture_pack.cpp)
    else()
        set(_srcs tests/debug_blitter_test.cpp src/debug/blitter_log.cpp src/debug/audio_ring.cpp)
    endif()
    add_executable(vj_debug_${_t}_test ${_srcs})
    target_include_directories(vj_debug_${_t}_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
    if(MSVC)
        target_compile_options(vj_debug_${_t}_test PRIVATE /W3 /utf-8)
        target_compile_definitions(vj_debug_${_t}_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(vj_debug_${_t}_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME debug_${_t}_unit COMMAND vj_debug_${_t}_test)
    set_tests_properties(debug_${_t}_unit PROPERTIES TIMEOUT 60)
endforeach()

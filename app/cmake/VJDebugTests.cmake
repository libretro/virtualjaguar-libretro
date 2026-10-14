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

#   debug_scan_unit      the cheat scanner (cheat_scan.*, scanner_actions.*): every mode at every width /
#                        signedness / alignment against a brute-force reference, narrowing and undo, and
#                        the way a result becomes a cheat -- the code replayed through the core's own
#                        cheat_parse_one / cheat_list_apply, hardcore keeping it out.  Mock core, no window.
add_executable(vj_debug_scan_test
    tests/debug_scan_test.cpp src/debug/cheat_scan.cpp src/debug/scanner_actions.cpp
    src/debug/debug_session.cpp src/cheats.cpp src/config.cpp src/paths.cpp
    "${VJ_ROOT}/src/core/cheat.c")
target_include_directories(vj_debug_scan_test PRIVATE src src/debug tests "${VJ_ROOT}/src/debug"
    "${VJ_ROOT}/src/core" "${VJ_ROOT}/libretro-common/include")
target_link_libraries(vj_debug_scan_test PRIVATE ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_debug_scan_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_scan_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_scan_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_scan_unit COMMAND vj_debug_scan_test)
set_tests_properties(debug_scan_unit PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")

#   debug_scan_e2e       the scanner against the REAL core (headless, the public yarc.j64): find a counter by
#                        Changed / Increased / Increased-by narrowing over a few frames, keep it as a cheat
#                        through the Cheats window's CheatManager, and the core holds it; with no cheat, with
#                        the cheat switched off, and under a hardcore block it keeps counting.
add_executable(vj_debug_scan_e2e
    tests/debug_scan_e2e.cpp src/debug/cheat_scan.cpp src/debug/scanner_actions.cpp src/debug/debug_session.cpp
    src/cheats.cpp src/config.cpp src/core_host.cpp src/paths.cpp)
target_include_directories(vj_debug_scan_e2e PRIVATE src src/debug tests "${VJ_ROOT}/src/debug")
target_link_libraries(vj_debug_scan_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_debug_scan_e2e PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_scan_e2e PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_scan_e2e PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_scan_e2e COMMAND vj_debug_scan_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(debug_scan_e2e PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "FAIL ")

#   debug_scan_ui_smoke  the scanner window drawn headlessly (an ImGui context with no renderer) over the mock
#                        core interface, through every state of the window: disabled, idle, 1 M candidates,
#                        narrowed, a value edited with Enter, the Add-as-cheat popup, undone, hardcore.
add_executable(vj_debug_scan_ui_smoke
    tests/debug_scan_ui_smoke.cpp src/debug/scanner_ui.cpp src/debug/cheat_scan.cpp src/debug/scanner_actions.cpp
    src/debug/debug_session.cpp src/cheats.cpp src/config.cpp src/core_host.cpp src/paths.cpp)
target_include_directories(vj_debug_scan_ui_smoke PRIVATE src src/debug tests "${VJ_ROOT}/src/debug")
target_link_libraries(vj_debug_scan_ui_smoke PRIVATE vj_core vj_imgui ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_debug_scan_ui_smoke PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_scan_ui_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_scan_ui_smoke PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_scan_ui_smoke COMMAND vj_debug_scan_ui_smoke)
set_tests_properties(debug_scan_ui_smoke PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")

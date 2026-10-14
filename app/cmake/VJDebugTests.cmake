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

#   debug_memory_unit    Memory viewer / CLUT logic (src/debug/memory_view.*) against the same mock: big-endian
#                        formatting, region clamps, go-to, change marks, incremental search, the write policy
#                        (checked against what the interface really does), the CLUT mirror, CRY / RGB16 maths.
add_executable(vj_debug_memory_test
    tests/debug_memory_test.cpp src/debug/memory_view.cpp src/debug/debug_session.cpp)
target_include_directories(vj_debug_memory_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
if(MSVC)
    target_compile_options(vj_debug_memory_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_memory_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_memory_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_memory_unit COMMAND vj_debug_memory_test)
set_tests_properties(debug_memory_unit PROPERTIES TIMEOUT 60)

#   debug_memory_ui_smoke  The Memory / Palette windows (memory_ui.cpp, clut_ui.cpp) run as real ImGui frames
#                          without a renderer against the mock, driven by synthetic key events: a typed hex
#                          value reaches the interface, ROM / register / B_CMD edits are refused before it.
add_executable(vj_debug_memory_ui_test
    tests/debug_memory_ui_test.cpp src/debug/memory_ui.cpp src/debug/clut_ui.cpp src/debug/memory_view.cpp
    src/debug/debug_session.cpp src/cheats.cpp src/config.cpp src/paths.cpp "${VJ_ROOT}/src/core/cheat.c")
target_include_directories(vj_debug_memory_ui_test PRIVATE src src/debug tests "${VJ_ROOT}/src/debug"
    "${VJ_ROOT}/src/core" "${VJ_ROOT}/libretro-common/include")
target_link_libraries(vj_debug_memory_ui_test PRIVATE vj_imgui ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_debug_memory_ui_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_memory_ui_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_memory_ui_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_memory_ui_smoke COMMAND vj_debug_memory_ui_test)
set_tests_properties(debug_memory_ui_smoke PROPERTIES TIMEOUT 60)

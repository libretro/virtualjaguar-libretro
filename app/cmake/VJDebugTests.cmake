# Debug-tools tests (#652 / #833).  All headless.
#
# #967 3a: a window smoke test = one ImGui frame loop over the mock core.  `target` / `test` name the executable and the
# ctest entry; the remaining arguments are the test's own sources (the session, the cheat manager and the theme come along).
function(vj_add_debug_ui_smoke target test)
    add_executable(${target} ${ARGN} ${VJ_DEBUG_SESSION_SRCS} src/cheats.cpp src/config.cpp src/paths.cpp
        "${VJ_ROOT}/src/core/cheat.c" src/theme.cpp src/theme_style.cpp src/theme_custom.cpp)
    target_include_directories(${target} PRIVATE src src/debug tests "${VJ_ROOT}/src/debug" "${VJ_ROOT}/src/core"
        "${VJ_ROOT}/libretro-common/include")
    target_link_libraries(${target} PRIVATE vj_imgui ${VJ_SDL3_TARGET})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME ${test} COMMAND ${target})
    set_tests_properties(${test} PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL " LABELS emu_thread)
endfunction()

#   debug_session_unit   DebugSession (app/src/debug) against the mock core interface
#                        (app/tests/debug_mock.h): when the tools are available, attach / halt /
#                        resume / step through the nested pump, the breakpoint list, the poke policy,
#                        the refresh cadence, the Tools registry.  No window, no core.
add_executable(vj_debug_session_test
    tests/debug_session_test.cpp ${VJ_DEBUG_SESSION_SRCS})
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
    tests/debug_e2e.cpp ${VJ_DEBUG_SESSION_SRCS} src/core_host.cpp src/paths.cpp)
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

#   debug_memory_unit    Memory viewer / CLUT logic (src/debug/memory_view.*) against the same mock: big-endian
#                        formatting, region clamps, go-to, change marks, incremental search, the write policy
#                        (checked against what the interface really does), the CLUT mirror, CRY / RGB16 maths.
add_executable(vj_debug_memory_test
    tests/debug_memory_test.cpp src/debug/memory_view.cpp ${VJ_DEBUG_SESSION_SRCS})
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
    ${VJ_DEBUG_SESSION_SRCS} src/cheats.cpp src/config.cpp src/paths.cpp "${VJ_ROOT}/src/core/cheat.c"
    src/theme.cpp src/theme_style.cpp src/theme_custom.cpp)
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

#   debug_scan_unit      the cheat scanner (cheat_scan.*, scanner_actions.*): every mode at every width /
#                        signedness / alignment against a brute-force reference, narrowing and undo, and
#                        the way a result becomes a cheat -- the code replayed through the core's own
#                        cheat_parse_one / cheat_list_apply, hardcore keeping it out.  Mock core, no window.
add_executable(vj_debug_scan_test
    tests/debug_scan_test.cpp src/debug/cheat_scan.cpp src/debug/scanner_actions.cpp
    ${VJ_DEBUG_SESSION_SRCS} src/cheats.cpp src/config.cpp src/paths.cpp
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
    tests/debug_scan_e2e.cpp src/debug/cheat_scan.cpp src/debug/scanner_actions.cpp ${VJ_DEBUG_SESSION_SRCS}
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
    ${VJ_DEBUG_SESSION_SRCS} src/cheats.cpp src/config.cpp src/core_host.cpp src/paths.cpp
    src/theme.cpp src/theme_style.cpp src/theme_custom.cpp)
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
    target_link_libraries(vj_debug_${_t}_test PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(vj_debug_${_t}_test PRIVATE /W3 /utf-8)
        target_compile_definitions(vj_debug_${_t}_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(vj_debug_${_t}_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME debug_${_t}_unit COMMAND vj_debug_${_t}_test)
    set_tests_properties(debug_${_t}_unit PROPERTIES TIMEOUT 60)
endforeach()
set_tests_properties(debug_blitter_unit PROPERTIES LABELS emu_thread)

#   debug_op_unit        Object list tool (app/src/debug/op_decode, op_render, op_view): the field decoder
#                        against JTRM-layout fixtures and op_list_dump.c's formulas, the bitmap renderer
#                        and pixel editor against the mock's 8 bpp object + CLUT and synthetic 1/2/4/8/16/24
#                        bpp bitmaps, the snapshot lifecycle, the screen mapping, PNG export.
add_executable(vj_debug_op_test
    tests/debug_op_test.cpp src/debug/op_decode.cpp src/debug/op_render.cpp src/debug/op_view.cpp
    ${VJ_DEBUG_SESSION_SRCS})
target_include_directories(vj_debug_op_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
target_link_libraries(vj_debug_op_test PRIVATE ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_debug_op_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_op_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_op_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_op_unit COMMAND vj_debug_op_test)
set_tests_properties(debug_op_unit PROPERTIES TIMEOUT 60)

#   debug_op_e2e         the Object list tool against the REAL core on a public ROM (yarc): arm the snapshot,
#                        run a frame, decode the list the OP walked (a stop object ends it, bitmap objects are
#                        drawable), render a bitmap from DRAM, export it as a PNG, paint a pixel and see it in RAM.
add_executable(vj_debug_op_e2e
    tests/debug_op_e2e.cpp src/debug/op_decode.cpp src/debug/op_render.cpp src/debug/op_view.cpp
    ${VJ_DEBUG_SESSION_SRCS} src/core_host.cpp src/paths.cpp)
target_include_directories(vj_debug_op_e2e PRIVATE src)
target_link_libraries(vj_debug_op_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_debug_op_e2e PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_op_e2e PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_op_e2e PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_op_e2e COMMAND vj_debug_op_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(debug_op_e2e PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "FAIL ")

#   debug_views_unit     DebugViews (debug_views.*): the request table and double buffers the emulation thread fills
#                        and the windows read -- clamps, per-kind fills against the mock, serials, a changed request
#                        invalidating its buffer, the epoch across a content switch, and a concurrent set/fill/read.
add_executable(vj_debug_views_test tests/debug_views_test.cpp src/debug/debug_views.cpp)
target_include_directories(vj_debug_views_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
target_link_libraries(vj_debug_views_test PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(vj_debug_views_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_views_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_views_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_views_unit COMMAND vj_debug_views_test)
set_tests_properties(debug_views_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

#   debug_core_job_unit  CoreJob (core_job.h): a job posted to the emulation thread, its result taken later (newest wins).
add_executable(vj_debug_core_job_test tests/debug_core_job_test.cpp ${VJ_DEBUG_SESSION_SRCS})
target_include_directories(vj_debug_core_job_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
target_link_libraries(vj_debug_core_job_test PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(vj_debug_core_job_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_core_job_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_core_job_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_core_job_unit COMMAND vj_debug_core_job_test)
set_tests_properties(debug_core_job_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

#   debug_cpu_ui_smoke   the CPU window (cpu_ui.cpp) drawn headlessly over the mock: registers and listing from
#                        published buffers, idle frames free of core calls, the Break/Step-greyed threaded state.
vj_add_debug_ui_smoke(vj_debug_cpu_ui_smoke debug_cpu_ui_smoke tests/debug_cpu_ui_smoke.cpp src/debug/cpu_ui.cpp)
target_link_libraries(vj_debug_cpu_ui_smoke PRIVATE vj_debug_cpu)

# #967 3a: the tool windows never touch vj_debug_iface; every window task adds its files to this list.
set(VJ_NO_IFACE_FILES memory_ui.cpp clut_ui.cpp cpu_ui.cpp op_ui.cpp)
add_test(NAME debug_no_direct_iface COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/src/debug
         "-DFILES=${VJ_NO_IFACE_FILES}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoDirectIface.cmake)
set_tests_properties(debug_no_direct_iface PROPERTIES LABELS emu_thread)

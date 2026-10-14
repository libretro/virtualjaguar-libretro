# CPU debugger (debug tools WP4, #652 / #833): 68000 / GPU / DSP registers, disassembly, breakpoints, stepping.
#
#   vj_musashi_dasm   the vendored Musashi 68000 disassembler (third_party/musashi, MIT).  C, app only: it is
#                     not part of the C89 core build.  Third-party code: warnings off.
#   vj_debug_cpu      cpu_view.cpp (the logic, no ImGui) over it; the app and the tests link this.
#   cpu_ui.cpp        the ImGui window, compiled into the app itself.
#
# Tests:  debug_cpu_unit  (logic + Musashi on fixed opcodes, against the mock core)
#         debug_cpu_e2e   (the REAL core on yarc: break at the 68000 entry, step, a breakpoint, step over, run to
#                          cursor, a GPU breakpoint, detach)
add_library(vj_musashi_dasm STATIC
    third_party/musashi/m68kdasm.c
    third_party/musashi/vj_m68kdasm.c)
target_include_directories(vj_musashi_dasm PUBLIC third_party/musashi)
if(MSVC)
    target_compile_options(vj_musashi_dasm PRIVATE /w)
else()
    target_compile_options(vj_musashi_dasm PRIVATE -w)
endif()

add_library(vj_debug_cpu STATIC src/debug/cpu_view.cpp)
target_include_directories(vj_debug_cpu PUBLIC src/debug "${VJ_ROOT}/src/debug")
target_link_libraries(vj_debug_cpu PUBLIC vj_musashi_dasm)
if(MSVC)
    target_compile_options(vj_debug_cpu PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_cpu PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_cpu PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

target_sources(virtualjaguar PRIVATE src/debug/cpu_ui.cpp)
target_link_libraries(virtualjaguar PRIVATE vj_debug_cpu)

if(VJ_APP_BUILD_TESTS)
    enable_testing()

    add_executable(vj_debug_cpu_test tests/debug_cpu_test.cpp src/debug/debug_session.cpp)
    target_include_directories(vj_debug_cpu_test PRIVATE tests)
    target_link_libraries(vj_debug_cpu_test PRIVATE vj_debug_cpu)
    if(MSVC)
        target_compile_options(vj_debug_cpu_test PRIVATE /W3 /utf-8)
        target_compile_definitions(vj_debug_cpu_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(vj_debug_cpu_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME debug_cpu_unit COMMAND vj_debug_cpu_test)
    set_tests_properties(debug_cpu_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

    add_executable(vj_debug_cpu_e2e
        tests/debug_cpu_e2e.cpp src/debug/debug_session.cpp src/core_host.cpp src/paths.cpp)
    target_include_directories(vj_debug_cpu_e2e PRIVATE src)
    target_link_libraries(vj_debug_cpu_e2e PRIVATE vj_core vj_debug_cpu ${VJ_SDL3_TARGET})
    if(MSVC)
        target_compile_options(vj_debug_cpu_e2e PRIVATE /W3 /utf-8)
        target_compile_definitions(vj_debug_cpu_e2e PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(vj_debug_cpu_e2e PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME debug_cpu_e2e COMMAND vj_debug_cpu_e2e "${VJ_ROOT}/test/roms/yarc.j64")
    set_tests_properties(debug_cpu_e2e PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "FAIL ")
endif()

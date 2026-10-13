# Headless smoke tests.  They run the real binary with SDL's dummy video and
# audio drivers: no window, no GL, so they work on CI runners with no display.
#
#   smoke_yarc      boot the in-tree public ROM (test/roms/yarc.j64) under HLE
#                   with no BIOS, run N frames, and check the final frame's
#                   FNV-1a hash (same hash the test harness computes).
#   smoke_reload    yarc -> jagniccc -> yarc in ONE process, delivered as
#                   drop events.  Fails unless the two yarc runs end on the same
#                   frame, i.e. unless loading a different game in between left
#                   no static state behind (the iOS-style reset path).
#   smoke_state     save a state (file + thumbnail PNG), run on, load it back, run
#                   on again: both runs must end on the same frame.
#   smoke_bad_option  an --option key the core never registered must be REJECTED
#                   (it would otherwise silently run the default).
#   smoke_cd        the synthetic Jaguar CD image, path-loaded (no BIOS).
#   remap_unit      controller remapping logic (test/remap_test.cpp): binding
#                   serialisation, profile / per-game override precedence, the
#                   config round trip, keypad -> core-input translation, the
#                   keypad overlay, bind capture, content identity.  No core.
#   input_e2e       the same translation driving the REAL core (test/input_e2e.cpp):
#                   every Jaguar control on both ports, incl. keypad 7-9 * #,
#                   must set exactly its joypad-matrix slot.
#
# The expected hash is a single value for every OS and CPU: the core is meant to
# be deterministic (run-ahead and netplay depend on it) and the SIMD blitter
# variants are checked bit-exact against the scalar reference by
# test/test_blitter_simd.  VERIFIED so far: macOS arm64 (NEON blitter) and
# macOS x86_64 under Rosetta (SSE2 blitter) -- see the PR notes; Linux and
# Windows are exercised by .github/workflows/app.yml.  If a platform ever
# disagrees, that is a core determinism finding, not a reason for a per-OS
# golden value.
# Regenerate with:  virtualjaguar --headless --frames 300 test/roms/yarc.j64

set(VJ_ROMS "${VJ_ROOT}/test/roms")
set(VJ_SMOKE_FRAMES 300 CACHE STRING "Frames per game in the smoke tests")
set(VJ_SMOKE_YARC_HASH "f4c30ea3" CACHE STRING "Expected final-frame hash of yarc.j64 (hex); empty disables the check")

if(VJ_SMOKE_YARC_HASH)
    set(_yarc_hash_args --expect-hash ${VJ_SMOKE_YARC_HASH})
else()
    set(_yarc_hash_args "")
endif()

add_test(NAME smoke_yarc
    COMMAND virtualjaguar --headless --frames ${VJ_SMOKE_FRAMES} ${_yarc_hash_args}
            "${VJ_ROMS}/yarc.j64")

add_test(NAME smoke_reload
    COMMAND virtualjaguar --headless --frames ${VJ_SMOKE_FRAMES}
            "${VJ_ROMS}/yarc.j64" "${VJ_ROMS}/jagniccc.j64" "${VJ_ROMS}/yarc.j64")

add_test(NAME smoke_state
    COMMAND virtualjaguar --headless --frames 240 --state-roundtrip
            "${VJ_ROMS}/jagniccc.j64")

add_test(NAME smoke_bad_option
    COMMAND virtualjaguar --headless --frames 5
            --option virtualjaguar_no_such_option=1 "${VJ_ROMS}/yarc.j64")
set_tests_properties(smoke_bad_option PROPERTIES
    PASS_REGULAR_EXPRESSION "rejected by the core")

add_test(NAME smoke_cd
    COMMAND virtualjaguar --headless --frames 120 --allow-blank
            "${VJ_ROMS}/synth_jagcd.chd")

set_tests_properties(smoke_yarc smoke_reload smoke_state smoke_cd PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

# ---------------------------------------------------------------------------
# Controller remapping tests (no window; remap_unit does not even load the core).
# ---------------------------------------------------------------------------
add_executable(vj_remap_test test/remap_test.cpp src/remap.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_remap_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_remap_test PRIVATE ${VJ_SDL3_TARGET})

add_executable(vj_input_e2e test/input_e2e.cpp src/remap.cpp src/config.cpp src/paths.cpp src/core_host.cpp)
target_include_directories(vj_input_e2e PRIVATE src)
target_link_libraries(vj_input_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})

foreach(_t vj_remap_test vj_input_e2e)
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W3 /utf-8)
        target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${_t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()

add_test(NAME remap_unit COMMAND vj_remap_test)
add_test(NAME input_e2e COMMAND vj_input_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(remap_unit input_e2e PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "FAIL ")

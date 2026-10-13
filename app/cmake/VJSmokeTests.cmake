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
# Game library (#838).  vj_library_tests needs no window, no GL and no internet:
# dat parsing, CRC identification of synthetic files, libretro-thumbnails naming and
# cache paths, a scan of a temp folder, and (POSIX) the download -> cache -> decode ->
# texture pipeline against a loopback HTTP server, then again with the server gone.
# ---------------------------------------------------------------------------
add_executable(vj_library_tests tests/library_tests.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_library_tests PRIVATE src)
target_link_libraries(vj_library_tests PRIVATE vj_library ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_library_tests PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_library_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_library_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME library_unit COMMAND vj_library_tests)
set_tests_properties(library_unit PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "FAIL ")

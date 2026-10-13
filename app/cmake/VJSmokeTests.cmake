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
#   smoke_locked_option  an --option naming a core option the app owns (alt_inputs ...) is rejected
#                   too, not silently run with the forced value.
#   smoke_bad_option  an --option key the core never registered must be REJECTED
#                   (it would otherwise silently run the default).
#   smoke_cd        the synthetic Jaguar CD image, path-loaded (no BIOS).
#   remap_unit      controller remapping logic (test/remap_test.cpp): binding
#                   serialisation, profile / per-game override precedence, the
#                   config round trip, keypad -> core-input translation, the
#                   keypad overlay, bind capture, content identity.  No core.
#   devices_unit    the non-pad devices without a core (test/devices_test.cpp): kinds and
#                   libretro ids, per-port / per-game persistence, Team Tap player
#                   assignment, light-gun geometry, mouse-capture policy, the Router's
#                   host-input -> core-input translation, the Pro Controller keyboard layout,
#                   the controls cheat sheet and its auto-show rule.
#   devices_e2e     the same Router driving the REAL core (test/devices_e2e.cpp): mouse and
#                   rotary quadrature state, analog / driving / 6D values decoded off
#                   $F14000 as a game does, the paddle ADC at $F17C00, the light gun's aim
#                   point, Team Tap socket slots, locked core options.  Each group has a
#                   negative control (no host input -> the rest value).
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

# An --option naming a core option the app owns (devices.h, lockedCoreOption) must be rejected too,
# not silently run with the forced value.
add_test(NAME smoke_locked_option
    COMMAND virtualjaguar --headless --frames 5
            --option virtualjaguar_alt_inputs=enabled "${VJ_ROMS}/yarc.j64")
set_tests_properties(smoke_locked_option PROPERTIES
    PASS_REGULAR_EXPRESSION "managed by the app.*rejected by the core")

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

# ---------------------------------------------------------------------------
# Controller remapping tests (no window; remap_unit does not even load the core).
# ---------------------------------------------------------------------------
add_executable(vj_remap_test test/remap_test.cpp src/remap.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_remap_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_remap_test PRIVATE ${VJ_SDL3_TARGET})

add_executable(vj_input_e2e test/input_e2e.cpp src/remap.cpp src/config.cpp src/paths.cpp src/core_host.cpp)
target_include_directories(vj_input_e2e PRIVATE src)
target_link_libraries(vj_input_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})

# The device layer (devices.h / router.h / cheatsheet.h): no core, then the real core.
set(VJ_DEVICE_SOURCES src/devices.cpp src/router.cpp src/remap.cpp src/config.cpp src/paths.cpp)
add_executable(vj_devices_test test/devices_test.cpp src/cheatsheet.cpp ${VJ_DEVICE_SOURCES})
target_include_directories(vj_devices_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_devices_test PRIVATE ${VJ_SDL3_TARGET})

add_executable(vj_devices_e2e test/devices_e2e.cpp src/core_host.cpp ${VJ_DEVICE_SOURCES})
target_include_directories(vj_devices_e2e PRIVATE src)
target_link_libraries(vj_devices_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})

foreach(_t vj_remap_test vj_input_e2e vj_devices_test vj_devices_e2e)
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W3 /utf-8)
        target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${_t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()

add_test(NAME remap_unit COMMAND vj_remap_test)
add_test(NAME input_e2e COMMAND vj_input_e2e "${VJ_ROMS}/yarc.j64")
add_test(NAME devices_unit COMMAND vj_devices_test)
add_test(NAME devices_e2e COMMAND vj_devices_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(remap_unit input_e2e devices_unit devices_e2e PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "FAIL ")

# ---------------------------------------------------------------------------
# Controls cheat-sheet overlay, through the real App (headless, no window).
#
#   controls_overlay_keyboard   no gamepad on Player 1 (headless has none): the overlay is
#                               showing after the load, and its text is the keyboard
#                               profile's bindings (B = Z, the Pro Controller's X = A ...).
#   controls_overlay_dismissed  "don't show again" (ui.controls.autoshow=0): hidden.
#   controls_overlay_rebound    a saved global rebinding (B = M, scancode 16) changes the text;
#                               the per-game layer is covered by devices_unit.
# ---------------------------------------------------------------------------
set(VJ_CTL_DISMISSED "${CMAKE_BINARY_DIR}/ctl_dismissed")
set(VJ_CTL_REBOUND "${CMAKE_BINARY_DIR}/ctl_rebound")
file(MAKE_DIRECTORY "${VJ_CTL_DISMISSED}" "${VJ_CTL_REBOUND}")
file(WRITE "${VJ_CTL_DISMISSED}/config.ini" "ui.controls.autoshow=0\n")
file(WRITE "${VJ_CTL_REBOUND}/config.ini" "input.kb.b=k16\n")

add_test(NAME controls_overlay_keyboard
    COMMAND virtualjaguar --headless --frames 5 --expect-controls-overlay shown --print-controls "${VJ_ROMS}/yarc.j64")
set_tests_properties(controls_overlay_keyboard PROPERTIES
    PASS_REGULAR_EXPRESSION "Player 1: Standard Joypad - keyboard.*B: Z.*Keypad 9 \\(Pro: X\\): [^\n]*, A\n")

add_test(NAME controls_overlay_dismissed
    COMMAND virtualjaguar --headless --frames 5 --data-dir "${VJ_CTL_DISMISSED}" --expect-controls-overlay hidden "${VJ_ROMS}/yarc.j64")

add_test(NAME controls_overlay_rebound
    COMMAND virtualjaguar --headless --frames 5 --data-dir "${VJ_CTL_REBOUND}" --expect-controls-overlay shown --print-controls "${VJ_ROMS}/yarc.j64")
set_tests_properties(controls_overlay_rebound PROPERTIES
    PASS_REGULAR_EXPRESSION "  B: M\n")
set_tests_properties(controls_overlay_keyboard controls_overlay_dismissed controls_overlay_rebound PROPERTIES
    TIMEOUT 120
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

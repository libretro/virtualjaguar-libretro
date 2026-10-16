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

# BIOS-only boots (#838, #833).  No BIOS file is needed: the core's built-in images run, so these work on CI.
# Neither passes --allow-blank: the boot ROM's logo and the CD BIOS's logo and cube must actually draw (the hashes
# are the final frame at 400 frames; like yarc's they are meant to be the same on every OS and CPU).
add_test(NAME smoke_boot_cd_bios
    COMMAND virtualjaguar --headless --frames 400 --boot-bios cd --expect-bios cd:builtin --expect-hash b83cb3ee)
add_test(NAME smoke_boot_cart_rom
    COMMAND virtualjaguar --headless --frames 400 --boot-bios cart --expect-bios cart:builtin --expect-hash 3cc9c43e)
set_tests_properties(smoke_boot_cd_bios PROPERTIES PASS_REGULAR_EXPRESSION "BIOS CD BIOS: built-in retail CD BIOS")
set_tests_properties(smoke_boot_cart_rom PROPERTIES PASS_REGULAR_EXPRESSION "BIOS Cart boot ROM: built-in Series K boot ROM")

set_tests_properties(smoke_yarc smoke_reload smoke_state smoke_cd smoke_boot_cd_bios smoke_boot_cart_rom PROPERTIES
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

# BIOS choice, import and classification (#838, #833): no core, no window, synthetic files.
add_executable(vj_bios_tests tests/bios_test.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_bios_tests PRIVATE src)
target_link_libraries(vj_bios_tests PRIVATE vj_library ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_bios_tests PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_bios_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_bios_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME bios_unit COMMAND vj_bios_tests)
set_tests_properties(bios_unit PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")

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

add_executable(vj_devices_e2e test/devices_e2e.cpp src/core_host.cpp src/hold_arbiter.cpp ${VJ_DEVICE_SOURCES})
target_include_directories(vj_devices_e2e PRIVATE src)
target_link_libraries(vj_devices_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})

add_executable(vj_bios_e2e tests/bios_e2e.cpp src/core_host.cpp src/paths.cpp)
target_include_directories(vj_bios_e2e PRIVATE src)
target_link_libraries(vj_bios_e2e PRIVATE vj_core ${VJ_SDL3_TARGET})

foreach(_t vj_remap_test vj_input_e2e vj_devices_test vj_devices_e2e vj_bios_e2e)
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W3 /utf-8)
        target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${_t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()

# Several copies of the app at once (instance.h): the number lock, per-instance paths, and the
# settings-file policy (a secondary copy must never rewrite the shared config.ini).
add_executable(vj_instance_test tests/instance_test.cpp src/instance.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_instance_test PRIVATE src)
target_link_libraries(vj_instance_test PRIVATE ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_instance_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_instance_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_instance_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME instance_unit COMMAND vj_instance_test)
set_tests_properties(instance_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

# Settings > Reset group / Reset all settings (#833): the key classification and the option plans, plus the
# real core's options (locked / session-overridden ones are skipped).
add_executable(vj_settings_reset_test
    tests/settings_reset_test.cpp src/settings_reset.cpp src/core_host.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_settings_reset_test PRIVATE src)
target_link_libraries(vj_settings_reset_test PRIVATE vj_core ${VJ_SDL3_TARGET})
if(WIN32)
    target_link_libraries(vj_settings_reset_test PRIVATE ws2_32 iphlpapi)
endif()
if(MSVC)
    target_compile_options(vj_settings_reset_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_settings_reset_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_settings_reset_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME settings_reset COMMAND vj_settings_reset_test "${VJ_ROMS}/yarc.j64")
set_tests_properties(settings_reset PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")

# UI fonts (#833): settings round trip, the per-OS font lookup against fake machines, custom-file validation,
# glyph coverage of the app's own text.  No ImGui context.
add_executable(vj_fonts_test tests/fonts_test.cpp src/fonts.cpp src/fonts_system.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_fonts_test PRIVATE src ${imgui_SOURCE_DIR})
target_link_libraries(vj_fonts_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
if(MSVC)
    target_compile_options(vj_fonts_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_fonts_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_fonts_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME fonts_unit COMMAND vj_fonts_test "${CMAKE_CURRENT_SOURCE_DIR}/src" "${imgui_SOURCE_DIR}")
set_tests_properties(fonts_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

# Installed-font enumeration (fonts_system.cpp, #935): name tables of synthetic fonts, the folder walk, the per-OS folders.
add_executable(vj_fonts_system_test tests/fonts_system_test.cpp src/fonts.cpp src/fonts_system.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_fonts_system_test PRIVATE src ${imgui_SOURCE_DIR})
target_link_libraries(vj_fonts_system_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
if(MSVC)
    target_compile_options(vj_fonts_system_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_fonts_system_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_fonts_system_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME fonts_system_unit COMMAND vj_fonts_system_test)
set_tests_properties(fonts_system_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

# The Dear ImGui half of the fonts (fonts_imgui.cpp): the symbol fallback in every typeface incl. the Retro pixel font.
add_executable(vj_fonts_imgui_test tests/fonts_imgui_test.cpp src/fonts.cpp src/fonts_system.cpp src/fonts_imgui.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_fonts_imgui_test PRIVATE src)
target_link_libraries(vj_fonts_imgui_test PRIVATE vj_imgui ${VJ_SDL3_TARGET} Threads::Threads)
if(MSVC)
    target_compile_options(vj_fonts_imgui_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_fonts_imgui_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_fonts_imgui_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME fonts_imgui_unit COMMAND vj_fonts_imgui_test)
set_tests_properties(fonts_imgui_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

# Frame pacing (pacing.h, #952): the loop's plan/charge/sleep/vsync-release decisions against a fake clock.
add_executable(vj_pacing_test tests/pacing_test.cpp)
target_include_directories(vj_pacing_test PRIVATE src)
if(MSVC)
    target_compile_options(vj_pacing_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_pacing_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_pacing_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME pacing_unit COMMAND vj_pacing_test)
set_tests_properties(pacing_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

add_test(NAME remap_unit COMMAND vj_remap_test)
add_test(NAME input_e2e COMMAND vj_input_e2e "${VJ_ROMS}/yarc.j64")
add_test(NAME devices_unit COMMAND vj_devices_test)
add_test(NAME devices_e2e COMMAND vj_devices_e2e "${VJ_ROMS}/yarc.j64")
add_test(NAME bios_e2e COMMAND vj_bios_e2e "${VJ_ROMS}/yarc.j64")
set_tests_properties(remap_unit input_e2e devices_unit devices_e2e bios_e2e PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "FAIL ")

# ---------------------------------------------------------------------------
# Controls cheat-sheet overlay, through the real App (headless, no window).
#
#   controls_overlay_default    the setting is OFF by default: no overlay after the load (the hint
#                               toast "Press F3 for the controls" is all a keyboard player gets).
#   controls_overlay_keyboard   the setting on (ui.controls.autoshow=1), no gamepad on Player 1
#                               (headless has none): the overlay is showing after the load, and its
#                               text is the keyboard profile's bindings (B = Z, the Pro Controller's
#                               X = A ...).
#   controls_overlay_dismissed  setting explicitly off (ui.controls.autoshow=0): hidden.
#   controls_overlay_rebound    a saved global rebinding (B = M, scancode 16) changes the text;
#                               the per-game layer is covered by devices_unit.
# ---------------------------------------------------------------------------
set(VJ_CTL_DISMISSED "${CMAKE_BINARY_DIR}/ctl_dismissed")
set(VJ_CTL_REBOUND "${CMAKE_BINARY_DIR}/ctl_rebound")
set(VJ_CTL_DEFAULT "${CMAKE_BINARY_DIR}/ctl_default")
file(MAKE_DIRECTORY "${VJ_CTL_DISMISSED}" "${VJ_CTL_REBOUND}" "${VJ_CTL_DEFAULT}")
set(VJ_CTL_ON "${CMAKE_BINARY_DIR}/ctl_on")
file(MAKE_DIRECTORY "${VJ_CTL_ON}")
file(WRITE "${VJ_CTL_ON}/config.ini" "ui.controls.autoshow=1\n")
file(WRITE "${VJ_CTL_DISMISSED}/config.ini" "ui.controls.autoshow=0\n")
file(WRITE "${VJ_CTL_REBOUND}/config.ini" "input.kb.b=k16\nui.controls.autoshow=1\n")

add_test(NAME controls_overlay_default
    COMMAND virtualjaguar --headless --frames 5 --data-dir "${VJ_CTL_DEFAULT}" --expect-controls-overlay hidden "${VJ_ROMS}/yarc.j64")
set_tests_properties(controls_overlay_default PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[osd\\] Press F3 for the controls")

add_test(NAME controls_overlay_keyboard
    COMMAND virtualjaguar --headless --frames 5 --data-dir "${VJ_CTL_ON}" --expect-controls-overlay shown --print-controls "${VJ_ROMS}/yarc.j64")
set_tests_properties(controls_overlay_keyboard PROPERTIES
    PASS_REGULAR_EXPRESSION "Player 1: Standard Joypad - keyboard.*B: Z.*Keypad 9 \\(Pro: X\\): [^\n]*, A\n")

add_test(NAME controls_overlay_dismissed
    COMMAND virtualjaguar --headless --frames 5 --data-dir "${VJ_CTL_DISMISSED}" --expect-controls-overlay hidden "${VJ_ROMS}/yarc.j64")

add_test(NAME controls_overlay_rebound
    COMMAND virtualjaguar --headless --frames 5 --data-dir "${VJ_CTL_REBOUND}" --expect-controls-overlay shown --print-controls "${VJ_ROMS}/yarc.j64")
set_tests_properties(controls_overlay_rebound PROPERTIES
    PASS_REGULAR_EXPRESSION "  B: M\n")
set_tests_properties(controls_overlay_default controls_overlay_keyboard controls_overlay_dismissed controls_overlay_rebound PROPERTIES
    TIMEOUT 120
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

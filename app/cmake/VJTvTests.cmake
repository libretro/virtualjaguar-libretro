# TV mode (#991): the pure units behind the couch UI.  No window and no core: runs everywhere.
#   hold_arbiter_unit   HoldArbiter (tap vs 500 ms hold on dual-use buttons) and ReleaseGuard
#                       (tests/hold_arbiter_test.cpp)
add_executable(vj_hold_arbiter_test tests/hold_arbiter_test.cpp src/hold_arbiter.cpp src/remap.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_hold_arbiter_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_hold_arbiter_test PRIVATE ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_hold_arbiter_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_hold_arbiter_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_hold_arbiter_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME hold_arbiter_unit COMMAND vj_hold_arbiter_test)
set_tests_properties(hold_arbiter_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

# Focus model + style metrics (tests/tv_focus_test.cpp) and nav intents (tests/tv_nav_test.cpp): pure.
#   tv_focus_unit   LinearFocus / GridFocus / FocusMemory / A-Z jump helpers, Metrics, Tween
#   tv_nav_unit     NavInput (repeat, stick hysteresis, triggers, newest-direction-wins) and the intent tables
#   tv_mode_unit    TvMode (the on/off state machine and its triggers), inputOwner, the stored-setting keys
add_executable(vj_tv_focus_test tests/tv_focus_test.cpp src/tv/tv_focus.cpp src/tv/tv_style.cpp)
target_include_directories(vj_tv_focus_test PRIVATE src)
add_executable(vj_tv_nav_test tests/tv_nav_test.cpp src/tv/tv_nav.cpp)
target_include_directories(vj_tv_nav_test PRIVATE src)
target_link_libraries(vj_tv_nav_test PRIVATE ${VJ_SDL3_TARGET})
add_executable(vj_tv_mode_test tests/tv_mode_test.cpp src/tv/tv_mode.cpp)
target_include_directories(vj_tv_mode_test PRIVATE src)
foreach(t vj_tv_focus_test vj_tv_nav_test vj_tv_mode_test)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W3 /utf-8)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()
add_test(NAME tv_focus_unit COMMAND vj_tv_focus_test)
add_test(NAME tv_nav_unit COMMAND vj_tv_nav_test)
add_test(NAME tv_mode_unit COMMAND vj_tv_mode_test)
set_tests_properties(tv_focus_unit tv_nav_unit tv_mode_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

# The TV in-game menu and its input ownership, through the real App (headless, no ImGui): F4 / Share-hold open and
# close it, F4 while typing or capturing does nothing, a pause survives it, Esc is Back, a pad walks it to Close game.
# The runner attaches a virtual gamepad (SDL_AttachVirtualJoystick, src/tv_script.cpp).  It relies on SDL's virtual-joystick driver (built into SDL3 on
# every OS), on a GAMEPAD-type virtual stick mapping its button i to SDL_GamepadButton i (MISC1 included), and on gamepad
# init being on in a headless run (App::initSdl does it when --tv-test-script is given).  Script times are real time
# (the hold arbiter reads SDL_GetTicks()).
set(VJ_TV_MENU_SCRIPT "${CMAKE_BINARY_DIR}/tv_menu_headless.txt")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../tests/tv_menu_headless.txt.in" "${VJ_TV_MENU_SCRIPT}" @ONLY)
add_test(NAME tv_menu_headless
    COMMAND virtualjaguar --headless --frames 30 --tv-test-script "${VJ_TV_MENU_SCRIPT}")
set_tests_properties(tv_menu_headless PROPERTIES
    TIMEOUT 180
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

# The in-game menu and the Base layer (sections, chrome, the screen stack) drawn headlessly (an ImGui context with no
# renderer) over a fake tv::Host: every page without an ImGui error, the netplay banner and the missing Load rows in
# the text actually drawn, a menu that leaves the pointer and the keyboard to the app (no window while closed, no
# keyboard capture while open), no Base window without TV mode or with a game running.
add_executable(vj_tv_ui_smoke
    tests/tv_ui_smoke.cpp src/tv/tv_shell.cpp src/tv/tv_ingame.cpp src/tv/tv_chrome.cpp src/tv/tv_home.cpp src/tv/tv_library.cpp src/tv/tv_details.cpp
    src/tv/tv_widgets.cpp src/tv/tv_focus.cpp src/tv/tv_mode.cpp src/tv/tv_nav.cpp src/tv/tv_style.cpp src/theme.cpp src/theme_style.cpp src/theme_custom.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_tv_ui_smoke PRIVATE src)
target_link_libraries(vj_tv_ui_smoke PRIVATE vj_imgui ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_tv_ui_smoke PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_tv_ui_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_tv_ui_smoke PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME tv_ui_smoke COMMAND vj_tv_ui_smoke)
set_tests_properties(tv_ui_smoke PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")

# The windowed-only script verbs (`wait library`, `shot PATH`, `quit`) fail their line headless with "windowed only"
# (tests/tv_script_windowed_only.cmake runs the app once per verb).  The windowed path itself is not CTest-covered: it
# needs a window and a scanned Library.
add_test(NAME tv_script_windowed_only
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:virtualjaguar> -DDIR=${CMAKE_BINARY_DIR}/tv_script_windowed_only
            -P "${CMAKE_CURRENT_LIST_DIR}/../tests/tv_script_windowed_only.cmake")
set_tests_properties(tv_script_windowed_only PROPERTIES TIMEOUT 240)   # the cmake script ends in FATAL_ERROR on a mismatch

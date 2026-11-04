# Native-OS integration tests (#833).  Headless, no GL, no core.
#
#   platform_unit        the menu model, dispatcher, recent-games conversion, association table, launcher-
#                        argument filter and the command-event round trip (tests/platform_test.cpp).
#   platform_assoc_sync  the file-type list must be the same in the C++ table, the macOS Info.plist, the
#                        Windows installer, the freedesktop .desktop file and the shared-mime-info XML (and a
#                        subset of what the core opens); the app id must be the same everywhere it appears.
#   native_menu_dump     macOS only: build the REAL NSMenu / Dock menu from the model and check every item
#                        (needs a window-server session; reports SKIP (77) over ssh / on a headless worker).

add_executable(vj_platform_test tests/platform_test.cpp src/platform/commands.cpp src/platform/events.cpp
               src/platform/viewports.cpp)
target_include_directories(vj_platform_test PRIVATE src)
target_link_libraries(vj_platform_test PRIVATE ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_platform_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_platform_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_platform_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME platform_unit COMMAND vj_platform_test)
set_tests_properties(platform_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

add_test(NAME platform_assoc_sync
    COMMAND ${CMAKE_COMMAND} -DVJ_APP_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DVJ_ROOT=${VJ_ROOT}
            -P ${CMAKE_CURRENT_LIST_DIR}/VJPlatformSyncCheck.cmake)
set_tests_properties(platform_assoc_sync PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

if(APPLE)
    # Exercises the Store implementation in every edition: it #includes file_access_macos.mm itself (for the
    # internals), so it defines VJ_MAC_APP_STORE directly and must NOT link vj_file_access (duplicate symbols).
    # file_access_define_sync below knows this one exception.
    add_executable(vj_file_access_macos_test tests/file_access_macos_test.mm src/config.cpp src/paths.cpp)
    target_include_directories(vj_file_access_macos_test PRIVATE src)
    target_compile_definitions(vj_file_access_macos_test PRIVATE VJ_MAC_APP_STORE=1)
    target_compile_options(vj_file_access_macos_test PRIVATE -fobjc-arc -Wno-deprecated-declarations)
    target_link_libraries(vj_file_access_macos_test PRIVATE ${VJ_SDL3_TARGET} "-framework Cocoa")
    add_test(NAME file_access_macos_unit COMMAND vj_file_access_macos_test)
    set_tests_properties(file_access_macos_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

    add_test(NAME native_menu_dump COMMAND virtualjaguar --dump-native-menu)
    set_tests_properties(native_menu_dump PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 FAIL_REGULAR_EXPRESSION "FAIL ")
endif()

# file_access_define_sync (#1147): platform/file_access.h is an edition switch, so every translation unit that
# includes it (or tests VJ_MAC_APP_STORE) must be compiled with the define exactly when VJ_MAC_APP_STORE is ON --
# the 3.10.0 Store build had it on the executable but not on vj_library.  Reads compile_commands.json, which the
# Makefile and Ninja generators write (CMAKE_EXPORT_COMPILE_COMMANDS, on by default in CMakeLists.txt).  Only an
# explicit -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF, or a generator that writes no database (Visual Studio, Xcode),
# leaves the test out.
if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "Makefiles|Ninja")
    add_test(NAME file_access_define_sync
        COMMAND ${CMAKE_COMMAND} -DVJ_APP_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DVJ_BUILD_DIR=${CMAKE_BINARY_DIR}
                -DVJ_MAC_APP_STORE=${VJ_MAC_APP_STORE}
                -P ${CMAKE_CURRENT_LIST_DIR}/VJFileAccessDefineCheck.cmake)
    set_tests_properties(file_access_define_sync PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")
endif()

add_executable(vj_window_chrome_test tests/window_chrome_test.cpp)
target_include_directories(vj_window_chrome_test PRIVATE src)
target_link_libraries(vj_window_chrome_test PRIVATE vj_imgui)
add_test(NAME window_chrome_unit COMMAND vj_window_chrome_test)
set_tests_properties(window_chrome_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

add_executable(vj_software_gl_test tests/software_gl_test.cpp)
target_include_directories(vj_software_gl_test PRIVATE src)
if(WIN32)
    target_sources(vj_software_gl_test PRIVATE src/platform/software_gl_windows.cpp)
    target_link_libraries(vj_software_gl_test PRIVATE shell32)
endif()
add_test(NAME software_gl_unit COMMAND vj_software_gl_test)
set_tests_properties(software_gl_unit PROPERTIES TIMEOUT 60)

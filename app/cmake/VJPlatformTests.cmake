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
    add_test(NAME native_menu_dump COMMAND virtualjaguar --dump-native-menu)
    set_tests_properties(native_menu_dump PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 FAIL_REGULAR_EXPRESSION "FAIL ")
endif()

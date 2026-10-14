# Colour themes (#833): selection logic, the SDL system-theme event, and the contrast of the palette and of
# the Dear ImGui style colours the app applies.  No window and no core: pure logic, runs everywhere.
add_executable(vj_theme_test tests/theme_test.cpp src/theme.cpp src/theme_style.cpp src/theme_custom.cpp src/paths.cpp)
target_include_directories(vj_theme_test PRIVATE src)
target_link_libraries(vj_theme_test PRIVATE vj_imgui ${VJ_SDL3_TARGET})
if(MSVC)
    target_compile_options(vj_theme_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_theme_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_theme_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME theme_unit COMMAND vj_theme_test)
set_tests_properties(theme_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

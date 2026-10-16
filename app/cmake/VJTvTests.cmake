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

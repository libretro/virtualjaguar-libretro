# Debug-tools tests (#652 / #833).  All headless.
#
#   debug_session_unit   DebugSession (app/src/debug) against the mock core interface
#                        (app/tests/debug_mock.h): when the tools are available, attach / halt /
#                        resume / step through the nested pump, the breakpoint list, the poke policy,
#                        the refresh cadence, the Tools registry.  No window, no core.
add_executable(vj_debug_session_test
    tests/debug_session_test.cpp src/debug/debug_session.cpp)
target_include_directories(vj_debug_session_test PRIVATE src/debug tests "${VJ_ROOT}/src/debug")
if(MSVC)
    target_compile_options(vj_debug_session_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_debug_session_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_debug_session_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME debug_session_unit COMMAND vj_debug_session_test)
set_tests_properties(debug_session_unit PROPERTIES TIMEOUT 60)

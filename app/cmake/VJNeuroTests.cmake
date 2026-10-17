# Neuro API client tests (docs/neuro.md).  No window, no GL, no internet, no Neuro.
#
#   neuro_unit      JSON, WebSocket framing / handshake / URLs, the input queue, the controller and profile
#                   actions and their validation, profile files, and the protocol against a fake server.
#   neuro_loopback  a WebSocket server on a free 127.0.0.1 port: startup, register, force, action, result,
#                   the buttons played out frame by frame, reregister_all, and the reconnect.  Skipped (77)
#                   on Windows.
add_executable(vj_neuro_test tests/neuro_test.cpp src/neuro_json.cpp src/neuro_ws.cpp src/neuro_actions.cpp
    src/neuro_profiles.cpp src/neuro_link.cpp src/neuro_agent.cpp)
target_include_directories(vj_neuro_test PRIVATE src)
target_link_libraries(vj_neuro_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads)
if(WIN32)
    target_link_libraries(vj_neuro_test PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(vj_neuro_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_neuro_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_neuro_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME neuro_unit COMMAND vj_neuro_test unit)
add_test(NAME neuro_loopback COMMAND vj_neuro_test loopback)
set_tests_properties(neuro_unit neuro_loopback PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")
set_tests_properties(neuro_loopback PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

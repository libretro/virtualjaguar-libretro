# Content protocol, local staging and the background browser, with generated fixtures.
add_executable(vj_content_server_test tests/content_server_test.cpp
    src/content_server.cpp src/content_server_browser.cpp src/neuro/neuro_json.cpp
    src/sha256.cpp src/secret_store.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_content_server_test PRIVATE src)
target_link_libraries(vj_content_server_test PRIVATE vj_library ${VJ_SECRET_LIBS})
if(MSVC)
    target_compile_options(vj_content_server_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_content_server_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_content_server_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME content_server_unit COMMAND vj_content_server_test)
set_tests_properties(content_server_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")
if(Python3_Interpreter_FOUND AND NOT VJ_HTTP_BACKEND STREQUAL "none")
    add_test(NAME content_server_launch COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/content_server_launch.py"
        "$<TARGET_FILE:vj_content_server_test>" "$<TARGET_FILE:virtualjaguar>" "${VJ_ROMS}/yarc.j64")
    set_tests_properties(content_server_launch PROPERTIES TIMEOUT 90)
endif()

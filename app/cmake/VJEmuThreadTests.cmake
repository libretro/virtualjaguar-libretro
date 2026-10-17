# Emulation thread (#967, docs/app-emulation-thread-design.md, docs/app-emulation-thread-plan-phase1.md).
# Every test here carries the label `emu_thread`: the ThreadSanitizer job in .github/workflows/app.yml runs exactly
# these (ctest -L emu_thread).
#
#   frame_exchange_unit   FrameExchange: newest wins, the reader's slot is never written, no tearing, content epochs

function(vj_emu_thread_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

add_executable(vj_frame_exchange_test tests/frame_exchange_test.cpp)
target_include_directories(vj_frame_exchange_test PRIVATE src)
target_link_libraries(vj_frame_exchange_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_frame_exchange_test)
add_test(NAME frame_exchange_unit COMMAND vj_frame_exchange_test)
set_tests_properties(frame_exchange_unit PROPERTIES TIMEOUT 120 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

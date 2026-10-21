# #967 3a: the debug tool windows never touch the core's debug interface -- they read published view buffers and
# marshal one-shot actions through DebugSession.  A static guard, run as
#   cmake -DDIR=<app/src/debug> "-DFILES=a.cpp;b.cpp" -P CheckNoDirectIface.cmake
# It strips `//` comments from each file in FILES and fails on a call through the interface or the old direct
# region accessor.  CMake regexes have no \b, so each pattern is anchored on the call's "(".
if(NOT DEFINED DIR OR NOT DEFINED FILES)
    message(FATAL_ERROR "usage: cmake -DDIR=<dir> \"-DFILES=<a;b;...>\" -P CheckNoDirectIface.cmake")
endif()

set(_pattern "iface\\(\\)|->(region|peek|poke|get_regs|set_reg|disasm_risc|attach|detach|break_set|break_clear|halt_next|halted_target|op_snapshot_arm|op_snapshot_get|cry_to_rgb|blit_log_enable|blit_log_head|blit_log_get|texreplace_reload|frame_number|halt_reason)\\(|memoryRegion")

foreach(_f IN LISTS FILES)
    set(_path "${DIR}/${_f}")
    if(NOT EXISTS "${_path}")
        message(SEND_ERROR "${_f}: not found under ${DIR}")
        continue()
    endif()
    file(READ "${_path}" _text)
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    string(REGEX MATCHALL "${_pattern}" _hits "${_text}")
    foreach(_h IN LISTS _hits)
        message(SEND_ERROR "${_f}: direct core access ${_h}")
    endforeach()
endforeach()

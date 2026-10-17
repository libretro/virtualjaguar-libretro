# tv_script_windowed_only (CTest): the --tv-test-script verbs `wait library`, `shot PATH` and `quit` exist only in a
# windowed run.  A headless run must fail the line, with the message "windowed only" and a non-zero exit.  The first
# failing line ends a headless run, so each verb gets a one-line script of its own.
# Run as:  cmake -DEXE=<virtualjaguar> -DDIR=<scratch dir> -P tv_script_windowed_only.cmake
file(MAKE_DIRECTORY "${DIR}")
set(n 0)
foreach(line "wait library" "shot windowed_only_test.png" "quit")
    math(EXPR n "${n}+1")
    set(script "${DIR}/tv_script_windowed_only_${n}.txt")
    file(WRITE "${script}" "# one windowed-only verb, run headless: it must fail\n0 ${line}\n")
    execute_process(COMMAND "${EXE}" --headless --frames 1 --tv-test-script "${script}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
    if(rc EQUAL 0)
        message(FATAL_ERROR "FAIL '${line}' succeeded headless (it is windowed only):\n${out}${err}")
    endif()
    string(REGEX MATCH "^[a-z]+" verb "${line}")
    if(NOT err MATCHES "tv script line 2: ${verb} is windowed only")
        message(FATAL_ERROR "FAIL '${line}': expected \"${verb} is windowed only\" on line 2, got (exit ${rc}):\n${out}${err}")
    endif()
    if(EXISTS "${DIR}/windowed_only_test.png")
        message(FATAL_ERROR "FAIL 'shot' wrote a file headless")
    endif()
endforeach()
message(STATUS "tv_script_windowed_only: all 3 verbs fail headless")

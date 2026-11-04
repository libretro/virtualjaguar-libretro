# file_access_define_sync (#1147): run with
#   cmake -DVJ_APP_DIR=<app> -DVJ_BUILD_DIR=<build> -DVJ_MAC_APP_STORE=<ON|OFF> -P VJFileAccessDefineCheck.cmake
#
# platform/file_access.h is an edition switch: with VJ_MAC_APP_STORE it declares the security-scoped-bookmark
# implementation (file_access_macos.mm), without it inline SDL pass-throughs whose remember()/forget() do
# nothing.  Two translation units that disagree link fine and fail at runtime (the 3.10.0 Store build: the
# executable had the define, vj_library did not, library folders lost their sandbox grant after a relaunch).
# This check reads compile_commands.json and fails (prints "FAIL ...") when a translation unit that includes
# the header -- directly or through another project header -- or that tests VJ_MAC_APP_STORE itself is
# compiled without the define while the option is ON, or with it while the option is OFF.
#
# One exception: tests/file_access_macos_test.mm #includes file_access_macos.mm to reach its internals and
# defines VJ_MAC_APP_STORE on its own, so it exercises the Store implementation in every edition.
cmake_minimum_required(VERSION 3.20)   # script mode starts at policy defaults: IN_LIST needs CMP0057
set(_fail 0)
function(_vj_fail msg)
    message("FAIL ${msg}")
    set(_fail 1 PARENT_SCOPE)
endfunction()

foreach(_v VJ_APP_DIR VJ_BUILD_DIR VJ_MAC_APP_STORE)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "FAIL ${_v} is not set (see the header of this script)")
    endif()
endforeach()
get_filename_component(VJ_APP_DIR "${VJ_APP_DIR}" REALPATH)

# ---- 1. which project sources depend on the edition ----
# Seed: every file that mentions VJ_MAC_APP_STORE (the header itself, the .mm, shaders.cpp's search path,
# the Discord status text).  Then, to a fixpoint, every file that #includes one of them by a path that ends
# in that file (so "platform/file_access.h" and "../src/platform/file_access_macos.mm" both resolve).
file(GLOB_RECURSE _sources LIST_DIRECTORIES false
    "${VJ_APP_DIR}/src/*.cpp" "${VJ_APP_DIR}/src/*.mm" "${VJ_APP_DIR}/src/*.h" "${VJ_APP_DIR}/src/*.hpp"
    "${VJ_APP_DIR}/tests/*.cpp" "${VJ_APP_DIR}/tests/*.mm" "${VJ_APP_DIR}/tests/*.h"
    "${VJ_APP_DIR}/test/*.cpp" "${VJ_APP_DIR}/test/*.mm" "${VJ_APP_DIR}/test/*.h")
set(_edition)
set(_real)
foreach(_f IN LISTS _sources)
    get_filename_component(_r "${_f}" REALPATH)
    list(APPEND _real "${_r}")
    file(READ "${_f}" _c)
    if(_c MATCHES "VJ_MAC_APP_STORE")
        list(APPEND _edition "${_r}")
    endif()
    string(REGEX MATCHALL "#[ \t]*(include|import)[ \t]*\"[^\"]+\"" _m "${_c}")
    set(_incs)
    foreach(_i IN LISTS _m)
        string(REGEX REPLACE "^#[ \t]*(include|import)[ \t]*\"([^\"]+)\"$" "\\2" _i "${_i}")
        string(REGEX REPLACE "^(\\.\\.?/)+" "" _i "${_i}")   # ../src/x.h -> src/x.h
        list(APPEND _incs "/${_i}")
    endforeach()
    string(MAKE_C_IDENTIFIER "${_r}" _id)
    set(_inc_${_id} "${_incs}")
endforeach()
if(NOT _edition)
    _vj_fail("no source under ${VJ_APP_DIR} mentions VJ_MAC_APP_STORE; the scan is broken")
endif()
set(_changed 1)
while(_changed)
    set(_changed 0)
    foreach(_f IN LISTS _real)
        if(_f IN_LIST _edition)
            continue()
        endif()
        string(MAKE_C_IDENTIFIER "${_f}" _id)
        foreach(_i IN LISTS _inc_${_id})
            string(LENGTH "${_i}" _il)
            foreach(_e IN LISTS _edition)
                string(LENGTH "${_e}" _el)
                if(_el GREATER _il)
                    math(EXPR _at "${_el} - ${_il}")
                    string(SUBSTRING "${_e}" ${_at} -1 _tail)
                    if(_tail STREQUAL _i)
                        list(APPEND _edition "${_f}")
                        set(_changed 1)
                        break()
                    endif()
                endif()
            endforeach()
            if(_f IN_LIST _edition)
                break()
            endif()
        endforeach()
    endforeach()
endwhile()
list(LENGTH _edition _n)
message(STATUS "${_n} sources depend on VJ_MAC_APP_STORE")

# ---- 2. compile_commands.json: one entry per (target, source) ----
set(_db "${VJ_BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${_db}")
    _vj_fail("missing ${_db} (CMAKE_EXPORT_COMPILE_COMMANDS; Makefile and Ninja generators only)")
endif()
if(NOT _fail)
    file(READ "${_db}" _json)
    # Split into lines.  To CMake a ';' inside [...] is not a list separator, and the whole file is one JSON
    # array, so the brackets go first (and come back in the paths below).
    string(REPLACE "[" "<LB>" _json "${_json}")
    string(REPLACE "]" "<RB>" _json "${_json}")
    string(REPLACE ";" "\;" _json "${_json}")
    string(REPLACE "\n" ";" _lines "${_json}")
    set(_cmd "")
    set(_checked 0)
    set(_seen)
    foreach(_l IN LISTS _lines)
        if(_l MATCHES "^[ \t]*\"command\"[ \t]*:[ \t]*\"(.*)\",?[ \t]*$")
            set(_cmd "${CMAKE_MATCH_1}")
        elseif(_l MATCHES "^[ \t]*\"file\"[ \t]*:[ \t]*\"(.*)\",?[ \t]*$")
            set(_file "${CMAKE_MATCH_1}")
            string(REPLACE "<LB>" "[" _file "${_file}")
            string(REPLACE "<RB>" "]" _file "${_file}")
            string(REPLACE "\\\\" "\\" _file "${_file}")
            string(REPLACE "\\\"" "\"" _file "${_file}")
            get_filename_component(_file "${_file}" REALPATH)
            if(_file IN_LIST _edition)
                if(_cmd MATCHES "(^| )[-/]DVJ_MAC_APP_STORE(=[^ ]*)?( |$)")
                    set(_has 1)
                else()
                    set(_has 0)
                endif()
                if(_file MATCHES "/tests/file_access_macos_test\\.mm$")
                    set(_want 1)   # the exception above
                elseif(VJ_MAC_APP_STORE)
                    set(_want 1)
                else()
                    set(_want 0)
                endif()
                file(RELATIVE_PATH _rel "${VJ_APP_DIR}" "${_file}")
                if(NOT _has EQUAL _want)
                    if(_want)
                        _vj_fail("${_rel} is compiled WITHOUT VJ_MAC_APP_STORE in a Store build (link vj_file_access): ${_cmd}")
                    else()
                        _vj_fail("${_rel} is compiled WITH VJ_MAC_APP_STORE in a non-Store build: ${_cmd}")
                    endif()
                endif()
                math(EXPR _checked "${_checked} + 1")
                list(APPEND _seen "${_rel}")
            endif()
            set(_cmd "")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _seen)
    list(SORT _seen)
    message(STATUS "checked ${_checked} compile commands: ${_seen}")
    # The two sides of #1147 must both be in the scan, or a path-matching bug would pass vacuously.
    foreach(_must "src/library_service.cpp" "src/app.cpp")
        if(NOT _must IN_LIST _seen)
            _vj_fail("${_must} was not found in compile_commands.json; the scan is broken")
        endif()
    endforeach()
endif()

if(_fail)
    message(FATAL_ERROR "file_access_define_sync: VJ_MAC_APP_STORE drifts between translation units (#1147)")
endif()
message(STATUS "file_access_define_sync: OK (VJ_MAC_APP_STORE=${VJ_MAC_APP_STORE})")

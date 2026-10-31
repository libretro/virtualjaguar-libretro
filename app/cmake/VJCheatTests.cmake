# Cheat tests (#835).  All headless; the ones that need data the repository does not
# carry (the downloaded database, private ROMs) are only registered when it is there,
# so CI -- which has neither -- runs the rest.
#
#   cheats_unit              .cht parsing, the conversion to the core's code format (every
#                            code replayed through the core's real parser), library
#                            matching, per-game persistence.  Synthetic fixtures.
#   cheat_apply              a manual code reaches the core's RAM;  _control: it does not
#                            without the code (so the check can fail)
#   cheat_state              ...and still holds after a state restore
#   cheat_persist_*          enable a cheat from a .cht, quit, start again with NO cheat
#                            options: the cheat is still on (the "survives a restart" bar);
#                            _control: a different data directory has nothing
#   cheats_corpus            (database fetched, the default) every file of the real database parses
#   cheats_install           (database fetched) `cmake --install` puts cheats/, the license and
#                            the credit README beside the executable
#   cheat_tempest_*          (corpus + private ROM) the real database's Tempest 2000
#                            "Infinite Lives" is found through the core's own title
#                            identity and reaches RAM
#   <each of the above through the real binary>_threaded   the same run with --emu-thread on (#967, label emu_thread, so the
#                            TSan job runs it); the persist chain gets its own data dirs and fixture names, so the two
#                            chains never share state

set(_fx "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures")
get_filename_component(_fx "${_fx}" ABSOLUTE)

add_executable(vj_cheats_test
    tests/cheats_test.cpp src/cheats.cpp src/cheat_store.cpp src/config.cpp src/paths.cpp src/sha256.cpp
    src/neuro/neuro_json.cpp
    "${VJ_ROOT}/src/core/cheat.c")
target_include_directories(vj_cheats_test PRIVATE src "${VJ_ROOT}/src/core" "${VJ_ROOT}/libretro-common/include")
target_link_libraries(vj_cheats_test PRIVATE ${VJ_SDL3_TARGET})
target_compile_definitions(vj_cheats_test PRIVATE VJ_TEST_FIXTURES="${_fx}")
if(MSVC)
    target_compile_options(vj_cheats_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_cheats_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_cheats_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME cheats_unit COMMAND vj_cheats_test)

# ---- through the real binary ----------------------------------------------------------------
set(_yarc "${VJ_ROMS}/yarc.j64")
set(_no_cheat "headless: FAIL ram 0x1F0000 is [0-9A-F]+, expected AB")

add_test(NAME cheat_apply
    COMMAND virtualjaguar --headless --frames 60 --cheat 001F0000:AB --expect-ram 1F0000=AB "${_yarc}")
add_test(NAME cheat_apply_control
    COMMAND virtualjaguar --headless --frames 60 --expect-ram 1F0000=AB "${_yarc}")
set_tests_properties(cheat_apply_control PROPERTIES PASS_REGULAR_EXPRESSION "${_no_cheat}")
add_test(NAME cheat_apply_control_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 60 --expect-ram 1F0000=AB "${_yarc}")
set_tests_properties(cheat_apply_control_threaded PROPERTIES PASS_REGULAR_EXPRESSION "${_no_cheat}")

add_test(NAME cheat_state
    COMMAND virtualjaguar --headless --frames 240 --state-roundtrip
            --cheat 001F0000:AB --expect-ram 1F0000=AB "${VJ_ROMS}/jagniccc.j64")
add_test(NAME cheat_state_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 240 --state-roundtrip
            --cheat 001F0000:AB --expect-ram 1F0000=AB "${VJ_ROMS}/jagniccc.j64")

# The save -> restart -> control chain, once inline and once threaded: <_th> is "" or "_threaded", and the twin has its
# own data dirs and fixture names, so the two chains never share state.
foreach(_th "" "_threaded")
    if(_th)
        set(_thargs --emu-thread on)
    else()
        set(_thargs)
    endif()
    set(_pd "${CMAKE_BINARY_DIR}/cheat-persist-data${_th}")
    set(_pc "${CMAKE_BINARY_DIR}/cheat-persist-control${_th}")
    add_test(NAME cheat_persist_clean${_th}
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${_pd}" "${_pc}")
    # Run 1: yarc.cht (a fixture whose name matches the ROM's file name) supplies "Lives byte".
    add_test(NAME cheat_persist_save${_th}
        COMMAND virtualjaguar --headless ${_thargs} --frames 60 --data-dir "${_pd}" --cheats-dir "${_fx}"
                --cheat-on "Lives byte" --expect-ram 1F0010=05 "${_yarc}")
    # Run 2: a new process, same data directory, NO cheat option: it must still be on.
    add_test(NAME cheat_persist_restart${_th}
        COMMAND virtualjaguar --headless ${_thargs} --frames 60 --data-dir "${_pd}" --cheats-dir "${_fx}"
                --expect-ram 1F0010=05 "${_yarc}")
    # Control: the same, but a data directory nothing was saved to.
    add_test(NAME cheat_persist_control${_th}
        COMMAND virtualjaguar --headless ${_thargs} --frames 60 --data-dir "${_pc}" --cheats-dir "${_fx}"
                --expect-ram 1F0010=05 "${_yarc}")
    set_tests_properties(cheat_persist_control${_th} PROPERTIES
        PASS_REGULAR_EXPRESSION "headless: FAIL ram 0x1F0010 is [0-9A-F]+, expected 05")
    add_test(NAME cheat_persist_cleanup${_th}
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${_pd}" "${_pc}")
    set_tests_properties(cheat_persist_clean${_th} PROPERTIES FIXTURES_SETUP cheat_persist_fresh${_th})
    set_tests_properties(cheat_persist_save${_th} PROPERTIES
        FIXTURES_REQUIRED cheat_persist_fresh${_th} FIXTURES_SETUP cheat_persist_saved${_th})
    set_tests_properties(cheat_persist_restart${_th} cheat_persist_control${_th} PROPERTIES
        FIXTURES_REQUIRED "cheat_persist_fresh${_th};cheat_persist_saved${_th}")
    set_tests_properties(cheat_persist_cleanup${_th} PROPERTIES
        FIXTURES_CLEANUP "cheat_persist_fresh${_th};cheat_persist_saved${_th}")
endforeach()
set_tests_properties(cheat_persist_clean_threaded cheat_persist_save_threaded cheat_persist_restart_threaded
                     cheat_persist_control_threaded cheat_persist_cleanup_threaded PROPERTIES LABELS emu_thread)

set_tests_properties(cheats_unit cheat_apply cheat_state cheat_persist_save cheat_persist_restart PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "headless: FAIL")
# The twins: also the threaded OK line (the run really was threaded), and no owner violation / TSan report.  The controls
# pass by failing for their stated reason, so they only guard against the threading errors.
set_tests_properties(cheat_state_threaded cheat_persist_save_threaded cheat_persist_restart_threaded PROPERTIES
    TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "${_vj_threaded_ok}"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
set_tests_properties(cheat_apply_control_threaded cheat_persist_control_threaded PROPERTIES
    TIMEOUT 300 LABELS emu_thread
    FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer")

# ---- the real database, when it was downloaded -------------------------------------------
if(VJ_CHEATS_DIR AND EXISTS "${VJ_CHEATS_DIR}/Tempest 2000.cht")
    add_test(NAME cheats_corpus COMMAND vj_cheats_test --corpus "${VJ_CHEATS_DIR}")
    if(VJ_CHEATS_BUNDLED)
        add_test(NAME cheats_install
            COMMAND ${CMAKE_COMMAND} "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DPREFIX=${CMAKE_BINARY_DIR}/cheat-install-test"
                    "-DCONFIG=$<CONFIG>" -P "${CMAKE_CURRENT_LIST_DIR}/VJCheatInstallCheck.cmake")
    endif()

    # A private ROM, found the way every other test does (scripts/find-rom.sh).
    find_program(_vj_bash bash)
    set(_t2k "")
    if(_vj_bash)
        execute_process(COMMAND "${_vj_bash}" scripts/find-rom.sh "Tempest 2000*"
                        WORKING_DIRECTORY "${VJ_ROOT}"
                        OUTPUT_VARIABLE _t2k OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            set(_t2k "")
        elseif(NOT IS_ABSOLUTE "${_t2k}")
            set(_t2k "${VJ_ROOT}/${_t2k}")
        endif()
    endif()
    if(_t2k AND EXISTS "${_t2k}")
        message(STATUS "VJCheatTests: Tempest 2000 for the database test: ${_t2k}")
        # No --cheat-set: the set is chosen by the core's own title ("Tempest 2000").
        add_test(NAME cheat_tempest_lives
            COMMAND virtualjaguar --headless --frames 600 --cheats-dir "${VJ_CHEATS_DIR}"
                    --cheat-on "Infinite Lives" --expect-ram 7D3D=05 "${_t2k}")
        add_test(NAME cheat_tempest_control
            COMMAND virtualjaguar --headless --frames 600 --cheats-dir "${VJ_CHEATS_DIR}"
                    --expect-ram 7D3D=05 "${_t2k}")
        set_tests_properties(cheat_tempest_control PROPERTIES
            PASS_REGULAR_EXPRESSION "headless: FAIL ram 0x007D3D is [0-9A-F]+, expected 05")
        set_tests_properties(cheat_tempest_lives PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "headless: FAIL")
        add_test(NAME cheat_tempest_lives_threaded
            COMMAND virtualjaguar --headless --emu-thread on --frames 600 --cheats-dir "${VJ_CHEATS_DIR}"
                    --cheat-on "Infinite Lives" --expect-ram 7D3D=05 "${_t2k}")
        add_test(NAME cheat_tempest_control_threaded
            COMMAND virtualjaguar --headless --emu-thread on --frames 600 --cheats-dir "${VJ_CHEATS_DIR}"
                    --expect-ram 7D3D=05 "${_t2k}")
        set_tests_properties(cheat_tempest_control_threaded PROPERTIES
            PASS_REGULAR_EXPRESSION "headless: FAIL ram 0x007D3D is [0-9A-F]+, expected 05"
            FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer")
        set_tests_properties(cheat_tempest_lives_threaded PROPERTIES TIMEOUT 300
            PASS_REGULAR_EXPRESSION "${_vj_threaded_ok}"
            FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
        set_tests_properties(cheat_tempest_lives_threaded cheat_tempest_control_threaded PROPERTIES LABELS emu_thread)
    else()
        message(STATUS "VJCheatTests: no private Tempest 2000 ROM; the database end-to-end test is not registered")
    endif()
endif()

# ---- the community cheat store, real HTTP over loopback (#1071) ----------------------------------
# Serves site/cheats/ from a throwaway local server and installs Doom - Evil Unleashed's pack through the real binary.
# Needs python3 and the private ROM, so it is not registered where either is missing.
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(_vj_bash2 bash)
if(Python3_Interpreter_FOUND AND _vj_bash2 AND EXISTS "${VJ_ROOT}/site/cheats/packs/5E2CDBC0.cht")
    execute_process(COMMAND "${_vj_bash2}" scripts/find-rom.sh "Doom - Evil Unleashed*"
                    WORKING_DIRECTORY "${VJ_ROOT}"
                    OUTPUT_VARIABLE _evil OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
    if(_rc EQUAL 0 AND _evil)
        if(NOT IS_ABSOLUTE "${_evil}")
            set(_evil "${VJ_ROOT}/${_evil}")
        endif()
        add_test(NAME cheat_community_install
            COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/../tests/cheat_community_smoke.py"
                    "$<TARGET_FILE:virtualjaguar>" "${VJ_ROOT}/site/cheats" "${_evil}" 5E2CDBC0
                    "${CMAKE_BINARY_DIR}/cheat-community-test")
        set_tests_properties(cheat_community_install PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "FAIL ")
        # Threaded: the script runs every app launch with --emu-thread on (its own work dir) and fails on an owner violation.
        add_test(NAME cheat_community_install_threaded
            COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/../tests/cheat_community_smoke.py"
                    "$<TARGET_FILE:virtualjaguar>" "${VJ_ROOT}/site/cheats" "${_evil}" 5E2CDBC0
                    "${CMAKE_BINARY_DIR}/cheat-community-test_threaded" on)
        set_tests_properties(cheat_community_install_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
            FAIL_REGULAR_EXPRESSION "FAIL |owner violation|ThreadSanitizer")
    else()
        message(STATUS "VJCheatTests: no private Doom - Evil Unleashed ROM; cheat_community_install is not registered")
    endif()
endif()

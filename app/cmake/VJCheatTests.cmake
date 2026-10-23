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

add_test(NAME cheat_state
    COMMAND virtualjaguar --headless --frames 240 --state-roundtrip
            --cheat 001F0000:AB --expect-ram 1F0000=AB "${VJ_ROMS}/jagniccc.j64")

set(_pd "${CMAKE_BINARY_DIR}/cheat-persist-data")
set(_pc "${CMAKE_BINARY_DIR}/cheat-persist-control")
add_test(NAME cheat_persist_clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_pd}" "${_pc}")
# Run 1: yarc.cht (a fixture whose name matches the ROM's file name) supplies "Lives byte".
add_test(NAME cheat_persist_save
    COMMAND virtualjaguar --headless --frames 60 --data-dir "${_pd}" --cheats-dir "${_fx}"
            --cheat-on "Lives byte" --expect-ram 1F0010=05 "${_yarc}")
# Run 2: a new process, same data directory, NO cheat option: it must still be on.
add_test(NAME cheat_persist_restart
    COMMAND virtualjaguar --headless --frames 60 --data-dir "${_pd}" --cheats-dir "${_fx}"
            --expect-ram 1F0010=05 "${_yarc}")
# Control: the same, but a data directory nothing was saved to.
add_test(NAME cheat_persist_control
    COMMAND virtualjaguar --headless --frames 60 --data-dir "${_pc}" --cheats-dir "${_fx}"
            --expect-ram 1F0010=05 "${_yarc}")
set_tests_properties(cheat_persist_control PROPERTIES
    PASS_REGULAR_EXPRESSION "headless: FAIL ram 0x1F0010 is [0-9A-F]+, expected 05")
add_test(NAME cheat_persist_cleanup
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_pd}" "${_pc}")
set_tests_properties(cheat_persist_clean PROPERTIES FIXTURES_SETUP cheat_persist_fresh)
set_tests_properties(cheat_persist_save PROPERTIES
    FIXTURES_REQUIRED cheat_persist_fresh FIXTURES_SETUP cheat_persist_saved)
set_tests_properties(cheat_persist_restart cheat_persist_control PROPERTIES
    FIXTURES_REQUIRED "cheat_persist_fresh;cheat_persist_saved")
set_tests_properties(cheat_persist_cleanup PROPERTIES FIXTURES_CLEANUP "cheat_persist_fresh;cheat_persist_saved")

set_tests_properties(cheats_unit cheat_apply cheat_state cheat_persist_save cheat_persist_restart PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

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
    else()
        message(STATUS "VJCheatTests: no private Tempest 2000 ROM; the database end-to-end test is not registered")
    endif()
endif()

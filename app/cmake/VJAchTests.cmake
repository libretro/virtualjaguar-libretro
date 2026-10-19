# RetroAchievements headless tests (#836).  No network, no account, no private
# ROM: the server is a directory of canned replies (app/tests/ra/<api>.json) and
# the game is the in-tree public yarc.j64, so these run on CI.
#
#   ra_unlock_softcore   sign in (mock token), identify yarc, serve a tiny set
#                        whose conditions address RAM the way real Jaguar sets do
#                        (0xI = 16-bit BE, 0xG = 32-bit BE).  At frame 60 the CORE'S
#                        OWN CHEAT ENGINE writes a word and a long through the
#                        emulated 68000 bus -- independent of how the app reads RAM.
#                        Both BE achievements must unlock and an unlock request must
#                        go out; the byte nothing writes and the
#                        byte-swapped word (0xI = $CDAB) must NOT fire; and nothing may fire
#                        BEFORE the write.  --ra-expect-hash checks that rcheevos
#                        hashed the whole cartridge file (MD5, computed here at
#                        configure time, independently of the app).  Softcore also
#                        proves a state save writes its progress sidecar and a load
#                        is allowed.
#   ra_hardcore          same session in hardcore: hardcore is active, a state SAVE
#                        works, a state LOAD is refused, and cheats / rewind / slow
#                        motion / frame advance report disallowed.
#   ra_hardcore_late_signin  signing in with hardcore on while a game is already
#                        running restarts it exactly once (clean start); the mock
#                        answers from pump(), one frame later, like the network.
#   ra_cart_bin_extension  a .bin cartridge is hashed as a cartridge (it is a cart
#                        extension for this core, not a disc track).
#   ra_hardcore_blocked_setting  hardcore wanted but virtualjaguar_pal=enabled (which
#                        rcheevos' disallowed-settings list forbids): hardcore stays
#                        off, the blocker is named, and states load normally.
#   ra_cd_chd_unsupported  a .chd disc is not identified (rcheevos cannot read CHD);
#                        the game still runs and the app says why instead of hashing
#                        garbage.

set(VJ_RA_FIX "${CMAKE_CURRENT_LIST_DIR}/../tests/ra")
get_filename_component(VJ_RA_FIX "${VJ_RA_FIX}" ABSOLUTE)
file(MD5 "${VJ_ROMS}/yarc.j64" VJ_YARC_MD5)

# The saved-cheat chain's data directory (the twin gets "_threaded" appended, see vj_ra_test).
set(_rcd "${CMAKE_BINARY_DIR}/ra-cheat-saved-data")

# vj_ra_test(NAME <name> [PASS <regex>] [FIXTURES_SETUP <f>] [FIXTURES_REQUIRED <f;...>] ARGS <app arguments...>)
# Registers <name> (inline, as it always ran) and <name>_threaded: the same run with --emu-thread on (#967 phase 2),
# labelled emu_thread, so the TSan job runs it too.  Frame-numbered steps and pokes must land on the same frame in
# both.  A PASS regex makes it a control (it passes by failing for the stated reason); its twin then fails only on an
# owner violation or a TSan report.  In the twin, the saved-cheat data dir and the fixture names get a _threaded
# suffix, so the two chains never share state.
function(vj_ra_test)
    cmake_parse_arguments(RA "" "NAME;PASS;FIXTURES_SETUP" "FIXTURES_REQUIRED;ARGS" ${ARGN})
    add_test(NAME ${RA_NAME} COMMAND virtualjaguar --headless ${RA_ARGS})
    set(_twin)
    foreach(_a IN LISTS RA_ARGS)
        string(REPLACE "${_rcd}" "${_rcd}_threaded" _a "${_a}")
        list(APPEND _twin "${_a}")
    endforeach()
    add_test(NAME ${RA_NAME}_threaded COMMAND virtualjaguar --headless --emu-thread on ${_twin})
    set_tests_properties(${RA_NAME} ${RA_NAME}_threaded PROPERTIES TIMEOUT 300)
    set_tests_properties(${RA_NAME}_threaded PROPERTIES LABELS emu_thread)
    if(RA_PASS)
        set_tests_properties(${RA_NAME} ${RA_NAME}_threaded PROPERTIES PASS_REGULAR_EXPRESSION "${RA_PASS}")
        set_tests_properties(${RA_NAME}_threaded PROPERTIES FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer")
    else()
        set_tests_properties(${RA_NAME} PROPERTIES FAIL_REGULAR_EXPRESSION "headless: FAIL")
        set_tests_properties(${RA_NAME}_threaded PROPERTIES
            FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
    endif()
    if(RA_FIXTURES_SETUP)
        set_tests_properties(${RA_NAME} PROPERTIES FIXTURES_SETUP ${RA_FIXTURES_SETUP})
        set_tests_properties(${RA_NAME}_threaded PROPERTIES FIXTURES_SETUP ${RA_FIXTURES_SETUP}_threaded)
    endif()
    if(RA_FIXTURES_REQUIRED)
        set(_req)
        foreach(_f IN LISTS RA_FIXTURES_REQUIRED)
            list(APPEND _req ${_f}_threaded)
        endforeach()
        set_tests_properties(${RA_NAME} PROPERTIES FIXTURES_REQUIRED "${RA_FIXTURES_REQUIRED}")
        set_tests_properties(${RA_NAME}_threaded PROPERTIES FIXTURES_REQUIRED "${_req}")
    endif()
endfunction()

vj_ra_test(NAME ra_unlock_softcore
    ARGS --frames 240
            --ra-mock "${VJ_RA_FIX}"
            --ra-poke "60:00100010 ABCD"
            --ra-poke "60:00100020 1234"
            --ra-poke "60:00100022 5678"
            --ra-expect-unlock 1001 --ra-expect-unlock 1002
            --ra-expect-no-unlock 1003 --ra-expect-no-unlock 1004
            --ra-expect-request login2 --ra-expect-request awardachievement
            --ra-expect-hash ${VJ_YARC_MD5}
            --ra-expect-softcore --ra-expect-reset 0
            "${VJ_ROMS}/yarc.j64")

vj_ra_test(NAME ra_hardcore
    ARGS --frames 120
            --ra-mock "${VJ_RA_FIX}" --ra-hardcore
            --ra-expect-hardcore --ra-expect-reset 0
            --ra-expect-hash ${VJ_YARC_MD5}
            "${VJ_ROMS}/yarc.j64")

# Signing in LATE (frame 40, the game already running unrestricted) with hardcore
# wanted must restart the game once: a hardcore run starts from a clean boot.
vj_ra_test(NAME ra_hardcore_late_signin
    ARGS --frames 120
            --ra-mock "${VJ_RA_FIX}" --ra-hardcore --ra-login-at 40
            --ra-expect-hardcore --ra-expect-reset 1
            --ra-expect-hash ${VJ_YARC_MD5}
            "${VJ_ROMS}/yarc.j64")

# .bin is a CARTRIDGE extension for this core (not a disc track): it must be
# hashed as a cartridge, i.e. to the same MD5 as the .j64 it was copied from.
configure_file("${VJ_ROMS}/yarc.j64" "${CMAKE_CURRENT_BINARY_DIR}/yarc_as_cart.bin" COPYONLY)
vj_ra_test(NAME ra_cart_bin_extension
    ARGS --frames 60
            --ra-mock "${VJ_RA_FIX}" --ra-expect-request achievementsets
            --ra-expect-hash ${VJ_YARC_MD5}
            "${CMAKE_CURRENT_BINARY_DIR}/yarc_as_cart.bin")

vj_ra_test(NAME ra_hardcore_blocked_setting
    ARGS --frames 60
            --ra-mock "${VJ_RA_FIX}" --ra-hardcore --ra-expect-blocked
            --option virtualjaguar_pal=enabled
            "${VJ_ROMS}/yarc.j64")

vj_ra_test(NAME ra_cd_chd_unsupported
    ARGS --frames 120 --allow-blank
            --ra-mock "${VJ_RA_FIX}" --ra-expect-load-error
            "${VJ_ROMS}/synth_jagcd.chd")

# The HTTP transport against a throwaway local server (no internet).  Exit 77 =
# skipped (Windows, or a build without libcurl).
add_executable(ach_http_test tests/ach_http_test.cpp src/ach_http.cpp)
target_include_directories(ach_http_test PRIVATE src)
target_compile_definitions(ach_http_test PRIVATE ${VJ_ACH_DEFS})
target_link_libraries(ach_http_test PRIVATE ${VJ_ACH_LIBS} Threads::Threads)
add_test(NAME ach_http COMMAND ach_http_test)
set_tests_properties(ach_http PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

# The login token's storage policy (#836, security): migration out of config.ini, fallback when no
# system keyring answers, secondary copies, logout.  Mock backends only -- the real OS credential
# store is touched solely when VJ_TEST_REAL_KEYCHAIN=1 is set (never by default, never on CI).
add_executable(vj_secret_store_test tests/secret_store_test.cpp src/secret_store.cpp src/config.cpp src/paths.cpp)
target_include_directories(vj_secret_store_test PRIVATE src)
target_link_libraries(vj_secret_store_test PRIVATE ${VJ_SDL3_TARGET} Threads::Threads ${VJ_SECRET_LIBS})
if(MSVC)
    target_compile_options(vj_secret_store_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_secret_store_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_secret_store_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME secret_store_unit COMMAND vj_secret_store_test)
set_tests_properties(secret_store_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")

# The dashboard's logic and picture cache (no core, no window, no network): games-list parsing against
# the same fixtures the mock server answers with, library matching, the daily cache file, filter / sort,
# the image cache (disk, offline mode, LRU).
add_executable(vj_ra_dash_test tests/ra_dash_test.cpp src/ra_dash.cpp src/ra_images.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_ra_dash_test PRIVATE src)
target_compile_definitions(vj_ra_dash_test PRIVATE VJ_RA_FIX="${VJ_RA_FIX}")
target_link_libraries(vj_ra_dash_test PRIVATE vj_library vj_rcheevos ${VJ_SDL3_TARGET} Threads::Threads)
if(MSVC)
    target_compile_options(vj_ra_dash_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_ra_dash_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_ra_dash_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME ra_dash_unit COMMAND vj_ra_dash_test)
set_tests_properties(ra_dash_unit PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")

# The dashboard's requests through the mock server: the Jaguar games list (one fixture per console,
# systemgames_17 / _77) and the user's progress (allprogress_17 / _77) are fetched, parsed by rc_client and
# applied: 6 games, 3 of them with unlocks.  Nothing is requested before the step asks (the tests above
# fail on any request without a fixture, so a stray automatic fetch would show up there).
vj_ra_test(NAME ra_dashboard_mock
    ARGS --frames 90
            --ra-mock "${VJ_RA_FIX}"
            --ra-step 20:games:refresh --ra-step 70:games:expect:6:3
            --ra-expect-request systemgames --ra-expect-request allprogress
            "${VJ_ROMS}/yarc.j64")
# Control: a wrong expectation must fail the step (the test cannot pass by doing nothing).
vj_ra_test(NAME ra_dashboard_mock_control PASS "expected 7 games, have 6"
    ARGS --frames 90
            --ra-mock "${VJ_RA_FIX}"
            --ra-step 20:games:refresh --ra-step 70:games:expect:7:3
            "${VJ_ROMS}/yarc.j64")

# Optional, ROM-gated: point VJ_RA_TEST_CUE at any Jaguar CD .cue to prove rcheevos
# identifies a real disc (the second-session boot executable is hashed).  Skipped
# (not registered) when unset or missing, so CI -- which has no discs -- never sees it.
set(VJ_RA_TEST_CUE "" CACHE FILEPATH "Optional Jaguar CD .cue for the ra_cd_cue test")
if(VJ_RA_TEST_CUE AND EXISTS "${VJ_RA_TEST_CUE}")
    add_test(NAME ra_cd_cue
        COMMAND virtualjaguar --headless --frames 60 --allow-blank
                --ra-mock "${VJ_RA_FIX}" --ra-expect-request achievementsets
                "${VJ_RA_TEST_CUE}")
    set_tests_properties(ra_cd_cue PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "headless: FAIL")
endif()

# ---- cheats vs hardcore (#835 + #836) ---------------------------------------------------
# RetroAchievements forbids cheats in hardcore.  Main-RAM byte 1F0000 is unused by
# yarc; the cheat writes AB there, a test step zeroes it first so "unwritten" is
# something we KNOW (RAM is randomised at reset).  Every test has a control that
# must fail for the reason stated, so none can pass by doing nothing.
#
#   ra_cheat_softcore            softcore: the cheat applies (the baseline)
#   ra_cheat_hardcore_blocked    hardcore from the start: the byte stays 00 and the
#                                player is told why;  _control: the same run in
#                                softcore must NOT satisfy "stays 00"
#   ra_cheat_toggle              softcore -> hardcore ON mid-game (restarts the game,
#                                clears the cheat; nothing re-applies it, not even
#                                the restart) -> hardcore OFF: it comes back;
#                                _control: without the hardcore step it never stays 00
#   ra_cheat_saved_*             a cheat selection SAVED on disk (a database cheat of
#                                the yarc.cht fixture, per-game state under --data-dir):
#                                a hardcore run does not apply it and does not touch
#                                the file; the next softcore run still has it on.
set(_cx "--ra-mock" "${VJ_RA_FIX}")
set(_cb "--cheat" "001F0000:AB")
set(_ra_cheat_fx "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures")
get_filename_component(_ra_cheat_fx "${_ra_cheat_fx}" ABSOLUTE)

vj_ra_test(NAME ra_cheat_softcore
    ARGS --frames 100 ${_cx} ${_cb}
            --ra-step 20:poke:1F0000=00 --ra-step 80:expect:1F0000=AB "${VJ_ROMS}/yarc.j64")

vj_ra_test(NAME ra_cheat_hardcore_blocked
    ARGS --frames 100 ${_cx} --ra-hardcore ${_cb}
            --ra-step 20:poke:1F0000=00 --ra-step 80:expect:1F0000=00
            --ra-expect-notice "disabled in hardcore" "${VJ_ROMS}/yarc.j64")
vj_ra_test(NAME ra_cheat_hardcore_blocked_control PASS "RAM 0x1F0000 is AB, expected 00"
    ARGS --frames 100 ${_cx} ${_cb}
            --ra-step 20:poke:1F0000=00 --ra-step 80:expect:1F0000=00 "${VJ_ROMS}/yarc.j64")

vj_ra_test(NAME ra_cheat_toggle
    ARGS --frames 160 ${_cx} ${_cb}
            --ra-step 30:expect:1F0000=AB
            --ra-step 31:hardcore:on --ra-step 40:poke:1F0000=00 --ra-step 100:expect:1F0000=00
            --ra-step 101:hardcore:off --ra-step 150:expect:1F0000=AB
            --ra-expect-notice "Cheats are off" --ra-expect-notice "Cheats are back"
            --ra-expect-reset 1 "${VJ_ROMS}/yarc.j64")
vj_ra_test(NAME ra_cheat_toggle_control PASS "RAM 0x1F0000 is AB, expected 00"
    ARGS --frames 160 ${_cx} ${_cb}
            --ra-step 40:poke:1F0000=00 --ra-step 100:expect:1F0000=00 "${VJ_ROMS}/yarc.j64")

add_test(NAME ra_cheat_saved_clean COMMAND ${CMAKE_COMMAND} -E remove_directory "${_rcd}")
add_test(NAME ra_cheat_saved_clean_threaded COMMAND ${CMAKE_COMMAND} -E remove_directory "${_rcd}_threaded")
vj_ra_test(NAME ra_cheat_saved_save FIXTURES_REQUIRED ra_cheat_fresh FIXTURES_SETUP ra_cheat_saved
    ARGS --frames 100 ${_cx} --data-dir "${_rcd}" --cheats-dir "${_ra_cheat_fx}"
         --cheat-on "Lives byte" --ra-step 80:expect:1F0010=05 "${VJ_ROMS}/yarc.j64")
vj_ra_test(NAME ra_cheat_saved_hardcore FIXTURES_REQUIRED "ra_cheat_fresh;ra_cheat_saved"
    FIXTURES_SETUP ra_cheat_hardcore_done
    ARGS --frames 100 ${_cx} --ra-hardcore --data-dir "${_rcd}"
         --cheats-dir "${_ra_cheat_fx}"
         --ra-step 20:poke:1F0010=00 --ra-step 80:expect:1F0010=00
         --ra-expect-notice "1 saved cheat" "${VJ_ROMS}/yarc.j64")
vj_ra_test(NAME ra_cheat_saved_softcore_again
    FIXTURES_REQUIRED "ra_cheat_fresh;ra_cheat_saved;ra_cheat_hardcore_done"
    ARGS --frames 100 ${_cx} --data-dir "${_rcd}" --cheats-dir "${_ra_cheat_fx}"
         --ra-step 20:poke:1F0010=00 --ra-step 80:expect:1F0010=05 "${VJ_ROMS}/yarc.j64")
add_test(NAME ra_cheat_saved_cleanup COMMAND ${CMAKE_COMMAND} -E remove_directory "${_rcd}")
add_test(NAME ra_cheat_saved_cleanup_threaded COMMAND ${CMAKE_COMMAND} -E remove_directory "${_rcd}_threaded")
set_tests_properties(ra_cheat_saved_clean PROPERTIES FIXTURES_SETUP ra_cheat_fresh)
set_tests_properties(ra_cheat_saved_clean_threaded PROPERTIES FIXTURES_SETUP ra_cheat_fresh_threaded LABELS emu_thread)
set_tests_properties(ra_cheat_saved_cleanup PROPERTIES
    FIXTURES_CLEANUP "ra_cheat_fresh;ra_cheat_saved;ra_cheat_hardcore_done")
set_tests_properties(ra_cheat_saved_cleanup_threaded PROPERTIES LABELS emu_thread
    FIXTURES_CLEANUP "ra_cheat_fresh_threaded;ra_cheat_saved_threaded;ra_cheat_hardcore_done_threaded")

# A reply must activate the game on the core owner, serialized with do_frame. A metadata-only
# address fallback still means rc_client is being mutated off that owner and must fail this test.
add_test(NAME ra_reply_owner_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 60 --ra-mock "${VJ_RA_FIX}"
        --ra-expect-request login2
        --ra-expect-request achievementsets --ra-expect-request startsession "${VJ_ROMS}/yarc.j64")
set_tests_properties(ra_reply_owner_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "ra: OK"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer|address check off the emulation thread")

# AchEvent: an rc_client event copied so it can cross threads (#967 phase 2).
add_executable(vj_ach_event_test tests/ach_event_test.cpp src/ach_event.cpp)
target_include_directories(vj_ach_event_test PRIVATE src)
target_link_libraries(vj_ach_event_test PRIVATE vj_rcheevos)
add_test(NAME ach_event_unit COMMAND vj_ach_event_test)
set_tests_properties(ach_event_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")

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

add_test(NAME ra_unlock_softcore
    COMMAND virtualjaguar --headless --frames 240
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

add_test(NAME ra_hardcore
    COMMAND virtualjaguar --headless --frames 120
            --ra-mock "${VJ_RA_FIX}" --ra-hardcore
            --ra-expect-hardcore --ra-expect-reset 0
            --ra-expect-hash ${VJ_YARC_MD5}
            "${VJ_ROMS}/yarc.j64")

# Signing in LATE (frame 40, the game already running unrestricted) with hardcore
# wanted must restart the game once: a hardcore run starts from a clean boot.
add_test(NAME ra_hardcore_late_signin
    COMMAND virtualjaguar --headless --frames 120
            --ra-mock "${VJ_RA_FIX}" --ra-hardcore --ra-login-at 40
            --ra-expect-hardcore --ra-expect-reset 1
            --ra-expect-hash ${VJ_YARC_MD5}
            "${VJ_ROMS}/yarc.j64")

# .bin is a CARTRIDGE extension for this core (not a disc track): it must be
# hashed as a cartridge, i.e. to the same MD5 as the .j64 it was copied from.
configure_file("${VJ_ROMS}/yarc.j64" "${CMAKE_CURRENT_BINARY_DIR}/yarc_as_cart.bin" COPYONLY)
add_test(NAME ra_cart_bin_extension
    COMMAND virtualjaguar --headless --frames 60
            --ra-mock "${VJ_RA_FIX}" --ra-expect-request achievementsets
            --ra-expect-hash ${VJ_YARC_MD5}
            "${CMAKE_CURRENT_BINARY_DIR}/yarc_as_cart.bin")

add_test(NAME ra_hardcore_blocked_setting
    COMMAND virtualjaguar --headless --frames 60
            --ra-mock "${VJ_RA_FIX}" --ra-hardcore --ra-expect-blocked
            --option virtualjaguar_pal=enabled
            "${VJ_ROMS}/yarc.j64")

add_test(NAME ra_cd_chd_unsupported
    COMMAND virtualjaguar --headless --frames 120 --allow-blank
            --ra-mock "${VJ_RA_FIX}" --ra-expect-load-error
            "${VJ_ROMS}/synth_jagcd.chd")

set_tests_properties(ra_unlock_softcore ra_hardcore ra_hardcore_late_signin ra_cart_bin_extension
                     ra_hardcore_blocked_setting ra_cd_chd_unsupported PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

# The HTTP transport against a throwaway local server (no internet).  Exit 77 =
# skipped (Windows, or a build without libcurl).
add_executable(ach_http_test tests/ach_http_test.cpp src/ach_http.cpp)
target_include_directories(ach_http_test PRIVATE src)
target_compile_definitions(ach_http_test PRIVATE ${VJ_ACH_DEFS})
target_link_libraries(ach_http_test PRIVATE ${VJ_ACH_LIBS} Threads::Threads)
add_test(NAME ach_http COMMAND ach_http_test)
set_tests_properties(ach_http PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

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
add_test(NAME ra_dashboard_mock
    COMMAND virtualjaguar --headless --frames 90
            --ra-mock "${VJ_RA_FIX}"
            --ra-step 20:games:refresh --ra-step 70:games:expect:6:3
            --ra-expect-request systemgames --ra-expect-request allprogress
            "${VJ_ROMS}/yarc.j64")
# Control: a wrong expectation must fail the step (the test cannot pass by doing nothing).
add_test(NAME ra_dashboard_mock_control
    COMMAND virtualjaguar --headless --frames 90
            --ra-mock "${VJ_RA_FIX}"
            --ra-step 20:games:refresh --ra-step 70:games:expect:7:3
            "${VJ_ROMS}/yarc.j64")
set_tests_properties(ra_dashboard_mock ra_dashboard_mock_control PROPERTIES TIMEOUT 300)
set_tests_properties(ra_dashboard_mock PROPERTIES FAIL_REGULAR_EXPRESSION "headless: FAIL")
set_tests_properties(ra_dashboard_mock_control PROPERTIES PASS_REGULAR_EXPRESSION "expected 7 games, have 6")

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

add_test(NAME ra_cheat_softcore
    COMMAND virtualjaguar --headless --frames 100 ${_cx} ${_cb}
            --ra-step 20:poke:1F0000=00 --ra-step 80:expect:1F0000=AB "${VJ_ROMS}/yarc.j64")

add_test(NAME ra_cheat_hardcore_blocked
    COMMAND virtualjaguar --headless --frames 100 ${_cx} --ra-hardcore ${_cb}
            --ra-step 20:poke:1F0000=00 --ra-step 80:expect:1F0000=00
            --ra-expect-notice "disabled in hardcore" "${VJ_ROMS}/yarc.j64")
add_test(NAME ra_cheat_hardcore_blocked_control
    COMMAND virtualjaguar --headless --frames 100 ${_cx} ${_cb}
            --ra-step 20:poke:1F0000=00 --ra-step 80:expect:1F0000=00 "${VJ_ROMS}/yarc.j64")
set_tests_properties(ra_cheat_hardcore_blocked_control PROPERTIES
    PASS_REGULAR_EXPRESSION "RAM 0x1F0000 is AB, expected 00")

add_test(NAME ra_cheat_toggle
    COMMAND virtualjaguar --headless --frames 160 ${_cx} ${_cb}
            --ra-step 30:expect:1F0000=AB
            --ra-step 31:hardcore:on --ra-step 40:poke:1F0000=00 --ra-step 100:expect:1F0000=00
            --ra-step 101:hardcore:off --ra-step 150:expect:1F0000=AB
            --ra-expect-notice "Cheats are off" --ra-expect-notice "Cheats are back"
            --ra-expect-reset 1 "${VJ_ROMS}/yarc.j64")
add_test(NAME ra_cheat_toggle_control
    COMMAND virtualjaguar --headless --frames 160 ${_cx} ${_cb}
            --ra-step 40:poke:1F0000=00 --ra-step 100:expect:1F0000=00 "${VJ_ROMS}/yarc.j64")
set_tests_properties(ra_cheat_toggle_control PROPERTIES
    PASS_REGULAR_EXPRESSION "RAM 0x1F0000 is AB, expected 00")

set(_rcd "${CMAKE_BINARY_DIR}/ra-cheat-saved-data")
add_test(NAME ra_cheat_saved_clean COMMAND ${CMAKE_COMMAND} -E remove_directory "${_rcd}")
add_test(NAME ra_cheat_saved_save
    COMMAND virtualjaguar --headless --frames 100 ${_cx} --data-dir "${_rcd}" --cheats-dir "${_ra_cheat_fx}"
            --cheat-on "Lives byte" --ra-step 80:expect:1F0010=05 "${VJ_ROMS}/yarc.j64")
add_test(NAME ra_cheat_saved_hardcore
    COMMAND virtualjaguar --headless --frames 100 ${_cx} --ra-hardcore --data-dir "${_rcd}"
            --cheats-dir "${_ra_cheat_fx}"
            --ra-step 20:poke:1F0010=00 --ra-step 80:expect:1F0010=00
            --ra-expect-notice "1 saved cheat" "${VJ_ROMS}/yarc.j64")
add_test(NAME ra_cheat_saved_softcore_again
    COMMAND virtualjaguar --headless --frames 100 ${_cx} --data-dir "${_rcd}" --cheats-dir "${_ra_cheat_fx}"
            --ra-step 20:poke:1F0010=00 --ra-step 80:expect:1F0010=05 "${VJ_ROMS}/yarc.j64")
add_test(NAME ra_cheat_saved_cleanup COMMAND ${CMAKE_COMMAND} -E remove_directory "${_rcd}")
set_tests_properties(ra_cheat_saved_clean PROPERTIES FIXTURES_SETUP ra_cheat_fresh)
set_tests_properties(ra_cheat_saved_save PROPERTIES FIXTURES_REQUIRED ra_cheat_fresh FIXTURES_SETUP ra_cheat_saved)
set_tests_properties(ra_cheat_saved_hardcore PROPERTIES
    FIXTURES_REQUIRED "ra_cheat_fresh;ra_cheat_saved" FIXTURES_SETUP ra_cheat_hardcore_done)
set_tests_properties(ra_cheat_saved_softcore_again PROPERTIES
    FIXTURES_REQUIRED "ra_cheat_fresh;ra_cheat_saved;ra_cheat_hardcore_done")
set_tests_properties(ra_cheat_saved_cleanup PROPERTIES
    FIXTURES_CLEANUP "ra_cheat_fresh;ra_cheat_saved;ra_cheat_hardcore_done")

set_tests_properties(ra_cheat_softcore ra_cheat_hardcore_blocked ra_cheat_toggle ra_cheat_saved_save
                     ra_cheat_saved_hardcore ra_cheat_saved_softcore_again PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

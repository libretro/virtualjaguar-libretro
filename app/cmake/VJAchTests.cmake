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

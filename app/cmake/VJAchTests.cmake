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
            --ra-expect-softcore
            "${VJ_ROMS}/yarc.j64")

add_test(NAME ra_hardcore
    COMMAND virtualjaguar --headless --frames 120
            --ra-mock "${VJ_RA_FIX}" --ra-hardcore
            --ra-expect-hardcore
            --ra-expect-hash ${VJ_YARC_MD5}
            "${VJ_ROMS}/yarc.j64")

add_test(NAME ra_cd_chd_unsupported
    COMMAND virtualjaguar --headless --frames 120 --allow-blank
            --ra-mock "${VJ_RA_FIX}" --ra-expect-load-error
            "${VJ_ROMS}/synth_jagcd.chd")

set_tests_properties(ra_unlock_softcore ra_hardcore ra_cd_chd_unsupported PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

# Headless smoke tests.  They run the real binary with SDL's dummy video and
# audio drivers: no window, no GL, so they work on CI runners with no display.
#
#   smoke_yarc      boot the in-tree public ROM (test/roms/yarc.j64) under HLE
#                   with no BIOS, run N frames, and check the final frame's
#                   FNV-1a hash (same hash the test harness computes).
#   smoke_reload    yarc -> jagniccc -> yarc in ONE process, delivered as
#                   drop events.  Fails unless the two yarc runs end on the same
#                   frame, i.e. unless loading a different game in between left
#                   no static state behind (the iOS-style reset path).
#   smoke_cd        the synthetic Jaguar CD image, path-loaded (no BIOS).
#
# The expected hash is a single value for every OS and CPU: the core is
# deterministic (that is what run-ahead and netplay rely on) and the blitter's
# SSE2/NEON/scalar variants are bit-identical (test_blitter_compare).
# Regenerate with:  virtualjaguar --headless --frames 300 test/roms/yarc.j64

set(VJ_ROMS "${VJ_ROOT}/test/roms")
set(VJ_SMOKE_FRAMES 300 CACHE STRING "Frames per game in the smoke tests")
set(VJ_SMOKE_YARC_HASH "f4c30ea3" CACHE STRING "Expected final-frame hash of yarc.j64 (hex); empty disables the check")

if(VJ_SMOKE_YARC_HASH)
    set(_yarc_hash_args --expect-hash ${VJ_SMOKE_YARC_HASH})
else()
    set(_yarc_hash_args "")
endif()

add_test(NAME smoke_yarc
    COMMAND virtualjaguar --headless --frames ${VJ_SMOKE_FRAMES} ${_yarc_hash_args}
            "${VJ_ROMS}/yarc.j64")

add_test(NAME smoke_reload
    COMMAND virtualjaguar --headless --frames ${VJ_SMOKE_FRAMES}
            "${VJ_ROMS}/yarc.j64" "${VJ_ROMS}/jagniccc.j64" "${VJ_ROMS}/yarc.j64")

add_test(NAME smoke_cd
    COMMAND virtualjaguar --headless --frames 120 --allow-blank
            "${VJ_ROMS}/synth_jagcd.chd")

set_tests_properties(smoke_yarc smoke_reload smoke_cd PROPERTIES
    TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "headless: FAIL")

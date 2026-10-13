# VJCheatData.cmake -- optional download of the libretro cheat database for the
# Atari Jaguar (#835).
#
#   cmake ... -DVJ_APP_FETCH_CHEATS=ON
#
# downloads the 41 cht/"Atari - Jaguar"/*.cht files of libretro/libretro-database into
# <build>/cheats/, next to the executable, where the app looks for them
# (<executable dir>/cheats/).  Everything is pinned: ONE commit of the database and a
# SHA-256 per file (VJCheatManifest.cmake); a file that does not hash as expected is
# discarded, never used.  A failed download only warns -- the app works without the
# files, you can also point it at any folder of .cht files from the Cheats window or
# with --cheats-dir.
#
# License.  The libretro-database repository is CC BY-SA 4.0 (LICENSE at the pinned
# commit, downloaded alongside the data).  The Jaguar cheats are credited there to
# Gent of Emucheats.  The files are not vendored in this repository and are not part
# of the program: packaging (#839) decides whether a release bundles them, and must
# then ship the license text and the credit with them (the LICENSE and a README are
# written next to the .cht files for that reason).
option(VJ_APP_FETCH_CHEATS "Download the pinned libretro Jaguar cheat database (CC BY-SA 4.0) next to the program" OFF)

set(VJ_CHEATS_DIR "")
if(NOT VJ_APP_FETCH_CHEATS)
    return()
endif()

include(VJCheatManifest)
set(VJ_CHEATS_DIR "${CMAKE_BINARY_DIR}/cheats")
file(MAKE_DIRECTORY "${VJ_CHEATS_DIR}")

set(_vj_cheat_ok 0)
set(_vj_cheat_bad 0)
foreach(_entry IN LISTS VJ_CHEAT_DB_FILES)
    string(REPLACE "|" ";" _f "${_entry}")
    list(GET _f 0 _name)
    list(GET _f 1 _url)
    list(GET _f 2 _sha)
    set(_dest "${VJ_CHEATS_DIR}/${_name}")
    set(_have 0)
    if(EXISTS "${_dest}")
        file(SHA256 "${_dest}" _cur)
        if(_cur STREQUAL _sha)
            set(_have 1)
        endif()
    endif()
    if(NOT _have)
        file(DOWNLOAD "${VJ_CHEAT_DB_BASE}/${_url}" "${_dest}"
             EXPECTED_HASH SHA256=${_sha} STATUS _st TIMEOUT 60)
        list(GET _st 0 _rc)
        if(NOT _rc EQUAL 0)
            message(WARNING "VJCheatData: could not fetch '${_name}' (${_st}); the app will not have its cheats")
            file(REMOVE "${_dest}")
            math(EXPR _vj_cheat_bad "${_vj_cheat_bad}+1")
            continue()
        endif()
    endif()
    math(EXPR _vj_cheat_ok "${_vj_cheat_ok}+1")
endforeach()

file(DOWNLOAD "${VJ_CHEAT_DB_LICENSE_URL}" "${VJ_CHEATS_DIR}/LICENSE-libretro-database.txt"
     EXPECTED_HASH SHA256=${VJ_CHEAT_DB_LICENSE_SHA256} STATUS _st TIMEOUT 60)
list(GET _st 0 _rc)
if(NOT _rc EQUAL 0)
    message(WARNING "VJCheatData: could not fetch the database LICENSE (${_st})")
    file(REMOVE "${VJ_CHEATS_DIR}/LICENSE-libretro-database.txt")
endif()
file(WRITE "${VJ_CHEATS_DIR}/README.txt"
"Atari Jaguar cheats for Virtual Jaguar
=======================================

These .cht files are from the libretro cheat database
(https://github.com/libretro/libretro-database, folder cht/Atari - Jaguar),
commit ${VJ_CHEAT_DB_COMMIT}.

All cheats were created by Gent of Emucheats (https://emucheats.emulation64.com/).
The database is licensed under Creative Commons Attribution-ShareAlike 4.0
International; see LICENSE-libretro-database.txt.
")
message(STATUS "VJCheatData: ${_vj_cheat_ok} of 41 cheat files in ${VJ_CHEATS_DIR} (${_vj_cheat_bad} failed)")

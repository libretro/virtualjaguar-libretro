# VJCheatData.cmake -- the libretro cheat database for the Atari Jaguar (#835), bundled.
#
# By default the 41 cht/"Atari - Jaguar"/*.cht files of libretro/libretro-database are
# downloaded at configure time into <build>/cheats/, next to the executable, where the
# app looks for them (<executable dir>/cheats/), and `cmake --install` puts them in
# <prefix>/bin/cheats/ beside the program, together with the database LICENSE and a
# credit README, so the CC BY-SA 4.0 attribution travels with the files.  Anything a
# packaging step (#839) takes from the install tree gets them too.
#
#   -DVJ_APP_FETCH_CHEATS=OFF    no download, nothing bundled (offline / distro builds);
#                                the app still works: point it at any folder of .cht
#                                files from the Cheats window or with --cheats-dir
#
# Everything is pinned: ONE commit of the database and a SHA-256 per file
# (VJCheatManifest.cmake); a file that does not hash as expected is discarded, never
# used.  The fetch is fail-soft: a failed download only warns (the first failure stops
# the rest, so a machine with no network does not wait 41 times) and the configure
# succeeds.  The cheats are only installed when the download completed AND the license
# text arrived: files are never bundled without their license.
#
# License.  The libretro-database repository is CC BY-SA 4.0 (LICENSE at the pinned
# commit).  The Jaguar cheats are credited there to Gent of Emucheats.  The files are
# not committed to this repository; they are data next to the program, not linked into
# it.
option(VJ_APP_FETCH_CHEATS "Download the pinned libretro Jaguar cheat database (CC BY-SA 4.0) and bundle it with the program" ON)

set(VJ_CHEATS_DIR "")
if(NOT VJ_APP_FETCH_CHEATS)
    return()
endif()

include(VJCheatManifest)
set(VJ_CHEATS_DIR "${CMAKE_BINARY_DIR}/cheats")
file(MAKE_DIRECTORY "${VJ_CHEATS_DIR}")

list(LENGTH VJ_CHEAT_DB_FILES _vj_cheat_total)
set(_vj_cheat_ok 0)
set(_vj_cheat_bad 0)
set(_vj_cheat_offline 0)
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
        else()
            file(REMOVE "${_dest}")
        endif()
    endif()
    if(NOT _have)
        if(_vj_cheat_offline)
            math(EXPR _vj_cheat_bad "${_vj_cheat_bad}+1")
            continue()
        endif()
        # (No EXPECTED_HASH: with it a failed download is a hard configure error even
        # with STATUS.  The hash is checked below instead; a mismatch counts as a failure.)
        file(DOWNLOAD "${VJ_CHEAT_DB_BASE}/${_url}" "${_dest}" STATUS _st TIMEOUT 30)
        list(GET _st 0 _rc)
        if(_rc EQUAL 0)
            file(SHA256 "${_dest}" _cur)
            if(NOT _cur STREQUAL _sha)
                set(_st "1;sha256 ${_cur} is not the pinned ${_sha}")
                set(_rc 1)
            endif()
        endif()
        if(NOT _rc EQUAL 0)
            message(WARNING "VJCheatData: could not fetch '${_name}' (${_st}); skipping the rest. "
                            "The app works without the cheat files (configure with -DVJ_APP_FETCH_CHEATS=OFF to silence this).")
            file(REMOVE "${_dest}")
            set(_vj_cheat_offline 1)
            math(EXPR _vj_cheat_bad "${_vj_cheat_bad}+1")
            continue()
        endif()
    endif()
    math(EXPR _vj_cheat_ok "${_vj_cheat_ok}+1")
endforeach()

set(_vj_cheat_license "${VJ_CHEATS_DIR}/LICENSE-libretro-database.txt")
set(_vj_cheat_license_ok 0)
if(EXISTS "${_vj_cheat_license}")
    file(SHA256 "${_vj_cheat_license}" _cur)
    if(_cur STREQUAL VJ_CHEAT_DB_LICENSE_SHA256)
        set(_vj_cheat_license_ok 1)
    else()
        file(REMOVE "${_vj_cheat_license}")
    endif()
endif()
if(NOT _vj_cheat_license_ok AND NOT _vj_cheat_offline)
    file(DOWNLOAD "${VJ_CHEAT_DB_LICENSE_URL}" "${_vj_cheat_license}" STATUS _st TIMEOUT 30)
    list(GET _st 0 _rc)
    if(_rc EQUAL 0)
        file(SHA256 "${_vj_cheat_license}" _cur)
        if(NOT _cur STREQUAL VJ_CHEAT_DB_LICENSE_SHA256)
            set(_st "1;sha256 mismatch")
            set(_rc 1)
        endif()
    endif()
    if(_rc EQUAL 0)
        set(_vj_cheat_license_ok 1)
    else()
        message(WARNING "VJCheatData: could not fetch the database LICENSE (${_st})")
        file(REMOVE "${_vj_cheat_license}")
    endif()
endif()

file(WRITE "${VJ_CHEATS_DIR}/README.txt"
"Atari Jaguar cheats for Virtual Jaguar
=======================================

These .cht files are from the libretro cheat database
(https://github.com/libretro/libretro-database, folder cht/Atari - Jaguar),
commit ${VJ_CHEAT_DB_COMMIT}.

All cheats were created by Gent of Emucheats (https://emucheats.emulation64.com/).
The database is licensed under Creative Commons Attribution-ShareAlike 4.0
International; see LICENSE-libretro-database.txt.  Share these files under the same
terms, with this credit.
")
message(STATUS "VJCheatData: ${_vj_cheat_ok} of ${_vj_cheat_total} cheat files in ${VJ_CHEATS_DIR} (${_vj_cheat_bad} missing)")

# ---- install tree ----------------------------------------------------------------------------
# <prefix>/bin/cheats/ beside the executable (where the app looks), license + credit
# included.  Only when the whole set and its license arrived: never one without the other.
if(_vj_cheat_ok EQUAL _vj_cheat_total AND _vj_cheat_license_ok)
    set(VJ_CHEATS_BUNDLED ON)
    install(DIRECTORY "${VJ_CHEATS_DIR}/" DESTINATION bin/cheats
            FILES_MATCHING PATTERN "*.cht" PATTERN "LICENSE-libretro-database.txt" PATTERN "README.txt")
else()
    set(VJ_CHEATS_BUNDLED OFF)
    message(WARNING "VJCheatData: the cheat set is incomplete or has no license text; "
                    "it will NOT be installed (the program installs without cheats).")
endif()

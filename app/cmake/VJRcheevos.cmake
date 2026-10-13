# VJRcheevos.cmake -- RetroAchievements (#836): rcheevos fetched at configure
# time, pinned by SHA256 like SDL3 and Dear ImGui (nothing is vendored), built as
# a plain C static library, plus the HTTP transport for rc_client.
#
#   rcheevos v12.5.0 (MIT), commit 1433173220a7eaede6a9ed7a18e94117be1821e0
#   Offline/dev iteration: -DFETCHCONTENT_SOURCE_DIR_RCHEEVOS=<unpacked dir>
#
# What is compiled, and why:
#   * everything Package.swift (rcheevos' own SPM manifest) builds -- rc_client,
#     the runtime (rcheevos/), the request builders (rapi/), the hashers (rhash/);
#   * PLUS src/rc_libretro.c, which RetroArch also builds: it maps emulated
#     addresses onto the core's memory map and carries the per-core disallowed
#     settings list (e.g. virtualjaguar_pal=enabled is not allowed in hardcore);
#   * NOT rc_client_external.c / rc_client_raintegration.c (the RAIntegration
#     toolkit, Windows-only and unrelated to playing).
# RC_CLIENT_SUPPORTS_HASH turns on rc_client_begin_identify_and_load_game, so
# rc_client itself hashes the cartridge / disc.  RC_DISABLE_LUA: rich presence
# needs no Lua.
#
# HTTP: libcurl when found (optional -- without it the app still builds and
# the achievements UI says it is unavailable); WinHTTP on Windows.

include(FetchContent)

FetchContent_Declare(rcheevos
    URL      https://github.com/RetroAchievements/rcheevos/archive/refs/tags/v12.5.0.tar.gz
    URL_HASH SHA256=e6df83de4e18f0a19206e711e1e589624dcfcf6bf529c14f6dd2bb2d1ced983f
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_GetProperties(rcheevos)
if(NOT rcheevos_POPULATED)
    FetchContent_MakeAvailable(rcheevos)
endif()

set(_rc "${rcheevos_SOURCE_DIR}")
file(GLOB _rc_runtime CONFIGURE_DEPENDS "${_rc}/src/rcheevos/*.c")
file(GLOB _rc_rapi    CONFIGURE_DEPENDS "${_rc}/src/rapi/*.c")
file(GLOB _rc_rhash   CONFIGURE_DEPENDS "${_rc}/src/rhash/*.c")
add_library(vj_rcheevos STATIC
    "${_rc}/src/rc_client.c"
    "${_rc}/src/rc_compat.c"
    "${_rc}/src/rc_util.c"
    "${_rc}/src/rc_version.c"
    "${_rc}/src/rc_libretro.c"
    ${_rc_runtime} ${_rc_rapi} ${_rc_rhash})
target_include_directories(vj_rcheevos
    PUBLIC  "${_rc}/include" "${_rc}/src"
    PRIVATE "${_rc}/src/rcheevos" "${_rc}/src/rapi" "${_rc}/src/rhash"
            "${VJ_ROOT}/libretro-common/include")
target_compile_definitions(vj_rcheevos PUBLIC RC_CLIENT_SUPPORTS_HASH)
target_compile_definitions(vj_rcheevos PRIVATE RC_DISABLE_LUA)
set_target_properties(vj_rcheevos PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE C)
# rc_libretro.h includes <libretro.h> for every consumer.
target_include_directories(vj_rcheevos PUBLIC "${VJ_ROOT}/libretro-common/include")
find_package(Threads REQUIRED)
target_link_libraries(vj_rcheevos PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(vj_rcheevos PRIVATE /W1)
    target_compile_definitions(vj_rcheevos PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
else()
    target_compile_options(vj_rcheevos PRIVATE -w)   # third-party code: not ours to warn about
endif()

# ---- HTTP transport (src/ach_http.cpp) --------------------------------------
set(VJ_ACH_LIBS "")
set(VJ_ACH_DEFS "")
if(WIN32)
    list(APPEND VJ_ACH_LIBS winhttp)
else()
    find_package(CURL QUIET)
    if(CURL_FOUND)
        list(APPEND VJ_ACH_LIBS CURL::libcurl)
        list(APPEND VJ_ACH_DEFS VJ_HAVE_CURL)
    endif()
endif()
if(VJ_ACH_DEFS OR WIN32)
    message(STATUS "RetroAchievements HTTP: ${VJ_ACH_LIBS}")
else()
    message(STATUS "RetroAchievements HTTP: none (libcurl not found; the feature will report itself unavailable)")
endif()

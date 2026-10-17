# Virtual Jaguar standalone app -- SDL3 + Dear ImGui + OpenGL 3.3 core.
#
# Build (from the repo root):
#   cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build app/build -j
#   ctest --test-dir app/build -C Release --output-on-failure
#
# The core (C89) is compiled from the sources listed in ../Makefile.common and
# linked statically; the app (C++17) drives it through the retro_* API only.
# The core's own Makefile build is unaffected by anything in this directory.

cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The app ships inside the core's releases, so it carries the core's version:
# CORE_BASE_VERSION in the top-level Makefile (e.g. "v3.9.1").  One source of
# truth means the packaging tag check, the About box, package metadata and the
# HTTP / RetroAchievements User-Agent can never drift from the core.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../Makefile" _vj_core_ver_line
     REGEX "^CORE_BASE_VERSION[ \t]*:=")
string(REGEX REPLACE "^CORE_BASE_VERSION[ \t]*:=[ \t]*v?([0-9]+\\.[0-9]+\\.[0-9]+).*$" "\\1"
       VJ_CORE_VERSION "${_vj_core_ver_line}")
if(NOT VJ_CORE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "could not read CORE_BASE_VERSION from ../Makefile (got '${_vj_core_ver_line}')")
endif()

project(VirtualJaguar LANGUAGES C CXX
        VERSION ${VJ_CORE_VERSION}
        DESCRIPTION "Virtual Jaguar - Atari Jaguar emulator")

# macOS also compiles Objective-C++ (src/platform/platform_macos.mm: the native menu bar, Dock menu).
# Use the C++ compiler for it: CMake otherwise searches PATH on its own and can pick a different
# clang (e.g. an old Swift toolchain's) that cannot parse the current SDK headers.
if(APPLE)
    if(NOT DEFINED CACHE{CMAKE_OBJCXX_COMPILER})
        set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}")
    endif()
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED OFF)   # the core is built with the toolchain default
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Packaging (#839): install layout, Windows resources/subsystem, Linux desktop files.
# Deferred to the end of this file, so it sees every target and every feature's install().
include("${CMAKE_CURRENT_LIST_DIR}/cmake/VJPackage.cmake")

option(VJ_APP_SYSTEM_SDL3 "Use an installed SDL3 (find_package) instead of fetching one" OFF)
option(VJ_APP_BUILD_TESTS "Build the headless smoke tests (CTest)" ON)
option(VJ_APP_LTO "Link-time optimisation (IPO) for the core, libchdr and the app in Release / RelWithDebInfo builds" OFF)
# Discord Rich Presence (#833).  The default is the project's "Virtual Jaguar" application in the Discord developer
# portal (an application ID is public, not a secret).  CI can override it with the DISCORD_APP_ID repository variable,
# users with config key discord.app_id.  A fork builds with its own ID, or -DVJ_DISCORD_APP_ID= (empty) to switch the
# feature off (Settings > Discord then says why).
set(VJ_DISCORD_APP_ID "1559739062682787870" CACHE STRING "Discord application ID (digits) for Rich Presence; empty = none built in")
if(NOT VJ_DISCORD_APP_ID STREQUAL "" AND NOT VJ_DISCORD_APP_ID MATCHES "^[0-9]+$")
    message(FATAL_ERROR "VJ_DISCORD_APP_ID must be the numeric application ID from the Discord developer portal (got '${VJ_DISCORD_APP_ID}')")
endif()
option(VJ_APP_WERROR "Treat warnings in app/ sources as errors" OFF)
option(VJ_APP_HTTP "Box art and database downloads (libcurl; WinHTTP on Windows). Off or missing: the library works from files and cached art" ON)
option(VJ_APP_FREETYPE "Rasterise UI fonts with FreeType (hinting + anti-aliasing choices; fetched, pinned, built static). Off: Dear ImGui's built-in stb_truetype" ON)
option(VJ_APP_BUNDLE_DB "Fetch the pinned libretro-database dat files at configure time and copy them next to the binary" ON)
set(VJ_APP_LIBRETRO_DB_DIR "" CACHE PATH
    "Offline builds: a folder holding 'Atari - Jaguar.dat' and 'Atari - Jaguar CD.dat' to use instead of downloading them")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# ---------------------------------------------------------------------------
# Dependencies (pinned; bump deliberately).
#   SDL3   release-3.4.16 (zlib)   Dear ImGui v1.92.8-docking (MIT)   rcheevos v12.5.0 (MIT)
# For offline/dev iteration: -DFETCHCONTENT_SOURCE_DIR_SDL3=<dir>
#                            -DFETCHCONTENT_SOURCE_DIR_IMGUI=<dir>
#                            -DFETCHCONTENT_SOURCE_DIR_FREETYPE=<dir>   (VJ_APP_FREETYPE, FreeType 2.14.1)
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(VJ_APP_SYSTEM_SDL3)
    find_package(SDL3 REQUIRED CONFIG)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3)
endif()

FetchContent_Declare(imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8-docking.tar.gz
    URL_HASH SHA256=ca0653454ed371b7a87e9b0bc29a5d15c9be7f7c0fbe778042fc48c71df1d3d8
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_MakeAvailable(imgui)
endif()

# FreeType (VJ_APP_FREETYPE, default ON; -DVJ_APP_FREETYPE=OFF falls back to stb_truetype): better small-size
# clarity through hinting.  FreeType License (BSD-like
# with a credit clause; FreeType documents it as compatible with GPLv3) -- see packaging/THIRD-PARTY-NOTICES.txt.
# Built static with every optional library switched off, so it needs nothing from the system.
if(VJ_APP_FREETYPE)
    set(FT_DISABLE_ZLIB   ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2  ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG    ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON  CACHE BOOL "" FORCE)
    set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    set(_vj_saved_shared ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(freetype
        URL      https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-1.tar.gz
        URL_HASH SHA256=44bd69d1f0750603410cfd4f26f1c5523c5a3a087a2dfa8639b757dc7c6677be
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(freetype)
    set(BUILD_SHARED_LIBS ${_vj_saved_shared})
endif()

if(TARGET SDL3::SDL3)
    set(VJ_SDL3_TARGET SDL3::SDL3)
elseif(TARGET SDL3::SDL3-static)
    set(VJ_SDL3_TARGET SDL3::SDL3-static)
else()
    message(FATAL_ERROR "No SDL3 target found")
endif()

add_library(vj_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(vj_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(vj_imgui PUBLIC ${VJ_SDL3_TARGET})
if(VJ_APP_FREETYPE)
    # Compiled as a separate source, NOT via IMGUI_ENABLE_FREETYPE: stb_truetype stays the atlas default,
    # so the Retro font renders exactly as before and FreeType is chosen per font (fonts_imgui.cpp).
    target_sources(vj_imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
    target_include_directories(vj_imgui PUBLIC ${imgui_SOURCE_DIR}/misc/freetype)
    target_link_libraries(vj_imgui PUBLIC freetype)
    target_compile_definitions(vj_imgui PUBLIC VJ_HAVE_FREETYPE=1)
endif()
if(APPLE)
    target_compile_definitions(vj_imgui PUBLIC GL_SILENCE_DEPRECATION)
endif()

# ---------------------------------------------------------------------------
# The core.
# ---------------------------------------------------------------------------
# Optimised builds only: Release and RelWithDebInfo get LTO when it is asked for
# and the toolchain supports it; Debug never does.
set(VJ_LTO_ENABLED OFF)
if(VJ_APP_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _vj_ipo OUTPUT _vj_ipo_msg LANGUAGES C CXX)
    if(_vj_ipo)
        set(VJ_LTO_ENABLED ON)
    else()
        message(WARNING "VJ_APP_LTO requested but the toolchain has no IPO support: ${_vj_ipo_msg}")
    endif()
endif()
message(STATUS "App LTO: ${VJ_LTO_ENABLED}  (build type: ${CMAKE_BUILD_TYPE}${CMAKE_CONFIGURATION_TYPES})")
function(vj_optimise target)
    if(VJ_LTO_ENABLED)
        set_target_properties(${target} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
endfunction()
include(VJCore)

# ---------------------------------------------------------------------------
# Game library (#838): scanning, identification, box art.  vj_library has no GL
# and no ImGui, so the headless tests (app/tests) link it too.
# HTTP: SDL3 has no client.  libcurl where the platform ships it (macOS SDK,
# libcurl4-openssl-dev on Linux), WinHTTP on Windows (part of the OS, no
# dependency).  Neither found: the feature degrades to "no downloads".
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
add_library(vj_library STATIC src/library.cpp src/library_lists.cpp src/library_service.cpp src/bios.cpp src/disc_hash.cpp src/http.cpp)
target_compile_definitions(vj_library PRIVATE VJ_APP_VERSION="${PROJECT_VERSION}")   # http.cpp User-Agent
target_include_directories(vj_library PUBLIC src)
target_link_libraries(vj_library PUBLIC ${VJ_SDL3_TARGET} Threads::Threads)
# .chd track-1 checksums for disc identification (the same libchdr the core reads CHDs with)
target_include_directories(vj_library PRIVATE "${VJ_ROOT}/deps/libchdr/include")
target_link_libraries(vj_library PRIVATE vj_libchdr)
set(VJ_HTTP_BACKEND "none")
if(VJ_APP_HTTP)
    if(WIN32)
        target_compile_definitions(vj_library PRIVATE VJ_HTTP_WINHTTP)
        target_link_libraries(vj_library PRIVATE winhttp)
        set(VJ_HTTP_BACKEND "WinHTTP")
    else()
        find_package(CURL QUIET)
        if(CURL_FOUND)
            target_compile_definitions(vj_library PRIVATE VJ_HTTP_CURL)
            target_link_libraries(vj_library PRIVATE CURL::libcurl)
            set(VJ_HTTP_BACKEND "libcurl")
        endif()
    endif()
endif()
message(STATUS "App HTTP backend: ${VJ_HTTP_BACKEND}")
message(STATUS "App fonts via FreeType: ${VJ_APP_FREETYPE}  (OFF = stb_truetype)")
if(MSVC)
    target_compile_options(vj_library PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_library PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_library PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(VJ_APP_WERROR)
        target_compile_options(vj_library PRIVATE -Werror)
    endif()
endif()

# libretro-database dat files (CC-BY-SA-4.0; never committed here).  Pinned to a
# commit and SHA256 like SDL3 and Dear ImGui; copied next to the binary.
set(VJ_DB_COMMIT 12c21cf79f1e20eff6ac68b80875816c717d0a7a)
set(VJ_DB_DIR "${CMAKE_BINARY_DIR}/libretro-db")
set(VJ_DB_HAVE OFF)
if(VJ_APP_BUNDLE_DB)
    file(MAKE_DIRECTORY "${VJ_DB_DIR}")
    set(_vj_db_names "Atari - Jaguar.dat" "Atari - Jaguar CD.dat")
    set(_vj_db_paths "no-intro/Atari%20-%20Jaguar.dat" "redump/Atari%20-%20Jaguar%20CD.dat")
    set(_vj_db_sha256
        "704fdf2f14e14bdae07836f2ff5600c223b38df870d81dbcd3acc0f04c846299"
        "6cb715e076d7a3a3e106d33c73207aa8068256533bc2b4e08eb6e1ce19d3cc8f")
    set(_vj_db_ok 0)
    foreach(_i RANGE 0 1)
        list(GET _vj_db_names ${_i} _n)
        list(GET _vj_db_paths ${_i} _p)
        list(GET _vj_db_sha256 ${_i} _h)
        if(VJ_APP_LIBRETRO_DB_DIR AND EXISTS "${VJ_APP_LIBRETRO_DB_DIR}/${_n}")
            configure_file("${VJ_APP_LIBRETRO_DB_DIR}/${_n}" "${VJ_DB_DIR}/${_n}" COPYONLY)
            math(EXPR _vj_db_ok "${_vj_db_ok} + 1")
        else()
            file(DOWNLOAD
                "https://raw.githubusercontent.com/libretro/libretro-database/${VJ_DB_COMMIT}/metadat/${_p}"
                "${VJ_DB_DIR}/${_n}"
                EXPECTED_HASH SHA256=${_h}
                TLS_VERIFY ON
                STATUS _st)
            list(GET _st 0 _rc)
            if(_rc EQUAL 0)
                math(EXPR _vj_db_ok "${_vj_db_ok} + 1")
            else()
                file(REMOVE "${VJ_DB_DIR}/${_n}")
                message(WARNING "Could not fetch '${_n}' (${_st}). The game library will title games from file names "
                                "until 'Update database' is used; for offline builds set -DVJ_APP_LIBRETRO_DB_DIR=<dir>.")
            endif()
        endif()
    endforeach()
    if(_vj_db_ok GREATER 0)
        set(VJ_DB_HAVE ON)
    endif()
endif()

# ---------------------------------------------------------------------------
# The app.
# ---------------------------------------------------------------------------
set(VJ_APP_SOURCES
    src/main.cpp
    src/app.cpp
    src/paths.cpp
    src/instance.cpp
    src/config.cpp
    src/core_host.cpp
    src/audio.cpp
    src/video.cpp
    src/present_util.cpp
    src/shaders.cpp
    src/shaders_ui.cpp
    src/input.cpp
    src/hold_arbiter.cpp
    src/remap.cpp
    src/remap_ui.cpp
    src/devices.cpp
    src/router.cpp
    src/cheatsheet.cpp
    src/cheatsheet_ui.cpp
    src/devices_ui.cpp
    src/ui.cpp
    src/theme.cpp
    src/theme_style.cpp
    src/theme_custom.cpp
    src/theme_ui.cpp
    src/fonts.cpp
    src/fonts_system.cpp
    src/fonts_imgui.cpp
    src/fonts_ui.cpp
    src/settings_reset.cpp
    src/settings_reset_ui.cpp
    src/library_ui.cpp
    src/bios_ui.cpp
    src/cheats.cpp
    src/cheats_core.cpp
    src/cheats_ui.cpp
    src/achievements.cpp
    src/achievements_ui.cpp
    src/achievements_games.cpp
    src/ra_dash.cpp
    src/ra_images.cpp
    src/ach_http.cpp
    src/secret_store.cpp
    src/netplay.cpp
    src/netplay_addr.cpp
    src/netplay_ui.cpp
    # Debug tools (#652 / #833): the session, the Tools menu and one window (+ its pure logic) per tool.
    src/debug/debug_session.cpp
    src/debug/debug_ui.cpp
    src/debug/memory_view.cpp
    src/debug/memory_ui.cpp
    src/debug/clut_ui.cpp
    src/debug/cheat_scan.cpp
    src/debug/scanner_actions.cpp
    src/debug/scanner_ui.cpp
    src/debug/op_decode.cpp
    src/debug/op_render.cpp
    src/debug/op_view.cpp
    src/debug/op_ui.cpp
    src/debug/texture_pack.cpp
    src/debug/texture_ui.cpp
    src/debug/blitter_log.cpp
    src/debug/blitter_ui.cpp
    src/debug/audio_ring.cpp
    src/debug/scope_ui.cpp
    src/desktop.cpp
    src/tv_script.cpp
    src/platform/commands.cpp
    src/platform/events.cpp
    src/platform/platform.cpp
    src/platform/tray.cpp
    src/platform/viewports.cpp
    src/tv/tv_focus.cpp
    src/tv/tv_nav.cpp
    src/tv/tv_style.cpp
    src/tv/tv_shell.cpp
    src/tv/tv_app_host.cpp
    src/tv/tv_ingame.cpp
    src/tv/tv_widgets.cpp
    src/discord_ipc.cpp
    src/discord_presence.cpp
    src/discord_app.cpp
    src/neuro_json.cpp src/neuro_ws.cpp src/neuro_actions.cpp src/neuro_profiles.cpp src/neuro_link.cpp src/neuro_agent.cpp
    src/neuro_app.cpp)

# Native-OS integration (#833): one source per OS behind src/platform/platform_os.h.
if(APPLE)
    list(APPEND VJ_APP_SOURCES src/platform/platform_macos.mm)
    set_source_files_properties(src/platform/platform_macos.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc;-Wno-deprecated-declarations")
elseif(WIN32)
    list(APPEND VJ_APP_SOURCES src/platform/platform_windows.cpp)
else()
    list(APPEND VJ_APP_SOURCES src/platform/platform_other.cpp)
endif()

# RetroAchievements (#836): rcheevos + HTTP transport.
include(VJRcheevos)

# The login token's home (secret_store.cpp): macOS Keychain (Security.framework), Windows Credential
# Manager (advapi32), Linux libsecret loaded with dlopen at run time (no build dependency).
if(APPLE)
    find_library(VJ_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(VJ_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    set(VJ_SECRET_LIBS ${VJ_SECURITY_FRAMEWORK} ${VJ_COREFOUNDATION_FRAMEWORK})
elseif(WIN32)
    set(VJ_SECRET_LIBS advapi32)
else()
    set(VJ_SECRET_LIBS ${CMAKE_DL_LIBS})
endif()

include(VJShaders)   # librashader headers (pinned), vj_app_shaders(), shader tests

add_executable(virtualjaguar ${VJ_APP_SOURCES})
vj_app_shaders(virtualjaguar)
vj_app_shader_assets(virtualjaguar)
set_target_properties(virtualjaguar PROPERTIES OUTPUT_NAME virtualjaguar)
vj_optimise(virtualjaguar)
target_include_directories(virtualjaguar PRIVATE src src/debug "${VJ_ROOT}/src/debug")
target_link_libraries(virtualjaguar PRIVATE vj_core vj_library vj_imgui vj_rcheevos ${VJ_SDL3_TARGET} ${VJ_ACH_LIBS} ${VJ_SECRET_LIBS})
target_compile_definitions(virtualjaguar PRIVATE ${VJ_ACH_DEFS})
message(STATUS "Discord Rich Presence application ID: ${VJ_DISCORD_APP_ID}")
if(WIN32)
    target_link_libraries(virtualjaguar PRIVATE iphlpapi ws2_32)   # netplay_addr.cpp: this machine's addresses
    target_link_libraries(virtualjaguar PRIVATE shell32)           # platform_windows.cpp: SHAddToRecentDocs, ShellExecuteW
endif()
if(APPLE)
    target_link_libraries(virtualjaguar PRIVATE "-framework Cocoa")
endif()
target_compile_definitions(virtualjaguar PRIVATE
    VJ_APP_VERSION="${PROJECT_VERSION}"
    VJ_DISCORD_APP_ID="${VJ_DISCORD_APP_ID}")
if(MSVC)
    target_compile_options(virtualjaguar PRIVATE /W3 /utf-8)
    target_compile_definitions(virtualjaguar PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(VJ_APP_WERROR)
        target_compile_options(virtualjaguar PRIVATE /WX)
    endif()
else()
    target_compile_options(virtualjaguar PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(VJ_APP_WERROR)
        target_compile_options(virtualjaguar PRIVATE -Werror)
    endif()
endif()
if(VJ_DB_HAVE)
    add_custom_command(TARGET virtualjaguar POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${VJ_DB_DIR}" "$<TARGET_FILE_DIR:virtualjaguar>/libretro-db")
endif()
# Optional: sign the dev executable after every build (macOS).  A stable signature stops macOS
# re-asking for network/keychain access after each rebuild.  Uses VJ_SIGN_IDENTITY / .env / the
# login keychain like app/packaging/local-package.sh, else ad hoc.  Never fails the build.
# On by default on macOS: a plain local build is signed with your Developer ID / Apple Development
# identity when the keychain has one (ad hoc otherwise).  CI and packaging sign separately.
if(APPLE AND NOT DEFINED ENV{CI})
    set(_vj_sign_dev_default ON)
else()
    set(_vj_sign_dev_default OFF)
endif()
option(VJ_APP_SIGN_DEV_BUILD "Sign the dev executable after each build (macOS, local use)" ${_vj_sign_dev_default})
if(VJ_APP_SIGN_DEV_BUILD AND APPLE)
    add_custom_command(TARGET virtualjaguar POST_BUILD
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/packaging/local-package.sh"
                --sign-exe "$<TARGET_FILE:virtualjaguar>"
        VERBATIM)
endif()
# Windows: the executable keeps the console subsystem for now.  A GUI-subsystem
# build would swallow the headless smoke test's stdout; packaging (#839) decides.

# Install layout: the executable in <prefix>/bin; data that belongs beside it (the
# bundled cheat database, VJCheatData.cmake) goes to <prefix>/bin/cheats.
install(TARGETS virtualjaguar RUNTIME DESTINATION bin)

# CPU debugger (debug tools WP4): vendored Musashi disassembler + cpu_view / cpu_ui + their tests.
include(VJDebugCpu)

# The libretro Jaguar cheat database (#835): pinned download, bundled by default.
include(VJCheatData)

# ---------------------------------------------------------------------------
# Headless smoke tests (SDL dummy drivers, no window, no GL).
# ---------------------------------------------------------------------------
if(VJ_APP_BUILD_TESTS)
    enable_testing()
    include(VJSmokeTests)
    include(VJAchTests)
    include(VJCheatTests)
    include(VJNetplayTests)
    include(VJDebugTests)
    include(VJPlatformTests)
    include(VJDiscordTests)
    include(VJNeuroTests)
    include(VJThemeTests)
    include(VJTvTests)
endif()

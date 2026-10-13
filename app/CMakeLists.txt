# Virtual Jaguar 4.0 standalone app -- SDL3 + Dear ImGui + OpenGL 3.3 core.
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

project(VirtualJaguar LANGUAGES C CXX
        VERSION 4.0.0
        DESCRIPTION "Virtual Jaguar - Atari Jaguar emulator")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED OFF)   # the core is built with the toolchain default
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(VJ_APP_SYSTEM_SDL3 "Use an installed SDL3 (find_package) instead of fetching one" OFF)
option(VJ_APP_BUILD_TESTS "Build the headless smoke tests (CTest)" ON)
option(VJ_APP_WERROR "Treat warnings in app/ sources as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# ---------------------------------------------------------------------------
# Dependencies (pinned; bump deliberately).
#   SDL3   release-3.4.16 (zlib)   Dear ImGui v1.92.8 (MIT)
# For offline/dev iteration: -DFETCHCONTENT_SOURCE_DIR_SDL3=<dir>
#                            -DFETCHCONTENT_SOURCE_DIR_IMGUI=<dir>
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
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8.tar.gz
    URL_HASH SHA256=fecb33d33930e12ff53a34064e9d3a06c8f7c3e04408f14cd36c80e3faac863b
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_MakeAvailable(imgui)
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
if(APPLE)
    target_compile_definitions(vj_imgui PUBLIC GL_SILENCE_DEPRECATION)
endif()

# ---------------------------------------------------------------------------
# The core.
# ---------------------------------------------------------------------------
include(VJCore)

# ---------------------------------------------------------------------------
# The app.
# ---------------------------------------------------------------------------
set(VJ_APP_SOURCES
    src/main.cpp
    src/app.cpp
    src/paths.cpp
    src/config.cpp
    src/core_host.cpp
    src/audio.cpp
    src/video.cpp
    src/input.cpp
    src/ui.cpp
    src/cheats.cpp
    src/cheats_core.cpp
    src/cheats_ui.cpp)

add_executable(virtualjaguar ${VJ_APP_SOURCES})
set_target_properties(virtualjaguar PROPERTIES OUTPUT_NAME virtualjaguar)
target_include_directories(virtualjaguar PRIVATE src)
target_link_libraries(virtualjaguar PRIVATE vj_core vj_imgui ${VJ_SDL3_TARGET})
target_compile_definitions(virtualjaguar PRIVATE
    VJ_APP_VERSION="${PROJECT_VERSION}")
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
# Windows: the executable keeps the console subsystem for now.  A GUI-subsystem
# build would swallow the headless smoke test's stdout; packaging (#839) decides.

# Install layout: the executable in <prefix>/bin; data that belongs beside it (the
# bundled cheat database, VJCheatData.cmake) goes to <prefix>/bin/cheats.
install(TARGETS virtualjaguar RUNTIME DESTINATION bin)

# The libretro Jaguar cheat database (#835): pinned download, bundled by default.
include(VJCheatData)

# ---------------------------------------------------------------------------
# Headless smoke tests (SDL dummy drivers, no window, no GL).
# ---------------------------------------------------------------------------
if(VJ_APP_BUILD_TESTS)
    enable_testing()
    include(VJSmokeTests)
    include(VJCheatTests)
endif()

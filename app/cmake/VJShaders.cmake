# Shader support (issue #834): RetroArch .slangp presets through librashader,
# loaded at RUNTIME.  Nothing third-party is vendored and no Rust toolchain is
# needed: at configure time we fetch librashader's two MIT-licensed C headers
# (pinned by SHA256) and compile against them; the MPL-2.0 library itself is
# found (dlopen / LoadLibrary) when the app runs.  If the headers cannot be
# fetched the app still builds, with shaders reported as unsupported.
#
#   VJ_APP_SHADERS              ON   build the shader path at all
#   VJ_LIBRASHADER_INCLUDE_DIR  ""   use librashader.h + librashader_ld.h from here (offline)
#   VJ_APP_FETCH_LIBRASHADER    ON*  also download the prebuilt library (Apple silicon macOS and
#                                    Windows; pinned by SHA256) and copy it beside the executable.
#                                    *ON on macOS and Windows, OFF elsewhere; set OFF for
#                                    offline builds.
#                                    Upstream publishes no Linux binary and its Intel-macOS zip
#                                    holds an arm64 dylib, so those come from source:
#   VJ_LIBRASHADER_LIB          ""   a librashader.{so,dylib,dll} you built (CI builds it from the
#                                    pinned source, app/packaging/librashader/build-from-source.sh):
#                                    copied beside the executable and shipped by `cmake --install`.
#                                    Wins over VJ_APP_FETCH_LIBRASHADER.  Without either, the
#                                    app finds a system copy (distro/OBS package) at run time.
#   VJ_APP_BUNDLE_SLANG_SHADERS ON   fetch a pinned subset of libretro/slang-shaders (tarball,
#                                    SHA256-verified) and ship it as shaders/slang-shaders/
#                                    beside the executable.  Fail-soft: no network, or a hash
#                                    mismatch, only means the app has no bundled presets.
#                                    OFF for distro/offline builds.
#   VJ_SLANG_SHADERS_ARCHIVE    ""   a local copy of the pinned tarball (offline builds)
#
# Pins.  Headers and from-source builds: the tag in app/packaging/librashader/pin.env (v0.12.1;
# its headers are byte-identical to v0.12.0's, so the SHA-256s below hold for both).  Prebuilt
# Windows/Apple-silicon binaries: librashader-v0.12.0, the newest tag that has them attached.

option(VJ_APP_SHADERS "Build librashader (RetroArch shader preset) support" ON)
# On by default where upstream publishes a binary, so a plain local build has working shaders.
if(APPLE OR WIN32)
    set(_vj_fetch_rashader_default ON)
else()
    set(_vj_fetch_rashader_default OFF)
endif()
option(VJ_APP_FETCH_LIBRASHADER "Download the prebuilt librashader runtime and ship it beside the executable" ${_vj_fetch_rashader_default})
set(VJ_LIBRASHADER_INCLUDE_DIR "" CACHE PATH "Directory holding librashader.h and librashader_ld.h (skips the download)")

# One pin for the source tag (headers + the from-source runtime builds).
set(VJ_LIBRASHADER_PIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../packaging/librashader/pin.env")
file(STRINGS "${VJ_LIBRASHADER_PIN_FILE}" _pin_tag REGEX "^LIBRASHADER_TAG=")
string(REGEX REPLACE "^LIBRASHADER_TAG=" "" VJ_LIBRASHADER_TAG "${_pin_tag}")
if(NOT VJ_LIBRASHADER_TAG)
    message(FATAL_ERROR "librashader: no LIBRASHADER_TAG in ${VJ_LIBRASHADER_PIN_FILE}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VJ_LIBRASHADER_PIN_FILE}")
# The tag whose release assets hold prebuilt binaries (Windows, Apple silicon).
set(VJ_LIBRASHADER_PREBUILT_TAG "librashader-v0.12.0")
set(VJ_LIBRASHADER_LIB "" CACHE FILEPATH "A librashader runtime you built (copied beside the executable and packaged); overrides the download")
set(VJ_LIBRASHADER_RAW "https://raw.githubusercontent.com/SnowflakePowered/librashader/${VJ_LIBRASHADER_TAG}/include")
set(VJ_LIBRASHADER_H_SHA256    "5d478897c391af3f60015810b67785ae1a286d262a845485276e36ded9f21e62")
set(VJ_LIBRASHADER_LD_H_SHA256 "bcffcbc854afb287c9f935c1a0e3b569f5e6775ef85914bd7f7025ad1f6bde33")

option(VJ_APP_BUNDLE_SLANG_SHADERS "Fetch a pinned subset of libretro/slang-shaders and ship it beside the executable" ON)
set(VJ_SLANG_SHADERS_ARCHIVE "" CACHE FILEPATH "Local copy of the pinned slang-shaders tarball (offline builds)")

# libretro/slang-shaders at one commit.  The tarball is what GitHub serves for
# /archive/<commit>.tar.gz (byte-identical on two downloads when this was pinned).
set(VJ_SLANG_COMMIT "e1d75632a205c70f14a4cc947c46d5abb7b3f7f1")
set(VJ_SLANG_SHA256 "b8bc2fbb31a754ed41ee4ab309ece63c08123ada8e5e7c5142621461f165bbd9")
set(VJ_SLANG_URL "https://github.com/libretro/slang-shaders/archive/${VJ_SLANG_COMMIT}.tar.gz")
# The CRT, scanline and interpolation families plus every directory their presets
# and shaders reference (include/, blurs/, misc/, ...): the transitive closure,
# computed by walking #reference / shaderN / textures / #include.  ~23 MB, ~900 files.
set(VJ_SLANG_DIRS anti-aliasing blurs crt denoisers edge-smoothing include interpolation
                  linear misc ntsc pixel-art-scaling reshade scanlines sharpen vhs)
set(VJ_SLANG_FILES bilinear.slangp nearest.slangp stock.slang README.md)

set(VJ_SHADERS_AVAILABLE OFF)
set(VJ_SHADERS_INCLUDE "")

if(VJ_APP_SHADERS)
    if(VJ_LIBRASHADER_INCLUDE_DIR)
        set(_inc "${VJ_LIBRASHADER_INCLUDE_DIR}")
    else()
        set(_inc "${CMAKE_BINARY_DIR}/librashader/include")
        file(MAKE_DIRECTORY "${_inc}")
        set(_ok TRUE)
        foreach(_f librashader.h librashader_ld.h)
            if(_f STREQUAL "librashader.h")
                set(_sha "${VJ_LIBRASHADER_H_SHA256}")
            else()
                set(_sha "${VJ_LIBRASHADER_LD_H_SHA256}")
            endif()
            # EXPECTED_HASH: an existing file with the right hash is not downloaded
            # again; a wrong hash is an error and the file is discarded.
            file(DOWNLOAD "${VJ_LIBRASHADER_RAW}/${_f}" "${_inc}/${_f}"
                 EXPECTED_HASH SHA256=${_sha} STATUS _st TLS_VERIFY ON)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                list(GET _st 1 _msg)
                message(WARNING "librashader: could not fetch ${_f} (${_msg}); shader support is OFF in this build")
                file(REMOVE "${_inc}/${_f}")
                set(_ok FALSE)
            endif()
        endforeach()
        if(NOT _ok)
            set(_inc "")
        endif()
    endif()
    if(_inc AND EXISTS "${_inc}/librashader.h" AND EXISTS "${_inc}/librashader_ld.h")
        set(VJ_SHADERS_AVAILABLE ON)
        set(VJ_SHADERS_INCLUDE "${_inc}")
    endif()
endif()
message(STATUS "App shaders (librashader, runtime-loaded): ${VJ_SHADERS_AVAILABLE}")

# The runtime library, for packaging and for trying shaders without building Rust.
#   1. VJ_LIBRASHADER_LIB: a library the caller built (CI: from the pinned source).
#   2. VJ_APP_FETCH_LIBRASHADER: upstream's prebuilt zip (Apple silicon macOS, Windows).
#   3. nothing: the app looks for a system copy at run time.
set(VJ_LIBRASHADER_LIB_FILE "")
if(VJ_LIBRASHADER_LIB)
    if(EXISTS "${VJ_LIBRASHADER_LIB}")
        get_filename_component(VJ_LIBRASHADER_LIB_FILE "${VJ_LIBRASHADER_LIB}" ABSOLUTE)
        message(STATUS "librashader runtime: ${VJ_LIBRASHADER_LIB_FILE} (VJ_LIBRASHADER_LIB)")
    else()
        message(FATAL_ERROR "VJ_LIBRASHADER_LIB is set but ${VJ_LIBRASHADER_LIB} does not exist")
    endif()
elseif(VJ_APP_FETCH_LIBRASHADER)
    set(_base "https://github.com/SnowflakePowered/librashader/releases/download/${VJ_LIBRASHADER_PREBUILT_TAG}")
    set(_zip "")
    if(APPLE)
        set(_arch "${CMAKE_OSX_ARCHITECTURES}")
        if(NOT _arch)
            set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        if(_arch MATCHES "arm64|aarch64")
            set(_zip "librashader-aarch64-macos-v0.12.0-optimized.zip")
            set(_sha "49808004a4904f6a99e0231092dcfdfe52b7b61f68430a4c9f1e165749c4c90e")
        elseif(_arch MATCHES "x86_64")
            # Upstream's "x86_64-macos" v0.12.0 zip holds an arm64 dylib (lipo-verified), so there
            # is no usable Intel download.  Build it: app/packaging/librashader/build-from-source.sh
            # x86_64-apple-darwin <dir>, then -DVJ_LIBRASHADER_LIB=<dir>/librashader.dylib.
            message(STATUS "librashader: no usable prebuilt for x86_64 macOS; pass -DVJ_LIBRASHADER_LIB=<your build> or install a system copy")
        endif()
        set(_libname "librashader.dylib")
    elseif(WIN32)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(_zip "librashader-aarch64-windows-v0.12.0-optimized.zip")
            set(_sha "acea3871d7c1cf345ae9da47378b399d89a1e05496160a652103ba5b7b5eb775")
        else()
            set(_zip "librashader-x86_64-windows-v0.12.0-optimized.zip")
            set(_sha "521fe0f364bfa705883f9e99fb1733a3a24f0f403bcc5591b6b78f6cff289183")
        endif()
        set(_libname "librashader.dll")
    endif()
    if(_zip)
        FetchContent_Declare(librashader_bin
            URL      "${_base}/${_zip}"
            URL_HASH SHA256=${_sha}
            DOWNLOAD_EXTRACT_TIMESTAMP ON)
        FetchContent_GetProperties(librashader_bin)
        if(NOT librashader_bin_POPULATED)
            FetchContent_MakeAvailable(librashader_bin)
        endif()
        if(EXISTS "${librashader_bin_SOURCE_DIR}/${_libname}")
            set(VJ_LIBRASHADER_LIB_FILE "${librashader_bin_SOURCE_DIR}/${_libname}")
            message(STATUS "librashader runtime: ${VJ_LIBRASHADER_LIB_FILE} (prebuilt ${_zip})")
        else()
            message(WARNING "librashader: ${_libname} not found in ${_zip}")
        endif()
    else()
        message(STATUS "librashader: no prebuilt runtime is published for this platform; install it separately")
    endif()
elseif(VJ_SHADERS_AVAILABLE)
    # The cache keeps an OFF from an earlier offline or packaging configure, and the default never
    # comes back: say so where a download exists (Windows, Apple silicon), or a local build quietly
    # ships no runtime and the Shader section reports the library missing.
    set(_arch "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT _arch)
        set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(WIN32 OR (APPLE AND _arch MATCHES "arm64|aarch64"))
        message(WARNING "librashader: VJ_APP_FETCH_LIBRASHADER is OFF and VJ_LIBRASHADER_LIB is unset, so no runtime "
                        "is copied beside the executable; shaders then need a system copy or VJ_LIBRASHADER. "
                        "Configure again with -DVJ_APP_FETCH_LIBRASHADER=ON to download the pinned prebuilt.")
    endif()
endif()

# ---- bundled slang-shaders subset -------------------------------------------------------
# Nothing from slang-shaders is committed to this repository.  It is downloaded
# into the build tree, verified, the pinned directories are extracted untouched
# (every shader keeps its own header; per-directory LICENSE/README/THANKS files
# such as crt/shaders/crt-royale/LICENSE.TXT come along because whole directories
# are copied), and the result is copied beside the executable.  slang-shaders has no
# repository-wide licence: each shader states its own (GPL, MIT, public domain...).
set(VJ_SLANG_SRC "")
if(VJ_APP_BUNDLE_SLANG_SHADERS)
    set(_arc "${VJ_SLANG_SHADERS_ARCHIVE}")
    set(_ok TRUE)
    if(_arc)
        set(_got "")
        if(EXISTS "${_arc}")
            file(SHA256 "${_arc}" _got)
        endif()
        if(NOT _got STREQUAL "${VJ_SLANG_SHA256}")
            message(WARNING "slang-shaders: ${_arc} does not match the pinned SHA256; not bundling")
            set(_ok FALSE)
        endif()
    else()
        set(_arc "${CMAKE_BINARY_DIR}/slang-shaders-${VJ_SLANG_COMMIT}.tar.gz")
        file(DOWNLOAD "${VJ_SLANG_URL}" "${_arc}"
             EXPECTED_HASH SHA256=${VJ_SLANG_SHA256} STATUS _st TLS_VERIFY ON
             TIMEOUT 600 INACTIVITY_TIMEOUT 60)
        list(GET _st 0 _code)
        if(NOT _code EQUAL 0)
            list(GET _st 1 _msg)
            message(WARNING "slang-shaders: could not fetch the shader archive (${_msg}); the app builds without bundled presets")
            file(REMOVE "${_arc}")
            set(_ok FALSE)
        endif()
    endif()
    if(_ok)
        # A SHORT destination: slang-shaders has file names of 100+ characters, and the
        # archive's own top folder adds 47; on Windows (MAX_PATH 260) every character of
        # the build directory path counts.
        set(_dest "${CMAKE_BINARY_DIR}/ss")
        set(_top "slang-shaders-${VJ_SLANG_COMMIT}")
        set(_stamp "")
        if(EXISTS "${_dest}/.vj-extracted")
            file(READ "${_dest}/.vj-extracted" _stamp)
        endif()
        if(NOT _stamp STREQUAL "${VJ_SLANG_COMMIT}")
            file(REMOVE_RECURSE "${_dest}")
            file(MAKE_DIRECTORY "${_dest}")
            set(_pat "")
            foreach(_d ${VJ_SLANG_DIRS})
                list(APPEND _pat "${_top}/${_d}/*")
            endforeach()
            foreach(_f ${VJ_SLANG_FILES})
                list(APPEND _pat "${_top}/${_f}")
            endforeach()
            file(ARCHIVE_EXTRACT INPUT "${_arc}" DESTINATION "${_dest}" PATTERNS ${_pat})
            # The longest-named file of the CRT-Royale set doubles as a "no path got too long" probe.
            if(EXISTS "${_dest}/${_top}/crt/shaders/crt-royale/TileableLinearSlotMaskTall15Wide9And4d5Horizontal9d14VerticalSpacingResizeTo64.png")
                file(WRITE "${_dest}/.vj-extracted" "${VJ_SLANG_COMMIT}")
            else()
                message(WARNING "slang-shaders: extraction incomplete (a build-directory path too long for this OS?); not bundling")
                file(REMOVE_RECURSE "${_dest}")
                set(_ok FALSE)
            endif()
        endif()
        if(_ok)
            set(VJ_SLANG_SRC "${_dest}/${_top}")
            file(WRITE "${VJ_SLANG_SRC}/NOTICE-virtualjaguar.txt"
"This folder is an unmodified subset of https://github.com/libretro/slang-shaders
at commit ${VJ_SLANG_COMMIT} (archive SHA-256 ${VJ_SLANG_SHA256}),
fetched when Virtual Jaguar was built.

slang-shaders has no single licence: every shader states its own in its header
(GPL, MIT, public domain, ...), and some directories carry LICENSE/README/THANKS
files (for example crt/shaders/crt-royale/LICENSE.TXT). They are all here, untouched.
Credit belongs to the individual shader authors named in those files.

Virtual Jaguar's own presets are in the parent folder (vj-*.slangp).
")
        endif()
    endif()
endif()
message(STATUS "App bundled slang-shaders subset: ${VJ_SLANG_SRC}")

# Hook the shader support into a target (compile definition, headers, bundled
# presets and, when asked for, the prebuilt runtime).
function(vj_app_shaders target)
    if(VJ_SHADERS_AVAILABLE)
        target_compile_definitions(${target} PRIVATE VJ_HAVE_LIBRASHADER=1)
        # This build ships (or stages beside the executable) a runtime: lets the Shader section
        # tell "this build does not include one" from "the bundled one failed to load".
        if(VJ_LIBRASHADER_LIB_FILE)
            target_compile_definitions(${target} PRIVATE VJ_LIBRASHADER_BUNDLED=1)
        endif()
        target_include_directories(${target} SYSTEM PRIVATE "${VJ_SHADERS_INCLUDE}")
    endif()
endfunction()

# First-party presets (app/shaders) and the optional prebuilt library, copied
# next to the executable.
function(vj_app_shader_assets target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:${target}>/shaders"
        COMMENT "Copying bundled shader presets")
    if(VJ_SLANG_SRC)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${VJ_SLANG_SRC}" "$<TARGET_FILE_DIR:${target}>/shaders/slang-shaders"
            COMMENT "Copying the bundled slang-shaders subset")
    endif()
    if(VJ_LIBRASHADER_LIB_FILE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VJ_LIBRASHADER_LIB_FILE}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying the prebuilt librashader runtime")
    endif()
endfunction()

# Headless tests for the shader layer: one executable, three CTest entries.
#   shaders_unit     placement math, preset parsing/validation, folder scan, and the
#                    library-missing / garbage-library fallbacks.  Needs nothing.
#   shaders_library  loads the real librashader and has it parse the bundled presets.
#                    SKIPPED (exit 77) when the library is not installed.
#   shaders_bundled  every preset in the bundled slang-shaders subset resolves its files, and
#                    the named CRT/scanline/interpolation presets are intact.  SKIPPED when
#                    nothing was bundled (offline, or -DVJ_APP_BUNDLE_SLANG_SHADERS=OFF).
#   shaders_gl       renders a test picture through the bundled presets on a hidden GL
#                    3.3 context and checks the pixels, plus the broken-shader
#                    fallback.  SKIPPED (exit 77) without the library or a display.
if(VJ_APP_BUILD_TESTS)
    enable_testing()
    add_executable(vj_shader_tests
        test/shader_tests.cpp
        src/shaders.cpp
        src/present_util.cpp
        src/paths.cpp
        src/config.cpp)
    target_include_directories(vj_shader_tests PRIVATE src ${VJ_CORE_INCLUDES})
    target_link_libraries(vj_shader_tests PRIVATE vj_imgui ${VJ_SDL3_TARGET})
    vj_app_shaders(vj_shader_tests)
    target_compile_definitions(vj_shader_tests PRIVATE
        VJ_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    if(MSVC)
        target_compile_options(vj_shader_tests PRIVATE /W3 /utf-8)
        target_compile_definitions(vj_shader_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(vj_shader_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    add_test(NAME shaders_unit COMMAND vj_shader_tests unit)
    add_test(NAME shaders_library COMMAND vj_shader_tests library)
    add_test(NAME shaders_gl COMMAND vj_shader_tests gl)
    if(VJ_SLANG_SRC)
        target_compile_definitions(vj_shader_tests PRIVATE VJ_SLANG_DIR="${VJ_SLANG_SRC}")
    endif()
    add_test(NAME shaders_bundled COMMAND vj_shader_tests bundled)
    set_tests_properties(shaders_unit shaders_library shaders_gl shaders_bundled PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "shader_tests: FAIL")
    if(VJ_LIBRASHADER_LIB_FILE)
        get_filename_component(_libdir "${VJ_LIBRASHADER_LIB_FILE}" DIRECTORY)
        set_tests_properties(shaders_library shaders_gl PROPERTIES
            ENVIRONMENT "VJ_LIBRASHADER=${VJ_LIBRASHADER_LIB_FILE}")
    endif()
endif()

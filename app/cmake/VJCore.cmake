# VJCore.cmake -- builds the Virtual Jaguar libretro core as static libraries.
#
# The core's own build is the Makefile.  This file is the *app's* view of it
# and must not drift:
#
#   * The source list is PARSED from Makefile.common (same regexes as
#     scripts/check-package-sources.sh), so adding a .c there needs no change
#     here.  CMAKE_CONFIGURE_DEPENDS re-runs configure when it changes.
#   * Compile defines/flags mirror what `make` passes on the host platform
#     (see `make -n`): -D__LIBRETRO__ -DINLINE=inline, the warning set,
#     -fno-strict-overflow (a codegen flag, not just a warning), libchdr's own
#     C99 translation unit with LIBCHDR_CFLAGS, and the blitter SIMD choice.
#   * The blitter SIMD variant is picked by the compiler, not by this file:
#     every blitter_simd_<arch>.c is compiled with -DBLITTER_SIMD_AUTODETECT,
#     exactly as Package.swift does (see src/tom/blitter_simd_arch.h).
#
# The core is NOT given -std=gnu89: `make` does not pass any -std either, and
# gnu89 changes `inline` linkage.  C89 conformance is gated by
# scripts/c89-lint.sh, not by the app build.
#
# Provides: vj_core (static, INTERFACE-includes the core headers).

set(VJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
get_filename_component(VJ_ROOT "${VJ_ROOT}" ABSOLUTE)

# ---- source list, parsed from Makefile.common ------------------------------
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${VJ_ROOT}/Makefile.common")
file(READ "${VJ_ROOT}/Makefile.common" _vj_mk)
string(REGEX MATCHALL "\\$\\(CORE_DIR\\)/[a-zA-Z0-9_/.-]+\\.c" _vj_core_m "${_vj_mk}")
string(REGEX MATCHALL "\\$\\(LIBRETRO_COMM_DIR\\)/[a-zA-Z0-9_/.-]+\\.c" _vj_lrc_m "${_vj_mk}")
list(REMOVE_DUPLICATES _vj_core_m)
list(REMOVE_DUPLICATES _vj_lrc_m)

set(VJ_CORE_SOURCES "")
foreach(_s IN LISTS _vj_core_m)
    string(REPLACE "$(CORE_DIR)/" "${VJ_ROOT}/" _p "${_s}")
    list(APPEND VJ_CORE_SOURCES "${_p}")
endforeach()
foreach(_s IN LISTS _vj_lrc_m)
    string(REPLACE "$(LIBRETRO_COMM_DIR)/" "${VJ_ROOT}/libretro-common/" _p "${_s}")
    list(APPEND VJ_CORE_SOURCES "${_p}")
endforeach()

list(LENGTH VJ_CORE_SOURCES _vj_n)
if(_vj_n LESS 60)
    message(FATAL_ERROR "VJCore: parsed only ${_vj_n} sources from Makefile.common "
                        "-- the parser is broken, not the list.")
endif()
foreach(_p IN LISTS VJ_CORE_SOURCES)
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "VJCore: ${_p} is listed in Makefile.common but missing")
    endif()
endforeach()

# ---- src/core/version.h (gitignored; src/core/version_fallback.h otherwise) --
find_program(VJ_SH NAMES sh bash)
if(VJ_SH)
    execute_process(COMMAND "${VJ_SH}" "${VJ_ROOT}/scripts/gen-version-h.sh"
                    WORKING_DIRECTORY "${VJ_ROOT}"
                    RESULT_VARIABLE _vj_ver_rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _vj_ver_rc EQUAL 0)
        message(STATUS "VJCore: gen-version-h.sh failed; using version_fallback.h")
    endif()
endif()

# ---- include directories (INCFLAGS in Makefile.common) ----------------------
set(VJ_CORE_INCLUDES
    "${VJ_ROOT}"
    "${VJ_ROOT}/src"
    "${VJ_ROOT}/src/core"
    "${VJ_ROOT}/src/tom"
    "${VJ_ROOT}/src/jerry"
    "${VJ_ROOT}/src/cd"
    "${VJ_ROOT}/src/bios"
    "${VJ_ROOT}/src/m68000"
    "${VJ_ROOT}/src/debug"
    "${VJ_ROOT}/libretro-common/include"
    "${VJ_ROOT}/deps/libchdr/include"
    "${VJ_ROOT}/deps/libchdr/deps/lzma-25.01/include"
    "${VJ_ROOT}/deps/libchdr/deps/miniz-3.1.1"
    "${VJ_ROOT}/deps/libchdr/deps/zstd-1.5.7")

# ---- libchdr: one C99 unity TU, built the way the Makefile builds it --------
add_library(vj_libchdr STATIC "${VJ_ROOT}/deps/libchdr/unity.c")
target_include_directories(vj_libchdr PRIVATE ${VJ_CORE_INCLUDES})
target_compile_definitions(vj_libchdr PRIVATE
    _7ZIP_ST MINIZ_DEFLATE_APIS WANT_RAW_DATA_SECTOR=1 WANT_SUBCODE=1
    VERIFY_BLOCK_CRC=1)
set_target_properties(vj_libchdr PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- the core ---------------------------------------------------------------
add_library(vj_core STATIC ${VJ_CORE_SOURCES})
target_include_directories(vj_core PUBLIC ${VJ_CORE_INCLUDES})
target_compile_definitions(vj_core PRIVATE
    __LIBRETRO__ BLITTER_SIMD_AUTODETECT=1)
set_target_properties(vj_core PROPERTIES POSITION_INDEPENDENT_CODE ON
                                          LINKER_LANGUAGE C)
target_link_libraries(vj_core PUBLIC vj_libchdr)

if(MSVC)
    # Mirrors the c-cpp.yml msvc-check job: C mode (never /TP, never /Za),
    # INLINE=_inline, no secure-CRT deprecation noise.
    target_compile_definitions(vj_core PRIVATE
        "INLINE=_inline" _CRT_SECURE_NO_DEPRECATE)
    target_compile_options(vj_core PRIVATE /W3 /FS)
    target_compile_definitions(vj_libchdr PRIVATE "INLINE=_inline" _CRT_SECURE_NO_DEPRECATE)
    target_compile_options(vj_libchdr PRIVATE /W3 /FS)
else()
    target_compile_definitions(vj_core PRIVATE "INLINE=inline")
    target_compile_definitions(vj_libchdr PRIVATE "INLINE=inline")
    target_compile_options(vj_core PRIVATE
        -Wall -Wno-sign-compare -Wno-unused-variable
        -Wno-unused-but-set-variable -Wno-unused-function -Wno-uninitialized
        -Wno-strict-aliasing -Wno-overflow
        # Codegen, not a warning: signed overflow must not be exploited.
        -fno-strict-overflow
        -fomit-frame-pointer -fno-common)
    # LIBCHDR_CFLAGS / LIBCHDR_WARNFLAGS in Makefile.common.
    target_compile_options(vj_libchdr PRIVATE
        -std=c99 -Wno-unused-function -Wno-unused-variable)
    # Optimisation: the Makefile's desktop default is -O3 -DNDEBUG, which is what
    # CMAKE_BUILD_TYPE=Release gives GCC/Clang (the top-level forces Release for
    # a single-config build with no type).  RelWithDebInfo would stop at -O2, so
    # the core is pinned to -O3 in both optimised configs (the Makefile's level
    # for unix / osx / win; target options come after the config flags, so this
    # wins).
    target_compile_options(vj_core PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-O3>)
    target_compile_options(vj_libchdr PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-O3>)
    # GNU-ld ELF targets (Makefile GC_STYLE=gnu, not win): hidden visibility and
    # no semantic interposition, so calls and globals inside the core bind
    # locally instead of through the PLT/GOT of a -fPIC object.  retro_* stay
    # exported through RETRO_API.
    if(UNIX AND NOT APPLE)
        target_compile_options(vj_core PRIVATE -fvisibility=hidden -fno-semantic-interposition)
    endif()
endif()
vj_optimise(vj_core)
vj_optimise(vj_libchdr)

# System libraries the core itself needs (jlink_tcp sockets, libm).
if(WIN32)
    target_link_libraries(vj_core PUBLIC ws2_32 winmm)
elseif(NOT APPLE)
    target_link_libraries(vj_core PUBLIC m)
endif()

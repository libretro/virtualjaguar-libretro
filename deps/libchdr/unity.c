/* Feature-test macros must precede every system header in this TU.
 * libchdr_chd.c defines the same pair, but that is too late here:
 * miniz/lzma/zstd are included first and pull in <stdio.h>. Clang
 * -std=c99 then treats fseeko/ftello as undeclared (CI Linux Clang). */
#if !defined(_WIN32) && !defined(__PS3__) && !defined(__SWITCH__) && !defined(__vita__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef __ANDROID__
#    ifndef _FILE_OFFSET_BITS
#      define _FILE_OFFSET_BITS 64
#    endif
#  endif
#endif

/* Disable unused features of miniz (but allow
   them to be restored by dependent projects). */
#ifndef MINIZ_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif

#ifndef MINIZ_DEFLATE_APIS
#define MINIZ_NO_DEFLATE_APIS
#endif

#ifndef MINIZ_STDIO
#define MINIZ_NO_STDIO
#endif

#ifndef MINIZ_TIME
#define MINIZ_NO_TIME
#endif

#include "deps/lzma-25.01/src/LzmaDec.c"
#include "deps/miniz-3.1.1/miniz.c"
#include "deps/zstd-1.5.7/zstddeclib.c"

/* snprintf shim for MSVC < 2015 (buildbot msvc05/10): libchdr_chd.c uses
 * snprintf for the V1/V2 hard-disk metadata string, and those CRTs ship
 * only _snprintf. Declared here rather than in the vendored sources so
 * `src/` stays byte-identical to the upstream pin.
 *
 * Deliberately NOT `#include <compat/msvc.h>`: that header defines
 * SIZE_MAX as _UI32_MAX when it is not already set, and dr_flac.h --
 * pulled in by libchdr_flac.c below -- derives DRFLAC_SIZE_MAX from
 * SIZE_MAX. On x64 that would quietly narrow it to 32 bits. Take the
 * one declaration this TU needs and nothing else. */
#if defined(_MSC_VER) && _MSC_VER < 1900
#include <stddef.h>
int c99_snprintf_retro__(char *s, size_t len, const char *format, ...);
#define snprintf c99_snprintf_retro__
#endif

#include "src/libchdr_bitstream.c"
#include "src/libchdr_cdrom.c"
#include "src/libchdr_chd.c"
#include "src/libchdr_codec_cdfl.c"
#include "src/libchdr_codec_cdlz.c"
#include "src/libchdr_codec_cdzl.c"
#include "src/libchdr_codec_cdzs.c"
#include "src/libchdr_codec_flac.c"
#include "src/libchdr_codec_huff.c"
#include "src/libchdr_codec_lzma.c"
#include "src/libchdr_codec_zlib.c"
#include "src/libchdr_codec_zstd.c"
#include "src/libchdr_flac.c"
#include "src/libchdr_huffman.c"

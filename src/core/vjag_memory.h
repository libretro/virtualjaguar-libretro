//
// MEMORY.H: Header file
//
// All Jaguar related memory and I/O locations are contained in this file
//

#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <stdint.h>
#include <string.h>

/* C89-safe restrict.  Unlocks autovec on scanline src/dst independently of
 * the NEON kernels; empty on compilers without a restrict spelling.
 *
 * #ifndef-guarded to match tom_scan_simd_{sse2,neon}.h and
 * shadowfb_simd_neon.h, which define it the same way: a TU that includes a
 * SIMD header before this one (src/tom/tom.c does) would otherwise define
 * it twice.  That is legal today only because the two spellings are
 * token-identical, which C permits -- the guard removes the dependency on
 * them staying that way. */
#ifndef VJ_RESTRICT
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#  define VJ_RESTRICT __restrict
#else
#  define VJ_RESTRICT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t jagMemSpace[];

extern uint8_t * jaguarMainRAM;
extern uint8_t * jaguarMainROM;
extern uint8_t * gpuRAM;
extern uint8_t * dspRAM;

extern uint32_t * butch, * dscntrl;
extern uint16_t * ds_data;
extern uint32_t * i2cntrl, * sbcntrl, * subdata, * subdatb, * sb_time, * fifo_data, * i2sdat2, * unknown;

extern uint16_t * memcon1, * memcon2, * hc, * vc, * lph, * lpv;
extern uint64_t * obData;
extern uint32_t * olp;
extern uint16_t * obf, * vmode, * bord1, * bord2, * hp, * hbb, * hbe, * hs,
	* hvs, * hdb1, * hdb2, * hde, * vp, * vbb, * vbe, * vs, * vdb, * vde,
	* veb, * vee, * vi, * pit0, * pit1, * heq;
extern uint32_t * bg;
extern uint16_t * int1, * int2;
extern uint8_t * clut, * lbuf;
extern uint32_t * g_flags, * g_mtxc, * g_mtxa, * g_end, * g_pc, * g_ctrl,
	* g_hidata, * g_divctrl;
extern uint32_t g_remain;
extern uint32_t * a1_base, * a1_flags, * a1_clip, * a1_pixel, * a1_step,
	* a1_fstep, * a1_fpixel, * a1_inc, * a1_finc, * a2_base, * a2_flags,
	* a2_mask, * a2_pixel, * a2_step, * b_cmd, * b_count;
extern uint64_t * b_srcd, * b_dstd, * b_dstz, * b_srcz1, * b_srcz2, * b_patd;
extern uint32_t * b_iinc, * b_zinc, * b_stop, * b_i3, * b_i2, * b_i1, * b_i0, * b_z3,
	* b_z2, * b_z1, * b_z0;
extern uint16_t * jpit1, * jpit2, * jpit3, * jpit4, * clk1, * clk2, * clk3, * j_int,
	* asidata, * asictrl;
extern uint16_t asistat;
extern uint16_t * asiclk, * joystick, * joybuts;
extern uint32_t * d_flags, * d_mtxc, * d_mtxa, * d_end, * d_pc, * d_ctrl,
	* d_mod, * d_divctrl;
extern uint32_t d_remain;
extern uint32_t * d_machi;
extern uint16_t * ltxd, lrxd, * rtxd, rrxd;
extern uint8_t * sclk, sstat;
extern uint32_t * smode;

// Read/write tracing enumeration
//
// Index 9 is spelled DEBUGGER, not DEBUG, and must stay that way: Xcode's
// stock Debug configuration sets GCC_PREPROCESSOR_DEFINITIONS = DEBUG=1, so
// on any Xcode or SwiftPM consumer `DEBUG` arrives here already expanded to
// `1` and the enumerator list fails to parse.  The wire value is unchanged
// (9), and whoName[9] already read "Debugger".

enum { UNKNOWN, JAGUAR, DSP, GPU, TOM, JERRY, M68K, BLITTER, OP, DEBUGGER };
extern const char * whoName[10];

// BIOS identification enum

// Some handy macros to help converting native endian to big endian (jaguar native)
// & vice versa
//
// On little-endian GCC/Clang/MSVC, memcpy + bswap compiles to a fused load/store
// plus rev (arm64 probe 2026-08-27: the shift-or form is 4x ldrb with no rev).
// memcpy + bswap is wrong on MSB_FIRST: a native load is already BE, so bswap
// would invert.  Shift-or stays the portable fallback.

#if !defined(MSB_FIRST)
#if defined(_MSC_VER)
#  include <stdlib.h>
#  define VJ_BSWAP16(v) ((uint16_t)_byteswap_ushort((unsigned short)(v)))
#  define VJ_BSWAP32(v) ((uint32_t)_byteswap_ulong((unsigned long)(v)))
#  define VJ_BSWAP64(v) ((uint64_t)_byteswap_uint64((unsigned __int64)(v)))
#elif defined(__GNUC__)
#  define VJ_BSWAP16(v) ((uint16_t)__builtin_bswap16((uint16_t)(v)))
#  define VJ_BSWAP32(v) ((uint32_t)__builtin_bswap32((uint32_t)(v)))
#  define VJ_BSWAP64(v) ((uint64_t)__builtin_bswap64((uint64_t)(v)))
#endif
#endif

#ifndef INLINE
#  if defined(_MSC_VER)
#    define INLINE __inline
#  elif defined(__GNUC__)
#    define INLINE __inline__
#  else
#    define INLINE
#  endif
#endif

#if defined(VJ_BSWAP32)
static INLINE uint32_t vj_get32(const uint8_t *r, unsigned a)
{
   uint32_t v;
   memcpy(&v, r + a, 4);
   return VJ_BSWAP32(v);
}

static INLINE void vj_set32(uint8_t *r, unsigned a, uint32_t v)
{
   uint32_t be;
   be = VJ_BSWAP32(v);
   memcpy(r + a, &be, 4);
}

static INLINE uint16_t vj_get16(const uint8_t *r, unsigned a)
{
   uint16_t v;
   memcpy(&v, r + a, 2);
   return VJ_BSWAP16(v);
}

static INLINE void vj_set16(uint8_t *r, unsigned a, uint16_t v)
{
   uint16_t be;
   be = VJ_BSWAP16(v);
   memcpy(r + a, &be, 2);
}

static INLINE uint64_t vj_get64(const uint8_t *r, unsigned a)
{
   uint64_t v;
   memcpy(&v, r + a, 8);
   return VJ_BSWAP64(v);
}

static INLINE void vj_set64(uint8_t *r, unsigned a, uint64_t v)
{
   uint64_t be;
   be = VJ_BSWAP64(v);
   memcpy(r + a, &be, 8);
}

#  define GET32(r, a)    vj_get32((const uint8_t *)(r), (unsigned)(a))
#  define SET32(r, a, v) vj_set32((uint8_t *)(r), (unsigned)(a), (uint32_t)(v))
#  define GET16(r, a)    vj_get16((const uint8_t *)(r), (unsigned)(a))
#  define SET16(r, a, v) vj_set16((uint8_t *)(r), (unsigned)(a), (uint16_t)(v))
#  define GET64(r, a)    vj_get64((const uint8_t *)(r), (unsigned)(a))
#  define SET64(r, a, v) vj_set64((uint8_t *)(r), (unsigned)(a), (uint64_t)(v))
#else
#define SET64(r, a, v) 	r[(a)] = ((v) & 0xFF00000000000000) >> 56, r[(a)+1] = ((v) & 0x00FF000000000000) >> 48, \
						r[(a)+2] = ((v) & 0x0000FF0000000000) >> 40, r[(a)+3] = ((v) & 0x000000FF00000000) >> 32, \
						r[(a)+4] = ((v) & 0xFF000000) >> 24, r[(a)+5] = ((v) & 0x00FF0000) >> 16, \
						r[(a)+6] = ((v) & 0x0000FF00) >> 8, r[(a)+7] = (v) & 0x000000FF
#define GET64(r, a)		(((uint64_t)r[(a)] << 56) | ((uint64_t)r[(a)+1] << 48) | \
						((uint64_t)r[(a)+2] << 40) | ((uint64_t)r[(a)+3] << 32) | \
						((uint64_t)r[(a)+4] << 24) | ((uint64_t)r[(a)+5] << 16) | \
						((uint64_t)r[(a)+6] << 8) | (uint64_t)r[(a)+7])
#define SET32(r, a, v)	r[(a)] = ((v) & 0xFF000000) >> 24, r[(a)+1] = ((v) & 0x00FF0000) >> 16, \
						r[(a)+2] = ((v) & 0x0000FF00) >> 8, r[(a)+3] = (v) & 0x000000FF
#define GET32(r, a)		((r[(a)] << 24) | (r[(a)+1] << 16) | (r[(a)+2] << 8) | r[(a)+3])
#define SET16(r, a, v)	r[(a)] = ((v) & 0xFF00) >> 8, r[(a)+1] = (v) & 0xFF
#define GET16(r, a)		((r[(a)] << 8) | r[(a)+1])
#endif

#ifdef __cplusplus
}
#endif

#endif	// __MEMORY_H__

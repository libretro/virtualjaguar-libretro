/*
 * Bit-exactness and performance test for blitter SIMD operations.
 *
 * Build (from repo root — link exactly one SIMD implementation):
 *   # On macOS ARM64 (NEON):
 *   cc -O2 -Isrc/core -o test/test_blitter_simd test/test_blitter_simd.c \
 *      src/tom/blitter_simd_neon.c
 *
 *   # On x86_64 (SSE2):
 *   cc -O2 -msse2 -Isrc/core -o test/test_blitter_simd test/test_blitter_simd.c \
 *      src/tom/blitter_simd_sse2.c
 *
 *   # Scalar-only (any platform):
 *   cc -O2 -Isrc/core -o test/test_blitter_simd test/test_blitter_simd.c \
 *      src/tom/blitter_simd_scalar.c
 *
 * Usage:
 *   ./test/test_blitter_simd           # Run bit-exactness tests
 *   ./test/test_blitter_simd --bench   # Run performance benchmark
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

/* The active (possibly SIMD) implementation */
#include "../src/tom/blitter_simd.h"

/* --- Scalar reference (linked from blitter_simd_scalar.c) --- */

/* We need the scalar reference for comparison. Since blitter_simd_scalar.c
 * defines blitter_simd_ops when it's the only impl linked, we need a
 * separate copy for comparison. We inline the reference here. */

static uint64_t ref_lfu(uint64_t srcd, uint64_t dstd, uint8_t lfu_func)
{
   uint64_t funcmask[2] = { 0, 0xFFFFFFFFFFFFFFFFULL };
   uint64_t func0 = funcmask[lfu_func & 0x01];
   uint64_t func1 = funcmask[(lfu_func >> 1) & 0x01];
   uint64_t func2 = funcmask[(lfu_func >> 2) & 0x01];
   uint64_t func3 = funcmask[(lfu_func >> 3) & 0x01];
   return (~srcd & ~dstd & func0)
        | (~srcd &  dstd & func1)
        | ( srcd & ~dstd & func2)
        | ( srcd &  dstd & func3);
}

static uint8_t ref_dcomp(uint64_t patd, uint64_t srcd, uint64_t dstd, bool cmpdst)
{
   uint8_t result = 0;
   uint64_t cmpd = patd ^ (cmpdst ? dstd : srcd);
   if ((cmpd & 0x00000000000000FFULL) == 0) result |= 0x01;
   if ((cmpd & 0x000000000000FF00ULL) == 0) result |= 0x02;
   if ((cmpd & 0x0000000000FF0000ULL) == 0) result |= 0x04;
   if ((cmpd & 0x00000000FF000000ULL) == 0) result |= 0x08;
   if ((cmpd & 0x000000FF00000000ULL) == 0) result |= 0x10;
   if ((cmpd & 0x0000FF0000000000ULL) == 0) result |= 0x20;
   if ((cmpd & 0x00FF000000000000ULL) == 0) result |= 0x40;
   if ((cmpd & 0xFF00000000000000ULL) == 0) result |= 0x80;
   return result;
}

static uint8_t ref_zcomp(uint64_t srcz, uint64_t dstz, uint8_t zmode)
{
   uint8_t result = 0;
   int i;
   for (i = 0; i < 4; i++)
   {
      uint16_t s = (uint16_t)(srcz >> (i * 16));
      uint16_t d = (uint16_t)(dstz >> (i * 16));
      if (((s < d) && (zmode & 0x01))
       || ((s == d) && (zmode & 0x02))
       || ((s > d) && (zmode & 0x04)))
         result |= (1u << i);
   }
   return result;
}

static uint64_t ref_byte_merge(uint64_t src, uint64_t dst, uint16_t mask)
{
   uint64_t result;
   result = ((src & mask) | (dst & ~(uint64_t)mask)) & 0x00000000000000FFULL;
   result |= ((mask & 0x0100) ? src : dst) & 0x000000000000FF00ULL;
   result |= ((mask & 0x0200) ? src : dst) & 0x0000000000FF0000ULL;
   result |= ((mask & 0x0400) ? src : dst) & 0x00000000FF000000ULL;
   result |= ((mask & 0x0800) ? src : dst) & 0x000000FF00000000ULL;
   result |= ((mask & 0x1000) ? src : dst) & 0x0000FF0000000000ULL;
   result |= ((mask & 0x2000) ? src : dst) & 0x00FF000000000000ULL;
   result |= ((mask & 0x4000) ? src : dst) & 0xFF00000000000000ULL;
   return result;
}

/* --- Simple xorshift64 PRNG --- */
static uint64_t prng_state = 0x123456789ABCDEF0ULL;

static uint64_t xorshift64(void)
{
   uint64_t x = prng_state;
   x ^= x << 13;
   x ^= x >> 7;
   x ^= x << 17;
   prng_state = x;
   return x;
}

/* --- Test helpers --- */
static int failures = 0;
static int tests_run = 0;

#define CHECK(cond, fmt, ...) do { \
   tests_run++; \
   if (!(cond)) { \
      failures++; \
      printf("  FAIL: " fmt "\n", ##__VA_ARGS__); \
   } \
} while (0)

/* --- LFU tests --- */
static void test_lfu(void)
{
   uint8_t func;
   int i;

   printf("Testing LFU...\n");

   /* Exhaustive test of all 16 functions with known values */
   for (func = 0; func < 16; func++)
   {
      /* All zeros */
      uint64_t got = blitter_simd_ops.lfu(0, 0, func);
      uint64_t exp = ref_lfu(0, 0, func);
      CHECK(got == exp, "lfu(0,0,%u): got 0x%016llx exp 0x%016llx",
            func, (unsigned long long)got, (unsigned long long)exp);

      /* All ones */
      got = blitter_simd_ops.lfu(~0ULL, ~0ULL, func);
      exp = ref_lfu(~0ULL, ~0ULL, func);
      CHECK(got == exp, "lfu(~0,~0,%u): got 0x%016llx exp 0x%016llx",
            func, (unsigned long long)got, (unsigned long long)exp);

      /* Mixed */
      got = blitter_simd_ops.lfu(0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, func);
      exp = ref_lfu(0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, func);
      CHECK(got == exp, "lfu(0xAA..,0x55..,%u): got 0x%016llx exp 0x%016llx",
            func, (unsigned long long)got, (unsigned long long)exp);
   }

   /* Random inputs */
   for (i = 0; i < 10000; i++)
   {
      uint64_t s = xorshift64();
      uint64_t d = xorshift64();
      uint8_t f = (uint8_t)(xorshift64() & 0x0F);
      uint64_t got = blitter_simd_ops.lfu(s, d, f);
      uint64_t exp = ref_lfu(s, d, f);
      CHECK(got == exp, "lfu random #%d: func=%u got 0x%016llx exp 0x%016llx",
            i, f, (unsigned long long)got, (unsigned long long)exp);
      if (got != exp) break;
   }
}

/* --- DCOMP tests --- */
static void test_dcomp(void)
{
   int i;
   uint64_t val;

   printf("Testing DCOMP...\n");

   /* Identical values -> all bytes match -> 0xFF */
   val = 0x0102030405060708ULL;
   CHECK(blitter_simd_ops.dcomp(val, val, 0, false) == 0xFF,
         "dcomp identical (cmpdst=false)");
   CHECK(blitter_simd_ops.dcomp(val, 0, val, true) == 0xFF,
         "dcomp identical (cmpdst=true)");

   /* All different -> 0x00 */
   CHECK(blitter_simd_ops.dcomp(0, ~0ULL, 0, false) == 0x00,
         "dcomp all-different");

   /* Only byte 0 differs (rest are all zero = matching) */
   CHECK(blitter_simd_ops.dcomp(0x00000000000000FFULL, 0x0000000000000000ULL, 0, false) == 0xFE,
         "dcomp byte 0 differs only");

   /* patd=0xFF, srcd=0xFF -> XOR=0 -> all bytes match */
   CHECK(blitter_simd_ops.dcomp(0xFF, 0xFF, 0, false) == 0xFF,
         "dcomp low bytes identical, upper zero=zero -> all match");

   /* Random inputs */
   for (i = 0; i < 10000; i++)
   {
      uint64_t p = xorshift64();
      uint64_t s = xorshift64();
      uint64_t d = xorshift64();
      bool c = (xorshift64() & 1) != 0;
      uint8_t got = blitter_simd_ops.dcomp(p, s, d, c);
      uint8_t exp = ref_dcomp(p, s, d, c);
      CHECK(got == exp, "dcomp random #%d: cmpdst=%d got 0x%02x exp 0x%02x",
            i, c, got, exp);
      if (got != exp) break;
   }
}

/* --- ZCOMP tests --- */
static void test_zcomp(void)
{
   int i;
   uint8_t zmode;

   printf("Testing ZCOMP...\n");

   /* Equal values with EQ mode -> all bits set */
   CHECK(blitter_simd_ops.zcomp(0x1234123412341234ULL,
                                 0x1234123412341234ULL, 0x02) == 0x0F,
         "zcomp equal values, EQ mode");

   /* src < dst with LT mode */
   CHECK(blitter_simd_ops.zcomp(0x0001000100010001ULL,
                                 0x0002000200020002ULL, 0x01) == 0x0F,
         "zcomp src<dst, LT mode");

   /* src > dst with GT mode */
   CHECK(blitter_simd_ops.zcomp(0x0002000200020002ULL,
                                 0x0001000100010001ULL, 0x04) == 0x0F,
         "zcomp src>dst, GT mode");

   /* All zmodes with known values */
   for (zmode = 0; zmode < 8; zmode++)
   {
      uint64_t s = 0x0001000200030004ULL;
      uint64_t d = 0x0002000200020002ULL;
      uint8_t got = blitter_simd_ops.zcomp(s, d, zmode);
      uint8_t exp = ref_zcomp(s, d, zmode);
      CHECK(got == exp, "zcomp zmode=%u: got 0x%02x exp 0x%02x",
            zmode, got, exp);
   }

   /* Random inputs */
   for (i = 0; i < 10000; i++)
   {
      uint64_t s = xorshift64();
      uint64_t d = xorshift64();
      uint8_t zm = (uint8_t)(xorshift64() & 0x07);
      uint8_t got = blitter_simd_ops.zcomp(s, d, zm);
      uint8_t exp = ref_zcomp(s, d, zm);
      CHECK(got == exp, "zcomp random #%d: zmode=%u got 0x%02x exp 0x%02x",
            i, zm, got, exp);
      if (got != exp) break;
   }
}

/* --- Byte Merge tests --- */
static void test_byte_merge(void)
{
   int i;
   uint64_t got;
   uint64_t exp;

   printf("Testing byte_merge...\n");

   /* All-select-src: mask = 0x7FFF -> all bytes from src */
   got = blitter_simd_ops.byte_merge(0xAAAAAAAAAAAAAAAAULL,
                                      0x5555555555555555ULL, 0x7FFF);
   exp = ref_byte_merge(0xAAAAAAAAAAAAAAAAULL,
                        0x5555555555555555ULL, 0x7FFF);
   CHECK(got == exp, "byte_merge all-src: got 0x%016llx exp 0x%016llx",
         (unsigned long long)got, (unsigned long long)exp);

   /* All-select-dst: mask = 0x0000 -> all bytes from dst */
   got = blitter_simd_ops.byte_merge(0xAAAAAAAAAAAAAAAAULL,
                                      0x5555555555555555ULL, 0x0000);
   exp = ref_byte_merge(0xAAAAAAAAAAAAAAAAULL,
                         0x5555555555555555ULL, 0x0000);
   CHECK(got == exp, "byte_merge all-dst: got 0x%016llx exp 0x%016llx",
         (unsigned long long)got, (unsigned long long)exp);

   /* Per-bit mask in byte 0 */
   got = blitter_simd_ops.byte_merge(0xFF, 0x00, 0x00AA);
   exp = ref_byte_merge(0xFF, 0x00, 0x00AA);
   CHECK(got == exp, "byte_merge per-bit byte0: got 0x%016llx exp 0x%016llx",
         (unsigned long long)got, (unsigned long long)exp);

   /* Random inputs */
   for (i = 0; i < 10000; i++)
   {
      uint64_t s = xorshift64();
      uint64_t d = xorshift64();
      uint16_t m = (uint16_t)(xorshift64() & 0x7FFF);
      got = blitter_simd_ops.byte_merge(s, d, m);
      exp = ref_byte_merge(s, d, m);
      CHECK(got == exp, "byte_merge random #%d: mask=0x%04x got 0x%016llx exp 0x%016llx",
            i, m, (unsigned long long)got, (unsigned long long)exp);
      if (got != exp) break;
   }
}

/* --- Performance benchmark --- */

#define BENCH_ITERS 1000000

/* Portable high-resolution timer.
 * Uses clock_gettime on POSIX, QueryPerformanceCounter on Windows.
 * Declare TIMER_DECL() once per scope, then use START/STOP/NS freely. */
#ifdef _WIN32
#include <windows.h>
static double get_time_ns(void)
{
   static LARGE_INTEGER freq = {0};
   LARGE_INTEGER count;
   if (freq.QuadPart == 0)
      QueryPerformanceFrequency(&freq);
   QueryPerformanceCounter(&count);
   return (double)count.QuadPart / (double)freq.QuadPart * 1e9;
}
#define TIMER_DECL()  double _timer_t0, _timer_t1
#define TIMER_START() (_timer_t0 = get_time_ns())
#define TIMER_STOP()  (_timer_t1 = get_time_ns())
#define TIMER_NS()    (_timer_t1 - _timer_t0)
#else
#define TIMER_DECL()  struct timespec _timer_ts0, _timer_ts1
#define TIMER_START() clock_gettime(CLOCK_MONOTONIC, &_timer_ts0)
#define TIMER_STOP()  clock_gettime(CLOCK_MONOTONIC, &_timer_ts1)
#define TIMER_NS()    (((double)(_timer_ts1.tv_sec - _timer_ts0.tv_sec) * 1e9) + (double)(_timer_ts1.tv_nsec - _timer_ts0.tv_nsec))
#endif

static void bench_lfu(void)
{
   TIMER_DECL();
   volatile uint64_t sink = 0;
   int i;
   double ref_ns, simd_ns;

   /* Ref */
   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += ref_lfu(0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, (uint8_t)(i & 0x0F));
   TIMER_STOP();
   ref_ns = TIMER_NS() / BENCH_ITERS;

   /* SIMD */
   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += blitter_simd_ops.lfu(0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, (uint8_t)(i & 0x0F));
   TIMER_STOP();
   simd_ns = TIMER_NS() / BENCH_ITERS;

   printf("  LFU:        ref=%6.1f ns/op  simd=%6.1f ns/op  speedup=%.2fx\n",
          ref_ns, simd_ns, ref_ns / simd_ns);
   (void)sink;
}

static void bench_dcomp(void)
{
   TIMER_DECL();
   volatile uint8_t sink = 0;
   int i;
   double ref_ns, simd_ns;

   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += ref_dcomp(0x0102030405060708ULL, (uint64_t)i, 0, false);
   TIMER_STOP();
   ref_ns = TIMER_NS() / BENCH_ITERS;

   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += blitter_simd_ops.dcomp(0x0102030405060708ULL, (uint64_t)i, 0, false);
   TIMER_STOP();
   simd_ns = TIMER_NS() / BENCH_ITERS;

   printf("  DCOMP:      ref=%6.1f ns/op  simd=%6.1f ns/op  speedup=%.2fx\n",
          ref_ns, simd_ns, ref_ns / simd_ns);
   (void)sink;
}

static void bench_zcomp(void)
{
   TIMER_DECL();
   volatile uint8_t sink = 0;
   int i;
   double ref_ns, simd_ns;

   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += ref_zcomp(0x0001000200030004ULL, 0x0002000200020002ULL, (uint8_t)(i & 0x07));
   TIMER_STOP();
   ref_ns = TIMER_NS() / BENCH_ITERS;

   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += blitter_simd_ops.zcomp(0x0001000200030004ULL, 0x0002000200020002ULL, (uint8_t)(i & 0x07));
   TIMER_STOP();
   simd_ns = TIMER_NS() / BENCH_ITERS;

   printf("  ZCOMP:      ref=%6.1f ns/op  simd=%6.1f ns/op  speedup=%.2fx\n",
          ref_ns, simd_ns, ref_ns / simd_ns);
   (void)sink;
}

static void bench_byte_merge(void)
{
   TIMER_DECL();
   volatile uint64_t sink = 0;
   int i;
   double ref_ns, simd_ns;

   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += ref_byte_merge(0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, (uint16_t)(i & 0x7FFF));
   TIMER_STOP();
   ref_ns = TIMER_NS() / BENCH_ITERS;

   TIMER_START();
   for (i = 0; i < BENCH_ITERS; i++)
      sink += blitter_simd_ops.byte_merge(0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, (uint16_t)(i & 0x7FFF));
   TIMER_STOP();
   simd_ns = TIMER_NS() / BENCH_ITERS;

   printf("  byte_merge: ref=%6.1f ns/op  simd=%6.1f ns/op  speedup=%.2fx\n",
          ref_ns, simd_ns, ref_ns / simd_ns);
   (void)sink;
}

int main(int argc, char *argv[])
{
   bool bench = (argc > 1 && strcmp(argv[1], "--bench") == 0);

   if (bench)
   {
      printf("==> Blitter SIMD Performance Benchmark (%d iterations)\n\n", BENCH_ITERS);
      bench_lfu();
      bench_dcomp();
      bench_zcomp();
      bench_byte_merge();
      printf("\nDone.\n");
      return 0;
   }

   printf("==> Blitter SIMD Bit-Exactness Tests\n\n");

   test_lfu();
   test_dcomp();
   test_zcomp();
   test_byte_merge();

   printf("\n==> Results: %d tests, %d passed, %d failed\n",
          tests_run, tests_run - failures, failures);

   return failures > 0 ? 1 : 0;
}

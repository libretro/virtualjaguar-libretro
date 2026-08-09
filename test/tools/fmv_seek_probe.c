/*
 * fmv_seek_probe.c -- per-field FMV presentation-clock + CD seek probe (#297)
 *
 * Diagnostic harness for the FMV scene-jump investigation.  Emits one CSV
 * row per emulated field with:
 *   - the ReadySoft presentation clock (48-bit fixed point; 16-bit integer
 *     at $562E, 32-bit fraction at $5630 -- Space Ace is +512 bytes)
 *   - CD diagnostic counters (seeks, fifoReads, fifoDrains, dsaIRQs)
 *   - HLE stream state (arm count, active, bytes, dest)
 *
 * Config via environment (keeps harness CLI parsing untouched):
 *   FMV_CSV=<path>       CSV output path (default stdout)
 *   FMV_CLOCK=<hex>      clock base address (default 562E; Space Ace 582E)
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./test -I./libretro-common/include \
 *      -o test/tools/fmv_seek_probe test/tools/fmv_seek_probe.c \
 *      test/harness/harness.c -ldl -lm
 */

#include "harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*counters_fn)(uint32_t *, uint32_t *, uint32_t *, uint32_t *,
                            uint32_t *, uint32_t *, uint32_t *);
typedef void (*wedge_fn)(uint32_t *, uint32_t *, uint32_t *);
typedef uint32_t (*u32_fn)(void);
typedef bool (*bool_fn)(void);

typedef struct
{
   FILE        *csv;
   uint8_t    **ramp;
   counters_fn  counters;
   wedge_fn     wedge;
   u32_fn       hleArm;
   bool_fn      hleActive;
   u32_fn       hleBytes;
   u32_fn       hleDest;
   uint32_t     clockAddr;
   uint32_t     prevSeekStarts;
   uint32_t     prevArm;
   uint32_t     prevClockInt;
   /* Optional PPM capture window: FMV_SHOT_FROM/TO/EVERY + FMV_SHOTDIR */
   const char  *shotDir;
   unsigned     shotFrom;
   unsigned     shotTo;
   unsigned     shotEvery;
   unsigned     curFrame;
   harness_config *cfg;
} probe_state;

static void VideoHook(void *userdata, const void *data, unsigned width,
                      unsigned height, size_t pitch)
{
   probe_state    *st = (probe_state *)userdata;
   const uint32_t *px;
   char            path[512];
   FILE           *f;
   unsigned        x, y;
   int             n;

   st->curFrame = st->cfg ? st->cfg->current_frame : 0;

   if (!st->shotDir || !data || width == 0 || height == 0)
      return;
   if (st->curFrame < st->shotFrom || st->curFrame > st->shotTo)
      return;
   if (st->shotEvery && (st->curFrame - st->shotFrom) % st->shotEvery)
      return;

   /* FMV_SHOTDIR is environment-controlled, so cap the write rather than
    * trusting the caller's path length. */
   n = snprintf(path, sizeof(path), "%s/f_%05u.ppm", st->shotDir,
                st->curFrame);
   if (n < 0 || (size_t)n >= sizeof(path))
      return;
   f = fopen(path, "wb");
   if (!f)
      return;
   fprintf(f, "P6\n%u %u\n255\n", width, height);
   for (y = 0; y < height; y++)
   {
      px = (const uint32_t *)((const uint8_t *)data + y * pitch);
      for (x = 0; x < width; x++)
      {
         uint32_t p = px[x];
         fputc((int)((p >> 16) & 0xFF), f);
         fputc((int)((p >> 8) & 0xFF), f);
         fputc((int)(p & 0xFF), f);
      }
   }
   fclose(f);
}

static uint32_t RamRead32(uint8_t *ram, uint32_t addr)
{
   return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr + 1] << 16)
        | ((uint32_t)ram[addr + 2] << 8) | (uint32_t)ram[addr + 3];
}

static uint32_t RamRead16(uint8_t *ram, uint32_t addr)
{
   return ((uint32_t)ram[addr] << 8) | (uint32_t)ram[addr + 1];
}

static bool FrameHook(void *userdata, unsigned frame)
{
   probe_state *st = (probe_state *)userdata;
   uint8_t     *ram;
   uint32_t     butchExec = 0, fifoIRQs = 0, dsaIRQs = 0, fifoReads = 0;
   uint32_t     seeks = 0, globalDisabled = 0, hleBytesTotal = 0;
   uint32_t     seekStarts = 0, seekDones = 0, fifoDrains = 0;
   uint32_t     clkInt = 0, clkFrac = 0;
   uint32_t     arm = 0, bytes = 0, dest = 0;
   int          active = 0;

   st->curFrame = frame;
   ram = st->ramp ? *st->ramp : NULL;

   if (st->counters)
      st->counters(&butchExec, &fifoIRQs, &dsaIRQs, &fifoReads, &seeks,
                   &globalDisabled, &hleBytesTotal);
   if (st->wedge)
      st->wedge(&seekStarts, &seekDones, &fifoDrains);
   if (ram)
   {
      clkInt  = RamRead16(ram, st->clockAddr);
      clkFrac = RamRead32(ram, st->clockAddr + 2);
   }
   if (st->hleArm)    arm    = st->hleArm();
   if (st->hleActive) active = st->hleActive() ? 1 : 0;
   if (st->hleBytes)  bytes  = st->hleBytes();
   if (st->hleDest)   dest   = st->hleDest();

   fprintf(st->csv,
           "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
           frame, butchExec, clkInt, clkFrac, seeks, seekStarts, seekDones,
           fifoDrains, fifoReads, dsaIRQs, arm, (unsigned)active, bytes, dest);

   st->prevSeekStarts = seekStarts;
   st->prevArm        = arm;
   st->prevClockInt   = clkInt;
   return true;
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   probe_state    st;
   const char    *csvPath;
   const char    *clockEnv;
   harness_result res;

   memset(&st, 0, sizeof(st));
   st.clockAddr = 0x562E;

   cfg.frames      = 2000;
   cfg.quiet       = 1;
   cfg.system_dir  = "test/roms/private";

   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   clockEnv = getenv("FMV_CLOCK");
   if (clockEnv && clockEnv[0])
      st.clockAddr = (uint32_t)strtoul(clockEnv, NULL, 16);

   csvPath = getenv("FMV_CSV");
   st.csv  = (csvPath && csvPath[0]) ? fopen(csvPath, "w") : stdout;
   if (!st.csv)
   {
      fprintf(stderr, "fmv_seek_probe: cannot open CSV '%s'\n", csvPath);
      return 1;
   }

   if (!harness_load_rom(&cfg))
      return 1;

   st.ramp      = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
   st.counters  = (counters_fn)harness_dlsym(&cfg, "CDROMDiagGetCounters");
   st.wedge     = (wedge_fn)harness_dlsym(&cfg, "CDROMDiagGetSeekWedgeState");
   st.hleArm    = (u32_fn)harness_dlsym(&cfg, "JaguarCDHLEStreamArmCount");
   st.hleActive = (bool_fn)harness_dlsym(&cfg, "JaguarCDHLEStreamActive");
   st.hleBytes  = (u32_fn)harness_dlsym(&cfg, "JaguarCDHLEStreamBytes");
   st.hleDest   = (u32_fn)harness_dlsym(&cfg, "JaguarCDHLEStreamDest");

   fprintf(st.csv,
           "field,butch,clkint,clkfrac,seeks,seekstarts,seekdones,"
           "fifodrains,fiforeads,dsairqs,hlearm,hleactive,hlebytes,hledest\n");

   st.cfg      = &cfg;
   st.shotDir  = getenv("FMV_SHOTDIR");
   st.shotFrom = (unsigned)(getenv("FMV_SHOT_FROM")
                            ? strtoul(getenv("FMV_SHOT_FROM"), NULL, 10) : 0);
   st.shotTo   = (unsigned)(getenv("FMV_SHOT_TO")
                            ? strtoul(getenv("FMV_SHOT_TO"), NULL, 10) : 0);
   st.shotEvery = (unsigned)(getenv("FMV_SHOT_EVERY")
                            ? strtoul(getenv("FMV_SHOT_EVERY"), NULL, 10) : 1);

   cfg.frame_callback      = FrameHook;
   cfg.frame_callback_data = &st;
   if (st.shotDir)
   {
      cfg.video_callback      = VideoHook;
      cfg.video_callback_data = &st;
   }

   harness_run(&cfg);

   if (st.csv != stdout)
      fclose(st.csv);

   res.status = "INFO";
   res.name   = "fmv_seek_probe";
   res.detail = "per-field CSV written";
   harness_report(&cfg, &res, 1);
   harness_shutdown(&cfg);
   return 0;
}

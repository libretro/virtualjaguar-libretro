/* crash_detect.c -- See crash_detect.h.  Cheap per-frame watchdog. */

#include "crash_detect.h"
#include "log.h"
#include "../jerry/dsp.h"   /* DSPIsRunning() returns bool -- match the canonical decl */
#include "../tom/gpu.h"     /* GPUIsRunning() */
#include <boolean.h>        /* project shim; bool / true / false */
#include <stdint.h>
#include <stddef.h>

/* External state we sample (see src/tom/gpu.c, src/jerry/dsp.c).
 * The IsRunning() functions come from the headers above; only the PC
 * statics need explicit extern decls here. */
extern uint32_t gpu_pc;
extern uint32_t dsp_pc;

/* ---------- tunables ---------- */

/* Valid PC ranges per processor.  Outside these = "PC escape".
 *
 * Match the address decoding in JaguarReadX/WriteX (src/core/jaguar.c):
 * any address < $E40000 lands in main RAM (mirrored 4x for the bottom
 * 8 MB), cart ROM, or the boot ROM region -- all of which can host
 * legitimate executable code.  Above $E40000 is register space and
 * unmapped territory; only the processor's own local SRAM is valid
 * for execution there.
 *
 * Earlier versions of this watchdog used `pc <= 0x1FFFFF` and
 * false-positively flagged any DSP/GPU code that ran from a RAM mirror
 * at $200000-$7FFFFF or from cart ROM at $800000+.  Caught by Copilot
 * review on PR #182. */
#define GPU_LOCAL_LO   0x00F03000u
#define GPU_LOCAL_HI   0x00F03FFFu
#define DSP_LOCAL_LO   0x00F1B000u
#define DSP_LOCAL_HI   0x00F1CFFFu
#define MAPPED_CODE_HI 0x00E3FFFFu  /* RAM mirrors + cart + boot ROM */

/* Wedge thresholds.  We sample once per frame, so 600 frames @ 60Hz =
 * 10 seconds of the same PC while still flagged "running". */
#define WEDGE_FRAMES_GPU   180   /* 3 sec of GPU stuck at one PC */
#define WEDGE_FRAMES_DSP   600   /* 10 sec for DSP -- many engines idle on a JR loop */
#define STALL_FRAMES_FB    300   /* 5 sec of identical framebuffer hash */

/* Halfline expectation per frame: 524 NTSC, 624 PAL.  Anomaly band is +/- 4. */

/* Verbose-mode heartbeat: dump current state every N frames. */
#define HEARTBEAT_FRAMES   600

/* Throttle each anomaly class so a wedged title doesn't spam the log. */
#define LOG_REPEAT_FRAMES  600   /* re-fire same signature at most every 10s */

/* ---------- state ---------- */

static int  cd_mode = CRASH_DETECT_ON;
static int  cd_initialized = 0;

static unsigned frame_no;

static uint32_t last_gpu_pc;
static unsigned gpu_same_pc_frames;
static uint32_t last_dsp_pc;
static unsigned dsp_same_pc_frames;

static uint32_t fb_hash_prev;
static unsigned fb_same_hash_frames;

static unsigned next_heartbeat_frame;

/* Last frame at which each signature fired -- prevents log spam. */
static unsigned last_log_gpu_escape;
static unsigned last_log_dsp_escape;
static unsigned last_log_gpu_wedge;
static unsigned last_log_dsp_wedge;
static unsigned last_log_fb_stall;

/* ---------- helpers ---------- */

static int gpu_pc_valid(uint32_t pc)
{
   if (pc <= MAPPED_CODE_HI) return 1;
   if (pc >= GPU_LOCAL_LO && pc <= GPU_LOCAL_HI) return 1;
   return 0;
}

static int dsp_pc_valid(uint32_t pc)
{
   if (pc <= MAPPED_CODE_HI) return 1;
   if (pc >= DSP_LOCAL_LO && pc <= DSP_LOCAL_HI) return 1;
   return 0;
}

/* Cheap rolling framebuffer hash.  Sample 256 evenly-spaced pixels --
 * enough entropy to detect a frozen frame, costs 256 ops per frame.
 * We deliberately skip alpha (top byte) so XRGB padding noise doesn't
 * inflate the hash. */
static uint32_t fb_hash(const uint32_t *fb, unsigned w, unsigned h)
{
   uint32_t h32 = 0x9E3779B9u;
   uint32_t total;
   uint32_t step;
   uint32_t i;

   if (!fb || w == 0 || h == 0) return 0;
   total = w * h;
   step  = (total > 256) ? (total / 256) : 1;
   for (i = 0; i < total; i += step)
   {
      uint32_t v = fb[i] & 0x00FFFFFFu;
      h32 ^= v + 0x9E3779B9u + (h32 << 6) + (h32 >> 2);
   }
   return h32;
}

static int may_log(unsigned *last_frame)
{
   if (frame_no - *last_frame < LOG_REPEAT_FRAMES && *last_frame != 0)
      return 0;
   *last_frame = (frame_no == 0) ? 1 : frame_no;
   return 1;
}

/* ---------- public API ---------- */

void CrashDetectInit(void)
{
   cd_initialized = 1;
   CrashDetectReset();
}

void CrashDetectReset(void)
{
   frame_no = 0;
   last_gpu_pc = 0;
   gpu_same_pc_frames = 0;
   last_dsp_pc = 0;
   dsp_same_pc_frames = 0;
   fb_hash_prev = 0;
   fb_same_hash_frames = 0;
   next_heartbeat_frame = HEARTBEAT_FRAMES;
   last_log_gpu_escape = 0;
   last_log_dsp_escape = 0;
   last_log_gpu_wedge = 0;
   last_log_dsp_wedge = 0;
   last_log_fb_stall = 0;
}

void CrashDetectSetMode(int mode)
{
   if (mode < CRASH_DETECT_OFF || mode > CRASH_DETECT_VERBOSE) mode = CRASH_DETECT_ON;
   cd_mode = mode;
}

void CrashDetectFrameTick(const uint32_t *fb, unsigned w, unsigned h)
{
   uint32_t cur_gpu_pc;
   uint32_t cur_dsp_pc;
   int      gpu_running;
   int      dsp_running;
   uint32_t cur_fb_hash;

   if (!cd_initialized) return;
   if (cd_mode == CRASH_DETECT_OFF) return;

   frame_no++;

   cur_gpu_pc = gpu_pc;
   cur_dsp_pc = dsp_pc;
   gpu_running = GPUIsRunning();
   dsp_running = DSPIsRunning();
   cur_fb_hash = fb_hash(fb, w, h);

   /* ---- GPU PC escape ---- */
   if (gpu_running && !gpu_pc_valid(cur_gpu_pc))
   {
      if (may_log(&last_log_gpu_escape))
         LOG_ERR("[CRASH-DETECT] gpu_pc_escape frame=%u pc=$%08X (valid: $0-$E3FFFF or $F03000-$F03FFF)\n",
                 frame_no, cur_gpu_pc);
   }

   /* ---- DSP PC escape ---- */
   if (dsp_running && !dsp_pc_valid(cur_dsp_pc))
   {
      if (may_log(&last_log_dsp_escape))
         LOG_ERR("[CRASH-DETECT] dsp_pc_escape frame=%u pc=$%08X (valid: $0-$E3FFFF or $F1B000-$F1CFFF)\n",
                 frame_no, cur_dsp_pc);
   }

   /* ---- GPU wedge: same PC, still running ---- */
   if (gpu_running && cur_gpu_pc == last_gpu_pc)
   {
      gpu_same_pc_frames++;
      if (gpu_same_pc_frames == WEDGE_FRAMES_GPU
          && may_log(&last_log_gpu_wedge))
         LOG_WRN("[CRASH-DETECT] gpu_wedge frame=%u pc=$%08X stuck for %u frames\n",
                 frame_no, cur_gpu_pc, WEDGE_FRAMES_GPU);
   }
   else
   {
      gpu_same_pc_frames = 0;
   }

   /* ---- DSP wedge: same PC, still running ---- */
   if (dsp_running && cur_dsp_pc == last_dsp_pc)
   {
      dsp_same_pc_frames++;
      if (dsp_same_pc_frames == WEDGE_FRAMES_DSP
          && may_log(&last_log_dsp_wedge))
         LOG_WRN("[CRASH-DETECT] dsp_wedge frame=%u pc=$%08X stuck for %u frames\n",
                 frame_no, cur_dsp_pc, WEDGE_FRAMES_DSP);
   }
   else
   {
      dsp_same_pc_frames = 0;
   }

   /* ---- Video stall: framebuffer hash unchanged while either
    * processor still running. */
   if (fb && cur_fb_hash == fb_hash_prev && (gpu_running || dsp_running))
   {
      fb_same_hash_frames++;
      if (fb_same_hash_frames == STALL_FRAMES_FB
          && may_log(&last_log_fb_stall))
         LOG_WRN("[CRASH-DETECT] video_stall frame=%u fb_hash=$%08X unchanged for %u frames "
                 "gpu_pc=$%08X gpu_run=%d dsp_pc=$%08X dsp_run=%d\n",
                 frame_no, cur_fb_hash, STALL_FRAMES_FB,
                 cur_gpu_pc, gpu_running, cur_dsp_pc, dsp_running);
   }
   else
   {
      fb_same_hash_frames = 0;
   }

   /* ---- Verbose heartbeat ---- */
   if (cd_mode == CRASH_DETECT_VERBOSE && frame_no >= next_heartbeat_frame)
   {
      LOG_INF("[CRASH-DETECT] heartbeat frame=%u gpu_pc=$%08X gpu_run=%d "
              "dsp_pc=$%08X dsp_run=%d fb_hash=$%08X\n",
              frame_no, cur_gpu_pc, gpu_running,
              cur_dsp_pc, dsp_running, cur_fb_hash);
      next_heartbeat_frame = frame_no + HEARTBEAT_FRAMES;
   }

   last_gpu_pc = cur_gpu_pc;
   last_dsp_pc = cur_dsp_pc;
   fb_hash_prev = cur_fb_hash;
}

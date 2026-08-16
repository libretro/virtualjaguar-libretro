/* crash_detect.c -- See crash_detect.h.  Cheap per-frame watchdog. */

#include "crash_detect.h"
#include "log.h"
#include "../jerry/dsp.h"   /* DSPIsRunning() returns bool -- match the canonical decl */
#include "../tom/gpu.h"     /* GPUIsRunning() */
#include "../cd/cdrom.h"    /* CDROMDiagGetSeekWedgeState(), CDTraceDump() */
#include "../tom/shadowfb.h" /* shadowHiresActive + resolve counters */
#include "settings.h"       /* bootConfig.isCDGame */
#include <boolean.h>        /* project shim; bool / true / false */
#include <stdint.h>
#include <stddef.h>

/* External state we sample (see src/tom/gpu.c, src/jerry/dsp.c).
 * The IsRunning() functions come from the headers above; only the PC
 * statics need explicit extern decls here. */
extern uint32_t gpu_pc;
extern uint32_t dsp_pc;
/* Executed-opcode counters (gpu.c / dsp.c).  The wedge predicate needs
 * them: a sampled PC that never changes does NOT mean the processor is
 * stuck -- deterministic per-frame slice budgets land the end-of-slice PC
 * on the same instruction of a healthy wait/spin loop every frame
 * (Super Burnout spins ~446k GPU opcodes/frame at one sampled PC; found
 * in the issue #378 overclock pilot).  A wedge is only real when the
 * processor is flagged running yet executed ZERO opcodes for the whole
 * window.  User-visible hangs stay covered by video_stall regardless. */
extern uint32_t gpu_exec_opcode_count;
extern uint32_t dsp_exec_opcode_count;

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
#define PC_ALIAS_MASK  0x00FFFFFFu  /* Jaguar addresses are 24-bit */

/* gpu_runaway: a start PC records its 4 KB page.  A main-RAM program that
 * flows across a page boundary is still "the program the GPU was started
 * at", so membership is a 64 KB window from that page, not the page alone
 * (Kimi review on #466).  32 slots is more GO targets than any title uses;
 * a later GO past that is logged rather than dropped silently. */
#define GPU_GO_PAGES_MAX  32
#define GPU_GO_PAGE_MASK  0x00FFF000u
#define GPU_GO_WINDOW     0x00010000u

/* Wedge thresholds.  We sample once per frame, so 600 frames @ 60Hz =
 * 10 seconds of the same PC while still flagged "running". */
#define WEDGE_FRAMES_GPU   180   /* 3 sec of GPU stuck at one PC */
#define WEDGE_FRAMES_DSP   600   /* 10 sec for DSP -- many engines idle on a JR loop */
#define STALL_FRAMES_FB    300   /* 5 sec of identical framebuffer hash */

/* cd_seek_wedge: a CD seek was issued but the FIFO drain counter (see
 * CDROMDiagGetSeekWedgeState()) hasn't advanced in this many frames while
 * a processor is still running. SEEK_DELAY_TICKS in cdrom.c is ~100
 * halfline ticks (~0.2 frames at NTSC's ~524 halflines/frame) even for a
 * from-scratch seek, so 300 frames (5 sec) is far beyond any legitimate
 * seek.
 *
 * KNOWN BENIGN CASE: a title that legitimately goes CD-idle for >5s
 * fires this too -- the signature cannot tell "game stopped draining
 * because it wedged" from "game finished the transfer and doesn't need
 * the disc right now".  Ground truth: Myst (bios mode) fires
 * cd_seek_wedge at ~frame 2260 with the drain frozen at the intro
 * movie's payload end (LBA 21189) while the game plays the movie's
 * ~6-second all-black dramatic pause (Cyan logo -> black -> match
 * strike -> burning MYST logo) entirely from RAM; the movie clock
 * (TOM PIT -> GPU IRQ2 -> $1D58C accumulator) keeps ticking at 12 Hz
 * throughout and the next asset loads right on schedule.  HLE mode
 * shows the identical black window (fires video_stall instead).
 * Corroborate with the trace ring / a longer freeze window before
 * treating a lone cd_seek_wedge line as a real stall. */
#define WEDGE_FRAMES_CD_SEEK 300

/* Halfline expectation per frame: 524 NTSC, 624 PAL.  Anomaly band is +/- 4. */

/* Verbose-mode heartbeat: dump current state every N frames. */
#define HEARTBEAT_FRAMES   600

/* Throttle each anomaly class so a wedged title doesn't spam the log. */
#define LOG_REPEAT_FRAMES  600   /* re-fire same signature at most every 10s */

/* ---------- state ---------- */

static int  cd_mode = CRASH_DETECT_ON;
static int  cd_initialized = 0;

static unsigned frame_no;

static unsigned gpu_zero_opcode_frames;
static uint32_t last_gpu_opcount;
static unsigned dsp_zero_opcode_frames;
static uint32_t last_dsp_opcount;

static uint32_t fb_hash_prev;
static unsigned fb_same_hash_frames;

static uint32_t last_cd_fifo_drains;
static unsigned cd_seek_wedge_frames;

static unsigned next_heartbeat_frame;

/* Previous heartbeat's hi-res resolve counters, so the line can report a
 * rate for the last window as well as the cumulative one.  Cumulative alone
 * is misleading on a title whose menus run for a thousand frames before any
 * supersampled content exists.
 *
 * `hb_hires_seeded` says whether those snapshots were actually taken.  They
 * are only ever taken by a heartbeat that ran, and heartbeats only run under
 * CRASH_DETECT_VERBOSE -- so if verbose is switched on mid-run the snapshots
 * are still zero while the counters are not, and a delta against them would
 * be cumulative-from-boot wearing a window label.  That is precisely the
 * misreading these counters exist to prevent (AvP: 53.6% cumulative vs 98.2%
 * live), so the first heartbeat seeds instead of reporting. */
static uint64_t hb_prev_hires_hits;
static uint64_t hb_prev_hires_miss_value;
static uint64_t hb_prev_hires_miss_epoch;
static uint64_t hb_prev_hires_miss_nopage;
static int      hb_hires_seeded;

/* Last frame at which each signature fired -- prevents log spam. */
static unsigned last_log_gpu_escape;
static unsigned last_log_dsp_escape;
static unsigned last_log_gpu_wedge;
static unsigned last_log_dsp_wedge;
static unsigned last_log_fb_stall;
static unsigned last_log_cd_seek_wedge;
static unsigned last_log_gpu_runaway;
static unsigned last_log_gpu_go_full;

static uint32_t gpu_go_pages[GPU_GO_PAGES_MAX];
static unsigned gpu_go_page_count;

/* ---------- helpers ---------- */

static uint32_t pc_canonical(uint32_t pc)
{
   return pc & PC_ALIAS_MASK;
}

static int gpu_pc_in_local(uint32_t pc)
{
   return (pc >= GPU_LOCAL_LO && pc <= GPU_LOCAL_HI);
}

static int dsp_pc_in_local(uint32_t pc)
{
   return (pc >= DSP_LOCAL_LO && pc <= DSP_LOCAL_HI);
}

static int gpu_pc_valid(uint32_t pc)
{
   pc = pc_canonical(pc);
   if (pc <= MAPPED_CODE_HI) return 1;
   if (gpu_pc_in_local(pc)) return 1;
   return 0;
}

static int dsp_pc_valid(uint32_t pc)
{
   /* High-byte garbage aliases in this core: GPUReadWord / DSP fetch
    * and the 68K bus path all do `addr &= 0x00FFFFFF` (gpu.c:331,
    * jaguar.c m68k_read_memory_*).  $FD012786 fetches from $012786.
    * Treating that as an escape was a false positive against our own
    * decode.  DSP has no gpu_runaway twin -- a Defender-style jump
    * into a data buffer is still invisible here. */
   pc = pc_canonical(pc);
   if (pc <= MAPPED_CODE_HI) return 1;
   if (dsp_pc_in_local(pc)) return 1;
   return 0;
}

/* True when this (already canonical) PC sits in a window the GPU was
 * started at.  Local RAM is always a legal start; main-RAM programs are
 * legal in [start_page, start_page + GPU_GO_WINDOW). */
static int gpu_pc_in_start_page(uint32_t pc)
{
   unsigned i;
   uint32_t start;

   if (gpu_pc_in_local(pc))
      return 1;

   for (i = 0; i < gpu_go_page_count; i++)
   {
      start = gpu_go_pages[i];
      if (pc >= start && pc < start + GPU_GO_WINDOW)
         return 1;
   }
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

/* Verbose-heartbeat extension: the OP shadow-resolve hit rate.
 *
 * Hi-res has one failure mode that produces no other symptom -- the blitter
 * stores every supersampled block correctly and the OP resolve then discards
 * all of them, putting 0.0000% supersampled pixels on screen with nothing in
 * the log.  Production and delivery are separate failure points; only
 * delivery fails silently.  A low `epoch=` share is the signature, and it has
 * been the answer twice (Doom, Alien vs Predator).
 *
 * Emitted only while hi-res is actually active, so 1x runs stay quiet, and
 * only under CRASH_DETECT_VERBOSE.  Counters are 64-bit and printed through
 * double (%.0f): C89 has no %llu, and `unsigned long` is 32-bit under MSVC,
 * which a long session would overflow. */

/* Everything up to and including the cumulative rate.  The window figure is
 * appended by one of two callers below -- a real percentage, or the literal
 * "n/a", never a number that means something else. */
#define HIRES_RESOLVE_FMT \
   "[CRASH-DETECT] hires_resolve frame=%u N=%dx hits=%.0f misses=%.0f " \
   "(epoch=%.0f value=%.0f nopage=%.0f) rate=%.1f%%"

#define HIRES_RESOLVE_ARGS \
   frame_no, shadowHiresN, (double)hits, (double)misses, \
   (double)m_epoch, (double)m_value, (double)m_nopage, rate

static void hires_resolve_heartbeat(void)
{
   uint64_t hits, m_value, m_epoch, m_nopage, misses, total;
   uint64_t d_hits, d_misses, d_total;
   double   rate, window_rate;
   int      have_window;

   if (!shadowHiresActive)
      return;

   hits     = shadowHiresResolveHits;
   m_value  = shadowHiresResolveMissValue;
   m_epoch  = shadowHiresResolveMissEpoch;
   m_nopage = shadowHiresResolveMissNoPage;
   misses   = m_value + m_epoch + m_nopage;
   total    = hits + misses;
   rate     = total ? (100.0 * (double)hits / (double)total) : 0.0;

   /* Two ways the baseline can be untrustworthy:
    *
    * 1. It was never taken.  Only a heartbeat seeds it and only verbose runs
    *    heartbeats, so switching verbose on mid-run leaves the snapshots at
    *    zero while the counters are already in the millions -- the delta
    *    would then be the cumulative figure under a window label, which is
    *    the exact misreading this line exists to prevent.
    * 2. The counters went backwards.  They and these snapshots reset
    *    independently (ShadowHiresShutdown vs CrashDetectReset).  No live
    *    path zeroes one without the other today -- every ShadowHiresShutdown
    *    call site is in load/unload/deinit and the load path resets the
    *    watchdog afterwards -- but the subtraction below is unsigned, so a
    *    future reordering would otherwise print a 19-digit window_rate.
    *
    * Either way: seed now, say "n/a", and report a true window next time.  A
    * diagnostic that can lie loudly is worse than one that cannot; one that
    * lies quietly is worse than both. */
   have_window = hb_hires_seeded
              && hits     >= hb_prev_hires_hits
              && m_value  >= hb_prev_hires_miss_value
              && m_epoch  >= hb_prev_hires_miss_epoch
              && m_nopage >= hb_prev_hires_miss_nopage;

   if (have_window)
   {
      d_hits   = hits - hb_prev_hires_hits;
      d_misses = (m_value  - hb_prev_hires_miss_value)
               + (m_epoch  - hb_prev_hires_miss_epoch)
               + (m_nopage - hb_prev_hires_miss_nopage);
      d_total  = d_hits + d_misses;

      window_rate = d_total ? (100.0 * (double)d_hits / (double)d_total) : 0.0;

      LOG_INF(HIRES_RESOLVE_FMT " window_rate=%.1f%%\n",
              HIRES_RESOLVE_ARGS, window_rate);
   }
   else
   {
      LOG_INF(HIRES_RESOLVE_FMT " window_rate=n/a (first window)\n",
              HIRES_RESOLVE_ARGS);
   }

   hb_prev_hires_hits        = hits;
   hb_prev_hires_miss_value  = m_value;
   hb_prev_hires_miss_epoch  = m_epoch;
   hb_prev_hires_miss_nopage = m_nopage;
   hb_hires_seeded           = 1;
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
   gpu_zero_opcode_frames = 0;
   last_gpu_opcount = 0;
   dsp_zero_opcode_frames = 0;
   last_dsp_opcount = 0;
   fb_hash_prev = 0;
   fb_same_hash_frames = 0;
   last_cd_fifo_drains = 0;
   cd_seek_wedge_frames = 0;
   next_heartbeat_frame = HEARTBEAT_FRAMES;
   hb_prev_hires_hits = 0;
   hb_prev_hires_miss_value = 0;
   hb_prev_hires_miss_epoch = 0;
   hb_prev_hires_miss_nopage = 0;
   hb_hires_seeded = 0;
   last_log_gpu_escape = 0;
   last_log_dsp_escape = 0;
   last_log_gpu_wedge = 0;
   last_log_dsp_wedge = 0;
   last_log_fb_stall = 0;
   last_log_cd_seek_wedge = 0;
   last_log_gpu_runaway = 0;
   last_log_gpu_go_full = 0;
   gpu_go_page_count = 0;
}

void CrashDetectNoteGPUGo(uint32_t pc)
{
   unsigned i;
   uint32_t page;

   pc = pc_canonical(pc);
   if (gpu_pc_in_local(pc))
      return;

   page = pc & GPU_GO_PAGE_MASK;
   for (i = 0; i < gpu_go_page_count; i++)
   {
      if (gpu_go_pages[i] == page)
         return;
   }
   if (gpu_go_page_count >= GPU_GO_PAGES_MAX)
   {
      if (may_log(&last_log_gpu_go_full))
         LOG_WRN("[CRASH-DETECT] gpu_go_pages full (%u); later GO $%08X dropped\n",
                 GPU_GO_PAGES_MAX, pc);
      return;
   }
   gpu_go_pages[gpu_go_page_count++] = page;
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
   uint32_t cd_seek_starts;
   uint32_t cd_seek_dones;
   uint32_t cd_fifo_drains;

   if (!cd_initialized) return;
   if (cd_mode == CRASH_DETECT_OFF) return;

   frame_no++;

   cur_gpu_pc = pc_canonical(gpu_pc);
   cur_dsp_pc = pc_canonical(dsp_pc);
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

   /* ---- GPU runaway: executing outside local RAM and outside any
    * page the GPU was ever started at.  A data-buffer spin in main RAM
    * is a valid mapped address, so gpu_pc_escape never fires for it
    * (Defender 2000 @ 3x, issue #461). */
   if (gpu_running && gpu_pc_valid(cur_gpu_pc)
       && !gpu_pc_in_start_page(cur_gpu_pc))
   {
      if (may_log(&last_log_gpu_runaway))
         LOG_ERR("[CRASH-DETECT] gpu_runaway frame=%u pc=$%08X (not local RAM, not a GPU-GO page)\n",
                 frame_no, cur_gpu_pc);
   }

   /* ---- DSP PC escape ---- */
   if (dsp_running && !dsp_pc_valid(cur_dsp_pc))
   {
      if (may_log(&last_log_dsp_escape))
         LOG_ERR("[CRASH-DETECT] dsp_pc_escape frame=%u pc=$%08X (valid: $0-$E3FFFF or $F1B000-$F1CFFF)\n",
                 frame_no, cur_dsp_pc);
   }

   /* ---- GPU wedge: still running, executing NOTHING ----
    * The opcode delta is the whole predicate.  A stable sampled PC is NOT
    * required: it aliases on healthy spin loops (see the extern block at
    * the top of this file), and requiring it can mask a real zero-opcode
    * wedge whose PC moves anyway (e.g. an external G_PC write while the
    * core executes nothing). */
   if (gpu_running && gpu_exec_opcode_count == last_gpu_opcount)
   {
      gpu_zero_opcode_frames++;
      if (gpu_zero_opcode_frames == WEDGE_FRAMES_GPU
          && may_log(&last_log_gpu_wedge))
         LOG_WRN("[CRASH-DETECT] gpu_wedge frame=%u pc=$%08X running but 0 opcodes for %u frames\n",
                 frame_no, cur_gpu_pc, WEDGE_FRAMES_GPU);
   }
   else
   {
      gpu_zero_opcode_frames = 0;
   }
   last_gpu_opcount = gpu_exec_opcode_count;

   /* ---- DSP wedge: same rule as the GPU.  (The old sampled-PC-only
    * predicate is also why WEDGE_FRAMES_DSP grew to 600: audio engines
    * idle in JR loops that alias exactly like Super Burnout's GPU wait.
    * The threshold stays conservative anyway.) ---- */
   if (dsp_running && dsp_exec_opcode_count == last_dsp_opcount)
   {
      dsp_zero_opcode_frames++;
      if (dsp_zero_opcode_frames == WEDGE_FRAMES_DSP
          && may_log(&last_log_dsp_wedge))
         LOG_WRN("[CRASH-DETECT] dsp_wedge frame=%u pc=$%08X running but 0 opcodes for %u frames\n",
                 frame_no, cur_dsp_pc, WEDGE_FRAMES_DSP);
   }
   else
   {
      dsp_zero_opcode_frames = 0;
   }
   last_dsp_opcount = dsp_exec_opcode_count;

   /* ---- Video stall: framebuffer hash unchanged while a processor is
    * running AND that processor is not in a healthy spin.  GPU: local
    * RAM or a recorded GO window (same predicate as gpu_runaway).
    * DSP: local RAM only (no start-page table).  A still image with
    * the GPU looping in a program it was started at is not a crash. */
   {
      int gpu_healthy_spin;
      int dsp_healthy_spin;
      int stalled_processor;

      gpu_healthy_spin = gpu_running && gpu_pc_in_start_page(cur_gpu_pc)
            && gpu_zero_opcode_frames == 0;
      dsp_healthy_spin = dsp_running && dsp_pc_in_local(cur_dsp_pc)
            && dsp_zero_opcode_frames == 0;
      stalled_processor = (gpu_running && !gpu_healthy_spin)
            || (dsp_running && !dsp_healthy_spin);

      if (fb && cur_fb_hash == fb_hash_prev && stalled_processor)
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
   }

   /* ---- CD seek wedge: a seek was issued (real-BIOS/BUTCHExec path) but
    * the FIFO drain counter has made no progress for WEDGE_FRAMES_CD_SEEK
    * frames while a processor is still running. Gated on cd_seek_starts
    * > 0 so non-CD games (and CD games before their first seek) never
    * evaluate this at all.
    *
    * Deliberately broader than "SEEK_START without SEEK_DONE": comparing
    * fifoDrains instead of seekDones catches both CD seek-wedge shapes
    * that need telling apart -- (a) the seek-completion response never
    * arrives (seekDones stays behind seekStarts, so fifoDrains obviously
    * never advances either), and (b) the seek completes fine but the
    * FIFO continuation dies afterward (seekDones catches up, fifoDrains
    * still never advances). Either way, the ring dump below shows which
    * one happened. */
   if (bootConfig.isCDGame)
   {
      CDROMDiagGetSeekWedgeState(&cd_seek_starts, &cd_seek_dones, &cd_fifo_drains);

      if (cd_seek_starts > 0 && cd_fifo_drains == last_cd_fifo_drains
          && (gpu_running || dsp_running))
      {
         cd_seek_wedge_frames++;
         if (cd_seek_wedge_frames == WEDGE_FRAMES_CD_SEEK
             && may_log(&last_log_cd_seek_wedge))
         {
            LOG_WRN("[CRASH-DETECT] cd_seek_wedge frame=%u seek_starts=%u seek_dones=%u "
                    "fifo_drains=%u unchanged for %u frames gpu_pc=$%08X gpu_run=%d "
                    "dsp_pc=$%08X dsp_run=%d\n",
                    frame_no, cd_seek_starts, cd_seek_dones, cd_fifo_drains,
                    WEDGE_FRAMES_CD_SEEK, cur_gpu_pc, gpu_running, cur_dsp_pc, dsp_running);
            CDTraceDump();
         }
      }
      else
      {
         cd_seek_wedge_frames = 0;
      }

      last_cd_fifo_drains = cd_fifo_drains;
   }

   /* ---- Verbose heartbeat ---- */
   if (cd_mode == CRASH_DETECT_VERBOSE && frame_no >= next_heartbeat_frame)
   {
      LOG_INF("[CRASH-DETECT] heartbeat frame=%u gpu_pc=$%08X gpu_run=%d "
              "dsp_pc=$%08X dsp_run=%d fb_hash=$%08X\n",
              frame_no, cur_gpu_pc, gpu_running,
              cur_dsp_pc, dsp_running, cur_fb_hash);
      hires_resolve_heartbeat();
      next_heartbeat_frame = frame_no + HEARTBEAT_FRAMES;
   }

   fb_hash_prev = cur_fb_hash;
}

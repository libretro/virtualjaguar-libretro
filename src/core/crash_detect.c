/* crash_detect.c -- See crash_detect.h.  Cheap per-frame watchdog. */

#include "crash_detect.h"
#include "log.h"
#include "../jerry/dsp.h"   /* DSPIsRunning() returns bool -- match the canonical decl */
#include "../tom/gpu.h"     /* GPUIsRunning() */
#include "../tom/blitter.h" /* BlitterNeverEnds() -- shared never-ending-blit predicate */
#include "../cd/cdrom.h"    /* CDROMDiagGetSeekWedgeState(), CDTraceDump() */
#include "../tom/shadowfb.h" /* shadowHiresActive + resolve counters */
#include "settings.h"       /* bootConfig.isCDGame */
#include "../m68000/m68kinterface.h" /* m68k_get_reg() for inframe_hang */
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
 * a processor is still running AND the drive still owes data (see
 * CrashDetectCDSeekWedgeFrame()). SEEK_DELAY_TICKS in cdrom.c is ~100
 * halfline ticks (~0.2 frames at NTSC's ~524 halflines/frame) even for a
 * from-scratch seek, so 300 frames (5 sec) is far beyond any legitimate
 * seek.
 *
 * Drains frozen alone is NOT a wedge: a game that finished its transfer
 * stops draining and plays from RAM (Myst's ~6 s black pause after the
 * intro movie, Primal Rage / BrainDead 13 after their FMV loads).  Issue
 * #741 measured 15 such fires across 7 bios-mode titles that all went on
 * to run: every one had BUTCH low byte $02 -- the engine's ISR clears the
 * master interrupt enable (bit 0) to signal completion.  Philia (bios)
 * ends its transfer the other way: BUTCH stays $03 but it clears I2CNTRL
 * bit 2 (I2S FIFO data enable) and waits for a button.  The real
 * IMASK-stuck transfer wedge (7c98e16, re-broken as a control) has both
 * still on -- BUTCH $03, I2CNTRL bit 2 set -- with nobody servicing the
 * interrupt.  FIFO-level state does not separate them (fifoDataReady=1 and
 * cdPlaying=1 in the BUTCH-$02 fires and in the wedge alike). */
#define WEDGE_FRAMES_CD_SEEK 300

/* BUTCH interrupt-control low byte: master enable | FIFO half-full enable. */
#define CD_BUTCH_FIFO_XFER_IRQS 0x03u
/* I2CNTRL low byte: I2S FIFO data enable. */
#define CD_I2S_FIFO_ENABLE      0x04u

/* inframe_hang (issue #740): a blit this large cannot be legitimate.
 * 2^24 pixels sweeps all 2 MB of main RAM at 1bpp eight times over; the
 * largest real blits are full-screen clears (~10^5 pixels).  The case that
 * motivated it: Music Demo (ScatoLOGIC) in BIOS mode, where a wedged GPU
 * writes $2710826D into B_COUNT (334M pixels) -- the accurate blitter runs
 * it for hours inside one register write and retro_run never returns. */
#define INFRAME_BLIT_PIXELS_MAX 0x01000000u

/* Work the DEFAULT (accurate) engine actually does for a B_COUNT, which is
 * what can hang the host -- not the JTRM's nominal size.  JTRM v8 (BLIT_COUNT
 * $F0223C) says each 16-bit counter takes 1..65536 with 0 encoding 65536.
 * The outer counter runs that way (ocount-- from 0 wraps to $FFFF).  The
 * inner one does not: INNER.NET (jag_sim netlists/tom) asserts inner0 when
 * the count is zero OR has underflowed (Inner0t/Uflowt, ~lines 662-673), and
 * BlitterMidsummer2 matches it, so a 0 inner count stops after its first
 * step -- at most one phrase, 64 pixels at 1bpp.  The pixel count assumes
 * the counter actually decrements; the one shape where it cannot (phrase
 * mode below 8bpp) is CrashDetectBlitNeverEnds.  Williams/Telegames carts (Troy
 * Aikman, Double Dragon V, Brutal Sports Football) write B_COUNT=0 at boot
 * and run fine; counting that as 2^32 pixels was a false alarm. */
static uint32_t inframe_blit_inner(uint32_t b_count)
{
   return (b_count & 0xFFFFu) ? (b_count & 0xFFFFu) : 64u;
}

static uint32_t inframe_blit_outer(uint32_t b_count)
{
   return (b_count >> 16) ? (b_count >> 16) : 0x10000u;
}

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
/* Sticky: how many times cd_seek_wedge has crossed its threshold since the
 * last CrashDetectReset().  Counted before the log throttle, so a test can
 * ask the watchdog directly instead of grepping the log (issue #799). */
static unsigned cd_seek_wedge_fires;

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
static unsigned last_log_inframe_blit;

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
   cd_seek_wedge_fires = 0;
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
   last_log_inframe_blit = 0;
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

int CrashDetectBlitIsAbsurd(uint32_t b_count)
{
   uint32_t inner = inframe_blit_inner(b_count);
   uint32_t outer = inframe_blit_outer(b_count);

   /* Compared without forming the product, which would not fit in 32 bits. */
   return outer > INFRAME_BLIT_PIXELS_MAX / inner;
}

/* A blit whose first inner loop can never end (issue #800).  The
 * predicate is the blitter's own (BlitterNeverEnds, src/tom/blitter_mmio.c,
 * where the INNER.NET derivation lives), so the watchdog, the dispatch
 * path and the accurate engine cannot drift apart. */
int CrashDetectBlitNeverEnds(uint32_t b_count, uint32_t dst_flags, uint32_t dst_x)
{
   return BlitterNeverEnds(b_count, dst_flags, dst_x);
}

void CrashDetectNoteBlit(uint32_t b_count, uint32_t b_cmd, uint32_t a1_base,
                         uint32_t dst_flags, uint32_t dst_x)
{
   int never_ends;

   if (!cd_initialized || cd_mode == CRASH_DETECT_OFF)
      return;
   never_ends = CrashDetectBlitNeverEnds(b_count, dst_flags, dst_x);
   if (!never_ends && !CrashDetectBlitIsAbsurd(b_count))
      return;
   if (!may_log(&last_log_inframe_blit))
      return;
   if (never_ends)
   {
      LOG_ERR("[CRASH-DETECT] inframe_hang frame=%u where=blitter b_count=$%08X "
              "never_ends=1%s dst_flags=$%08X dst_x=%u b_cmd=$%08X a1_base=$%08X "
              "gpu_pc=$%08X gpu_run=%d dsp_pc=$%08X dsp_run=%d m68k_pc=$%06X "
              "(phrase-mode blit below 8bpp: the inner counter never "
              "decrements; the emulated blitter stays hung until reset)\n",
              frame_no + 1, b_count,
              BlitterNeverEndsApprox(b_cmd) ? " approx=1" : "",
              dst_flags, (unsigned)(dst_x & 0xFFFFu),
              b_cmd, a1_base, pc_canonical(gpu_pc), (int)GPUIsRunning(),
              pc_canonical(dsp_pc), (int)DSPIsRunning(),
              (unsigned)(m68k_get_reg(NULL, M68K_REG_PC) & PC_ALIAS_MASK));
      return;
   }
   LOG_ERR("[CRASH-DETECT] inframe_hang frame=%u where=blitter b_count=$%08X "
           "pixels=%.0f b_cmd=$%08X a1_base=$%08X gpu_pc=$%08X gpu_run=%d "
           "dsp_pc=$%08X dsp_run=%d m68k_pc=$%06X (blit runs synchronously; "
           "the frame may never complete)\n",
           frame_no + 1, b_count,
           (double)inframe_blit_inner(b_count)
              * (double)inframe_blit_outer(b_count),
           b_cmd, a1_base, pc_canonical(gpu_pc), (int)GPUIsRunning(),
           pc_canonical(dsp_pc), (int)DSPIsRunning(),
           (unsigned)(m68k_get_reg(NULL, M68K_REG_PC) & PC_ALIAS_MASK));
}

void CrashDetectSetMode(int mode)
{
   if (mode < CRASH_DETECT_OFF || mode > CRASH_DETECT_VERBOSE) mode = CRASH_DETECT_ON;
   cd_mode = mode;
}

int CrashDetectCDSeekWedgeFrame(uint32_t seek_starts, uint32_t seek_dones,
                                uint32_t fifo_drains, uint32_t last_fifo_drains,
                                int processor_running, uint8_t butch_int,
                                uint8_t i2s_ctrl)
{
   if (seek_starts == 0 || fifo_drains != last_fifo_drains || !processor_running)
      return 0;
   /* Shape (a): the seek-complete response never arrived. */
   if (seek_starts != seek_dones)
      return 1;
   /* Shape (b): seeks done, but the game still has a FIFO transfer open
    * (interrupts armed, FIFO data enabled) and no drain has happened. */
   return (butch_int & CD_BUTCH_FIFO_XFER_IRQS) == CD_BUTCH_FIFO_XFER_IRQS
       && (i2s_ctrl & CD_I2S_FIFO_ENABLE) != 0;
}

unsigned CrashDetectCDSeekWedgeFires(void)
{
   return cd_seek_wedge_fires;
}

unsigned CrashDetectCDSeekWedgeStreak(void)
{
   return cd_seek_wedge_frames;
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
   uint8_t  cd_butch_int;
   uint8_t  cd_i2s_ctrl;

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
    * frames while a processor is still running and the drive still owes
    * data. Gated on cd_seek_starts > 0 so non-CD games (and CD games before
    * their first seek) never evaluate this at all.
    *
    * Catches both CD seek-wedge shapes -- (a) the seek-completion response
    * never arrives (seekDones stays behind seekStarts), and (b) the seek
    * completes but the FIFO continuation dies afterward while the game
    * still has the transfer open. A finished transfer (the game cleared
    * BUTCH bit 0 or I2CNTRL bit 2) is not counted; see
    * CrashDetectCDSeekWedgeFrame(). The ring dump below shows which shape
    * happened. */
   if (bootConfig.isCDGame)
   {
      CDROMDiagGetSeekWedgeState(&cd_seek_starts, &cd_seek_dones, &cd_fifo_drains);
      cd_butch_int = CDROMDiagGetButchIntCtrl();
      cd_i2s_ctrl  = CDROMDiagGetI2SCtrl();

      if (CrashDetectCDSeekWedgeFrame(cd_seek_starts, cd_seek_dones,
                                      cd_fifo_drains, last_cd_fifo_drains,
                                      gpu_running || dsp_running,
                                      cd_butch_int, cd_i2s_ctrl))
      {
         cd_seek_wedge_frames++;
         if (cd_seek_wedge_frames == WEDGE_FRAMES_CD_SEEK)
            cd_seek_wedge_fires++;
         if (cd_seek_wedge_frames == WEDGE_FRAMES_CD_SEEK
             && may_log(&last_log_cd_seek_wedge))
         {
            LOG_WRN("[CRASH-DETECT] cd_seek_wedge frame=%u seek_starts=%u seek_dones=%u "
                    "fifo_drains=%u unchanged for %u frames butch_int=$%02X i2s_ctrl=$%02X gpu_pc=$%08X "
                    "gpu_run=%d dsp_pc=$%08X dsp_run=%d\n",
                    frame_no, cd_seek_starts, cd_seek_dones, cd_fifo_drains,
                    WEDGE_FRAMES_CD_SEEK, (unsigned)cd_butch_int,
                    (unsigned)cd_i2s_ctrl, cur_gpu_pc,
                    gpu_running, cur_dsp_pc, dsp_running);
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

#include "blitter.h"
#include "blitter_internal.h"
#include "blit_memo.h"
#include "texdump.h"
#include "texreplace.h"

#include <string.h>
#include "bus_arbiter.h"
#include "gpu.h"
#include "../core/log.h"
#include "settings.h"
#include "vjag_memory.h"

#define A1_FLAGS        ((uint32_t)0x04)
#define A1_PIXEL        ((uint32_t)0x0C)
#define COMMAND         ((uint32_t)0x38)
#define SRCDATA         ((uint32_t)0x40)
#define DSTDATA         ((uint32_t)0x48)
#define DSTZ            ((uint32_t)0x50)
#define SRCZINT         ((uint32_t)0x58)
#define SRCZFRAC        ((uint32_t)0x60)
#define PATTERNDATA     ((uint32_t)0x68)
#define INTENSITYINC    ((uint32_t)0x70)
#define PHRASEINT0      ((uint32_t)0x7C)
#define PHRASEINT1      ((uint32_t)0x80)
#define PHRASEINT2      ((uint32_t)0x84)
#define PHRASEINT3      ((uint32_t)0x88)
#define PHRASEZ0        ((uint32_t)0x8C)
#define PHRASEZ1        ((uint32_t)0x90)
#define PHRASEZ2        ((uint32_t)0x94)
#define PHRASEZ3        ((uint32_t)0x98)

/* How long this blit would keep the bus on real hardware, in system
 * clocks (issue #399/#401: the emulated blitter completes every blit in
 * zero emulated time, so titles that pace a render-bound loop on blit
 * completion -- Jaguar Doom's menu, whose M_Drawer never calls
 * I_Update() and so has no VBL gate at all -- iterate faster than
 * hardware, and their auto-repeat thresholds land inside a normal
 * button tap).
 *
 * The blitter is the top-priority bus master: while a blit runs,
 * anything else that needs the bus waits.  Each dispatched blit opens
 * a busy window of its priced duration; the window decays with real
 * emulated time (BlitterTimingTick), and the NEXT blitter-register
 * access from either master pays the remainder --
 * BlitterTimingChargeAccess routes a 68K wait through the serialized
 * pending-stall channel (drained by M68KExecuteWithStalls, which
 * always leaves the 68K an eighth of each slice so IRQ delivery is
 * never starved) and a GPU wait into the current GPU instruction's
 * bus-stall accumulator.  Back-to-back blit chains -- Doom's menu
 * erase/draw sequences, GPU wall-column loops -- therefore serialize
 * at the real bus rate, while a master that fires one blit and never
 * returns pays nothing further.
 *
 * Cost model, from the JTRM DRAM timing used by bus_arbiter.h: each
 * inner-loop unit performs one memory op per enabled port --
 * destination write always, plus source read (SRCEN), source Z read
 * (SRCENZ), destination read (DSTEN), destination Z read (DSTENZ),
 * Z write (DSTWRZ).  In phrase mode (A1 XADD = phrase) a unit moves
 * 64 bits, so the pixel count collapses by 64/bpp.  A write-only
 * sweep stays in one DRAM row and pays the fixed 2-clock page-mode
 * cycle per op; any second enabled port interleaves distant rows, so
 * every op carries the row-change overhead too (+3 clocks at the
 * default DRAMSPEED, MEMCON1 0x1861). */
static uint32_t BlitDurationSysclks(void)
{
   uint32_t count, cmd, a1flags;
   uint32_t ops, per_op;
   uint64_t units, clks;

   count   = GET32(blitter_ram, 0x3C);          /* B_COUNT */
   cmd     = GET32(blitter_ram, COMMAND);
   a1flags = GET32(blitter_ram, A1_FLAGS);

   units = (uint64_t)(count & 0xFFFF) * (count >> 16);

   ops = 1;                                     /* dst write */
   if (cmd & 0x0001) ops++;                     /* SRCEN */
   if (cmd & 0x0002) ops++;                     /* SRCENZ */
   if (cmd & 0x0008) ops++;                     /* DSTEN */
   if (cmd & 0x0010) ops++;                     /* DSTENZ */
   if (cmd & 0x0020) ops++;                     /* DSTWRZ */

   if (((a1flags >> 16) & 3) == 0)
   {
      /* Phrase mode: one memory op per 64-bit phrase. */
      uint32_t bpp = 1u << ((a1flags >> 3) & 7);
      if (bpp > 32)
         bpp = 32;
      units = (units * bpp + 63) / 64;
   }

   /* Write-only sweeps stay in one DRAM row and run at the page-mode
    * cycle (2 clocks).  As soon as a second port is enabled the access
    * pattern interleaves two (or more) distant regions -- source row,
    * destination row, back -- so in the worst case every access pays
    * the row-change overhead on top: 2 + 3 clocks at the Jaguar's
    * default DRAMSPEED (MEMCON1 0x1861; same table bus_arbiter.h
    * uses). */
   per_op = (ops > 1) ? 5u : 2u;

   clks = units * ops * per_op;

   /* Garbage counts (uninitialised B_COUNT) must not freeze a master
    * for seconds: cap at two fields' worth of bus time (a real
    * fullscreen interleaved copy legitimately exceeds one field).
    * Deliberately LARGER than the one-field 68K debt cap in
    * BlitterTimingChargeAccess(): this window models how long the
    * hardware blit actually occupies the bus (and is what a GPU
    * accessor pays in full), while the 68K debt cap separately bounds
    * pad-latch staleness -- see the comment at that clamp. */
   if (clks > 885560u)
      clks = 885560u;
   return (uint32_t)clks;
}

/* Busy window: how much bus time the most recent blit still owns.
 * Decays with emulated time (BlitterTimingTick from HalflineCallback);
 * the NEXT blitter-register access from either master pays whatever
 * remains and closes the window (BlitterTimingConsumeWait).  Serialized
 * (savestates must replay identically with the model enabled). */
static uint32_t blitterBusyClks = 0;

void BlitterTimingTick(uint32_t sysclks)
{
   if (blitterBusyClks > sysclks)
      blitterBusyClks -= sysclks;
   else
      blitterBusyClks = 0;
}

static uint32_t BlitterTimingConsumeWait(void)
{
   uint32_t r = blitterBusyClks;
   blitterBusyClks = 0;
   return r;
}

uint32_t BlitterTimingGetBusy(void)
{
   return blitterBusyClks;
}

void BlitterTimingSetBusy(uint32_t clks)
{
   blitterBusyClks = clks;
}

/* A master touched a blitter register while a blit is (still) running.
 * On hardware the blitter is the highest-priority bus master, so the
 * toucher waits out the remainder.  The 68K's wait goes through the
 * pending-stall channel; a GPU wait is charged to the current GPU
 * instruction's bus-stall accumulator (the load/store that touched us
 * simply takes that long). */
static void BlitterTimingChargeAccess(uint32_t who)
{
   uint32_t remaining;
   if (!vjs.blitterTiming || blitterBusyClks == 0)
      return;
   remaining = BlitterTimingConsumeWait();
   if (who == GPU)
      GPUChargeBusStall(remaining);
   else if (who != DSP)
   {
      /* Cap the total owed at one field.  On hardware nothing
       * accumulates across fields -- a blit finishes inside its
       * field and the bus frees -- so unbounded debt is an artifact
       * of this scalar approximation, and letting it compound
       * freezes the 68K for multiple fields, starves VI delivery,
       * and leaves the pad latch stale (one tap read as many, the
       * exact bug this model exists to fix).  The cap bounds latch
       * staleness to what real hardware can exhibit. */
      busArbiter.m68k_pending_stall += remaining;
      if (busArbiter.m68k_pending_stall > 442780u)
         busArbiter.m68k_pending_stall = 442780u;
   }
}


void BlitterInit(void)
{
   BlitterReset();
}


void BlitterReset(void)
{
   memset(blitter_ram, 0x00, 0xA0);
   /* The register file is not the whole of the blitter's serialised
    * state: the B_CMD decode statics live in blitter.c and used to
    * survive teardown into the next session's savestate (#479). */
   BlitterResetDecodeState();
}


void BlitterDone(void)
{
   /* iOS cannot dlclose the core, so nothing re-zeroes statics between
    * sessions -- teardown has to do it explicitly (see CLAUDE.md). */
   BlitterReset();
}


uint8_t BlitterReadByte(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFF;

   BlitterTimingChargeAccess(who);

   /* Real hardware returns $00000805, as documented in the JTRM. */
   if (offset == (COMMAND + 0))
      return 0x00;
   if (offset == (COMMAND + 1))
      return 0x00;
   if (offset == (COMMAND + 2))
      return 0x08;
   if (offset == (COMMAND + 3))
      return 0x05;	/* always idle/never stopped (collision detection ignored!) */

   /* Jaguar I bug: A1_PIXEL is mirrored when A1_FLAGS is read. */
   if (offset >= A1_FLAGS && offset <= (A1_FLAGS + 3))
      return blitter_ram[offset + 0x08];

   /* Jaguar I bug: A2_PIXEL is mirrored when A2_MASK is read. */
   if (offset >= 0x2C && offset <= 0x2F)
      return blitter_ram[offset + 0x04];

   return blitter_ram[offset];
}


uint16_t BlitterReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   return ((uint16_t)BlitterReadByte(offset, who) << 8) | (uint16_t)BlitterReadByte(offset + 1, who);
}


uint32_t BlitterReadLong(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   return (BlitterReadWord(offset, who) << 16) | BlitterReadWord(offset + 2, who);
}


void BlitterWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFF;

   BlitterTimingChargeAccess(who);

   /* INTENSITY writes also update their PATTERNDATA/SRCDATA mirrors. */
   if ((offset >= PHRASEINT0) && (offset <= (PHRASEZ3 + 3)))
   {
      switch (offset)
      {
      /* INTENSITY registers 0-3 */
      case PHRASEINT0 + 0: break;
      case PHRASEINT0 + 1: blitter_ram[PATTERNDATA + 7] = data; break;
      case PHRASEINT0 + 2: blitter_ram[SRCDATA + 6] = data; break;
      case PHRASEINT0 + 3: blitter_ram[SRCDATA + 7] = data; break;

      case PHRASEINT1 + 0: break;
      case PHRASEINT1 + 1: blitter_ram[PATTERNDATA + 5] = data; break;
      case PHRASEINT1 + 2: blitter_ram[SRCDATA + 4] = data; break;
      case PHRASEINT1 + 3: blitter_ram[SRCDATA + 5] = data; break;

      case PHRASEINT2 + 0: break;
      case PHRASEINT2 + 1: blitter_ram[PATTERNDATA + 3] = data; break;
      case PHRASEINT2 + 2: blitter_ram[SRCDATA + 2] = data; break;
      case PHRASEINT2 + 3: blitter_ram[SRCDATA + 3] = data; break;

      case PHRASEINT3 + 0: break;
      case PHRASEINT3 + 1: blitter_ram[PATTERNDATA + 1] = data; break;
      case PHRASEINT3 + 2: blitter_ram[SRCDATA + 0] = data; break;
      case PHRASEINT3 + 3: blitter_ram[SRCDATA + 1] = data; break;

      /* Z registers 0-3 */
      case PHRASEZ0 + 0: blitter_ram[SRCZINT + 6] = data; break;
      case PHRASEZ0 + 1: blitter_ram[SRCZINT + 7] = data; break;
      case PHRASEZ0 + 2: blitter_ram[SRCZFRAC + 6] = data; break;
      case PHRASEZ0 + 3: blitter_ram[SRCZFRAC + 7] = data; break;

      case PHRASEZ1 + 0: blitter_ram[SRCZINT + 4] = data; break;
      case PHRASEZ1 + 1: blitter_ram[SRCZINT + 5] = data; break;
      case PHRASEZ1 + 2: blitter_ram[SRCZFRAC + 4] = data; break;
      case PHRASEZ1 + 3: blitter_ram[SRCZFRAC + 5] = data; break;

      case PHRASEZ2 + 0: blitter_ram[SRCZINT + 2] = data; break;
      case PHRASEZ2 + 1: blitter_ram[SRCZINT + 3] = data; break;
      case PHRASEZ2 + 2: blitter_ram[SRCZFRAC + 2] = data; break;
      case PHRASEZ2 + 3: blitter_ram[SRCZFRAC + 3] = data; break;

      case PHRASEZ3 + 0: blitter_ram[SRCZINT + 0] = data; break;
      case PHRASEZ3 + 1: blitter_ram[SRCZINT + 1] = data; break;
      case PHRASEZ3 + 2: blitter_ram[SRCZFRAC + 0] = data; break;
      case PHRASEZ3 + 3: blitter_ram[SRCZFRAC + 1] = data; break;
      }
   }
   else if (offset >= SRCDATA && offset <= (PATTERNDATA + 7))
   {
      /* 64-bit register longword swap: SRCDATA..PATTERNDATA (0x40-0x6F)
       * are six contiguous 8-byte registers.  The Jaguar's F-bus maps the
       * low address to the LOW longword and the high address to the HIGH
       * longword.  GET64 reads blitter_ram in big-endian byte order
       * (offset+0 = MSB), so we XOR bit 2 to swap the two 4-byte halves.
       * The PHRASEINT/PHRASEZ path above has its own per-byte mapping.
       * The Gouraud init reads (gd_c[]/gd_i[] in blitter.c) are designed
       * for this swapped layout. */
      blitter_ram[offset ^ 4] = data;
   }
   else
      blitter_ram[offset] = data;
}


void BlitterWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
   BlitterWriteByte(offset + 0, data >> 8, who);
   BlitterWriteByte(offset + 1, data & 0xFF, who);

   if ((offset & 0xFF) == 0x3A)
   {
      /* Compute the bus time BEFORE dispatch: the engines update their
       * shadow registers as they run. */
      uint32_t busClks = 0;
      int trBlit = 0;

      /* Re-entrancy guard (issue #659).
       *
       * A blit whose destination lands in $F022xx writes the blitter's own
       * registers as pixel data -- blitter_write_word()/_long() route
       * anything at or above $200000 through JaguarWrite*(), which comes
       * straight back here.  A pixel landing on B_CMD's low word (offset
       * $3A) then dispatches a SECOND blit from inside the first.
       *
       * Hardware cannot do that.  The blitter is one state machine and one
       * bus master; it is not re-entrant, and a write to B_CMD while it is
       * running cannot start a nested blit on top of the running one.  The
       * register write itself is real and still happens -- only the nested
       * dispatch is refused.
       *
       * Chroma-Luma Color Pick (PD) is the case that exposed it: the 68K
       * programs B_COUNT = $2704A7E4 (42980 x 9988 = 429M pixels), the blit
       * runs away into the register file, writes $0000 over B_CMD, and
       * re-enters with the same count still latched. Traced at depth=1, but
       * nothing bounded the nesting, and retro_run never returned -- a hard
       * freeze of the frontend, invisible to crash_detect because every
       * watchdog signature is checked between frames.
       *
       * Deliberately NOT a cap on blit size. That blit is finite (~16s of
       * Jaguar time) and hardware would complete it, so truncating it would
       * be a visible deviation; worse, abandoning a blit part-way leaves
       * A1/A2 and the counters in a state hardware never reaches, and those
       * are savestated (the determinism run-ahead and netplay depend on).
       * This guard is transient within a single dispatch, always zero
       * between frames, and so has no savestate surface at all. */
      static int blit_in_progress = 0;

      /* Warn once.  A title reaching this is misprogramming its blit
       * destination and the user wants to know why the picture is wrong,
       * but a runaway blit can arrive here on every pixel, so this must
       * never become a per-pixel log.
       *
       * NOTE for anyone touching the includes: this file is also compiled
       * STANDALONE by test/test_blitter_mmio, outside CFLAGS and without
       * linking the core.  src/core/log.h needs -DINLINE (supplied by that
       * Makefile rule) and resolves to vj_log_cb (stubbed in the test's
       * existing stub block).  Both are required together. */
      if (blit_in_progress)
      {
         static int warned = 0;
         if (!warned)
         {
            warned = 1;
            LOG_WRN("[blitter] B_CMD written while a blit is running "
                    "(destination overlaps $F022xx) -- refusing the nested "
                    "blit; see issue #659\n");
         }
         return;
      }

      if (vjs.blitterTiming)
         busClks = BlitDurationSysclks();

      /* Texture dump (issue #369): capture the register-described
       * source window BEFORE dispatch, engine-independently.  Read-only
       * host-side work; the emulated machine cannot observe it. */
      if (texDumpEnabled)
         TexDumpLaunch();

      /* Texture replacement (issue #369 deliverable 2): hash the source
       * window pre-dispatch (dump mode above captures the ORIGINAL
       * tile, so dump+replace compose for authors).  On a pack hit the
       * post-dispatch hook records pack RGB into the shadow
       * framebuffer -- host-side presentation only, the emulated
       * machine cannot observe it either. */
      if (texReplaceEnabled)
         trBlit = TexReplacePreBlit();

      blit_in_progress = 1;
      if (BlitterCompareIsEnabled())
         BlitterRunComparison();
      else if (!BlitMemoLaunch())
      {
         /* Memo off: dispatch as before. */
         if (vjs.useFastBlitter)
            blitter_blit(GET32(blitter_ram, COMMAND));
         else
            BlitterMidsummer2();
      }
      blit_in_progress = 0;

      if (trBlit)
         TexReplacePostBlit();

      /* The blit's results are complete (memory is final, as before);
       * only the TIME is owed.  The window is paid by the next
       * blitter-register access from either master -- back-to-back
       * blit chains (Doom's menu erase/draw sequences) serialize at
       * the real bus rate, while a master that fires one blit and
       * walks away pays nothing further. */
      blitterBusyClks = busClks;
   }
}


void BlitterWriteLong(uint32_t offset, uint32_t data, uint32_t who)
{
   BlitterWriteWord(offset + 0, data >> 16, who);
   BlitterWriteWord(offset + 2, data & 0xFFFF, who);
}

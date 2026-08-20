//
// DAC (really, Synchronous Serial Interface) Handler
//
// Originally by David Raingeard
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Rewritten by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
// JLH  04/30/2012  Changed SDL audio handler to run JERRY
//

/* The libretro audio path resamples from the I2S rate (determined by
 * SCLK) to 48 kHz output using linear interpolation.  When SCLK changes
 * mid-frame (e.g. Skyhammer / Iron Soldier 2 pitch effects), the
 * resampler adapts to the new rate on the next sample callback.
 *
 * I2S samples (written by the DSP to LTXD/RTXD) are captured into a
 * ring buffer at the hardware I2S rate.  DSPSampleCallback fires at
 * 48 kHz and linearly interpolates between ring buffer entries to
 * produce the output stream. */

#include "dac.h"
#include "log.h"  /* CDDA-DIAG */

#include <string.h>
#include "cdrom.h"
#include "dsp.h"
#include "event.h"
#include "jerry.h"
#include "jaguar.h"
#include "tom.h"
#include "m68000/m68kinterface.h"
#include "settings.h"
#include "perf_iface.h"

#include <libretro.h>

extern retro_audio_sample_batch_t audio_batch_cb;

#define BUFFER_SIZE		0x10000	/* Make the DAC buffers 64K x 16 bits */
#define DAC_AUDIO_RATE		48000	/* Set the audio rate to 48 KHz */

/* Ring buffer for I2S samples produced by the DSP at hardware rate */
#define I2S_RING_SIZE		16384	/* Power of 2, must be > max samples/frame (PAL SCLK=0 ~8311) */
#define I2S_RING_MASK		(I2S_RING_SIZE - 1)

/* Steady-state distance the read cursor keeps behind the write cursor,
 * and the drift beyond which it snaps back instead of free-running.
 * Capture (word strobe) and consumption (rate ratio) both derive from
 * SCLK, so in steady state the lag only jitters by a fraction of a
 * sample; the resync handles discontinuities -- SCLK reprogramming,
 * savestate loads, a DSP that stops feeding the port. */
#define I2S_TARGET_LAG		2
#define I2S_RESYNC_LAG		256

/* Jaguar memory locations */

#define LTXD			0xF1A148
#define RTXD			0xF1A14C
#define LRXD			0xF1A148
#define RRXD			0xF1A14C
#define SCLK			0xF1A150
#define SMODE			0xF1A154

/* Global variables */
uint16_t * sampleBuffer;
static int bufferIndex = 0;
static int numberOfSamples = 0;
static bool bufferDone = false;

/* I2S resampling state */
static int16_t i2sRingL[I2S_RING_SIZE];
static int16_t i2sRingR[I2S_RING_SIZE];
static uint32_t i2sWritePos = 0;	/* next write position in ring */
static uint32_t i2sWriteCount = 0;	/* total samples captured this frame */
static uint32_t i2sNonZeroCount = 0;	/* samples with non-zero amplitude this frame */
static double i2sPhase = 0.0;		/* fractional read position */
static double i2sRateRatio = 1.0;	/* i2s_rate / 48000.0 */
static uint32_t i2sResyncCount = 0;	/* gross-drift resyncs this session (diagnostic) */

/* Output-sample cadence, derived per frame in DACPrepareFrame (#393).
 *
 * The batch is a fixed `length` (800 NTSC / 960 PAL pairs) but the
 * emulated field is NOT 800 sample periods long: it is VP+1 halflines
 * (799.275 periods at 48 kHz for the standard NTSC VP=523, 958.464 for
 * PAL VP=623).  Scheduling the chain at a flat 1/48000 s landed only
 * 799 (NTSC) callbacks inside the frame while ring capture (word
 * strobes) tracked the full field, so the read cursor fell behind by
 * 0.275 x ratio ring samples per frame.  PAL drifts the other way by
 * the same magnitude: a 624-halfline field is 958.464 periods but the
 * flat schedule consumed 960, so the cursor gained 1.536 x ratio ring
 * samples per frame.  The lag climbed from I2S_TARGET_LAG to I2S_RESYNC_LAG and
 * the gross-drift resync snapped it back, discarding ~254 ring samples
 * (~12 ms) in one hop -- an audible skip every ~36 s NTSC / ~8 s PAL
 * in every title with continuous DSP audio (issue #393).
 *
 * Instead, spread the `length/2` output samples evenly across the real
 * field (period = frame_us / pairs) and scale the per-sample phase
 * step by the same factor (frame_periods / pairs), so per-frame
 * consumption equals capture identically -- for any VP, any SCLK,
 * master or slave mode.  The half-period first-callback offset keeps
 * the final callback strictly inside the frame, away from the
 * frame-boundary race with HalflineCallback. */
static double i2sSamplePeriodUs = 1000000.0 / (double)DAC_AUDIO_RATE;
static double i2sStepScale = 1.0;

/* Private function prototypes */
static void DACUpdateSCLKRate(void);

void DACInit(void)
{
   DACReset();

   *ltxd = 0;
   lrxd  = 0;
   *sclk = 19;									/* Default is roughly 22 KHz */
   DACUpdateSCLKRate();
}


/* Reset the sound buffer FIFOs */
void DACReset(void)
{
   *ltxd = 0;
   lrxd  = 0;
   sstat = 0;

   i2sWritePos = 0;
   i2sWriteCount = 0;
   i2sNonZeroCount = 0;
   i2sPhase = 0.0;
   i2sRateRatio = 1.0;
   i2sResyncCount = 0;
   i2sSamplePeriodUs = 1000000.0 / (double)DAC_AUDIO_RATE;
   i2sStepScale = 1.0;
   memset(i2sRingL, 0, sizeof(i2sRingL));
   memset(i2sRingR, 0, sizeof(i2sRingR));
}

void DACDone(void)
{
}

uint32_t DACGetI2SWriteCount(void)
{
   return i2sWriteCount;
}

uint32_t DACGetI2SNonZeroCount(void)
{
   return i2sNonZeroCount;
}

/* Diagnostics for the resample cursor pair (issue #393): the distance the
 * read cursor trails the write cursor, and how many times the gross-drift
 * resync in DSPSampleCallback has snapped it back. */
double DACGetI2SLag(void)
{
   return (double)i2sWritePos - i2sPhase;
}

uint32_t DACGetI2SResyncCount(void)
{
   return i2sResyncCount;
}

/* Update the rate ratio when SCLK or SMODE changes */
static void DACUpdateSCLKRate(void)
{
   uint32_t sclk_val;
   double i2s_rate;
   double sys_clock;

   if (*smode & SMODE_INTERNAL)
   {
      /* Master mode: JERRY generates the bit clock from SCLK. */
      sclk_val = (uint32_t)(*sclk);
      sys_clock = (double)SYSTEM_CLOCK_RATE;
      /* sample_rate = system_clock / (64 * (SCLK + 1)) */
      i2s_rate = sys_clock / (64.0 * (sclk_val + 1));
   }
   else
   {
      /* Slave mode: the word clock is external (BUTCH, CD audio).  SCLK
       * is meaningless here -- JERRYI2SCallback drives the DSP ISR at a
       * fixed 22.675737 us (44100 Hz), so LTXD/RTXD writes land in the
       * ring at that rate.  Deriving the ratio from a stale SCLK (games
       * leave the reset value 19 = 20.8 kHz) made the resampler consume
       * under half of each frame's samples and discard the rest at the
       * DACPrepareFrame ring reset -- a 60 Hz chop over ALL slave-mode
       * DSP output (CD music and synth SFX alike), heard as loud
       * crunched static on Jaguar CD titles. */
      i2s_rate = 44100.0;
   }
   i2sRateRatio = i2s_rate / (double)DAC_AUDIO_RATE;

   /* Clamp to a sane range to avoid division by zero or absurd values.
    * SCLK=0 gives divider=64, i2s_rate~415kHz, ratio~8.66.
    * Upper bound of 16.0 allows all valid SCLK values (0-255). */
   if (i2sRateRatio < 0.01)
      i2sRateRatio = 0.01;
   if (i2sRateRatio > 16.0)
      i2sRateRatio = 16.0;
}

/* Called when the DSP writes a sample pair to LTXD/RTXD.
 * Stores the sample in the ring buffer for later resampling. */
static void DACCaptureSample(int16_t left, int16_t right)
{
   i2sRingL[i2sWritePos & I2S_RING_MASK] = left;
   i2sRingR[i2sWritePos & I2S_RING_MASK] = right;
   i2sWritePos++;
   i2sWriteCount++;
   if (left != 0 || right != 0)
      i2sNonZeroCount++;

   /* Both cursors run monotonically for the whole session; fold them
    * back together once per ring lap.  Subtracting a whole ring size
    * leaves every masked index unchanged, keeps the uint32 write cursor
    * from wrapping and the double read cursor in a range where adding
    * a fractional step still resolves exactly. */
   if (i2sWritePos >= (uint32_t)(2 * I2S_RING_SIZE)
       && i2sPhase >= (double)I2S_RING_SIZE)
   {
      i2sWritePos -= (uint32_t)I2S_RING_SIZE;
      i2sPhase    -= (double)I2S_RING_SIZE;
   }
}

/* One I2S word strobe has elapsed: latch whatever the DSP left in
 * LTXD/RTXD into the resample ring.
 *
 * The I2S port is a holding register, not a queue.  Hardware serialises
 * exactly one sample pair per word strobe and simply overwrites the
 * holding register on any write in between -- those intermediate values
 * never reach the DAC.  Capturing on every RTXD write instead fabricated
 * samples that were never transmitted: Atari Karts (master mode, SCLK=19)
 * writes 417 pairs per frame against 346 word strobes, so the ring grew
 * 20% faster than DSPSampleCallback consumed it and DACPrepareFrame threw
 * the surplus away on the next frame.  That reset yanked the output phase
 * forward once per frame -- a 60 Hz step of mean amplitude ~2959 (peak
 * 21881) in an otherwise ~110-amplitude signal, heard as constant
 * crackle on cartridge titles.  It is the master-mode twin of the
 * slave-mode chop described in DACUpdateSCLKRate().
 */
void DACWordStrobe(void)
{
   DACCaptureSample((int16_t)(*ltxd), (int16_t)(*rtxd));
}

void DSPSampleCallback(void)
{
   int16_t outL, outR;
   uint32_t idx0, idx1;
   double frac;
   int32_t s0L, s1L, s0R, s1R;

   /* Guard: hold current register value until the ring has data */
   if (i2sWritePos < 2)
   {
      outL = (int16_t)(*ltxd);
      outR = (int16_t)(*rtxd);
   }
   else
   {
      /* i2sPhase is a monotonic read cursor into the captured I2S
       * stream; it advances by the rate ratio per 48 kHz output sample
       * and carries across frames.  Both cursors are rebased together
       * in DACCaptureSample, which leaves masked indices unchanged.
       *
       * Resetting the cursor every frame (the old scheme) discarded the
       * read position and re-anchored at the newest sample, stepping
       * the output once per frame at 60 Hz. */
      double lag = (double)i2sWritePos - i2sPhase;

      /* Resync only on gross drift: a rate change mid-stream (SCLK
       * write), a savestate load, or a DSP that stopped feeding the
       * port.  In steady state capture and consumption both derive
       * from SCLK, so the lag stays put. */
      if (lag < 0.0 || lag > (double)I2S_RESYNC_LAG)
      {
         i2sResyncCount++;
         i2sPhase = (double)i2sWritePos - (double)I2S_TARGET_LAG;
         if (i2sPhase < 0.0)
            i2sPhase = 0.0;
         lag = (double)i2sWritePos - i2sPhase;
      }

      idx0 = (uint32_t)i2sPhase;
      frac = i2sPhase - (double)idx0;
      idx1 = idx0 + 1;

      /* Never read at or past the write cursor: those slots hold the
       * previous lap of the ring. */
      if (idx0 >= i2sWritePos)
         idx0 = i2sWritePos - 1;
      if (idx1 >= i2sWritePos)
         idx1 = i2sWritePos - 1;

      s0L = (int32_t)i2sRingL[idx0 & I2S_RING_MASK];
      s1L = (int32_t)i2sRingL[idx1 & I2S_RING_MASK];
      s0R = (int32_t)i2sRingR[idx0 & I2S_RING_MASK];
      s1R = (int32_t)i2sRingR[idx1 & I2S_RING_MASK];

      outL = (int16_t)(s0L + (int32_t)((double)(s1L - s0L) * frac));
      outR = (int16_t)(s0R + (int32_t)((double)(s1R - s0R) * frac));

      /* Underrun: hold position until the next capture lands rather
       * than running past the write head. */
      if (lag > 1.0)
         i2sPhase += i2sRateRatio * i2sStepScale;
   }

   sampleBuffer[bufferIndex + 0] = (uint16_t)outL;
   sampleBuffer[bufferIndex + 1] = (uint16_t)outR;
   bufferIndex += 2;

   if (bufferIndex >= numberOfSamples)
   {
      bufferDone = true;
      return;
   }

   SetCallbackTime(DSPSampleCallback, i2sSamplePeriodUs, EVENT_JERRY);
}

void DACPrepareFrame(int length)
{
   uint32_t vp1;
   int out_pairs;
   double halfline_us, frame_us, pairs;

   /* No return between here and VJP_LEAVE (perf_iface.h). */
   VJP_ENTER(VJP_DAC);

   RemoveCallback(DSPSampleCallback);
   bufferIndex = 0;
   numberOfSamples = length;
   bufferDone = false;

   /* Derive the output cadence from the field the frame loop will
    * actually run: JaguarExecuteNew ends the frame when VC wraps after
    * VP+1 halflines (HalflineCallback).  Clamp a garbage VP (boot-time
    * zero, mid-init writes) to the hardware default field. */
   vp1 = (uint32_t)(TOMReadWord(0xF0003E, JAGUAR) & 0x7FF) + 1;
   if (vp1 < 200 || vp1 > 1200)
      vp1 = JaguarGetDefaultFieldHalflines();
   halfline_us = JaguarGetHalflinePeriodUs();
   frame_us = (double)vp1 * halfline_us;
   out_pairs = length / 2;
   pairs = (double)out_pairs;
   i2sSamplePeriodUs = frame_us / pairs;
   i2sStepScale = (frame_us * ((double)DAC_AUDIO_RATE / 1000000.0)) / pairs;

   /* Per-frame diagnostic counters only (DACGetI2SWriteCount /
    * DACGetI2SNonZeroCount consumers).  The ring and both cursors
    * deliberately survive the frame boundary -- resetting and
    * re-anchoring them at the newest register value every frame was
    * the other half of the 60 Hz step. */
   i2sWriteCount = 0;
   i2sNonZeroCount = 0;

   /* Refresh rate ratio in case SCLK was written between frames */
   DACUpdateSCLKRate();

   SetCallbackTime(DSPSampleCallback, 0.5 * i2sSamplePeriodUs, EVENT_JERRY);

   VJP_LEAVE(VJP_DAC);
}

void SoundCallback(void * userdata, uint16_t * buffer, int length)
{

   /* An NTSC field is 524 halflines = 16651.56 us = 799.27 periods of
    * the 48 kHz sample clock, so the event chain lands 799 pairs and
    * the 800th does not exist yet.  Pad it by HOLDING the last pair the
    * resampler produced: a DAC starved of new data keeps transmitting
    * what it last received.
    *
    * The batch stays a fixed `length`, which is what keeps the
    * delivered rate exactly on the advertised rate (800 pairs x the
    * field rate = 48043.6 Hz NTSC, 960 x 50.08013 = 48076.9 Hz PAL).
    * It read "the advertised 48 kHz (800 x 60 fps)" until #392 made
    * both advertised numbers real; the reasoning was always the
    * fixed batch, not the round number.
    * Submitting the short count instead lost the 0.27 remainder every
    * frame -- 108 samples/sec of deficit that drained the frontend's
    * buffer until it underran, a pop every few seconds in every title.
    *
    * Holding only works because the resample cursors now persist across
    * the frame boundary: the held pair is continuous with the next
    * frame's first output.  Before that change the cursor was
    * re-anchored at the newest sample each frame, so holding merely
    * moved the step from the tail of one frame to the head of the next. */
   if (bufferIndex < length)
   {
      uint16_t holdL;
      uint16_t holdR;
      int idx;

      holdL = (bufferIndex >= 2)
         ? buffer[bufferIndex - 2] : (uint16_t)((int16_t)(*ltxd));
      holdR = (bufferIndex >= 2)
         ? buffer[bufferIndex - 1] : (uint16_t)((int16_t)(*rtxd));

      for (idx = bufferIndex; idx < length; idx += 2)
      {
         buffer[idx + 0] = holdL;
         buffer[idx + 1] = holdR;

         /* A held pair still stands for one output period of real
          * time: advance the read cursor for it too, or the deficit
          * accumulates into a gross-drift resync skip (#393).  Same
          * underrun guard as DSPSampleCallback. */
         if (((double)i2sWritePos - i2sPhase) > 1.0)
            i2sPhase += i2sRateRatio * i2sStepScale;
      }
   }

   audio_batch_cb((int16_t *)buffer, length / 2);
}

/* LTXD/RTXD/SCLK/SMODE ($F1A148/4C/50/54) */
void DACWriteByte(uint32_t offset, uint8_t data, uint32_t who)
{
   if (offset == SCLK + 3)
      DACWriteWord(SCLK + 2, (uint16_t)data, UNKNOWN);
   else if (offset == SMODE + 3)
      DACWriteWord(SMODE + 2, (uint16_t)data, UNKNOWN);
}


void DACWriteWord(uint32_t offset, uint16_t data, uint32_t who)
{
   if (offset == LTXD + 2)
   {
      /* Holding register only.  The pair is latched into the resample
       * ring by DACWordStrobe() when the I2S word clock ticks, not here
       * -- see the comment on DACWordStrobe(). */
      *ltxd = data;
   }
   else if (offset == RTXD + 2)
   {
      *rtxd = data;
   }
   else if (offset == SCLK + 2)					/* Sample rate */
   {
      *sclk = data & 0xFF;
      DACUpdateSCLKRate();
      JERRYI2SInterruptTimer = -1;
      RemoveCallback(JERRYI2SCallback);
      /* Restart the timer chain one I2S frame period out; a synchronous
       * JERRYI2SCallback() here would assert the SSI interrupt inside
       * the very instruction that wrote SCLK (see jerry.c). */
      JERRYRescheduleI2S();
   }
   else if (offset == SMODE + 2)
   {
      /* CDDA-DIAG: SMODE master/slave switches are the CD_jeri fingerprint
       * (doc 06 p.7: slave $14 = CD data flows to the I2S port). Rare. */
      if ((*smode ^ data) & 0x01)
         LOG_DBG("[CDDA] SMODE $%04X -> $%04X (%s)\n", *smode, data,
                 (data & 0x01) ? "INTERNAL/master" : "slave: CD -> I2S");
      *smode = data;
      /* The resample ratio depends on master/slave (see DACUpdateSCLKRate) */
      DACUpdateSCLKRate();
   }
}

uint8_t DACReadByte(uint32_t offset, uint32_t who)
{
   uint16_t value = DACReadWord(offset & 0xFFFFFFFE, who);

   if (offset & 0x01)
      return value & 0xFF;

   return value >> 8;
}

uint16_t DACReadWord(uint32_t offset, uint32_t who)
{
   if (offset == LRXD || offset == RRXD)
      return 0x0000;
   else if (offset == LRXD + 2)
   {
      /* CDDA-DIAG: the CD-audio mix gate opening shows up as a flood of
       * LRXD reads from the DSP ISR; near-zero reads = gate closed. */
      static uint32_t lrxdReads = 0;
      lrxdReads++;
      if (lrxdReads <= 5 || (lrxdReads % 100000) == 0)
         LOG_DBG("[CDDA] LRXD read #%u val=$%04X who=%u\n", lrxdReads, lrxd, who);
      return lrxd;
   }
   else if (offset == RRXD + 2)
      return rrxd;
   else if (offset == SCLK)
      return 0x0000;
   else if (offset == SCLK + 2)
      return sstat & 0x03;

   return 0xFFFF;
}

#include "state.h"

size_t DACStateSave(uint8_t *buf)
{
	uint8_t *start = buf;

	STATE_SAVE_VAR(buf, bufferIndex);
	STATE_SAVE_VAR(buf, numberOfSamples);
	STATE_SAVE_VAR(buf, bufferDone);
	STATE_SAVE_VAR(buf, i2sWritePos);
	STATE_SAVE_VAR(buf, i2sWriteCount);
	STATE_SAVE_VAR(buf, i2sNonZeroCount);
	STATE_SAVE_VAR(buf, i2sPhase);
	STATE_SAVE_VAR(buf, i2sRateRatio);

	/* v8: the I2S hardware registers themselves.  They live in
	 * jagMemSpace at $F1A148-$F1A157, which no STATE_SAVE_BUF covers —
	 * jerry_ram_8 is a separate array, not that window — so before this
	 * they survived a load only by accident, as whatever the previous
	 * run left behind.  See STATE_VERSION_DAC_REGISTERS. */
	STATE_SAVE_VAR(buf, *ltxd);
	STATE_SAVE_VAR(buf, *rtxd);
	STATE_SAVE_VAR(buf, *sclk);
	STATE_SAVE_VAR(buf, *smode);
	STATE_SAVE_VAR(buf, lrxd);
	STATE_SAVE_VAR(buf, rrxd);
	STATE_SAVE_VAR(buf, sstat);

	/* v9: ring contents -- the cursors above index into this data and
	 * both now survive frame boundaries (STATE_VERSION_DAC_I2S_RING). */
	STATE_SAVE_BUF(buf, i2sRingL, sizeof(i2sRingL));
	STATE_SAVE_BUF(buf, i2sRingR, sizeof(i2sRingR));

	return (size_t)(buf - start);
}

size_t DACStateLoad(const uint8_t *buf, uint32_t stateVersion)
{
	const uint8_t *start = buf;

	STATE_LOAD_VAR(buf, bufferIndex);
	STATE_LOAD_VAR(buf, numberOfSamples);
	STATE_LOAD_VAR(buf, bufferDone);
	/* The I2S resampler fields were added in
	 * STATE_VERSION_DAC_I2S_RESAMPLER; a v1 state (written only by
	 * release v2.2.0) carries none of them.  Consume nothing and fall
	 * back to the DACInit() defaults — DACPrepareFrame re-seeds
	 * writePos/writeCount, truncates the phase, and re-derives the rate
	 * ratio from the restored SMODE/SCLK registers at the top of the
	 * next retro_run, before any sample is resampled, so the defaults
	 * never reach the audio output.  Reading fields the layout does not
	 * carry would desync every module that follows. */
	if (stateVersion >= STATE_VERSION_DAC_I2S_RESAMPLER)
	{
		STATE_LOAD_VAR(buf, i2sWritePos);
		STATE_LOAD_VAR(buf, i2sWriteCount);
	}
	else
	{
		i2sWritePos = 0;
		i2sWriteCount = 0;
	}
	/* i2sNonZeroCount was added in STATE_VERSION_DAC_I2S_NONZEROCOUNT.
	 * Older states do not carry it, so consume nothing and start from the
	 * value DACPrepareFrame would establish; reading it would desync every
	 * module that follows. */
	if (stateVersion >= STATE_VERSION_DAC_I2S_NONZEROCOUNT)
		STATE_LOAD_VAR(buf, i2sNonZeroCount);
	else
		i2sNonZeroCount = 0;
	if (stateVersion >= STATE_VERSION_DAC_I2S_RESAMPLER)
	{
		STATE_LOAD_VAR(buf, i2sPhase);
		STATE_LOAD_VAR(buf, i2sRateRatio);
	}
	else
	{
		i2sPhase = 0.0;
		i2sRateRatio = 1.0;
	}

	/* The I2S hardware registers (see DACStateSave).  Layouts older than
	 * STATE_VERSION_DAC_REGISTERS carry no slot for them; consume nothing
	 * and leave them as they are, which is the behaviour those states were
	 * written against.  Reading fields the layout does not carry would
	 * desync every module that follows. */
	if (stateVersion >= STATE_VERSION_DAC_REGISTERS)
	{
		STATE_LOAD_VAR(buf, *ltxd);
		STATE_LOAD_VAR(buf, *rtxd);
		STATE_LOAD_VAR(buf, *sclk);
		STATE_LOAD_VAR(buf, *smode);
		STATE_LOAD_VAR(buf, lrxd);
		STATE_LOAD_VAR(buf, rrxd);
		STATE_LOAD_VAR(buf, sstat);
		/* The resample ratio is derived from SCLK/SMODE, so re-derive it
		 * now that they hold the restored values rather than the ones the
		 * previous run left behind. */
		DACUpdateSCLKRate();
	}

	if (stateVersion >= STATE_VERSION_DAC_I2S_RING)
	{
		STATE_LOAD_BUF(buf, i2sRingL, sizeof(i2sRingL));
		STATE_LOAD_BUF(buf, i2sRingR, sizeof(i2sRingR));
	}
	else
	{
		/* Older states carry no ring data: zero it.  The loaded cursor
		 * pair is left alone -- a v8 state's phase and writePos are
		 * mutually consistent (both were frame-local), and the read
		 * cursor's gross-drift resync covers anything pathological on
		 * the first sample after load. */
		memset(i2sRingL, 0, sizeof(i2sRingL));
		memset(i2sRingR, 0, sizeof(i2sRingR));
	}

	return (size_t)(buf - start);
}

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

#include <string.h>
#include "cdrom.h"
#include "dsp.h"
#include "event.h"
#include "jerry.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"
#include "settings.h"

#include <libretro.h>

extern retro_audio_sample_batch_t audio_batch_cb;

#define BUFFER_SIZE		0x10000	/* Make the DAC buffers 64K x 16 bits */
#define DAC_AUDIO_RATE		48000	/* Set the audio rate to 48 KHz */

/* Ring buffer for I2S samples produced by the DSP at hardware rate */
#define I2S_RING_SIZE		16384	/* Power of 2, must be > max samples/frame (PAL SCLK=0 ~8311) */
#define I2S_RING_MASK		(I2S_RING_SIZE - 1)

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
static double i2sPhase = 0.0;		/* fractional read position */
static double i2sRateRatio = 1.0;	/* i2s_rate / 48000.0 */

/* Private function prototypes */

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
   i2sPhase = 0.0;
   i2sRateRatio = 1.0;
   memset(i2sRingL, 0, sizeof(i2sRingL));
   memset(i2sRingR, 0, sizeof(i2sRingR));
}

void DACDone(void)
{
}

/* Called by JTRM-accurate I2S rate to update the rate ratio when SCLK changes */
void DACUpdateSCLKRate(void)
{
   uint32_t sclk_val;
   double i2s_rate;
   double sys_clock;

   sclk_val = (uint32_t)(*sclk);
   sys_clock = vjs.hardwareTypeNTSC ? 26590906.0 : 26593900.0;
   /* sample_rate = system_clock / (64 * (SCLK + 1)) */
   i2s_rate = sys_clock / (64.0 * (sclk_val + 1));
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
}

void DSPSampleCallback(void)
{
   int16_t outL, outR;
   uint32_t idx0, idx1;
   double frac;
   int32_t s0L, s1L, s0R, s1R;

   /* Guard: hold current register value if ring is empty (e.g. after reset) */
   if (i2sWriteCount < 2)
   {
      outL = (int16_t)(*ltxd);
      outR = (int16_t)(*rtxd);
   }
   else
   {
      /* Linear interpolation between ring buffer samples.
       * i2sPhase is our fractional position in the I2S sample stream.
       * We advance by i2sRateRatio for each 48 kHz output sample. */
      idx0 = (uint32_t)i2sPhase;
      frac = i2sPhase - (double)idx0;
      idx1 = idx0 + 1;

      /* Clamp indices to available data */
      if (idx0 >= i2sWriteCount)
         idx0 = i2sWriteCount - 1;
      if (idx1 >= i2sWriteCount)
         idx1 = i2sWriteCount - 1;

      s0L = (int32_t)i2sRingL[idx0 & I2S_RING_MASK];
      s1L = (int32_t)i2sRingL[idx1 & I2S_RING_MASK];
      s0R = (int32_t)i2sRingR[idx0 & I2S_RING_MASK];
      s1R = (int32_t)i2sRingR[idx1 & I2S_RING_MASK];

      outL = (int16_t)(s0L + (int32_t)((double)(s1L - s0L) * frac));
      outR = (int16_t)(s0R + (int32_t)((double)(s1R - s0R) * frac));

      /* Advance phase by the rate ratio */
      i2sPhase += i2sRateRatio;
   }

   sampleBuffer[bufferIndex + 0] = (uint16_t)outL;
   sampleBuffer[bufferIndex + 1] = (uint16_t)outR;
   bufferIndex += 2;

   if (bufferIndex >= numberOfSamples)
   {
      bufferDone = true;
      return;
   }

   SetCallbackTime(DSPSampleCallback, 1000000.0 / (double)DAC_AUDIO_RATE, EVENT_JERRY);
}

void DACPrepareFrame(int length)
{
   RemoveCallback(DSPSampleCallback);
   bufferIndex = 0;
   numberOfSamples = length;
   bufferDone = false;

   /* Seed ring with current DAC register values so interpolation has valid
    * endpoints before the first real DSP write arrives.  Two copies give
    * both idx0 and idx1 valid data for the interpolator. */
   i2sRingL[0] = (int16_t)(*ltxd);
   i2sRingR[0] = (int16_t)(*rtxd);
   i2sRingL[1] = (int16_t)(*ltxd);
   i2sRingR[1] = (int16_t)(*rtxd);
   i2sWritePos = 2;
   i2sWriteCount = 2;
   i2sPhase = i2sPhase - (double)(uint32_t)i2sPhase;

   /* Refresh rate ratio in case SCLK was written between frames */
   DACUpdateSCLKRate();

   SetCallbackTime(DSPSampleCallback, 1000000.0 / (double)DAC_AUDIO_RATE, EVENT_JERRY);
}

void SoundCallback(void * userdata, uint16_t * buffer, int length)
{
   int idx;

   RemoveCallback(DSPSampleCallback);

   if (bufferIndex < length)
   {
      for (idx = bufferIndex; idx < length; idx += 2)
      {
         buffer[idx + 0] = (uint16_t)((int16_t)(*ltxd));
         buffer[idx + 1] = (uint16_t)((int16_t)(*rtxd));
      }
   }

   audio_batch_cb((int16_t *)buffer, length / 2);
}

/* LTXD/RTXD/SCLK/SMODE ($F1A148/4C/50/54) */
void DACWriteByte(uint32_t offset, uint8_t data, uint32_t who)
{
   if (offset == SCLK + 3)
      DACWriteWord(offset - 3, (uint16_t)data, UNKNOWN);
}


void DACWriteWord(uint32_t offset, uint16_t data, uint32_t who)
{
   if (offset == LTXD + 2)
   {
      *ltxd = data;
      /* Capture left sample; pair completes when RTXD is written */
   }
   else if (offset == RTXD + 2)
   {
      *rtxd = data;
      /* Both channels written — capture the stereo pair */
      DACCaptureSample((int16_t)(*ltxd), (int16_t)data);
   }
   else if (offset == SCLK + 2)					/* Sample rate */
   {
      *sclk = data & 0xFF;
      DACUpdateSCLKRate();
      JERRYI2SInterruptTimer = -1;
      RemoveCallback(JERRYI2SCallback);
      JERRYI2SCallback();
   }
   else if (offset == SMODE + 2)
   {
      *smode = data;
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
      return lrxd;
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
	STATE_SAVE_VAR(buf, i2sPhase);
	STATE_SAVE_VAR(buf, i2sRateRatio);

	return (size_t)(buf - start);
}

size_t DACStateLoad(const uint8_t *buf)
{
	const uint8_t *start = buf;

	STATE_LOAD_VAR(buf, bufferIndex);
	STATE_LOAD_VAR(buf, numberOfSamples);
	STATE_LOAD_VAR(buf, bufferDone);
	STATE_LOAD_VAR(buf, i2sWritePos);
	STATE_LOAD_VAR(buf, i2sWriteCount);
	STATE_LOAD_VAR(buf, i2sPhase);
	STATE_LOAD_VAR(buf, i2sRateRatio);

	return (size_t)(buf - start);
}

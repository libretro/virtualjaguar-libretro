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

/* The libretro audio path samples LTXD/RTXD at 48 kHz into sampleBuffer.
 * JERRYI2SCallback separately models DSP SSI interrupt timing from SCLK/SMODE;
 * changing either path affects both audio output and DSP-side synchronization. */

#include "dac.h"

#include "cdrom.h"
#include "dsp.h"
#include "event.h"
#include "jerry.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"
#include "settings.h"

#include <libretro.h>

extern retro_audio_sample_batch_t audio_batch_cb;

#define BUFFER_SIZE		0x10000	// Make the DAC buffers 64K x 16 bits
#define DAC_AUDIO_RATE		48000	// Set the audio rate to 48 KHz

// Jaguar memory locations

#define LTXD			0xF1A148
#define RTXD			0xF1A14C
#define LRXD			0xF1A148
#define RRXD			0xF1A14C
#define SCLK			0xF1A150
#define SMODE			0xF1A154

// Global variables
uint16_t * sampleBuffer;
static int bufferIndex = 0;
static int numberOfSamples = 0;
static bool bufferDone = false;

// Private function prototypes

void DACInit(void)
{
   DACReset();

   *ltxd = 0;
   lrxd  = 0;
   *sclk = 19;									// Default is roughly 22 KHz
}


// Reset the sound buffer FIFOs
void DACReset(void)
{
   *ltxd = 0;
   lrxd  = 0;
   sstat = 0;
}

void DACDone(void)
{
}

void DSPSampleCallback(void)
{
   sampleBuffer[bufferIndex + 0] = *ltxd;
   sampleBuffer[bufferIndex + 1] = *rtxd;
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
         buffer[idx + 0] = *ltxd;
         buffer[idx + 1] = *rtxd;
      }
   }

   audio_batch_cb((int16_t *)buffer, length / 2);
}

// LTXD/RTXD/SCLK/SMODE ($F1A148/4C/50/54)
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
   }
   else if (offset == RTXD + 2)
      *rtxd = data;
   else if (offset == SCLK + 2)					// Sample rate
   {
      *sclk = data & 0xFF;
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

	return (size_t)(buf - start);
}

size_t DACStateLoad(const uint8_t *buf)
{
	const uint8_t *start = buf;

	STATE_LOAD_VAR(buf, bufferIndex);
	STATE_LOAD_VAR(buf, numberOfSamples);
	STATE_LOAD_VAR(buf, bufferDone);

	return (size_t)(buf - start);
}

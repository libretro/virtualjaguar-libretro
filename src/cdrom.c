//
// CD handler
//
// Originally by David Raingeard
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Extensive rewrites/cleanups/fixes by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
//

#include "cdrom.h"

#include <stdio.h>
#include <string.h>									// For memset, etc.
#include "cdintf.h"									// System agnostic CD interface functions
#include "log.h"
#include "gpu.h"
#include "dsp.h"
#include "jaguar.h"
#include "jerry.h"
#include "m68000/m68kinterface.h"

// HLE (High-Level Emulation) CD data transfer: bypass the GPU ISR FIFO loop
// and copy sector data directly from cdBuf to main RAM. The GPU ISR's FIFO
// handler has two problems: (1) the GPU main loop drains the FIFO before the
// ISR can read it, and (2) the ISR data area at $F03124/$F03128 is never
// initialized by the BIOS. This HLE path copies data in C and updates the
// GPU RAM buffer pointer at $F03118 so the boot stub sees progress.
// Set to 0 to use the original GPU ISR path (for debugging).
#define CD_DATA_TRANSFER_HLE 0

// How many bytes to transfer per BUTCHExec call in HLE mode.
// One sector of CD-ROM user data = 2048 bytes. Raw sector = 2352 bytes.
// Transfer multiple sectors per call to avoid needing thousands of calls.
#define HLE_BYTES_PER_TICK   2352

/* CD debug tracing -- set to 1 to enable verbose logging */
#define CD_DEBUG 0
#if CD_DEBUG
#define CD_LOG(...) LOG_DBG("[CD] " __VA_ARGS__)
#else
#define CD_LOG(...) ((void)0)
#endif

// Timing constants for seek and FIFO simulation (in half-line ticks, ~31.8μs each)
// Per MiSTer FPGA: seek has a multi-tier delay (30-315ms), FIFO fills at I2S rate.
// These values are shortened for software emulation but preserve the required ordering:
// seek response MUST arrive via interrupt AFTER DSA_tx returns, and FIFO MUST NOT
// be ready during the DSARX phase (or the 68K handler sends STOP).
// The BIOS polls BUTCH+2 once after $12xx (no response expected yet), then sends
// STOP. On real hardware the seek continues internally despite STOP — the drive
// completes the seek and queues the $0100 response 30-300ms later. The BIOS's
// main loop (or DSP) detects the seek completion and initiates data transfer.
// STOP must NOT cancel the seek delay. Value chosen to be short enough to complete
// within a few frames but long enough to occur AFTER the BIOS's single poll.
#define SEEK_DELAY_TICKS     100  // ~3.2ms — completes after BIOS poll + STOP
#define FIFO_FILL_TICKS      8    // ~254μs before FIFO half-full after play starts
#define FIFO_REFILL_TICKS    5    // ~159μs to refill FIFO after GPU ISR drains it
#define FIFO_DRAIN_READS     16   // 16 word-reads = 8 GPU longword loads = 32 bytes

/*
   BUTCH     equ  $DFFF00		; base of Butch=interrupt control register, R/W
   DSCNTRL   equ  BUTCH+4		; DSA control register, R/W
   DS_DATA   equ  BUTCH+$A		; DSA TX/RX data, R/W
   I2CNTRL   equ  BUTCH+$10	; i2s bus control register, R/W
   SBCNTRL   equ  BUTCH+$14	; CD subcode control register, R/W
   SUBDATA   equ  BUTCH+$18	; Subcode data register A
   SUBDATB   equ  BUTCH+$1C	; Subcode data register B
   SB_TIME   equ  BUTCH+$20	; Subcode time and compare enable (D24)
   FIFO_DATA equ  BUTCH+$24	; i2s FIFO data
   I2SDAT1   equ  BUTCH+$24	; i2s FIFO data
   I2SDAT2   equ  BUTCH+$28	; i2s FIFO data
   equ  BUTCH+$2C	; CD EEPROM interface

   ;
   ; Butch's hardware registers
   ;
   ;BUTCH     equ  $DFFF00		;base of Butch=interrupt control register, R/W
   ;
   ;  When written (Long):
   ;
   ;  bit0 - set to enable interrupts
   ;  bit1 - enable CD data FIFO half full interrupt
   ;  bit2 - enable CD subcode frame-time interrupt (@ 2x spped = 7ms.)
   ;  bit3 - enable pre-set subcode time-match found interrupt
   ;  bit4 - CD module command transmit buffer empty interrupt
   ;  bit5 - CD module command receive buffer full
   ;  bit6 - CIRC failure interrupt
   ;
   ;  bit7-31  reserved, set to 0
   ;
   ;  When read (Long):
   ;
   ;  bit0-8 reserved
   ;
   ;  bit9  - CD data FIFO half-full flag pending
   ;  bit10 - Frame pending
   ;  bit11 - Subcode data pending
   ;  bit12 - Command to CD drive pending (trans buffer empty if 1)
   ;  bit13 - Response from CD drive pending (rec buffer full if 1)
   ;  bit14 - CD uncorrectable data error pending
   ;
   ;   Offsets from BUTCH
   ;
   O_DSCNTRL   equ  4		; DSA control register, R/W
   O_DS_DATA   equ  $A		; DSA TX/RX data, R/W
   ;
   O_I2CNTRL   equ  $10		; i2s bus control register, R/W
   ;
   ;  When read:
   ;
   ;  b0 - I2S data from drive is ON if 1
   ;  b1 - I2S path to Jerry is ON if 1
   ;  b2 - reserved
   ;  b3 - host bus width is 16 if 1, else 32
   ;  b4 - FIFO state is not empty if 1
   ;
   O_SBCNTRL   equ  $14		; CD subcode control register, R/W
   O_SUBDATA   equ  $18		; Subcode data register A
   O_SUBDATB   equ  $1C		; Subcode data register B
   O_SB_TIME   equ  $20		; Subcode time and compare enable (D24)
   O_FIFODAT   equ  $24		; i2s FIFO data
   O_I2SDAT2   equ  $28		; i2s FIFO data (old)
   */

/*
   Commands sent through DS_DATA:

   $01nn - ? Play track nn ? Seek to track nn ?
   $0200 - Stop CD
   $03nn - Read session nn TOC (short)
   $0400 - Pause CD
   $0500 - Unpause CD
   $10nn - Goto (min?)
   $11nn - Goto (sec?)
   $12nn - Goto (frm?)
   $14nn - Read session nn TOC (full)
   $15nn - Set CD mode
   $18nn - Spin up CD to session nn
   $5000 - ?
   $5100 - Mute CD (audio mode only)
   $51FF - Unmute CD (audio mode only)
   $5400 - Read # of sessions on CD
   $70nn - Set oversampling mode

   Commands send through serial bus:

   $100 - ? Acknowledge ? (Erase/Write disable)
   $130 - ? (Seems to always prefix the $14n commands) (Erase/Write enable)
   $140 - Returns ACK (1) (Write to NVRAM?) (Write selected register)
   $141 - Returns ACK (1)
   $142 - Returns ACK (1)
   $143 - Returns ACK (1)
   $144 - Returns ACK (1)
   $145 - Returns ACK (1)
   $180 - Returns 16-bit value (NVRAM?) (read from EEPROM)
   $181 - Returns 16-bit value
   $182 - Returns 16-bit value
   $183 - Returns 16-bit value
   $184 - Returns 16-bit value
   $185 - Returns 16-bit value

   ;  The BUTCH interface for the CD-ROM module is a long-word register,
   ;   where only the least signifigant 4 bits are used
   ;
   eeprom	equ	$DFFF2c			;interface to CD-eeprom
   ;
   ;  bit3 - busy if 0 after write cmd, or Data In after read cmd 
   ;  bit2 - Data Out
   ;  bit1 - clock
   ;  bit0 - Chip Select (CS)
   ;
   ;
   ;   Commands specific to the National Semiconductor NM93C14
   ;
   ;
   ;  9-bit commands..
   ;			 876543210
   eREAD	equ	%110000000		;read from EEPROM
   eEWEN	equ	%100110000		;Erase/write Enable
   eERASE	equ	%111000000		;Erase selected register
   eWRITE	equ	%101000000		;Write selected register
   eERAL	equ	%100100000		;Erase all registers
   eWRAL	equ	%100010000		;Writes all registers
   eEWDS	equ	%100000000		;Erase/Write disable (default)

   So... are there $40 words of memory? 128 bytes?

*/

// External variables
extern uint8_t jerry_ram_8[];
extern uint8_t * jaguarMainRAM;

// Private function prototypes

static void CDROMBusWrite(uint16_t);
static uint16_t CDROMBusRead(void);

#define BUTCH		0x00				// base of Butch == interrupt control register, R/W
#define DSCNTRL 	(BUTCH + 0x04)		// DSA control register, R/W
#define DS_DATA		(BUTCH + 0x0A)		// DSA TX/RX data, R/W
#define I2CNTRL		(BUTCH + 0x10)		// i2s bus control register, R/W
#define SBCNTRL		(BUTCH + 0x14)		// CD subcode control register, R/W
#define SUBDATA		(BUTCH + 0x18)		// Subcode data register A
#define SUBDATB		(BUTCH + 0x1C)		// Subcode data register B
#define SB_TIME		(BUTCH + 0x20)		// Subcode time and compare enable (D24)
#define FIFO_DATA	(BUTCH + 0x24)		// i2s FIFO data
#define I2SDAT2		(BUTCH + 0x28)		// i2s FIFO data (old)
#define UNKNOWN		(BUTCH + 0x2C)		// Seems to be some sort of I2S interface

const char * BReg[12] = { "BUTCH", "DSCNTRL", "DS_DATA", "???", "I2CNTRL",
   "SBCNTRL", "SUBDATA", "SUBDATB", "SB_TIME", "FIFO_DATA", "I2SDAT2",
   "UNKNOWN" };

static uint8_t cdRam[0x100];
static uint16_t cdCmd = 0, cdPtr = 0;
static bool haveCDGoodness;
static uint32_t min, sec, frm, block;
static uint8_t cdBuf[2352 + 96];
static uint32_t cdBufPtr = 2352;

// NM93C14 EEPROM: 64 x 16-bit words (128 bytes)
static uint16_t cdrom_eeprom_ram[64];

// DSA response tracking: bit 13 (RX full) should only be set
// when we actually have a response ready after a DS_DATA write.
static bool dsaResponseReady = false;

// Tracks whether the current response is multi-word (TOC) or single-word.
// Used by DSCNTRL read to clear bit 13 for single-word responses (MiSTer behavior).
static bool isMultiWordResponse = false;

// BUTCH status bit tracking (per MiSTer FPGA reference):
// bit 12 (TX buffer empty): set when DS_DATA is written, cleared when DSCNTRL is read
// This transition is critical — the GPU CD code checks for bit 12 cleared after
// reading DSCNTRL before proceeding to read DS_DATA.
static bool txBufferEmpty = true;

// CD playback state — controls bits 10/11 in BUTCH status and FIFO filling
static bool cdPlaying = false;

// Seek delay: in MiSTer FPGA, seek is NOT instantaneous. The response ($0100)
// and FIFO data are only available after a delay. The GPU ISR polls BUTCH and
// expects bit 13 to be 0 while the seek is in progress. If we set it immediately,
// the ISR sees an unexpected state and sends STOP ($0200).
static int32_t seekDelay = 0;

// FIFO state for Butch data delivery
// On real hardware, the FIFO fills asynchronously via I2S after seeking.
// It is NOT instantly available at seek completion — the BIOS processes
// the seek response ($0100) first, then data arrives.
static bool fifoDataReady = false;

// FIFO drain/refill tracking: simulates the 16-deep hardware FIFO.
// The GPU ISR reads 8 longwords (16 word-reads) per invocation, draining
// the FIFO. After drain, it refills at I2S rate before the next interrupt.
static uint32_t fifoReadCount = 0;
static int32_t fifoFillDelay = 0;

// DSA response queue: on real hardware, the DSA serial bus has separate
// TX and RX buffers. Sending a new command via TX does NOT discard an
// unread response in RX. This is critical for the seek+stop sequence:
// the BIOS sends $12xx (seek), then $0200 (STOP) before reading the seek
// response. Without a queue, STOP overwrites cdCmd and the seek response
// ($0100) is lost, causing the formatter to never start data streaming.
#define DSA_QUEUE_SIZE 4
static uint16_t dsaQueue[DSA_QUEUE_SIZE];
static uint32_t dsaQueueHead = 0;
static uint32_t dsaQueueTail = 0;
static uint32_t dsaQueueCount = 0;

static void DSAQueuePush(uint16_t response)
{
   if (dsaQueueCount < DSA_QUEUE_SIZE)
   {
      dsaQueue[dsaQueueTail] = response;
      dsaQueueTail = (dsaQueueTail + 1) % DSA_QUEUE_SIZE;
      dsaQueueCount++;
      dsaResponseReady = true;
      CD_LOG("DSA queue push: $%04X (count=%u)\n", response, dsaQueueCount);
   }
}

static uint16_t DSAQueuePop(void)
{
   if (dsaQueueCount > 0)
   {
      uint16_t response = dsaQueue[dsaQueueHead];
      dsaQueueHead = (dsaQueueHead + 1) % DSA_QUEUE_SIZE;
      dsaQueueCount--;
      if (dsaQueueCount == 0)
      {
         dsaResponseReady = false;
      }
      CD_LOG("DSA queue pop: $%04X (remaining=%u)\n", response, dsaQueueCount);
      return response;
   }
   return 0x0400;  // Error — empty queue
}


void CDROMInit(void)
{
   haveCDGoodness = CDIntfInit();
   CD_LOG("CDROMInit: haveCDGoodness=%d\n", haveCDGoodness);

   if (haveCDGoodness)
   {
      uint32_t i, numSess = CDIntfGetNumSessions();
      CD_LOG("Disc: %u sessions\n", numSess);
      for (i = 0; i < numSess; i++)
      {
         CD_LOG("  Session %u: firstTrack=%u lastTrack=%u leadout=%02u:%02u:%02u\n", i,
                CDIntfGetSessionInfo(i, 0), CDIntfGetSessionInfo(i, 1),
                CDIntfGetSessionInfo(i, 2), CDIntfGetSessionInfo(i, 3),
                CDIntfGetSessionInfo(i, 4));
      }
   }
}

void CDROMReset(void)
{
   memset(cdRam, 0x00, 0x100);
   cdCmd = 0;
   cdPtr = 0;
   min = sec = frm = block = 0;
   cdBufPtr = 2352;
   fifoDataReady = false;
   dsaResponseReady = false;
   isMultiWordResponse = false;
   txBufferEmpty = true;
   cdPlaying = false;
   seekDelay = 0;
   fifoReadCount = 0;
   fifoFillDelay = 0;
   dsaQueueHead = 0;
   dsaQueueTail = 0;
   dsaQueueCount = 0;

   // Initialize EEPROM to 0xFFFF (blank/erased state), then set
   // factory default values.  The Jaguar CD BIOS reads specific EEPROM
   // addresses during boot and loops if they don't contain expected
   // values (a real CD unit's NM93C14 is factory-programmed).
   memset(cdrom_eeprom_ram, 0xFF, sizeof(cdrom_eeprom_ram));
   cdrom_eeprom_ram[0] = 0x0024;
   cdrom_eeprom_ram[1] = 0x0004;
   cdrom_eeprom_ram[2] = 0x0071;
   cdrom_eeprom_ram[3] = 0xFF67;
   cdrom_eeprom_ram[4] = 0x892F;
   cdrom_eeprom_ram[5] = 0x8000;
}

void CDROMDone(void)
{
   CDIntfDone();
}


//
// This approach is probably wrong, but let's do it for now.
// What's needed is a complete overhaul of the interrupt system so that
// interrupts are handled as they're generated--instead of the current
// scheme where they're handled on scanline boundaries.
//
void BUTCHExec(uint32_t cycles)
{
   if (!haveCDGoodness)
      return;

   // Seek delay countdown — runs independently of interrupt enable and STOP state.
   // On real hardware, STOP halts playback but does NOT cancel an in-progress seek.
   // The drive continues seeking and delivers $0100 when it reaches the target.
   // This is critical for the boot sequence: BIOS sends seek+STOP, then waits for
   // the seek response to arrive in the main loop.
   if (seekDelay > 0)
   {
      seekDelay--;
      if (seekDelay == 0)
      {
         // Seek complete: queue the response and start data output.
         // On real hardware, the drive starts outputting I2S data immediately
         // upon reaching the target position. Even if STOP was sent during the
         // seek, the drive completes the seek and begins data output briefly —
         // the FIFO fills with the first sector data. The BIOS relies on this
         // data being available for the DSP to read via the I2S/SSI path.
         DSAQueuePush(0x0100);
         cdPlaying = true;
         fifoDataReady = true;
         fifoReadCount = 0;

         CD_LOG("BUTCHExec: seek complete block=%u (MSF %02u:%02u:%02u) — queued $0100, FIFO+playback active\n",
                block, min, sec, frm);
      }
   }

   // FIFO refill countdown — simulates I2S filling the 16-deep FIFO.
   // After the GPU ISR drains it (16 word-reads), we wait before setting
   // half-full again. Also handles initial fill after play starts.
   if (fifoFillDelay > 0)
   {
      fifoFillDelay--;
      if (fifoFillDelay == 0 && cdPlaying)
      {
         fifoDataReady = true;
         fifoReadCount = 0;
         CD_LOG("BUTCHExec: FIFO half-full — ready for GPU ISR\n");
      }
   }

#if CD_DATA_TRANSFER_HLE
   // HLE CD data transfer: when FIFO is ready and CD is playing, copy sector
   // data directly to main RAM and update the GPU buffer pointer at $F03118.
   // This bypasses the GPU ISR FIFO handler entirely.
   if (fifoDataReady && cdPlaying)
   {
      uint32_t destPtr = GPUReadLong(0xF03118, UNKNOWN);
      uint32_t destEnd = GPUReadLong(0xF0311C, UNKNOWN);

      if (destPtr > 0 && destEnd > destPtr && destEnd < 0x200000)
      {
         uint32_t remaining = destEnd - destPtr;
         uint32_t toTransfer = (remaining > HLE_BYTES_PER_TICK) ? HLE_BYTES_PER_TICK : remaining;
         toTransfer &= ~1;  // Word-align for I2S swap

         for (uint32_t i = 0; i < toTransfer; i += 2)
         {
            if (cdBufPtr >= 2352)
            {
               block++;
               CDIntfReadBlock(block, cdBuf);
               cdBufPtr = 0;
            }
            // Word-swap: Jaguar I2S path swaps bytes within each 16-bit word
            uint8_t b0 = cdBuf[cdBufPtr++];
            uint8_t b1 = (cdBufPtr < 2352) ? cdBuf[cdBufPtr++] : 0;
            jaguarMainRAM[(destPtr + i) & 0x1FFFFF] = b1;
            if (i + 1 < toTransfer)
               jaguarMainRAM[(destPtr + i + 1) & 0x1FFFFF] = b0;
         }

         destPtr += toTransfer;
         GPUWriteLong(0xF03118, destPtr, UNKNOWN);

         static uint32_t hleTransferCount = 0;
         hleTransferCount++;
         if (hleTransferCount <= 5 || (hleTransferCount % 1000) == 0)
            CD_LOG("HLE transfer #%u: %u bytes → $%06X (end=$%06X, block=%u)\n",
                   hleTransferCount, toTransfer, destPtr, destEnd, block);

         if (destPtr >= destEnd)
         {
            LOG_DBG("[CD-HLE] Transfer complete: dest=$%06X, end=$%06X, block=%u\n",
                    destPtr, destEnd, block);
            cdPlaying = false;
            fifoDataReady = false;
         }
      }
   }
#endif

   uint32_t butchWrite = GET32(cdRam, BUTCH);

   if (!(butchWrite & 0x01))       // Global interrupt enable not set
      return;

   // Generate interrupts through JERRY external interrupt -> 68K INT2.
   // Per MiSTer FPGA: eint = global_en && (fifo_int || rbuf_int || ...)
   // where fifo_int = bit1 && bit9, rbuf_int = bit5 && bit13.
   // Only assert on rising edge to prevent infinite ISR re-entry.
   {
      static bool prevIRQState = false;
      bool shouldIRQ = false;

      if ((butchWrite & 0x02) && fifoDataReady)              // FIFO half-full
         shouldIRQ = true;
      if ((butchWrite & 0x20) && dsaResponseReady)           // DSARX (response ready)
         shouldIRQ = true;

      if (shouldIRQ && !prevIRQState)
      {
         JERRYSetPendingIRQ(IRQ2_EXTERNAL);
         if (JERRYIRQEnabled(IRQ2_EXTERNAL))
            m68k_set_irq(2);

         GPUSetIRQLine(GPUIRQ_DSP, ASSERT_LINE);
      }
      prevIRQState = shouldIRQ;
   }
}


// CD-ROM memory access functions

uint8_t CDROMReadByte(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   return cdRam[offset & 0xFF];
}

static uint8_t trackNum = 1, minTrack, maxTrack;

uint16_t CDROMReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   uint16_t data = 0x0000;

   offset &= 0xFF;

   if (offset == BUTCH)
      data = GET16(cdRam, BUTCH);    // Top word: control bits (cdbios, cdreset, etc.)
   else if (offset == BUTCH + 2)
   {
      // Read-side BUTCH status register (bits 9-14) merged with
      // write-side enable bits (bits 0-6). Per MiSTer FPGA, the full
      // register is always returned on reads — enables are visible alongside status.
      data = GET16(cdRam, BUTCH + 2) & 0x007F;  // bits 0-6 always readable

      if (haveCDGoodness)
      {
         if (txBufferEmpty)
            data |= (1 << 12);
         if (cdPlaying)
         {
            data |= (1 << 10);
            data |= (1 << 11);
         }
         if (dsaResponseReady)
            data |= (1 << 13);
         if (fifoDataReady)
            data |= (1 << 9);
      }
   }
   else if (offset == DSCNTRL || offset == DSCNTRL + 2)
   {
      // DSCNTRL read: returns stored value, clears bit 12 (TX buffer empty).
      // Per MiSTer FPGA (butch.v line 1522-1525), it also clears bit 13 for
      // single-word responses. However, in our software emulation, the GPU ISR
      // reads DSCNTRL before checking BUTCH — clearing bit 13 here would destroy
      // the response before the ISR sees it. Instead, we clear bit 13 when
      // DS_DATA is actually read (see DS_DATA handler below).
      data = GET16(cdRam, offset);
      txBufferEmpty = false;  // Clear bit 12 — GPU sees this transition
   }
   else if (offset == I2CNTRL || offset == I2CNTRL + 2)
   {
      // I2S bus control register readback — return stored value with dynamic bit 4.
      // Per MiSTer FPGA: bit 4 (FIFO not empty) is hardware-driven, not software-set.
      data = GET16(cdRam, offset);
      if (haveCDGoodness && fifoDataReady)
         data |= (1 << 4);              // FIFO not empty (dynamic)
   }
   else if (offset == DS_DATA && haveCDGoodness)
   {
      // DSA response queue takes priority — this ensures the seek response
      // ($0100) is delivered before a later STOP response ($0200) even when
      // the BIOS sends seek+stop without reading between them.
      if (dsaQueueCount > 0)
      {
         data = DSAQueuePop();
         // Apply side effects based on the queued response
         if (data == 0x0100)
         {
            // Seek complete — playback and FIFO were already activated
            // at seek completion in BUTCHExec. Re-assert in case STOP
            // cleared them between seek completion and this read.
            cdPlaying = true;
            if (!fifoDataReady)
            {
               fifoDataReady = true;
               fifoReadCount = 0;
            }
            CD_LOG("Queued seek response $0100 consumed\n");
         }
         else if (data == 0x0200)
         {
            // STOP response consumed — stop was already processed on write
            CD_LOG("Queued STOP response $0200 consumed\n");
         }
         // dsaResponseReady is managed by DSAQueuePop
      }
      else if ((cdCmd & 0xFF00) == 0x0100)				// Play Title
      {
         data = 0x0100 | (cdCmd & 0xFF);			// Echo: $01nn -> $01nn (Found)
         cdPlaying = true;
         fifoDataReady = true;
         CD_LOG("Play Title response consumed — playback and FIFO now active\n");
      }
      else if ((cdCmd & 0xFF00) == 0x0200)			// Stop CD
      {
         data = 0x0200;								// Stopped
      }
      else if ((cdCmd & 0xFF00) == 0x0300)		// Read session TOC (5 words)
      {

         /*
TOC: [Sess] [adrCtl] [?] [point] [?] [?] [?] [?] [pmin] [psec] [pframe]
TOC: 1 10 00 a0 00:00:00 00 01:00:00
TOC: 1 10 00 a1 00:00:00 00 01:00:00
TOC: 1 10 00 a2 00:00:00 00 03:42:42
TOC: 1 10 00  1 00:00:00 00 00:02:00   <-- Track #1
TOC: 1 50 00 b0 06:12:42 02 79:59:74
TOC: 1 50 00 c0 128:00:32 00 97:18:06
TOC: 2 10 00 a0 00:00:00 00 02:00:00
TOC: 2 10 00 a1 00:00:00 00 11:00:00
TOC: 2 10 00 a2 00:00:00 00 54:32:18
TOC: 2 10 00  2 00:00:00 00 06:14:42   <-- Track #2
TOC: 2 10 00  3 00:00:00 00 06:24:42   <-- Track #3
TOC: 2 10 00  4 00:00:00 00 17:42:00   <-- Track #4
TOC: 2 10 00  5 00:00:00 00 22:26:15   <-- Track #5
TOC: 2 10 00  6 00:00:00 00 29:50:16   <-- Track #6
TOC: 2 10 00  7 00:00:00 00 36:01:49   <-- Track #7
TOC: 2 10 00  8 00:00:00 00 40:37:59   <-- Track #8
TOC: 2 10 00  9 00:00:00 00 45:13:70   <-- Track #9
TOC: 2 10 00  a 00:00:00 00 49:50:06   <-- Track #10
TOC: 2 10 00  b 00:00:00 00 54:26:17   <-- Track #11
*/

         //Should do something like so:
         //			data = GetSessionInfo(cdCmd & 0xFF, cdPtr);
         data = CDIntfGetSessionInfo(cdCmd & 0xFF, cdPtr);
         CD_LOG("TOC-03: sess_param=%u cdPtr=%u data=$%04X\n",
                cdCmd & 0xFF, cdPtr, data);
         if (data == 0xFF)	// Failed...
            data = 0x0400;
         else
            data |= (0x20 | cdPtr++) << 8;
      }
      // Seek: only $12xx (Goto Frame) generates a response ($0100 = Found).
      // $10xx/$11xx (Goto Min/Sec) do NOT generate responses on their own.
      // This path is the fallback for seek responses NOT delivered via the queue
      // (e.g. if the BIOS reads DS_DATA while cdCmd is still $12xx and no STOP
      // was interleaved). Normally the queue path above handles seek responses.
      else if ((cdCmd & 0xFF00) == 0x1200)
      {
         data = 0x0100;	// Found (seek complete)
         cdPlaying = true;
         fifoDataReady = true;
         fifoReadCount = 0;
         CD_LOG("Seek response $0100 consumed (direct) — cdPlaying=true\n");
      }
      else if ((cdCmd & 0xFF00) == 0x1400)		// Read "full" session TOC
      {
         //Need to be a bit more tricky here, since it's reading the "session" TOC instead of the
         //full TOC--so we need to check for the min/max tracks for each session here... [DONE]

         if (trackNum > maxTrack)
            data = 0x400;
         else
         {
            // Wire format for $14xx response (5 words per track):
            //   $60nn = track number
            //   $61nn = track number (repeated, per original VJ code)
            //   $62nn = absolute minutes (MSF)
            //   $63nn = absolute seconds (MSF)
            //   $64nn = absolute frames (MSF)
            if (cdPtr < 0x62)
               data = (cdPtr << 8) | trackNum;
            else if (cdPtr < 0x65)
               data = (cdPtr << 8) | CDIntfGetTrackInfo(trackNum, (cdPtr - 2) & 0x0F);

            CD_LOG("TOC-14: sess=%u trk=%u cdPtr=$%02X data=$%04X\n",
                   cdCmd & 0xFF, trackNum, cdPtr, data);

            cdPtr++;
            if (cdPtr == 0x65)
               cdPtr = 0x60, trackNum++;
         }

         // Note that it seems to return track info in sets of 4 (or is it 5?)
         /*
            ;    +0 - track # (must be non-zero)
            ;    +1 - absolute minutes (0..99), start of track
            ;    +2 - absolute seconds (0..59), start of track
            ;    +3 - absolute frames, (0..74), start of track
            ;    +4 - session # (0..99)
            ;    +5 - track duration minutes
            ;    +6 - track duration seconds
            ;    +7 - track duration frames
            */
         // Seems to be the following format: $60xx -> Track #xx
         //                                   $61xx -> min?   (trk?)
         //                                   $62xx -> sec?   (min?)
         //                                   $63xx -> frame? (sec?)
         //                                   $64xx -> ?      (frame?)
         /*			cdPtr++;
                  switch (cdPtr)
                  {
                  case 1:
                  data = 0x6000 | trackNum;	// Track #
                  break;
                  case 2:
                  data = 0x6100 | trackNum;	// Track # (again?)
                  break;
                  case 3:
                  data = 0x6200 | minutes[trackNum];	// Minutes
                  break;
                  case 4:
                  data = 0x6300 | seconds[trackNum];	// Seconds
                  break;
                  case 5:
                  data = 0x6400 | frames[trackNum];		// Frames
                  trackNum++;
                  cdPtr = 0;
                  }//*/
      }
      else if ((cdCmd & 0xFF00) == 0x1500)		// Set Mode
         data = 0x1700 | (cdCmd & 0xFF);			// Mode Status: $17nn
      else if ((cdCmd & 0xFF00) == 0x1800)		// Spin up session #
         data = 0x0143;								// Spun Up
      else if ((cdCmd & 0xFF00) == 0x5400)		// Read # of sessions
         data = 0x5400 | (CDIntfGetNumSessions() & 0xFF);
      else if ((cdCmd & 0xFF00) == 0x7000)		// Set DAC Mode
         data = cdCmd;								// Echo: $70nn
      else
         data = 0x0400;

      // Multi-word commands: keep dsaResponseReady true while there are
      // more data words to deliver; clear it after the last data word so
      // the BIOS sees bit 13 go low and knows the response is complete.
      // $0400 (error/done) always clears.
      // NOTE: Queue-based responses (seek, stop) manage dsaResponseReady
      // through DSAQueuePop() and skip this block entirely.
      if (dsaQueueCount > 0)
      {
         // Queue still has entries — dsaResponseReady stays true
      }
      else if (data == 0x0400)
      {
         dsaResponseReady = false;
         isMultiWordResponse = false;
      }
      else if ((cdCmd & 0xFF00) == 0x0300 && cdPtr >= 5)
      {
         dsaResponseReady = false;  // Session TOC: 5 data words delivered
         isMultiWordResponse = false;
      }
      else if ((cdCmd & 0xFF00) == 0x1400 && trackNum > maxTrack)
      {
         dsaResponseReady = false;  // Full TOC: all tracks delivered
         isMultiWordResponse = false;
      }
      // Single-word responses: clear dsaResponseReady after data is consumed.
      // This must happen HERE (not in DSCNTRL read) because the GPU ISR reads
      // DSCNTRL before checking BUTCH for bit 13 — clearing in DSCNTRL would
      // destroy the response before the ISR ever sees it.
      else if (!isMultiWordResponse)
      {
         dsaResponseReady = false;
         isMultiWordResponse = false;
      }
   }
   else if (offset == DS_DATA && !haveCDGoodness)
      data = 0x0400;								// No CD interface present, so return error
   else if (offset >= FIFO_DATA && offset <= FIFO_DATA + 3)
   {
      {
         extern uint32_t gpu_pc;
         static uint32_t fifoReadTraceCount = 0;
         fifoReadTraceCount++;
         if (fifoReadTraceCount <= 20 || (fifoReadTraceCount % 100000) == 0)
         {
            CD_LOG("FIFO_DATA read #%u offset=$%02X who=%u fifoReady=%d cdPlaying=%d cdBufPtr=%u GPU_PC=$%06X\n",
                   fifoReadTraceCount, offset, who, fifoDataReady, cdPlaying, cdBufPtr, gpu_pc);
         }
      }
      if (haveCDGoodness && fifoDataReady)
      {
         if (cdBufPtr >= 2352 && cdPlaying)
         {
            block++;
            CDIntfReadBlock(block, cdBuf);
            cdBufPtr = 0;
         }
         if (cdBufPtr < 2352)
         {
            data = (cdBuf[cdBufPtr] << 8) | cdBuf[cdBufPtr + 1];
            cdBufPtr += 2;
         }
         fifoReadCount++;
         if (fifoReadCount >= FIFO_DRAIN_READS)
         {
            fifoDataReady = false;
            fifoFillDelay = FIFO_REFILL_TICKS;
         }
      }
   }
   else if (offset >= FIFO_DATA + 4 && offset <= FIFO_DATA + 7)
   {
      // I2SDAT2 read -- alternate FIFO port, also delivers sector data.
      if (haveCDGoodness && fifoDataReady)
      {
         if (cdBufPtr >= 2352 && cdPlaying)
         {
            block++;
            CDIntfReadBlock(block, cdBuf);
            cdBufPtr = 0;
         }
         if (cdBufPtr < 2352)
         {
            data = (cdBuf[cdBufPtr] << 8) | cdBuf[cdBufPtr + 1];
            cdBufPtr += 2;
         }
         fifoReadCount++;
         if (fifoReadCount >= FIFO_DRAIN_READS)
         {
            fifoDataReady = false;
            fifoFillDelay = FIFO_REFILL_TICKS;
         }
      }
   }
   else
      data = GET16(cdRam, offset);

   //Returning $00000008 seems to cause it to use the starfield. Dunno why.
   // It looks like it's getting the CD_mode this way...
   if (offset == UNKNOWN + 2)
      data = CDROMBusRead();

   // Log non-EEPROM-bus reads. Suppress GPU RAM dumps to reduce trace noise.
   if (offset != UNKNOWN + 2 && offset != UNKNOWN)
   {
      uint32_t gpuPC = GPUGetPC();
      int gpuRun = GPUIsRunning();
      static const char *whoNames[] = {"UNK","JAG","DSP","GPU","TOM","JER","68K","BLT","OP","DBG"};
      CD_LOG("ReadWord offset=0x%02X data=0x%04X (cmd=0x%04X, dsaRdy=%d) who=%s gpuRun=%d [68K_PC=$%06X GPU_PC=$%06X]\n",
             offset, data, cdCmd, dsaResponseReady,
             (who < 10) ? whoNames[who] : "???", gpuRun,
             m68k_get_reg(NULL, M68K_REG_PC), gpuPC);
   }

   return data;
}

void CDROMWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFF;
   cdRam[offset] = data;
}

void CDROMWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFF;

   // BUTCH+2 (low word of ICR): only enable bits (0-6) are writable.
   // Per MiSTer FPGA butch.v: status bits (9-14) are read-only, computed from
   // hardware state (FIFO fill level, DSA response queue, etc.). They are NOT
   // write-1-to-clear. The GPU ISR reads BUTCH (getting enables+status), modifies
   // enable bits, and writes back — status bits in the write data are ignored.
   // Interrupts are acknowledged by performing the corresponding action:
   //   - FIFO half-full (bit 9): drain FIFO by reading FIFO_DATA/I2SDAT2
   //   - DSARX (bit 13): consume response by reading DS_DATA
   if (offset == BUTCH + 2)
   {
      SET16(cdRam, offset, data & 0x007F);  // Store only enable bits (0-6)
      CD_LOG("WriteWord BUTCH+2: data=0x%04X enables=0x%02X [PC=$%06X]\n",
             data, data & 0x7F, m68k_get_reg(NULL, M68K_REG_PC));
      return;
   }

   SET16(cdRam, offset, data);

   if (offset < UNKNOWN)  // Don't log EEPROM bus writes ($2C/$2E) — too noisy
      CD_LOG("WriteWord offset=0x%02X data=0x%04X [PC=$%06X]\n", offset, data, m68k_get_reg(NULL, M68K_REG_PC));

   // Command register
   if (offset == DS_DATA)
   {
      CD_LOG("DS_DATA write: cmd=0x%04X\n", data);
      cdCmd = data;
      txBufferEmpty = true;  // Per MiSTer: set bit 12 on command write

      // $10xx/$11xx (Goto Min/Sec): no actual response data, but the BIOS's
      // DSA_tx routine polls BUTCH bit 13 after every command. We must keep
      // dsaResponseReady=true so DSA_tx exits. The original emulator code
      // always returned bit 13=1 on BUTCH+2 reads.
      // $12xx (Goto Frame): response delivered after seek delay.
      if ((data & 0xFF00) == 0x1200)
      {
         // Compute target block from accumulated min/sec + this frame value
         uint8_t newFrm = data & 0x00FF;
         int32_t absBlock = (((min * 60) + sec) * 75) + newFrm;
         uint32_t newBlock = (absBlock >= 150) ? (uint32_t)(absBlock - 150) : 0;

         // Skip redundant seeks: if CD is already playing at the target block,
         // don't restart the seek state machine. The boot stub calls CD_read
         // in a tight loop, and each call re-sends $10/$11/$12 commands.
         // Restarting seekDelay each time would keep dsaResponseReady cycling
         // true, preventing the GPU ISR from ever taking the FIFO data path
         // (bit 13 stays set, masking bit 9).
         if (cdPlaying && newBlock == block && seekDelay <= 0 && dsaQueueCount == 0)
         {
            CD_LOG("Skipping redundant seek to block %u (already playing)\n", block);
         }
         else
         {
            dsaResponseReady = false;
            isMultiWordResponse = false;
            seekDelay = SEEK_DELAY_TICKS;
         }
      }
      else if ((data & 0xFF00) == 0x1000 || (data & 0xFF00) == 0x1100)
      {
         // $10xx/$11xx (Goto Min/Sec) do NOT generate serial bus responses
         // on real hardware (confirmed by MiSTer FPGA). The BIOS's DSA_tx
         // polls bit 12 (TX buffer empty), not bit 13 (RX full).
         // Setting dsaResponseReady=true here caused BUTCHExec to fire
         // spurious GPU IRQs — the ISR read DS_DATA, got $0400 (error),
         // and corrupted the CD boot state.
         dsaResponseReady = false;
         isMultiWordResponse = false;
      }
      else if ((data & 0xFF00) == 0x0300 || (data & 0xFF00) == 0x1400)
      {
         dsaResponseReady = true;
         isMultiWordResponse = true;  // TOC responses are multi-word
      }
      else if ((data & 0xFF00) == 0x0200)
      {
         // STOP response is queued below, don't set dsaResponseReady here
         isMultiWordResponse = false;
      }
      else
      {
         dsaResponseReady = true;
         isMultiWordResponse = false;
      }

      if ((data & 0xFF00) == 0x0200)				// Stop CD
      {
         /* Auth-fail trap: if the last CD read landed in a virtual-pregap gap
          * (silence), the BIOS is now issuing STOP because audio-signature
          * authentication failed.  Log the 68K PC and recent PC history so
          * we can identify the BIOS auth branch and patch/trap it. */
         if (CDIntfLastReadWasVirtualPregap())
         {
            CD_LOG("AUTH: STOP after virtual-pregap read LBA=%u  68K_PC=$%06X  GPU_PC=$%06X\n",
                   CDIntfLastVirtualPregapLBA(),
                   m68k_get_reg(NULL, M68K_REG_PC),
                   GPUGetPC());
            CDIntfClearLastReadVirtualPregap();
         }
         cdPtr = 0;
         cdPlaying = false;
         // seekDelay is NOT zeroed — on real hardware, STOP halts playback
         // but does not cancel an in-progress seek. The drive continues
         // seeking and delivers $0100 when it reaches the target position.
         // This is critical for the BIOS boot: seek+STOP, then wait for
         // seek completion in the main loop.
         fifoFillDelay = 0;
         // On real hardware, STOP halts the drive motor but data already in
         // the FIFO and sector buffer remains readable. Don't clear the buffer
         // — the DSP needs to read the boot sector data that was loaded during
         // the seek. cdBufPtr stays where it is so ButchIsReadyToSend can
         // still return true for remaining data.
         if (cdBufPtr >= 2352)
         {
            fifoDataReady = false;
            fifoReadCount = 0;
         }
         // Queue the STOP response in the DSA RX buffer
         DSAQueuePush(0x0200);
      }
      else if ((data & 0xFF00) == 0x0300)			// Read session TOC (5 words)
         cdPtr = 0;
      else if ((data & 0xFF00) == 0x0400)			// Pause CD
         cdPlaying = false;
      else if ((data & 0xFF00) == 0x0500)			// Unpause CD
         cdPlaying = true;
      else if ((data & 0xFF00) == 0x1000)			// Seek to minute position
         min = data & 0x00FF;
      else if ((data & 0xFF00) == 0x1100)			// Seek to second position
         sec = data & 0x00FF;
      else if ((data & 0xFF00) == 0x1200)			// Seek to frame position
      {
         uint8_t newFrm = data & 0x00FF;
         int32_t absBlock = (((min * 60) + sec) * 75) + newFrm;
         uint32_t newBlock = (absBlock >= 150) ? (uint32_t)(absBlock - 150) : 0;

         // Skip redundant seek (same guard as the seekDelay handler above)
         if (cdPlaying && newBlock == block && seekDelay <= 0 && dsaQueueCount == 0)
         {
            frm = newFrm;
            // Don't re-read block, don't reset cdBufPtr — data is already flowing
         }
         else
         {
            frm = newFrm;
            block = newBlock;

            uint32_t discTotal = CDIntfGetDiscTotalSectors();
            if (discTotal > 0 && block >= discTotal)
            {
               uint32_t redirectLBA = CDIntfGetSession2GameDataLBA();
               CD_LOG("Out-of-range seek: block=%u exceeds disc size %u "
                      "(MSF %02u:%02u:%02u). Redirecting to session 2 game data at LBA %u\n",
                      block, discTotal, min, sec, frm, redirectLBA);
               block = redirectLBA;
            }

            CDIntfReadBlock(block, cdBuf);
            cdBufPtr = 0;
            CD_LOG("Seek started: block=%u (MSF %02u:%02u:%02u), delay=%d ticks\n",
                   block, min, sec, frm, SEEK_DELAY_TICKS);
         }
      }
      else if ((data & 0xFF00) == 0x1400)			// Read "full" TOC for session
      {
         cdPtr = 0x60;
         minTrack = CDIntfGetSessionInfo(data & 0xFF, 0);
         maxTrack = CDIntfGetSessionInfo(data & 0xFF, 1);
         trackNum = minTrack;
      }
   }//*/

   if (offset == UNKNOWN + 2)
      CDROMBusWrite(data);
}

// State machine for sending/receiving data along a serial bus

enum ButchState { ST_INIT, ST_RISING, ST_FALLING };
static enum ButchState currentState = ST_INIT;
static uint16_t counter = 0;
static bool cmdTx = false;
static uint16_t busCmd;
static uint16_t rxData, txData;
static uint16_t rxDataBit;
static bool firstTime = false;

static void CDROMBusWrite(uint16_t data)
{
   // NM93C14 EEPROM serial interface emulation
   // Register bits: 0=CS, 1=CLK, 2=DI (data to EEPROM), 3=DO (data from EEPROM)
   //
   // The BIOS protocol uses a 3-write cycle per clock:
   //   1. Write with bit0=1 to start command phase
   //   2. Write with bit0=0 + bit2=data for each command/data bit
   //   3. Transition writes (state machine ticks)
   //
   // The state machine processes data only in the RISING state.

   switch (currentState)
   {
      case ST_INIT:
         currentState = ST_RISING;
         break;
      case ST_RISING:
         if (data & 0x0001)							// Command coming (CS asserted)
         {
            cmdTx = true;
            counter = 0;
            busCmd = 0;
         }
         else
         {
            if (cmdTx)
            {
               busCmd <<= 1;						// Make room for next bit
               busCmd |= (data & 0x04);			// & put it in
               counter++;

               if (counter == 9)
               {
                  busCmd >>= 2;					// Because we ORed bit 2, we need to shift right by 2
                  cmdTx = false;

                  CD_LOG("BusCmd: 0x%03X [PC=$%06X]\n", busCmd, m68k_get_reg(NULL, M68K_REG_PC));

                  // NM93C14 command decoding:
                  // 9-bit command = start(1) + opcode(2) + address(6)
                  // Opcodes: 10=READ, 01=WRITE, 11=ERASE, 00=special
                  uint16_t opcode = (busCmd >> 6) & 0x03;
                  uint16_t addr = busCmd & 0x3F;

                  if (opcode == 2)  // READ (10 binary)
                  {
                     rxData = cdrom_eeprom_ram[addr];
                     CD_LOG("EEPROM READ addr=%u -> 0x%04X\n", addr, rxData);
                  }
                  else if (opcode == 1)  // WRITE (01 binary)
                  {
                     // txData will be collected in data phase, then written
                     CD_LOG("EEPROM WRITE addr=%u (data follows)\n", addr);
                     rxData = 0;
                  }
                  else if (opcode == 3)  // ERASE (11 binary)
                  {
                     cdrom_eeprom_ram[addr] = 0xFFFF;
                     CD_LOG("EEPROM ERASE addr=%u\n", addr);
                     rxData = 0;
                  }
                  else  // Special commands (00 binary)
                  {
                     // EWDS (100000000), EWEN (100110000), ERAL, WRAL
                     CD_LOG("EEPROM special cmd=0x%03X\n", busCmd);
                     rxData = 0;
                  }

                  counter = 0;
                  firstTime = true;
                  txData = 0;
               }
            }
            else
            {
               // Data phase: output response bits (READ) or collect input bits (WRITE)
               if (firstTime)
               {
                  // NM93C14 outputs a dummy 0 bit before data (ready indicator)
                  rxDataBit = 0;
                  firstTime = false;
               }
               else
               {
                  txData = (txData << 1) | ((data & 0x04) >> 2);
                  rxDataBit = (rxData & 0x8000) >> 12;
                  rxData <<= 1;
               }
               counter++;
            }
         }

         currentState = ST_FALLING;
         break;
      case ST_FALLING:
         currentState = ST_INIT;
         break;
   }
}

static uint16_t CDROMBusRead(void)
{
   // It seems the counter == 0 simply waits for a single bit acknowledge-- !!! FIX !!!
   // Or does it? Hmm. It still "pumps" 16 bits through above, so how is this special?
   // Seems to be because it sits and looks at it as if it will change. Dunno!

   return rxDataBit;
}

//
// This simulates a read from BUTCH over the SSI to JERRY.
// Reads CD audio data from the disc image.
//
uint16_t GetWordFromButchSSI(uint32_t offset, uint32_t who/*= UNKNOWN*/)
{
   bool go = ((offset & 0x0F) == 0x0A || (offset & 0x0F) == 0x0E ? true : false);

   if (!go)
      return 0x000;

   cdBufPtr += 2;

   if (cdBufPtr >= 2352)
   {
      CDIntfReadBlock(block, cdBuf);
      block++;
      cdBufPtr = 0;
   }

   // CD audio is 16-bit stereo, little-endian on disc (Red Book format)
   // The Jaguar expects right channel in upper 16 bits, left in lower 16
   return (cdBuf[cdBufPtr + 1] << 8) | cdBuf[cdBufPtr + 0];
}

bool CDROMHasData(void)
{
   return haveCDGoodness && cdBufPtr < 2352;
}

bool CDROMIsBiosOverride(void)
{
   // BUTCH bit 18 (BIOS_OVRD): when set, cart-space reads ($800000+) return
   // CD FIFO data instead of BIOS ROM. The upper word of BUTCH ($DFFF00) is
   // stored in cdRam[0..1]; bit 18 of the longword = bit 2 of the upper word.
   return haveCDGoodness && (cdRam[BUTCH + 1] & 0x04);
}

uint8_t CDROMReadFifoByte(uint32_t who)
{
   if (!haveCDGoodness || !cdPlaying)
      return 0x00;

   if (cdBufPtr >= 2352)
   {
      block++;
      CDIntfReadBlock(block, cdBuf);
      cdBufPtr = 0;
   }
   if (cdBufPtr < 2352)
   {
      uint8_t val = cdBuf[cdBufPtr++];
      return val;
   }
   return 0x00;
}

bool ButchIsReadyToSend(void)
{
   // On real hardware, BUTCH sends I2S data when the FIFO has data from the
   // CD drive, independent of software register writes. The emulation runs
   // the DSP (audio callback) AFTER the 68K finishes the frame, so the DSP
   // never sees intermediate I2CNTRL values. Check actual data availability
   // instead of the software register bit. The sector buffer (cdBuf) is
   // loaded during seek and contains valid data until fully consumed.
   if (haveCDGoodness && cdBufPtr < 2352)
      return true;
   return ((cdRam[I2CNTRL + 3] & 0x02) ? true : false);
}

//
// This simulates a read from BUTCH over the SSI to JERRY.
// Delivers CD audio samples to the DAC left/right receive registers.
//
static uint32_t ssiXmitCount = 0;

void SetSSIWordsXmittedFromButch(void)
{
   ssiXmitCount++;
   if (ssiXmitCount <= 5 || (ssiXmitCount % 10000) == 0)
      CD_LOG("SSI xmit #%u: cdBufPtr=%u block=%u cdPlaying=%d\n",
             ssiXmitCount, cdBufPtr, block, cdPlaying);
   // Advance by 4 bytes (one stereo sample: 2 bytes L + 2 bytes R)
   cdBufPtr += 4;

   if (cdBufPtr >= 2352)
   {
      CDIntfReadBlock(block, cdBuf);
      block++;
      cdBufPtr = 0;
   }

   // CD audio is interleaved 16-bit stereo samples in little-endian
   // Left channel = bytes [ptr+2..ptr+3], Right channel = bytes [ptr+0..ptr+1]
   // (CD audio byte order: LL LH RL RH per sample pair)
   lrxd = (cdBuf[cdBufPtr + 3] << 8) | cdBuf[cdBufPtr + 2];
   rrxd = (cdBuf[cdBufPtr + 1] << 8) | cdBuf[cdBufPtr + 0];
}

/*
   [18667]
   TOC for MYST

CDINTF: Disc summary
# of sessions: 2, # of tracks: 10
Session info:
1: min track= 1, max track= 1, lead out= 1:36:67
2: min track= 2, max track=10, lead out=55:24:71
Track info:
1: start= 0:02:00
2: start= 4:08:67
3: start= 4:16:65
4: start= 4:29:19
5: start=29:31:03
6: start=33:38:50
7: start=41:38:60
8: start=44:52:18
9: start=51:51:22
10: start=55:18:73

CDROM: Read sector 18517 (18667 - 150)...

0000: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0018: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0048: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0060: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0078: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0090: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00A8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00C0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00D8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00F0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0108: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0120: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0138: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0150: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0168: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0180: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0198: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01B0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01C8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01E0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01F8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0210: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0228: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0258: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0270: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0288: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
02A0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
02B8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
02D0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
02E8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0300: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0318: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0330: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0348: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0360: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0378: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0390: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
03A8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
03C0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
03D8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
03F0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0408: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0420: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0438: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0450: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0468: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0480: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0498: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
04B0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
04C8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
04E0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
04F8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0510: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0528: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0540: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0558: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0570: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0588: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
05A0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
05B8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
05D0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
05E8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0600: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0618: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0630: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0648: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0660: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0678: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0690: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
06A8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
06C0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
06D8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
06F0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0708: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0720: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0738: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0750: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0768: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0780: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0798: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
07B0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
07C8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00[54 41 49 52]54 41
07E0: 49 52 54 41 49 52 54 41 49 52 54 41 49 52 54 41 49 52 54 41 49 52 54 41
07F8: 49 52 54 41 49 52 54 41 49 52 54 41 49 52 54 41 49 52 54 41 49 52 54 41
0810: 49 52 54 41 49 52[54 41 49 52]54 41 52 41 20 49 50 41 52 50 56 4F 44 45
0828: 44 20 54 41 20 41 45 48 44 41 52 45 41 20 52 54 20 49[00 00 00 50]01 00
0840: 80 83 FC 23 07 00 07 00 F0 00 0C 21 FC 23 07 00 07 00 F1 00 0C A1 FC 33
0858: FF FF F0 00 4E 00 7C 2E 1F 00 FC FF 00 61 08 00 F9 4E 00 00 00 51 E7 48
0870: 00 FE 39 30 F1 00 02 40 40 02 10 00 00 67 1C 00 79 42 01 00 8C D3 3C 34
0888: 37 03 3C 30 81 05 3C 3C 0A 01 3C 38 F1 00 00 60 1A 00 FC 33 01 00 01 00
08A0: 8C D3 3C 34 4B 03 3C 30 65 05 3C 3C 42 01 3C 38 1F 01 C0 33 01 00 88 D3
08B8: C4 33 01 00 8A D3 00 32 41 E2 41 94 7C D4 04 00 7C 92 01 00 41 00 00 04
08D0: C1 33 01 00 82 D3 C1 33 F0 00 3C 00 C2 33 01 00 80 D3 C2 33 F0 00 38 00
08E8: C2 33 F0 00 3A 00 06 3A 44 9A C5 33 01 00 84 D3 44 DC C6 33 01 00 86 D3
0900: F9 33 01 00 84 D3 F0 00 46 00 FC 33 FF FF F0 00 48 00 FC 23 00 00 00 00
0918: F0 00 2A 00 FC 33 00 00 F0 00 58 00 DF 4C 7F 00 75 4E 00 00 00 00 00 00

Raw P-W subchannel data:

00: 80 80 C0 80 80 80 80 C0 80 80 80 80 80 80 C0 80
10: 80 80 80 80 80 80 80 80 80 80 80 80 80 80 80 80
20: 80 80 80 80 80 80 80 80 80 80 80 80 80 80 80 C0
30: 80 80 80 80 80 80 80 80 80 80 80 80 80 C0 80 80
40: 80 80 80 80 C0 80 80 80 80 C0 C0 80 80 C0 C0 80
50: C0 80 80 C0 C0 C0 80 80 C0 80 80 80 C0 80 80 80

P subchannel data: FF FF FF FF FF FF FF FF FF FF FF FF
Q subchannel data: 21 02 00 00 00 01 00 04 08 66 9C 88

Run address: $5000, Length: $18380
*/


/*
   CD_read function from the CD BIOS: Note that it seems to direct the EXT1 interrupt
   to the GPU--so that would mean *any* interrupt that BUTCH generates would be routed
   to the GPU...

   read:
   btst.l	#31,d0
   bne.w	.play
   subq.l	#4,a0		; Make up for ISR pre-increment
   move.l	d0,-(sp)
   move.l	BUTCH,d0
   and.l	#$ffff0000,d0
   move.l	d0,BUTCH	; NO INTERRUPTS!!!!!!!!!!!
   move.l	(sp)+,d0
   ;		move.l	#0,BUTCH

   move.w	#$101,J_INT

   move.l	d1,-(sp)
   move.l	I2CNTRL,d1	;Read I2S Control Register
   bclr	#2,d1		; Stop data
   move.l	d1,I2CNTRL
   move.l	(sp)+,d1

   move.l	PTRLOC,a2
   move.l	a0,(a2)+
   move.l	a1,(a2)+
   move.l	#0,(a2)+

   btst.b	#7,INITTYPE
   beq	.not_bad
   move.l	PTRLOC,a0
   asl.l	#5,d2

   move.l	d2,-(sp)

   or.l	#$089a3c1a,d2		; These instructions include the bclr
   move.l	d2,188(a0)

   move.l	(sp)+,d2

   swap	d2
   or.l	#$3c1a1838,d2		; These instructions include the bclr
   move.l	d2,196(a0)

   move.l	#16,(a2)+
   move.l	d1,(a2)

   .not_bad:

   move.w	DS_DATA,d1			; Clear any pending DSARX states
   move.l	I2CNTRL,d1			; Clear any pending errors

   ; Drain the FIFO so that we don't get overloaded

   .dump:
   move.l	FIFO_DATA,d1
   move.l	I2CNTRL,d1
   btst	#4,d1
   bne.b	.dump

   .butch_go:
   move.l	BUTCH,d1
   and.l	#$FFFF0000,d1
   or.l	#%000100001,d1			 ;Enable DSARX interrupt
   move.l	d1,BUTCH
   ;		move.l	#%000100001,BUTCH		 ;Enable DSARX interrupt

   ; Do a play @

   .play:	move.l	d0,d1		; mess with copy in d1
lsr.l	#8,d1		; shift the byte over
lsr.w	#8,d1
or.w	#$1000,d1	; format it for goto
move.w	d1,DS_DATA	; DSA tx
bsr.b	DSA_tx

move.l	d0,d1		; mess with copy in d1
lsr.w	#8,d1
or.w	#$1100,d1	; format it for goto
move.w	d1,DS_DATA	; DSA tx
bsr.b	DSA_tx

move.l	d0,d1		; mess with copy in d1
and.w	#$00FF,d1	; mask for minutes
or.w	#$1200,d1	; format it for goto
move.w	d1,DS_DATA	; DSA tx
bsr.b	DSA_tx

rts


****************************
* Here's the GPU interrupt *
****************************

JERRY_ISR:
movei	#G_FLAGS,r30
load	(r30),r29		;read the flags

movei	#BUTCH,r24

make_ptr:
move	pc,Ptrloc
movei	#(make_ptr-PTRPOS),TEMP
sub	TEMP,Ptrloc

HERE:
move	pc,r25
movei	#(EXIT_ISR-HERE),r27
add	r27,r25

; Is this a DSARX interrupt?

load	(r24),r27		;check for DSARX int pending
btst	#13,r27
jr	z,fifo_read			; This should ALWAYS fall thru the first time

; Set the match bit, to allow data
;	moveq	#3,r26			; enable FIFO only
; Don't just jam a value
; Clear the DSARX and set FIFO
bclr	#5,r27
   bset	#1,r27
store	r27,(r24)
   addq	#$10,r24
   load	(r24),r27
   bset	#2,r27
   store	r27,(r24)		; Disable SUBCODE match

   ; Now we clear the DSARX interrupt in Butch

   subq	#12,r24			; does what the above says
   load	(r24),r26		;Clears DSA pending interrupt
   addq	#6,r24
   loadw	(r24),r27		; Read DSA response
   btst	#10,r27			; Check for error
   jr	nz,error
   or	r26,r26
jump	(r25)
   ;	nop

   fifo_read:
   ; Check for ERROR!!!!!!!!!!!!!!!!!!!!!
   btst	#14,r27
   jr	z,noerror
   bset	#31,r27
   error:
   addq	#$10,r24
   load	(r24),TEMP
   or	TEMP,TEMP
   subq	#$10,r24
   load	(Ptrloc),TEMP
   addq	#8,Ptrloc
store	TEMP,(Ptrloc)
   subq	#8,Ptrloc
   noerror:
   load	(Ptrloc),Dataptr	;get pointer

   ; Check to see if we should stop
   addq	#4,Ptrloc
   load	(Ptrloc),TEMP
   subq	#4,Ptrloc
   cmp	Dataptr,TEMP
   jr	pl,notend
   ;	nop
   bclr	#0,r27
store	r27,(r24)

   notend:
   movei	#FIFO_DATA,CDdata
   move	CDdata,r25
   addq	#4,CDdata
   loptop:
   load 	(CDdata),TEMP
   load	(r25),r30
   load	(CDdata),r21
   load	(r25),r22
   load	(CDdata),r24
   load	(r25),r20
   load	(CDdata),r19
   load	(r25),r18
   addq	#4,Dataptr
store	TEMP,(Dataptr)
   addqt	#4,Dataptr
store	r30,(Dataptr)
   addqt	#4,Dataptr
store	r21,(Dataptr)
   addqt	#4,Dataptr
store	r22,(Dataptr)
   addqt	#4,Dataptr
store	r24,(Dataptr)
   addqt	#4,Dataptr
store	r20,(Dataptr)
   addqt	#4,Dataptr
store	r19,(Dataptr)
   addqt	#4,Dataptr
store	r18,(Dataptr)

store	Dataptr,(Ptrloc)

   exit_isr:
   movei	#J_INT,r24	; Acknowledge in Jerry
   moveq	#1,TEMP
   bset	#8,TEMP
storew	TEMP,(r24)

   .if FLAG
   ; Stack r18
   load	(r31),r18
   addq	#4,r31

   ; Stack r19
   load	(r31),r19
   addq	#4,r31

   ; Stack r20
   load	(r31),r20
   addq	#4,r31

   ; Stack r21
   load	(r31),r21
   addq	#4,r31

   ; Stack r22
   load	(r31),r22
   addq	#4,r31

   ; Stack r23
   load	(r31),r23
   addq	#4,r31

   ; Stack r26
   load	(r31),r26
   addq	#4,r31

   ; Stack r27
   load	(r31),r27
   addq	#4,r31

   ; Stack r24
   load	(r31),r24
   addq	#4,r31

   ; Stack r25
   load	(r31),r25
   addq	#4,r31
   .endif

   movei	#G_FLAGS,r30

   ;r29 already has flags
   bclr	#3,r29		;IMASK
   bset	#10,r29		;Clear DSP int bit in TOM

   load	(r31),r28	;Load return address


   addq	#2,r28		;Fix it up
   addq	#4,r31
   jump	(r28)		;Return
   store	r29,(r30)	;Restore broken flags


   align long

   stackbot:
   ds.l	20
   STACK:


   */

#include "state.h"

size_t CDROMStateSave(uint8_t *buf)
{
	uint8_t *start = buf;

	STATE_SAVE_BUF(buf, cdRam, sizeof(cdRam));
	STATE_SAVE_VAR(buf, cdCmd);
	STATE_SAVE_VAR(buf, cdPtr);
	STATE_SAVE_VAR(buf, haveCDGoodness);
	STATE_SAVE_VAR(buf, min);
	STATE_SAVE_VAR(buf, sec);
	STATE_SAVE_VAR(buf, frm);
	STATE_SAVE_VAR(buf, block);
	STATE_SAVE_BUF(buf, cdBuf, sizeof(cdBuf));
	STATE_SAVE_VAR(buf, cdBufPtr);
	STATE_SAVE_VAR(buf, trackNum);
	STATE_SAVE_VAR(buf, minTrack);
	STATE_SAVE_VAR(buf, maxTrack);
	STATE_SAVE_VAR(buf, currentState);
	STATE_SAVE_VAR(buf, counter);
	STATE_SAVE_VAR(buf, cmdTx);
	STATE_SAVE_VAR(buf, busCmd);
	STATE_SAVE_VAR(buf, rxData);
	STATE_SAVE_VAR(buf, txData);
	STATE_SAVE_VAR(buf, rxDataBit);
	STATE_SAVE_VAR(buf, firstTime);
	STATE_SAVE_BUF(buf, cdrom_eeprom_ram, sizeof(cdrom_eeprom_ram));
	STATE_SAVE_VAR(buf, dsaResponseReady);
	STATE_SAVE_VAR(buf, isMultiWordResponse);
	STATE_SAVE_VAR(buf, txBufferEmpty);
	STATE_SAVE_VAR(buf, cdPlaying);
	STATE_SAVE_VAR(buf, seekDelay);
	STATE_SAVE_VAR(buf, fifoDataReady);
	STATE_SAVE_VAR(buf, fifoReadCount);
	STATE_SAVE_VAR(buf, fifoFillDelay);

	return (size_t)(buf - start);
}

size_t CDROMStateLoad(const uint8_t *buf)
{
	const uint8_t *start = buf;

	STATE_LOAD_BUF(buf, cdRam, sizeof(cdRam));
	STATE_LOAD_VAR(buf, cdCmd);
	STATE_LOAD_VAR(buf, cdPtr);
	STATE_LOAD_VAR(buf, haveCDGoodness);
	STATE_LOAD_VAR(buf, min);
	STATE_LOAD_VAR(buf, sec);
	STATE_LOAD_VAR(buf, frm);
	STATE_LOAD_VAR(buf, block);
	STATE_LOAD_BUF(buf, cdBuf, sizeof(cdBuf));
	STATE_LOAD_VAR(buf, cdBufPtr);
	STATE_LOAD_VAR(buf, trackNum);
	STATE_LOAD_VAR(buf, minTrack);
	STATE_LOAD_VAR(buf, maxTrack);
	STATE_LOAD_VAR(buf, currentState);
	STATE_LOAD_VAR(buf, counter);
	STATE_LOAD_VAR(buf, cmdTx);
	STATE_LOAD_VAR(buf, busCmd);
	STATE_LOAD_VAR(buf, rxData);
	STATE_LOAD_VAR(buf, txData);
	STATE_LOAD_VAR(buf, rxDataBit);
	STATE_LOAD_VAR(buf, firstTime);
	STATE_LOAD_BUF(buf, cdrom_eeprom_ram, sizeof(cdrom_eeprom_ram));
	STATE_LOAD_VAR(buf, dsaResponseReady);
	STATE_LOAD_VAR(buf, isMultiWordResponse);
	STATE_LOAD_VAR(buf, txBufferEmpty);
	STATE_LOAD_VAR(buf, cdPlaying);
	STATE_LOAD_VAR(buf, seekDelay);
	STATE_LOAD_VAR(buf, fifoDataReady);
	STATE_LOAD_VAR(buf, fifoReadCount);
	STATE_LOAD_VAR(buf, fifoFillDelay);

	return (size_t)(buf - start);
}

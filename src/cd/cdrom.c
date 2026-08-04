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
#include <stdlib.h>
#include <string.h>
#include "cdintf.h"
#include "jagcd_boot.h"
#include "jagcd_hle.h"
#include "log.h"
#include "gpu.h"
#include "dsp.h"
#include "jaguar.h"
#include "jerry.h"
#include "tom.h"
#include "settings.h"
#include "m68000/m68kinterface.h"

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
#define FIFO_DRAIN_READS     16   // 16 word-reads = 8 GPU longword loads = 32 bytes

/* Refill pacing: the real drive streams data at double-speed CD-DA rate,
 * 150 sectors/s x 2352 bytes = 352,800 B/s (= the 2x I2S rate).  One
 * half-full batch (8 x 32-bit FIFO entries = 32 bytes) therefore arrives
 * every 90.7us = 2.85 halfline ticks (31.78us each).  The old fixed
 * 5-tick period paced the stream at ~201 KB/s -- 57% of hardware -- and
 * schedule-driven streaming engines (ReadySoft: Dragon's Lair, Space Ace)
 * dead-reckon the disc position against a GPU-timer clock (48-bit counter
 * at $562E/$5630, incremented in the game's GPU IRQ2 handler) and declare
 * a CD read error when the data stream slips behind the disc schedule
 * (68K $4C0C deadline-overshoot check).  Device-traced on both titles:
 * FMV segment loops, then the in-game "error reading CD" dialog.
 * The period is error-diffused in hundredths of a tick so the average
 * rate matches hardware without a fractional-time event system.
 *
 * This constant is the DOUBLE-speed period; single speed doubles it (see
 * cdDriveSpeed / CDROMNextRefillDelay). */
#define FIFO_REFILL_PERIOD_X100  285

/* Drive speed, latched from the DSA "Set Mode" command ($15nn).
 *
 * Ground truth, in order of authority:
 *
 * 1. Jaguar CD-ROM manual (docs/atari-jaguar-1999/'06 - Jaguar CD-ROM.pdf'),
 *    p.10 section 2.7.7 "CD_mode".  Input D0.W, "Speed/mode desired":
 *      Bit 0 => Speed: 0 = Single, 1 = Double
 *      Bit 1 => Mode:  0 = Audio,  1 = Data
 *    Purpose: "This call sets the speed of the CD to either single or
 *    double-speed and the data mode to either audio or data."
 *
 * 2. That layout is the *BIOS API* argument, NOT the DSA payload.  The CD
 *    BIOS translates it.  Disassembled from the retail CD BIOS ROM (the
 *    only $15xx emitter in the image; identical code in the embedded
 *    src/bios/jagcdbios.c and jagdevcdbios.c), at $808978:
 *
 *      move.w  d0,d2         ; caller's D0.W
 *      and.w   #$1,d2        ; keep bit 0 (speed request)
 *      add.w   #$1,d2        ; -> 1 = single, 2 = double
 *      btst    #1,d0         ; audio/data
 *      beq.s   +
 *      bset    #3,d2         ; data mode -> payload bit 3
 *   +  or.w    #$1500,d2     ; DSA command $15
 *      move.w  d2,$DFFF0A    ; transmit
 *      bsr     ...           ; await response
 *      bset    #9,d2         ; $15nn -> $17nn  (expected Mode Status echo)
 *      cmp.w   d1,d2         ; retry the whole command if it does not match
 *
 *    So on the wire the speed is a ONE-BASED CODE in the low bits, not a
 *    bit: 1 = single, 2 = double.  Bit 3 ($08) carries data(1)/audio(0).
 *    The four reachable payloads are $1501 single/audio, $1502
 *    double/audio, $1509 single/data, $150A double/data.
 *
 *    Reading the payload's bit 0 as "the speed bit" would invert both
 *    payloads we have actually observed: Baldies sends $150A (double/data,
 *    bit0=0) and Primal Rage's CDDA hand-off sends $1501 (single/audio,
 *    bit0=1).  Note BizHawk's HLE is not wrong for BizHawk -- it emulates
 *    the BIOS *call* and sees D0 directly (cd_mode = D0 & 3, delay >>
 *    (cd_mode & 1)); it never sees the DSA form.  We are on the DSA side.
 *
 * Power-on default: the manual does not state the drive's reset speed.  We
 * default to DOUBLE, which both preserves the previous fixed-2x behaviour
 * and matches p.8 section 2.6, which describes double speed as the normal
 * state a failing read is recovered from ("while running in double-speed
 * mode").
 *
 * Only the speed is acted on.  Bit 3 (audio/data) is decoded for the log
 * line below but deliberately not modelled: per section 2.7.7 its effect
 * ("when in audio mode, the CD mechanism may alter data or mute it
 * entirely to correct for 'spikes' in the data") is drive-internal. */
#define CD_SPEED_SINGLE 1
#define CD_SPEED_DOUBLE 2
static uint32_t cdDriveSpeed = CD_SPEED_DOUBLE;

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
/* Independent read head for the slave-mode SSI (CD -> JERRY I2S) path.
 * On real hardware the drive outputs ONE stream that both the BUTCH FIFO
 * and JERRY's I2S port observe; consumers don't advance the disc.  Our
 * FIFO path and SSI path each emulate "the stream" by pulling sectors on
 * demand, so they MUST NOT share a cursor: with a shared cdBufPtr every
 * GPU-ISR FIFO word-read stole 2 bytes out of the DSP's LRXD/RRXD stream
 * (and vice versa).  Device-traced: a CD_jeri title streaming data via
 * the DSP in slave mode while its GPU ISR drained the FIFO storm got a
 * gap-riddled stream, failed sector validation, and re-seeked forever
 * (audio loops, next screen never loads). */
static uint8_t ssiBuf[2352 + 96];
static uint32_t ssiBufPtr = 2352;
static uint32_t ssiBlock = 0;

// NM93C14 EEPROM: 64 x 16-bit words (128 bytes)
// Exposed so libretro.c can pack/unpack it into the .srm save buffer.
uint16_t cdrom_eeprom_ram[64];

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
static bool cdPrevShouldIRQ = false;   /* edge detect for 68K EXT delivery */
static int32_t fifoRefillAccum = 0;   /* hundredths of a tick, error diffusion */

// Diagnostic counters for CD data path debugging
static uint32_t diag_butchExecCalls = 0;
static uint32_t diag_fifoIRQsFired = 0;
static uint32_t diag_dsaIRQsFired = 0;
static uint32_t diag_fifoReads = 0;
static uint32_t diag_seekCommands = 0;
static uint32_t diag_butchGlobalDisabled = 0;

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

// DSA response turnaround, in half-line ticks (~31.8us each).  On real
// hardware a response word arrives over the DSA serial link hundreds of
// microseconds after the command — it is NEVER already pending in the same
// instant the 68K writes DS_DATA.  Making queued responses visible
// synchronously created a steal race: if the 68K's timeslice ended between
// writing a command and its first BUTCH bit-13 poll iteration, the game's
// GPU CD ISR (entered for a routine FIFO half-full service — near-constant
// during streaming) saw bit 13 already set, acked DSCNTRL and consumed the
// response from DS_DATA.  The 68K then polled bit 13 forever.
// Device-traced on Primal Rage (bios): the attract loop's CDDA hand-off
// sends Set Mode $1501 + Set DAC $7001 and blocks on bit 13 for the $70nn
// echo (68K wait loop at $3544); losing that race blacked out the attract
// sequence ~103 s in, permanently.  Deferring visibility to a BUTCHExec
// tick closes the race structurally: JaguarExecuteNew runs the 68K slice
// first after every event boundary, so a polling 68K always sees the
// response before the GPU ISR can run.  A GPU-consumed response with no
// 68K waiter (this game's normal $12xx seek flow) still works exactly as
// before, just one serial delay later.
#define DSA_RESPONSE_DELAY_TICKS 4
static int32_t dsaResponseDelay = 0;

/* While an HLE CD_read stream is delivering into the destination buffer,
 * BUTCH must not flag FIFO data ready: the HLE stream *is* the transfer,
 * and the game's GPU CD ISR would otherwise drain unframed FIFO words
 * into the same buffer at its live write pointer, corrupting freshly
 * streamed bytes.  Iron Soldier 2's loader issues its own DSA seek
 * ($10/$11/$12) to the data track in parallel with the BIOS CD_read
 * call; the seek-done path re-armed the FIFO mid-stream and the ISR
 * stomped 13 bytes at the match checksum base -> checksum mismatch ->
 * infinite retry.  Once the stream completes the ISR's write pointer is
 * parked past the end address, so post-stream FIFO drains are harmless
 * (and some drivers rely on them to see the drive "playing"). */
static bool FIFOFeedAllowed(void)
{
   return !JaguarCDHLEStreamActive();
}

static void DSAQueuePush(uint16_t response)
{
   if (dsaQueueCount < DSA_QUEUE_SIZE)
   {
      dsaQueue[dsaQueueTail] = response;
      dsaQueueTail = (dsaQueueTail + 1) % DSA_QUEUE_SIZE;
      dsaQueueCount++;
      // Response becomes visible (BUTCH bit 13) after a serial-transfer
      // delay, counted down in BUTCHExec — never in the same timeslice.
      // If a previous response is still visible or already in transit,
      // this word just queues behind it.
      if (!dsaResponseReady && dsaResponseDelay <= 0)
         dsaResponseDelay = DSA_RESPONSE_DELAY_TICKS;
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
         dsaResponseDelay = 0;
      }
      else
      {
         // Next queued word arrives after its own serial-word delay.
         dsaResponseReady = false;
         dsaResponseDelay = DSA_RESPONSE_DELAY_TICKS;
      }
      CD_LOG("DSA queue pop: $%04X (remaining=%u)\n", response, dsaQueueCount);
      return response;
   }
   return 0x0400;  // Error — empty queue
}


/* --- CD trace ring: records DSA traffic + seek/FIFO state transitions ---
 *
 * Diagnostic-only instrumentation for CD boot-hang triage (see
 * docs/cd-boot-matrix.md).  Never touches savestates -- all of this is
 * reset to empty on CDROMReset() the same as the other diag_* counters,
 * never serialized.
 *
 * Toggle: core option `virtualjaguar_cd_trace` (CDTraceSetEnabled(),
 * called from libretro.c's check_variables()) OR'd with env
 * VJ_CD_TRACE=1 for headless harness use (CDTraceEnvWantsTrace(), cached
 * after the first call so getenv() runs at most once per process).
 *
 * Zero-cost when off: CDTracePush()'s very first statement is the
 * `!cdTraceEnabled` branch, so every call site pays exactly one untaken
 * branch and nothing else -- no extra work, no memory writes.
 *
 * Ring size is a power of 2 so the head index wraps with a mask
 * (`& (CD_TRACE_SIZE - 1)`) instead of modulo -- cheap and, since
 * cdTraceHead is unsigned, never invokes negative-modulo UB. */
#ifndef CD_TRACE_SIZE          /* overridable (-DCD_TRACE_SIZE=8192) for deep traces; power of two */
#define CD_TRACE_SIZE 256
#endif

enum
{
   CD_TRACE_DSA_TX = 0,     /* DS_DATA write (command sent to drive) */
   CD_TRACE_DSA_RX,         /* DS_DATA read (response consumed) */
   CD_TRACE_SEEK_START,     /* $12xx armed a real (non-redundant) seek */
   CD_TRACE_SEEK_DONE,      /* seekDelay reached 0, $0100 queued */
   CD_TRACE_FIFO_FILL,      /* fifoDataReady transitioned to true */
   CD_TRACE_FIFO_DRAIN,     /* FIFO_DRAIN_READS reached, fifoDataReady cleared */
   CD_TRACE_STOP,           /* $0200 (STOP) command processed */
   CD_TRACE_HLE_READ,       /* HLE CD_read (see jagcd_hle.c) -- carries LBA */
   CD_TRACE_I2S_CTRL        /* I2CNTRL data-enable (bit 2) transition; value =
                                new low word.  The FIFO refill loop silently
                                parks while this bit is clear, so an OFF edge
                                with no later ON edge explains every "drains
                                frozen" wedge that isn't a STOP. */
};

static const char * const cdTraceKindName[] = {
   "DSA_TX", "DSA_RX", "SEEK_START", "SEEK_DONE",
   "FIFO_FILL", "FIFO_DRAIN", "STOP", "HLE_READ", "I2S_CTRL"
};

typedef struct
{
   uint32_t tick;    /* diag_butchExecCalls at event time -- one tick per
                         BUTCHExec() invocation, which HalflineCallback()
                         drives once per halfline (~524 NTSC / ~624 PAL per
                         frame; see crash_detect.c's halfline comment).
                         Not itself a frame counter -- cdrom.c has no
                         direct frame counter in scope -- but monotonic and
                         fine-grained enough to order events within and
                         across frames; divide by ~524/2 for a rough NTSC
                         frame estimate when reading a dump by eye. */
   uint16_t kind;    /* CD_TRACE_* */
   uint16_t value;   /* command/response word, or (HLE_READ) low 16 bits
                         of the requested byte count */
   uint32_t block;   /* absolute block/LBA at event time */
} CDTraceEntry;

static CDTraceEntry cdTrace[CD_TRACE_SIZE];
static uint32_t cdTraceHead = 0;    /* next slot to write (wraps via mask) */
static uint32_t cdTraceCount = 0;   /* valid entries, saturates at CD_TRACE_SIZE */
static int cdTraceEnabled = 0;
static int cdTraceEnvChecked = 0;   /* cache: getenv() runs at most once */
static int cdTraceEnvWants = 0;

/* Activity counters consumed by crash_detect.c's cd_seek_wedge watchdog
 * (CDROMDiagGetSeekWedgeState()).  Cheap monotonic uint32 counters -- no
 * allocation, not part of any savestate. */
static uint32_t cdSeekStartCount = 0;
static uint32_t cdSeekDoneCount = 0;
static uint32_t cdFifoDrainCount = 0;

/* Last-observed I2CNTRL data-enable state (bit 2 of the low byte), so
 * writes that don't change it stay out of the trace ring.  -1 = unknown
 * (traces the first write after reset regardless of value). */
static int cdTraceLastI2SEnable = -1;

/* First (boot-relevant) seek target LBA, captured post-redirect the first
 * time a non-redundant $12xx seek runs after reset.  0xFFFFFFFF = none. */
static uint32_t diag_firstSeekBlock = 0xFFFFFFFFu;

static int CDTraceEnvWantsTrace(void)
{
   const char *e;

   if (cdTraceEnvChecked)
      return cdTraceEnvWants;

   e = getenv("VJ_CD_TRACE");
   cdTraceEnvWants = (e != NULL && e[0] == '1') ? 1 : 0;
   cdTraceEnvChecked = 1;
   return cdTraceEnvWants;
}

/* Called from libretro.c's check_variables() with the core option's
 * enabled/disabled state; always OR'd with the env override so
 * VJ_CD_TRACE=1 works even for harnesses that never poll core options. */
void CDTraceSetEnabled(int enabled)
{
   cdTraceEnabled = enabled || CDTraceEnvWantsTrace();
}

/* VJ_CD_TRACE_LIVE=1: log every trace event as it is pushed instead of
 * relying on the 256-entry ring dump (which only fires on cd_seek_wedge).
 * Diagnostic-only, env-gated, cached after first check. */
static int cdTraceLiveChecked = 0;
static int cdTraceLiveWants = 0;

static int CDTraceLive(void)
{
   const char *e;

   if (cdTraceLiveChecked)
      return cdTraceLiveWants;

   e = getenv("VJ_CD_TRACE_LIVE");
   cdTraceLiveWants = (e != NULL && e[0] == '1') ? 1 : 0;
   cdTraceLiveChecked = 1;
   return cdTraceLiveWants;
}

static void CDTracePush(uint16_t kind, uint16_t value, uint32_t blk)
{
   CDTraceEntry *e;

   if (!cdTraceEnabled)
      return;

   e = &cdTrace[cdTraceHead & (CD_TRACE_SIZE - 1)];
   e->tick  = diag_butchExecCalls;
   e->kind  = kind;
   e->value = value;
   e->block = blk;
   cdTraceHead++;
   if (cdTraceCount < CD_TRACE_SIZE)
      cdTraceCount++;

   if (CDTraceLive())
      LOG_INF("[CD-TRACE-LIVE] tick=%u kind=%-10s value=$%04X block=%u\n",
              e->tick, cdTraceKindName[kind], value, blk);
}

/* Next FIFO refill delay in whole ticks, error-diffusing the fractional
 * hardware period (see FIFO_REFILL_PERIOD_X100) so the long-run average
 * matches the real drive: 352,800 B/s at double speed (2.85 ticks per
 * 32-byte batch, so 2 or 3), 176,400 B/s at single speed (5.70 ticks, so
 * 5 or 6).
 *
 * The speed is read HERE, when an interval is armed -- an already-counting
 * fifoFillDelay is never retroactively rescaled.  Two reasons: a spindle
 * speed change physically takes effect for subsequent sectors, not for the
 * one already in the servo/FIFO pipeline; and it keeps a mid-transfer Set
 * Mode from disturbing an in-flight transfer, matching the per-read latch
 * the HLE stream uses (jagcd_hle.c: hleStream.speedMult).  The difference
 * is at most one ~91us interval, so this is a correctness-of-form choice
 * rather than a behavioural one -- and section 2.6's read-error recovery
 * sequence issues its single->double pair BETWEEN reads, so the manual
 * never requires mid-transfer application.
 *
 * Scaling the accumulator input rather than the returned delay keeps the
 * fractional remainder, and with it the error diffusion. */
static int32_t CDROMNextRefillDelay(void)
{
   int32_t d;
   fifoRefillAccum += (int32_t)(FIFO_REFILL_PERIOD_X100 * CD_SPEED_DOUBLE
                                / cdDriveSpeed);
   d = fifoRefillAccum / 100;
   fifoRefillAccum %= 100;
   return d;
}

/* Trace I2CNTRL data-enable (bit 2) edges.  Called after any store that
 * touches the I2CNTRL low byte (word or byte path -- the GPU ISR uses
 * 32-bit stores that arrive as two word writes, but byte stores exist). */
static void CDTraceI2SWrite(void)
{
   int enable = (cdRam[I2CNTRL + 3] & 0x04) ? 1 : 0;

   if (enable != cdTraceLastI2SEnable)
   {
      CDTracePush(CD_TRACE_I2S_CTRL,
                  (uint16_t)((cdRam[I2CNTRL + 2] << 8) | cdRam[I2CNTRL + 3]),
                  block);
      cdTraceLastI2SEnable = enable;
   }
}

/* Public wrapper for jagcd_hle.c -- the HLE CD_read path performs a
 * synchronous seek+transfer in one call (no separate seek-delay state
 * machine like the real-BIOS path), so a single HLE_READ event carrying
 * the target LBA is the start+done pair for that path. */
void CDTraceHLERead(uint32_t lba, uint16_t byteCountTrunc)
{
   CDTracePush(CD_TRACE_HLE_READ, byteCountTrunc, lba);
}

/* --- HLE audio-streaming control (called from jagcd_hle.c) ---
 *
 * On real hardware a CD_read leaves the drive PLAYING at the position
 * just past the transferred data; games then call CD_I2S_enable (+
 * CD_set_DAC_mode) with SMODE in slave mode and the drive's I2S stream
 * flows through the SSI into the DSP (Primal Rage plays its Probe-logo
 * music exactly this way).  The HLE CD_read is a synchronous memcpy that
 * never touches the drive state, so without these hooks the SSI head
 * stays unprimed, ButchIsReadyToSend() is false, and the DSP receives
 * ZERO slave-mode SSI interrupts — its audio driver is unclocked and the
 * game wedges waiting for playback progress.
 *
 * CDROMHLEStartAudio(): position the drive/SSI head at `lba` and start
 * playback, mirroring the seek-completion priming in BUTCHExec.
 * CDROMHLESetAudioPlaying(): pause/resume gate — SetSSIWordsXmittedFromButch
 * outputs silence (but keeps clocking the DSP) while cdPlaying is false. */
void CDROMHLEStartAudio(uint32_t lba)
{
   if (!haveCDGoodness)
      return;

   block = lba;
   if (!CDIntfReadBlock(lba, ssiBuf))
      memset(ssiBuf, 0, 2352);
   ssiBlock  = lba + 1;
   ssiBufPtr = 0;
   cdPlaying = true;
   seekDelay = 0;
   CD_LOG("HLE audio start: LBA %u (SSI head primed)\n", lba);
}

void CDROMHLESetAudioPlaying(int playing)
{
   cdPlaying = playing ? true : false;
   CD_LOG("HLE audio playing=%d (block=%u)\n", playing, block);
}

/* An HLE data CD_read models the real BIOS CD_read's DSA seek to the
 * data track: any in-progress audio play ends there, and the BUTCH FIFO
 * stops feeding the GPU CD ISR (the HLE stream delivers the data
 * itself, so the ISR has nothing to drain).  Leaving the CDDA feed
 * running double-drives the transfer: the game's GPU CD ISR keeps
 * firing on FIFO-ready edges and drains stale audio words to its live
 * write pointer inside the destination buffer, stomping freshly
 * streamed data (Iron Soldier 2 match load: the ISR corrupted 13 bytes
 * at the checksum base -> checksum mismatch -> infinite retry loop).
 * Audio resumes when the game calls CD_I2S_enable (hle_post_read_lba)
 * or issues a bit-31 just-seek CD_read, exactly as on hardware. */
void CDROMHLEDataReadBegin(void)
{
   if (!haveCDGoodness)
      return;
   cdPlaying     = false;
   fifoDataReady = false;
   fifoReadCount = 0;
   CD_LOG("HLE data read: audio play + FIFO feed stopped\n");
}

/* Park the drive head at `lba`, paused, with the data-path framing the
 * seek handler establishes (cdBufPtr=2: BUTCH's one-word capture skew —
 * see the $12xx Goto Frame handler).  Used by the BIOS boot-stub
 * shortcut: the real BIOS leaves the head just past the boot executable
 * it streamed, and games may resume with a bare Unpause ($0500) and no
 * seek (Battle Morph) — data must then flow from here, not block 0. */
void CDROMSetHeadPosition(uint32_t lba)
{
   if (!haveCDGoodness)
      return;

   block = lba;
   if (!CDIntfReadBlock(block, cdBuf))
      memset(cdBuf, 0, 2352);
   cdBufPtr = 2;
   memcpy(ssiBuf, cdBuf, sizeof(ssiBuf));
   ssiBufPtr = 0;
   ssiBlock  = lba + 1;
   cdPlaying = false;
   fifoDataReady = false;
   fifoReadCount = 0;
   seekDelay = 0;
   LOG_INF("[CD] Head parked at LBA %u (post-boot-stub position)\n", lba);
}

void CDTraceDump(void)
{
   uint32_t i, n, start;
   const CDTraceEntry *e;

   if (cdTraceCount == 0)
   {
      LOG_INF("[CD-TRACE] ring empty (trace was off, or no events recorded yet)\n");
      return;
   }

   n = cdTraceCount;
   start = (cdTraceHead - n) & (CD_TRACE_SIZE - 1);
   LOG_INF("[CD-TRACE] dumping %u entries (ring capacity %u)\n", n, (unsigned)CD_TRACE_SIZE);
   for (i = 0; i < n; i++)
   {
      e = &cdTrace[(start + i) & (CD_TRACE_SIZE - 1)];
      LOG_INF("[CD-TRACE] #%u tick=%u kind=%-10s value=$%04X block=%u\n",
              i, e->tick, cdTraceKindName[e->kind], e->value, e->block);
   }
}

/* Read-only accessor for crash_detect.c's cd_seek_wedge watchdog. Any
 * pointer may be NULL. */
void CDROMDiagGetSeekWedgeState(uint32_t *seekStarts, uint32_t *seekDones,
                                uint32_t *fifoDrains)
{
   if (seekStarts) *seekStarts = cdSeekStartCount;
   if (seekDones)  *seekDones  = cdSeekDoneCount;
   if (fifoDrains) *fifoDrains = cdFifoDrainCount;
}

uint32_t CDROMDiagGetFirstSeekBlock(void)
{
   return diag_firstSeekBlock;
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
   ssiBufPtr = 2352;
   ssiBlock = 0;
   fifoDataReady = false;
   dsaResponseReady = false;
   isMultiWordResponse = false;
   txBufferEmpty = true;
   cdPlaying = false;
   seekDelay = 0;
   fifoReadCount = 0;
   fifoFillDelay = 0;
   fifoRefillAccum = 0;
   cdDriveSpeed = CD_SPEED_DOUBLE;   /* power-on default, see the constant */
   cdPrevShouldIRQ = false;
   dsaQueueHead = 0;
   dsaQueueTail = 0;
   dsaQueueCount = 0;
   dsaResponseDelay = 0;
   cdTraceLastI2SEnable = -1;

   diag_butchExecCalls = 0;
   diag_fifoIRQsFired = 0;
   diag_dsaIRQsFired = 0;
   diag_fifoReads = 0;
   diag_seekCommands = 0;
   diag_butchGlobalDisabled = 0;

   // Trace ring + wedge-watchdog counters are diagnostic state, not part
   // of the emulated hardware -- reset per game load same as the diag_*
   // counters above, but cdTraceEnabled itself is a runtime toggle (core
   // option / env var) and must NOT be reset here or every reset would
   // silently drop the user's VJ_CD_TRACE=1 / core-option setting.
   cdTraceHead = 0;
   cdTraceCount = 0;
   cdSeekStartCount = 0;
   cdSeekDoneCount = 0;
   cdFifoDrainCount = 0;
   diag_firstSeekBlock = 0xFFFFFFFFu;

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

void CDROMDiagSummary(void)
{
   LOG_INF("[CD-DIAG] butchExec=%u globalDisabled=%u seeks=%u "
           "fifoIRQs=%u dsaIRQs=%u fifoReads=%u "
           "cdPlaying=%d fifoReady=%d i2sEn=%d\n",
           diag_butchExecCalls, diag_butchGlobalDisabled,
           diag_seekCommands, diag_fifoIRQsFired, diag_dsaIRQsFired,
           diag_fifoReads, cdPlaying, fifoDataReady,
           (cdRam[I2CNTRL + 3] & 0x04) != 0);
}

void CDROMDiagGetCounters(uint32_t *butchExec,
                          uint32_t *fifoIRQs,
                          uint32_t *dsaIRQs,
                          uint32_t *fifoReads,
                          uint32_t *seeks,
                          uint32_t *globalDisabled,
                          uint32_t *hleBytes)
{
   if (butchExec)      *butchExec      = diag_butchExecCalls;
   if (fifoIRQs)       *fifoIRQs       = diag_fifoIRQsFired;
   if (dsaIRQs)        *dsaIRQs        = diag_dsaIRQsFired;
   if (fifoReads)      *fifoReads      = diag_fifoReads;
   if (seeks)          *seeks          = diag_seekCommands;
   if (globalDisabled) *globalDisabled = diag_butchGlobalDisabled;
   if (hleBytes)       *hleBytes       = 0;  /* HLETransferTick removed */
}


//
// This approach is probably wrong, but let's do it for now.
// What's needed is a complete overhaul of the interrupt system so that
// interrupts are handled as they're generated--instead of the current
// scheme where they're handled on scanline boundaries.
//
void BUTCHExec(uint32_t cycles)
{
   uint32_t butchWrite;

   if (!haveCDGoodness)
      return;

   diag_butchExecCalls++;

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
         // upon reaching the target position, but the FIFO only fills when
         // I2CNTRL bit 2 (I2S data enable) is set. The BIOS clears bit 2
         // at the start of CD_read, so FIFO data is NOT instantly available
         // at seek completion — it only becomes available after the GPU ISR
         // processes the DSARX response and re-enables I2CNTRL bit 2.
         DSAQueuePush(0x0100);
         cdSeekDoneCount++;
         CDTracePush(CD_TRACE_SEEK_DONE, 0x0100, block);
         cdPlaying = true;
         {
            bool i2sDataEnabled = (cdRam[I2CNTRL + 3] & 0x04) != 0;
            if (i2sDataEnabled && FIFOFeedAllowed())
            {
               fifoDataReady = true;
               fifoReadCount = 0;
               CDTracePush(CD_TRACE_FIFO_FILL, 0, block);
            }
            else
            {
               fifoDataReady = false;
               fifoFillDelay = FIFO_FILL_TICKS;
            }
         }

         CD_LOG("BUTCHExec: seek complete block=%u (MSF %02u:%02u:%02u) -- queued $0100, FIFO+playback active\n",
                block, min, sec, frm);
      }
   }

   // FIFO refill countdown — simulates I2S filling the 16-deep FIFO.
   // After the GPU ISR drains it (16 word-reads), we wait before setting
   // half-full again. Also handles initial fill after play starts.
   // Only refill when I2CNTRL bit 2 (I2S data enable) is set — the BIOS
   // clears this at the start of CD_read and the GPU ISR re-enables it
   // after processing the DSARX seek response.
   if (fifoFillDelay > 0)
   {
      bool i2sDataEnabled = (cdRam[I2CNTRL + 3] & 0x04) != 0;
      fifoFillDelay--;
      if (fifoFillDelay == 0 && cdPlaying && i2sDataEnabled && FIFOFeedAllowed())
      {
         fifoDataReady = true;
         fifoReadCount = 0;
         CDTracePush(CD_TRACE_FIFO_FILL, 0, block);
         CD_LOG("BUTCHExec: FIFO half-full -- ready for GPU ISR\n");
      }
      else if (fifoFillDelay == 0 && cdPlaying && (!i2sDataEnabled || !FIFOFeedAllowed()))
      {
         fifoFillDelay = 1;  // Retry next tick
      }
   }

   // DSA response turnaround countdown — runs independently of the BUTCH
   // interrupt enables below (response arrival is drive-side; the CD BIOS
   // clears BUTCH bit 0 during CD_read and still expects responses).
   if (dsaResponseDelay > 0)
   {
      dsaResponseDelay--;
      if (dsaResponseDelay == 0 && dsaQueueCount > 0)
         dsaResponseReady = true;
   }

   /* Removed: HLETransferTick shortcut for BIOS strategy.  It existed to
    * compensate for the GPU CD ISR's PTRPOS divergence on the FIFO path
    * back when BUTCH wasn't ticking and the GPU IRQ chain was broken.
    * With BUTCHExec wired in, the IRQ-line fix routing to GPU IRQ1, and
    * the recent CPU/GPU/DSP/IRQ accuracy work, the native FIFO path now
    * delivers correct transfers — and the HLE shortcut became actively
    * harmful (Primal Rage's BIOS path was wedging at $22002200 because
    * the HLE shortcut and the now-functional FIFO IRQs both fought for
    * the same data area).  Removing it flips Primal Rage to PASS. */
   butchWrite = GET32(cdRam, BUTCH);

   if (!(butchWrite & 0x01))       // Global interrupt enable not set
   {
      diag_butchGlobalDisabled++;
      return;
   }

   {
      bool shouldIRQ = false;

      if ((butchWrite & 0x02) && fifoDataReady)
         shouldIRQ = true;
      if ((butchWrite & 0x20) && dsaResponseReady)
         shouldIRQ = true;

      if (shouldIRQ)
      {
         if ((butchWrite & 0x02) && fifoDataReady)
            diag_fifoIRQsFired++;
         if ((butchWrite & 0x20) && dsaResponseReady)
            diag_dsaIRQsFired++;

         JERRYSetPendingIRQ(IRQ2_EXTERNAL);
         /* CD BIOS clears BUTCH bit 0 before issuing CD_read, so the 68K
          * side of the EXT1 line is dormant during transfers. The CD
          * data path is GPU-side: BUTCH -> JERRY external latch -> GPU
          * IRQ **1** (the DSP/JERRY-sourced GPU interrupt, vector
          * $F03010 = int# * 16 per JTRM). The CD BIOS installs its
          * CD-data ISR entry stub at $F03010, enables only G_FLAGS
          * INT_ENA1 ($20), acks via INT_CLR1 (G_FLAGS bit 10) and
          * re-arms the JERRY external latch (J_INT = $0101) in the ISR
          * epilogue — all four fingerprints of IRQ1, none of IRQ0.
          * Asserting GPUIRQ_CPU (IRQ0) here left the latch permanently
          * masked (the BIOS never sets INT_ENA0): the ISR never ran,
          * never consumed the DSA response, never set I2CNTRL bit 2, so
          * the FIFO never filled — the BIOS-mode boot deadlock in
          * Primal Rage / Highlander / Iron Soldier 2.
          * Asserting m68k IRQ2 here lands on a stale 68K vector when the
          * BIOS hasn't installed its EXT1 trampoline (Hover Strike,
          * Primal Rage), corrupting the stack with a bogus return address.
          * Keep the JERRY pending bit (so JINTCTRL reads see it) but skip
          * BLIND m68k_set_irq dual-delivery.
          *
          * The GPU latch assert lives INSIDE the edge gate below: real
          * BUTCH raises its IRQ line once per condition onset (FIFO
          * crossing half-full, DSA response arriving), and TOM latches
          * that edge.  Asserting every BUTCHExec tick while the level
          * holds re-latches GPU IRQ1 immediately after every ISR ack —
          * an IRQ storm that livelocks the GPU (RTI -> instant
          * redispatch), and guarantees a stale pending latch whenever
          * the 68K stop/reprograms/restarts the GPU.  That pending latch
          * dispatched at restart BEFORE the new program's `movei
          * #$F04000,r31` stack init, pushing the return address through
          * a stale r31 into GPU code — Hover Strike's B-skip lockup
          * (press B at the boot logo: the CD driver's timer stub movei
          * at $F03110 got its operand overwritten, the game's schedule
          * clock at $5DC2E froze, and the 68K polled it forever). */

         /* 68K delivery IS required when software asks for it: gate on
          * JINTCTRL's external-interrupt enable, exactly like the JERRY
          * timer IRQs (jerry.c JERRYPIT1Callback).  The CD BIOS leaves
          * the enable clear during boot (no EXT trampoline installed
          * yet -- blind delivery corrupted the 68K stack in Hover
          * Strike / Primal Rage), but the CD_jeri/DSP flow ("Jaguar
          * CD-ROM" doc p.7/p.12: SMODE=$14, just-seek CD_read + CD_ack
          * for Red Book audio) installs a 68K-side handler and enables
          * the JERRY external interrupt; CD_ack then waits for that
          * handler to consume the DSA response.  Suspected missing link
          * for Primal Rage CD-audio (see
          * docs/cd-diagnosis/primal-rage-cdda-diagnosis.md). */
         /* Edge-triggered: deliver once per condition onset, not per tick.
          * Level delivery every halfline is an IRQ storm that starves the
          * 68K (4 matrix titles regressed to wall-clock hangs when this
          * was level-triggered). */
         /* Edge-paced (see the BUTCH+2 ack comment in CDROMWriteWord):
          * assert the GPU latch once per service cycle, not per tick. */
         if (!cdPrevShouldIRQ)
            GPUSetIRQLine(GPUIRQ_DSP, ASSERT_LINE);

         if (!cdPrevShouldIRQ)
         {
            int tomEna = TOMIRQEnabled(IRQ_DSP);
            int jerEna = JERRYIRQEnabled(IRQ2_EXTERNAL) ? 1 : 0;
            /* CDDA-DIAG: log every rising edge with gate states so device
             * logs show WHY delivery did or didn't happen. */
            static uint32_t cddaEdgeCount = 0;
            cddaEdgeCount++;
            if (cddaEdgeCount <= 20 || (cddaEdgeCount % 5000) == 0)
               LOG_DBG("[CDDA] BUTCH IRQ edge #%u tom=%d jerryExt=%d "
                       "fifoReady=%d dsaReady=%d butch=$%08X tick=%u%s\n",
                       cddaEdgeCount, tomEna, jerEna,
                       fifoDataReady, dsaResponseReady, butchWrite,
                       diag_butchExecCalls,
                       (tomEna && jerEna) ? " -> 68K IPL2" : "");
            if (tomEna && jerEna)
               m68k_set_irq(2);
         }
      }
      /* The edge is only "consumed" if a GPU that can capture IRQs saw
       * it — a halted OR single-step-parked GPU's interrupt logic is not
       * clocked (GPUSetIRQLine drops asserts in both states).  Keeping
       * the tracker false across such a window means the still-high line
       * re-asserts on the first tick after the 68K lets the GPU run;
       * marking it true there would swallow the edge forever and stall
       * the transfer (the press-B@550/650 variant of the Hover Strike
       * lockup; Iron Soldier 2's post-load engine bring-up drives the
       * GPU through a SINGLE_STEP handshake and lost the FIFO-ready
       * edge the same way — the streaming ISR never got its first IRQ). */
      cdPrevShouldIRQ = shouldIRQ && GPUCanCaptureIRQ();
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
      // DSCNTRL read: returns stored value. On real hardware (MiSTer butch.v),
      // reading DSCNTRL transitions the serial bus from "pending" to "sending".
      // In our emulation serial transmission is instantaneous, so bit 12 (TX
      // buffer empty) stays at its current state. The GPU ISR reads DSCNTRL as
      // part of its handshake but does NOT use bit 12 — it only cares about
      // the DS_DATA response value. Clearing txBufferEmpty here would race with
      // the 68K's DSA_tx polling loop that checks BUTCH+2 bit 12.
      data = GET16(cdRam, offset);
      // Real hardware clears the DSA pending interrupt latch when the
      // CPU/GPU reads DSCNTRL — that's the documented "DSA ack" semantic
      // (cdrom.c:1643 BIOS listing comment: "Clears DSA pending interrupt").
      // Without this clear, dsaResponseReady stays true forever after the
      // first seek, so BUTCHExec re-fires GPU IRQ0 every halfline (2.9 M
      // spurious IRQs across a 6 K-frame Primal Rage run, which keeps the
      // GPU thrashing in its DSARX-handler ISR while the 68K spins on a
      // mailbox the GPU can't write to because the ISR never returns
      // long enough to do real work).
      dsaResponseReady = false;
      /* If a queued response word is still undelivered, re-assert RX-full
       * after another serial-word delay: the ack clears the interrupt
       * latch, but the drive MCU still has data to hand over, and on real
       * hardware bit 13 (rec buffer full) rises again when the next word
       * lands in the receive buffer. */
      if (dsaQueueCount > 0 && dsaResponseDelay <= 0)
         dsaResponseDelay = DSA_RESPONSE_DELAY_TICKS;
   }
   else if (offset == I2CNTRL || offset == I2CNTRL + 2)
   {
      data = GET16(cdRam, offset);
      /* I2CNTRL bit 4 = FIFO-not-empty status. Now that HLETransferTick is
       * gone (BIOS runs the native FIFO drain loop), the bit can always
       * reflect fifoDataReady. */
      if (haveCDGoodness && fifoDataReady)
         data |= (1 << 4);
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
            if (!fifoDataReady && FIFOFeedAllowed())
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
         if (FIFOFeedAllowed())
            fifoDataReady = true;
         CD_LOG("Play Title response consumed -- playback and FIFO now active\n");
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

         /* $0300 short TOC: BIOS polls DS_DATA for $03xx responses
          * (echoes the command prefix).  Each of the 5 response words has
          * high byte $03 and low byte = session info value.  The BIOS
          * checks bit 0 of each word: if set, more data follows; if clear,
          * TOC transfer is complete.  After all 5 data words, return $0300
          * as end-of-data marker (bit 0 clear). */
         if (cdPtr < 5)
         {
            data = CDIntfGetSessionInfo(cdCmd & 0xFF, cdPtr);
            CD_LOG("TOC-03: sess_param=%u cdPtr=%u data=$%04X\n",
                   cdCmd & 0xFF, cdPtr, data);
            if (data == 0xFF)
               data = 0x0400;
            else
            {
               data = 0x0300 | (data & 0xFF);
               cdPtr++;
            }
         }
         else
         {
            data = 0x0300;  /* end-of-data: high byte $03, bit 0 clear */
         }
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
         if (FIFOFeedAllowed())
         {
            fifoDataReady = true;
            fifoReadCount = 0;
         }
         CD_LOG("Seek response $0100 consumed (direct) -- cdPlaying=true\n");
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
      else if ((cdCmd & 0xFF00) == 0x5000)		// Disc status poll
         data = 0x0300 | (CDIntfGetNumSessions() & 0xFF);
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

      CDTracePush(CD_TRACE_DSA_RX, data, block);
   }
   else if (offset == DS_DATA && !haveCDGoodness)
      data = 0x0400;								// No CD interface present, so return error
   else if (offset >= FIFO_DATA && offset <= FIFO_DATA + 3)
   {
      diag_fifoReads++;
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
      /* Deliver data whenever the drive is playing, not only while
       * fifoDataReady.  The CD BIOS GPU ISR reads NINE longwords (18 word
       * reads) per invocation in sentinel-scan mode -- more than
       * FIFO_DRAIN_READS (16).  Gating data on fifoDataReady made word
       * reads 17-18 of every invocation return $0000, which reset the
       * ISR's 16-consecutive-sentinel counter ($F03234: moveq #16,r26) on
       * EVERY invocation, so the sync mark could never be accepted.  Real
       * BUTCH reads past the fill level return stale FIFO content, never
       * zeros; delivering the next sequential words is the closest linear
       * approximation.  fifoDataReady still paces the half-full IRQ
       * (drain/refill below) exactly as before. */
      if (haveCDGoodness && (fifoDataReady || cdPlaying))
      {
         if (cdBufPtr >= 2352 && cdPlaying)
         {
            block++;
            CDIntfReadBlock(block, cdBuf);
            cdBufPtr = 0;
         }
         if (cdBufPtr < 2352)
         {
            data = (cdBuf[cdBufPtr + 1] << 8) | cdBuf[cdBufPtr];
            cdBufPtr += 2;
         }
         if (fifoDataReady)
         {
            fifoReadCount++;
            if (fifoReadCount >= FIFO_DRAIN_READS)
            {
               fifoDataReady = false;
               fifoFillDelay = CDROMNextRefillDelay();
               cdFifoDrainCount++;
               CDTracePush(CD_TRACE_FIFO_DRAIN, (uint16_t)fifoReadCount, block);
            }
         }
      }
   }
   else if (offset >= FIFO_DATA + 4 && offset <= FIFO_DATA + 7)
   {
      /* Same delivery rule as FIFO_DATA above (I2SDAT2 pops the same
       * sequential stream; see that comment). */
      if (haveCDGoodness && (fifoDataReady || cdPlaying))
      {
         if (cdBufPtr >= 2352 && cdPlaying)
         {
            block++;
            CDIntfReadBlock(block, cdBuf);
            cdBufPtr = 0;
         }
         if (cdBufPtr < 2352)
         {
            data = (cdBuf[cdBufPtr + 1] << 8) | cdBuf[cdBufPtr];
            cdBufPtr += 2;
         }
         if (fifoDataReady)
         {
            fifoReadCount++;
            if (fifoReadCount >= FIFO_DRAIN_READS)
            {
               fifoDataReady = false;
               fifoFillDelay = CDROMNextRefillDelay();
               cdFifoDrainCount++;
               CDTracePush(CD_TRACE_FIFO_DRAIN, (uint16_t)fifoReadCount, block);
            }
         }
      }
   }
   else
      data = GET16(cdRam, offset);

   /* SUBCODE-DIAG: reads of the (unimplemented) subcode registers. */
   if (offset >= SBCNTRL && offset < SB_TIME + 4)
   {
      static uint32_t subReads = 0;
      subReads++;
      if (subReads <= 40 || (subReads % 10000) == 0)
         LOG_DBG("[SUBCODE] read %s+%u -> $%04X who=%u 68kpc=$%06X\n",
                 (offset >= SB_TIME) ? "SB_TIME" :
                 (offset >= SUBDATB) ? "SUBDATB" :
                 (offset >= SUBDATA) ? "SUBDATA" : "SBCNTRL",
                 offset & 3, data, who, m68k_get_reg(NULL, M68K_REG_PC));
   }

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
   /* I2CNTRL bit 4 (FIFO-not-empty) is read-only status, computed from
    * FIFO state on read -- see CDROMWriteWord below. */
   if (offset == I2CNTRL + 3)
      data &= ~0x10;
   cdRam[offset] = data;
   if (offset == I2CNTRL + 3)
      CDTraceI2SWrite();
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
      /* SUBCODE-DIAG: BUTCH bit2 = subcode frame-time int enable, bit3 =
       * SB_TIME time-match int enable.  Neither is emulated yet -- log
       * arming edges so device/headless runs show which titles depend on
       * the subcode position path (suspected FMV scene sequencing). */
      {
         static uint16_t prevSubEna = 0;
         if ((prevSubEna ^ data) & 0x0C)
            LOG_DBG("[SUBCODE] BUTCH int enables $%02X -> $%02X (frame=%d match=%d) who=%u 68kpc=$%06X\n",
                    prevSubEna & 0x7F, data & 0x7F,
                    (data >> 2) & 1, (data >> 3) & 1, who,
                    m68k_get_reg(NULL, M68K_REG_PC));
         prevSubEna = data;
      }
      SET16(cdRam, offset, data & 0x007F);  // Store only enable bits (0-6)
      /* Writing the enable word is how software services/acks the BUTCH
       * interrupt (the GPU ISR entry does a RMW here every invocation).
       * Real BUTCH re-evaluates its IRQ line after the ack: if a condition
       * (FIFO half-full, DSA response) still holds, the line rises again
       * and TOM latches a NEW edge.  Re-arm the edge detector so BUTCHExec
       * asserts once per SERVICE CYCLE — not once per condition onset
       * (starves transfers: a single missed edge kills the stream) and
       * not once per tick (IRQ storm: the GPU latch is pending at every
       * instant, so any 68K stop/reprogram/restart of the GPU dispatches
       * into the new program before its `movei #$F04000,r31` stack init
       * and pushes a return address through stale r31 into GPU code —
       * Hover Strike's B-skip lockup). */
      cdPrevShouldIRQ = false;
      CD_LOG("WriteWord BUTCH+2: data=0x%04X enables=0x%02X [PC=$%06X]\n",
             data, data & 0x7F, m68k_get_reg(NULL, M68K_REG_PC));
      return;
   }

   /* SUBCODE-DIAG: any traffic on the subcode registers (SBCNTRL $14,
    * SUBDATA $18, SUBDATB $1C, SB_TIME $20) -- currently RAM-backed
    * no-ops in this emulator. */
   if (offset >= SBCNTRL && offset < SB_TIME + 4)
   {
      static uint32_t subWrites = 0;
      subWrites++;
      if (subWrites <= 40 || (subWrites % 10000) == 0)
         LOG_DBG("[SUBCODE] write %s+%u = $%04X who=%u 68kpc=$%06X\n",
                 (offset >= SB_TIME) ? "SB_TIME" :
                 (offset >= SUBDATB) ? "SUBDATB" :
                 (offset >= SUBDATA) ? "SUBDATA" : "SBCNTRL",
                 offset & 3, data, who, m68k_get_reg(NULL, M68K_REG_PC));
   }

   /* I2CNTRL bit 4 (FIFO-not-empty) is read-only status ("When read: b4 -
    * FIFO state is not empty if 1" -- BUTCH register doc above), computed
    * from fifoDataReady in the read handler.  Games do read-modify-write
    * cycles on I2CNTRL, so a set status bit rides along in the write data;
    * storing it made the bit stick at 1 forever.  Device-traced on Dragon's
    * Lair (bios): end-of-transfer writes $0011 (RMW carrying b4), then
    * drains the FIFO with a "read FIFO_DATA; btst #4,I2CNTRL; bne" flush
    * loop that can never see empty -- 68K wedges, GPU parks in its mailbox
    * wait, video freezes after the play command.  Same silicon pattern as
    * the BUTCH+2 enable/status split above. */
   if (offset == I2CNTRL + 2)
      data &= ~0x0010;

   SET16(cdRam, offset, data);

   if (offset == I2CNTRL + 2)
      CDTraceI2SWrite();

   if (offset < UNKNOWN)  // Don't log EEPROM bus writes ($2C/$2E) — too noisy
      CD_LOG("WriteWord offset=0x%02X data=0x%04X [PC=$%06X]\n", offset, data, m68k_get_reg(NULL, M68K_REG_PC));

   // Command register
   if (offset == DS_DATA)
   {
      CD_LOG("DS_DATA write: cmd=0x%04X\n", data);
      /* CDDA-DIAG: audio-flow commands, rare -- log the first 40 and
       * then every 500th so device RetroArch logs show the play sequence
       * without a wedge dump, while a stuck game cannot flood the log.
       * $01 Play / $02 Stop / $04 Pause / $05 Unpause? / $15 Set Mode /
       * $51 Mute-Unmute / $70 Set DAC Mode. */
      {
         uint8_t hi = (uint8_t)(data >> 8);
         if (hi == 0x01 || hi == 0x04 || hi == 0x05 || hi == 0x15 ||
             hi == 0x51 || hi == 0x70)
         {
            static uint32_t cddaCmdCount = 0;
            cddaCmdCount++;
            if (cddaCmdCount <= 40 || (cddaCmdCount % 500) == 0)
               LOG_DBG("[CDDA] DSA cmd $%04X #%u tick=%u block=%u i2s=$%02X\n",
                       data, cddaCmdCount, diag_butchExecCalls, block,
                       cdRam[I2CNTRL + 3]);
         }
      }
      cdCmd = data;
      txBufferEmpty = true;  // Per MiSTer: set bit 12 on command write
      CDTracePush(CD_TRACE_DSA_TX, data, block);

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
            /* The drive still answers a no-op Goto with Found ($0100) —
             * every $12xx elicits a response on real hardware.  Skipping
             * it entirely left drivers that wait for the seek-complete
             * DSA IRQ hanging: Philia seeks to the block it is already
             * streaming ($1006/$1105/$123E -> LBA 27287 while at 27287),
             * its GPU driver waits for Found, never drains the FIFO
             * again, and the game freezes (cd_seek_wedge with all seeks
             * done).  Queue the response WITHOUT restarting the seek
             * state machine — the guard's purpose (don't cycle
             * dsaResponseReady/seekDelay, don't disturb the in-flight
             * stream, keep cdBufPtr) is preserved; DSAQueuePush paces
             * delivery like any other immediate DSA answer. */
            DSAQueuePush(0x0100);
         }
         else
         {
            diag_seekCommands++;
            cdSeekStartCount++;
            CDTracePush(CD_TRACE_SEEK_START, data, newBlock);
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
      else if ((data & 0xFF00) == 0x0400 || (data & 0xFF00) == 0x0500 ||
               (data & 0xFF00) == 0x1500 || (data & 0xFF00) == 0x1800 ||
               (data & 0xFF00) == 0x5000 || (data & 0xFF00) == 0x5400 ||
               (data & 0xFF00) == 0x7000)
      {
         /* Single-word, side-effect-free responses: synthesize NOW and
          * queue them, like real hardware's DSA RX path.  Synthesizing on
          * READ from the single cdCmd latch loses a response whenever a
          * game sends two commands back-to-back without reading DS_DATA
          * in between -- device-traced on Baldies (bios): $7001 (Set DAC
          * Mode) immediately followed by $150A (Set Mode) dropped the
          * $70nn echo and the game wedged polling for it. */
         uint16_t resp;
         if ((data & 0xFF00) == 0x0400 || (data & 0xFF00) == 0x0500)
            /* Pause / Pause-Release complete with the error-status word
             * $04nn, nn = error code, $00 = none -- i.e. $0400 is the
             * generic "done, no error" ack in the Philips CDD-family DSA
             * protocol, NOT an error indicator.  Ground truth: Primal
             * Rage's own 68K CD driver (RAM $BE42, from the Atari CD
             * library) masks the response to its high byte and treats
             * ONLY $04xx as success for Stop/Mute/Pause/Unpause-class
             * commands, and its multi-word collector ($C20C) uses an
             * $04xx word as the end-of-response terminator.  A previous
             * change made these complete with Found ($0100) after
             * misreading mister_ground_truth.h's DSA_RSP_ERROR -- that
             * made the game flag every Pause/Unpause as failed. */
            resp = 0x0400;
         else if ((data & 0xFF00) == 0x1500)
            resp = 0x1700 | (data & 0xFF);                    /* Mode Status */
         else if ((data & 0xFF00) == 0x1800)
            resp = 0x0143;                                    /* Spun Up     */
         else if ((data & 0xFF00) == 0x5000)
            resp = 0x0300 | (CDIntfGetNumSessions() & 0xFF);  /* Disc status */
         else if ((data & 0xFF00) == 0x5400)
            resp = 0x5400 | (CDIntfGetNumSessions() & 0xFF);  /* # sessions  */
         else
            resp = data;                                      /* $70nn echo  */
         DSAQueuePush(resp);
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
         CDTracePush(CD_TRACE_STOP, 0x0200, block);
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
            uint32_t discTotal;
            frm = newFrm;
            block = newBlock;

            discTotal = CDIntfGetDiscTotalSectors();
            if (discTotal > 0 && block >= discTotal)
            {
               uint32_t redirectLBA = CDIntfGetSession2GameDataLBA();
               CD_LOG("Out-of-range seek: block=%u exceeds disc size %u "
                      "(MSF %02u:%02u:%02u). Redirecting to session 2 game data at LBA %u\n",
                      block, discTotal, min, sec, frm, redirectLBA);
               block = redirectLBA;
            }

            CDIntfReadBlock(block, cdBuf);
            /* Start the word stream ONE 16-bit word into the sector, not at
             * byte 0.  BUTCH assembles its 32-bit FIFO entries from the I2S
             * word stream with a one-word capture skew relative to sector
             * data, and Jaguar CD discs are mastered for exactly that
             * grouping: the boot-stub sync mark (16 repeats of the D1
             * sentinel, e.g. Primal Rage DDL9=$44444C39 at LBA 117224 byte
             * 42, Baldies CINE=$43494E45 at LBA 20958 byte 46) always sits
             * at byte offset == 2 (mod 4).  Grouped from word 1, entries
             * assemble as (sentinel_hi<<16)|sentinel_lo for exactly 16
             * consecutive longwords -- the count the CD BIOS GPU ISR
             * requires at $F03248 (subq #1,r26 from 16) -- and the game
             * code that follows begins exactly on an entry boundary.
             * Grouped from word 0 (the old behaviour) every entry reads
             * ($4C39xxxx) and the ISR scans the disc forever: the
             * "streaming wall".  One skipped word once per seek; the
             * stream stays linear from byte 2 onward. */
            cdBufPtr = 2;
            /* The SSI (DSP audio) head starts sample-aligned at byte 0:
             * the one-word capture skew above belongs to BUTCH's FIFO
             * entry assembly, not to the raw I2S word stream -- Red Book
             * samples are framed L/R from the start of the sector.
             * ssiBuf already holds this sector, so the next sector the
             * head will need from disc is block + 1. */
            ssiBlock = block + 1;
            memcpy(ssiBuf, cdBuf, sizeof(ssiBuf));
            ssiBufPtr = 0;
            if (diag_firstSeekBlock == 0xFFFFFFFFu)
               diag_firstSeekBlock = block;
            CD_LOG("Seek started: block=%u (MSF %02u:%02u:%02u), delay=%d ticks\n",
                   block, min, sec, frm, SEEK_DELAY_TICKS);
         }
      }
      else if ((data & 0xFF00) == 0x1500)			// Set Mode
      {
         /* Latch the drive speed.  The payload's low bits are a one-based
          * speed CODE (1 = single, 2 = double) and bit 3 selects data(1) /
          * audio(0) -- see the cdDriveSpeed comment near the top of this
          * file for the manual citation (p.10 sec 2.7.7) and the CD BIOS
          * disassembly that establishes the wire format.  The $17nn Mode
          * Status echo is queued by the response path above; the BIOS
          * retries the whole command until that echo matches, so the echo
          * must keep reflecting the payload verbatim.
          *
          * An unrecognised code leaves the speed alone rather than
          * guessing: every code the BIOS can emit is 1 or 2, so anything
          * else came from a game's own driver and we have no ground truth
          * for it. */
         uint32_t code = data & 0x07;
         const char *why = "unchanged (unknown code)";

         if (code == CD_SPEED_SINGLE || code == CD_SPEED_DOUBLE)
         {
            cdDriveSpeed = code;
            why = (code == CD_SPEED_SINGLE) ? "single (1x)" : "double (2x)";
         }

         /* Census log: rare command, capped, so it is safe to emit at INFO
          * unconditionally (same policy as the [CDDA] line above). */
         {
            static uint32_t setModeCount = 0;
            setModeCount++;
            if (setModeCount <= 40 || (setModeCount % 500) == 0)
               LOG_DBG("[CD-MODE] Set Mode $%04X #%u -> speed=%s mode=%s "
                       "rate=%lu B/s tick=%u block=%u\n",
                       data, setModeCount, why,
                       (data & 0x08) ? "data" : "audio",
                       (unsigned long)(352800u / CD_SPEED_DOUBLE * cdDriveSpeed),
                       diag_butchExecCalls, block);
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
                  uint16_t opcode;
                  uint16_t addr;
                  busCmd >>= 2;					// Because we ORed bit 2, we need to shift right by 2
                  cmdTx = false;

                  CD_LOG("BusCmd: 0x%03X [PC=$%06X]\n", busCmd, m68k_get_reg(NULL, M68K_REG_PC));

                  // NM93C14 command decoding:
                  // 9-bit command = start(1) + opcode(2) + address(6)
                  // Opcodes: 10=READ, 01=WRITE, 11=ERASE, 00=special
                  opcode = (busCmd >> 6) & 0x03;
                  addr = busCmd & 0x3F;

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

   /* SSI head, not the FIFO cursor -- see ssiBuf declaration. */
   ssiBufPtr += 2;

   if (ssiBufPtr >= 2352)
   {
      CDIntfReadBlock(ssiBlock, ssiBuf);
      ssiBlock++;
      ssiBufPtr = 0;
   }

   // CD audio is 16-bit stereo, little-endian on disc (Red Book format)
   // The Jaguar expects right channel in upper 16 bits, left in lower 16
   return (ssiBuf[ssiBufPtr + 1] << 8) | ssiBuf[ssiBufPtr + 0];
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
   // instead of the software register bit. The SSI head's sector buffer is
   // loaded during seek and contains valid data until fully consumed.
   if (haveCDGoodness && ssiBufPtr < 2352)
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
      CD_LOG("SSI xmit #%u: ssiBufPtr=%u ssiBlock=%u cdPlaying=%d\n",
             ssiXmitCount, ssiBufPtr, ssiBlock, cdPlaying);

   /* A paused/stopped/seeking drive outputs silence and holds position;
    * the I2S bit clock keeps running (the DSP synth is clocked off these
    * interrupts in slave mode) but no disc data moves.  Without this
    * gate the head streamed forever -- through pauses and past track
    * ends into data sectors, which a game with its CD mix enabled plays
    * as full-scale noise ("loud clipped static"). */
   if (!cdPlaying || seekDelay > 0)
   {
      lrxd = 0;
      rrxd = 0;
      return;
   }

   /* Safety: if the head is somehow past the sector (e.g. I2S enabled
    * before any seek loaded ssiBuf), refill before reading. */
   if (ssiBufPtr >= 2352)
   {
      CDIntfReadBlock(ssiBlock, ssiBuf);
      ssiBlock++;
      ssiBufPtr = 0;
   }

   // Red Book audio is interleaved 16-bit little-endian stereo samples,
   // LEFT first: bytes [ptr+0..1] = left, [ptr+2..3] = right.  The drive
   // frames the I2S word stream on sample boundaries, so unlike the BUTCH
   // FIFO data path (cdBufPtr, one-word capture skew) the audio head is
   // sample-aligned from byte 0 of the sector.
   lrxd = (ssiBuf[ssiBufPtr + 1] << 8) | ssiBuf[ssiBufPtr + 0];
   rrxd = (ssiBuf[ssiBufPtr + 3] << 8) | ssiBuf[ssiBufPtr + 2];

   // Advance by 4 bytes (one stereo sample).  Uses the SSI head's own
   // cursor -- see ssiBuf declaration for why this must not share
   // cdBufPtr with the FIFO read path.  Refill eagerly on exhaustion so
   // ButchIsReadyToSend() (ssiBufPtr < 2352) stays true across sector
   // boundaries while the drive plays.
   ssiBufPtr += 4;
   if (ssiBufPtr >= 2352)
   {
      CDIntfReadBlock(ssiBlock, ssiBuf);
      ssiBlock++;
      ssiBufPtr = 0;
   }
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

/* Size of the two staging buffers (`static uint8_t cdBuf2[2532 + 96],
 * cdBuf3[2532 + 96];`) that the pre-STATE_VERSION_CDROM_RESTRUCTURE CDROM
 * chunk carried after `firstTime`.  The arrays were deleted with the
 * unfinished BUTCH stub, so the legacy loader has to skip them by an
 * explicit byte count rather than a sizeof. */
#define CDROM_LEGACY_STAGING_BYTES ((size_t)((2532 + 96) * 2))

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
	STATE_SAVE_BUF(buf, ssiBuf, sizeof(ssiBuf));
	STATE_SAVE_VAR(buf, ssiBufPtr);
	STATE_SAVE_VAR(buf, ssiBlock);
	STATE_SAVE_BUF(buf, dsaQueue, sizeof(dsaQueue));
	STATE_SAVE_VAR(buf, dsaQueueHead);
	STATE_SAVE_VAR(buf, dsaQueueTail);
	STATE_SAVE_VAR(buf, dsaQueueCount);
	STATE_SAVE_VAR(buf, dsaResponseDelay);
	STATE_SAVE_VAR(buf, cdDriveSpeed);

	return (size_t)(buf - start);
}

size_t CDROMStateLoad(const uint8_t *buf, uint32_t stateVersion)
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
	/* Layout fork.  Everything above is common to every format version;
	 * from here the pre-CD-support layout (STATE_VERSION_CDROM_RESTRUCTURE)
	 * diverges completely.  Releases v2.2.0 (v1), v2.3.0/v2.3.1 (v2) and
	 * v2.3.2 (v3) wrote two staging buffers here — cdBuf2 and cdBuf3, both
	 * `uint8_t [2532 + 96]` — which the CD-support work removed and
	 * replaced with the BUTCH/FIFO/DSA/SSI working set below.  Consuming
	 * the new field list from an old blob would read 5256 bytes of stale
	 * sector data as flags and then leave the cursor 2627 bytes short,
	 * desyncing the Joystick, Memory Track and DAC chunks that follow.
	 * Skip the dead buffers by their byte count (the arrays no longer
	 * exist, so the size has to be spelled out) and start the drive from
	 * the same idle state CDROMReset() establishes.  Those cores had no
	 * working CD path — cdBuf2/cdBuf3 were only ever written by the
	 * unfinished BUTCH stub — so nothing emulated is lost. */
	if (stateVersion < STATE_VERSION_CDROM_RESTRUCTURE)
	{
		buf += CDROM_LEGACY_STAGING_BYTES;
		/* Start the drive from the same clean idle state CDROMReset()
		 * establishes.  The fields loaded above from the blob (cdRam,
		 * cdBufPtr, etc.) come from cores that had no working CD path,
		 * so any BIOS_OVRD or data-ready flag they carry is stale noise.
		 * Overwrite all drive-state fields unconditionally; the one
		 * exception is cdrom_eeprom_ram — persistent NVM backed by a
		 * file on disk that an old blob has nothing to say about, so
		 * leave whatever the session already loaded in place. */
		memset(cdRam, 0x00, 0x100);
		cdBufPtr            = 2352;
		dsaResponseReady    = false;
		isMultiWordResponse = false;
		txBufferEmpty       = true;
		cdPlaying           = false;
		seekDelay           = 0;
		fifoDataReady       = false;
		fifoReadCount       = 0;
		fifoFillDelay       = 0;
		fifoRefillAccum     = 0;
		cdPrevShouldIRQ     = false;
		memset(ssiBuf, 0x00, sizeof(ssiBuf));
		ssiBufPtr           = 2352;
		ssiBlock            = 0;
		cdTraceLastI2SEnable = -1;
	}
	else
	{
		STATE_LOAD_BUF(buf, cdrom_eeprom_ram, sizeof(cdrom_eeprom_ram));
		STATE_LOAD_VAR(buf, dsaResponseReady);
		STATE_LOAD_VAR(buf, isMultiWordResponse);
		STATE_LOAD_VAR(buf, txBufferEmpty);
		STATE_LOAD_VAR(buf, cdPlaying);
		STATE_LOAD_VAR(buf, seekDelay);
		STATE_LOAD_VAR(buf, fifoDataReady);
		STATE_LOAD_VAR(buf, fifoReadCount);
		STATE_LOAD_VAR(buf, fifoFillDelay);
		STATE_LOAD_BUF(buf, ssiBuf, sizeof(ssiBuf));
		STATE_LOAD_VAR(buf, ssiBufPtr);
		STATE_LOAD_VAR(buf, ssiBlock);
	}
	/* v3 and older states predate the DSA response queue (see
	 * STATE_VERSION_CDROM_DSA_QUEUE): leave those fields at a safe empty
	 * state instead of consuming bytes the layout never carried. */
	if (stateVersion >= STATE_VERSION_CDROM_DSA_QUEUE)
	{
		STATE_LOAD_BUF(buf, dsaQueue, sizeof(dsaQueue));
		STATE_LOAD_VAR(buf, dsaQueueHead);
		STATE_LOAD_VAR(buf, dsaQueueTail);
		STATE_LOAD_VAR(buf, dsaQueueCount);
		STATE_LOAD_VAR(buf, dsaResponseDelay);
	}
	else
	{
		memset(dsaQueue, 0, sizeof(dsaQueue));
		dsaQueueHead = 0;
		dsaQueueTail = 0;
		dsaQueueCount = 0;
		dsaResponseDelay = 0;
	}

	/* v4 and older states predate the latched drive speed.  Fall back to
	 * the power-on default rather than consuming bytes the layout never
	 * carried; a title that cares re-issues Set Mode before its next read
	 * (the CD BIOS retries the command until the $17nn echo matches, so it
	 * never assumes a speed it did not just set). */
	if (stateVersion >= STATE_VERSION_CDROM_DRIVE_SPEED)
	{
		STATE_LOAD_VAR(buf, cdDriveSpeed);
		/* CDROMNextRefillDelay divides by this, so never trust it blindly
		 * from a state blob. */
		if (cdDriveSpeed != CD_SPEED_SINGLE && cdDriveSpeed != CD_SPEED_DOUBLE)
			cdDriveSpeed = CD_SPEED_DOUBLE;
	}
	else
		cdDriveSpeed = CD_SPEED_DOUBLE;

	return (size_t)(buf - start);
}

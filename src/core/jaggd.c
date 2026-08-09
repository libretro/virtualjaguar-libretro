/*
 * jaggd.c -- Jaguar GameDrive (JagGD) flash cartridge emulation
 *
 * Implements the two things game code actually touches on a GameDrive
 * (issue #312, spec: docs/jgd-interface-notes.md):
 *
 *   1. The ASIC SPI mailbox at $F16002/$F16004/$F16005 (JERRY GPIO2),
 *      exactly as driven by RetroHQ's published gdbios_bindings.s:
 *      the GDWaitData handshake (HAVE_DATA bit 3 sequencing), the
 *      byte-exchange data register (bit 15 busy, bit 5 rx latch), and
 *      the two micro commands games issue raw: 12 (HW version) and
 *      $80 (fetch the GDBIOS blob).
 *
 *   2. 1 MB-granular bank switching of a 16 MB SDRAM image into the
 *      six cart-window pages ($8xxxxx..$Dxxxxx), reached exclusively
 *      through the GDBIOS blob ABI (functions 5/6) -- and since the
 *      emulated micro serves the blob, we serve our own ~200-byte 68K
 *      blob whose banking functions poke an emulator-defined backdoor
 *      register at $F16006.  BigPEmu does the same thing structurally
 *      (it cannot ship RetroHQ's copyrighted blob either).
 *
 * Out of scope (matches BigPEmu): GD menu, SD/FAT filesystem, GPU
 * async reads, encrypted .jgd images, .MRQ sidecars.  File/dir/async
 * blob functions return -1; serial numbers return zeros-with-success.
 */

#include "jaggd.h"

#include <stdlib.h>
#include <string.h>

#include "log.h"

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

uint8_t  jgdActive = 0;
uint8_t  jgdWriteEnabled = 0;
uint8_t  jgdPage[6] = { 0, 1, 2, 3, 4, 5 };
uint8_t *jgdROM = NULL;

static int jgdMode = JGD_MODE_AUTO;

/* SPI engine.  Transactions complete instantly, so the busy bit (15)
 * always reads clear; HAVE_DATA (bit 3) and LATCH_FULL (bit 5) are the
 * only synthesized status bits.  State machine mirrors the exact code
 * shape of gdbios_bindings.s (see JGDStatusWrite below). */
enum
{
   JGD_SPI_IDLE = 0,   /* nothing in flight */
   JGD_SPI_START,      /* $0010 seen, awaiting the $0011 handshake ack */
   JGD_SPI_CMD,        /* collecting cmd lo/hi + param-size lo/hi */
   JGD_SPI_PARAMS,     /* collecting declared parameter bytes */
   JGD_SPI_PROC,       /* header+params complete, awaiting "process it" */
   JGD_SPI_RESP        /* serving the response FIFO */
};

#define JGD_ST_SLAVE_SELECT 0x0001
#define JGD_ST_HAVE_DATA    0x0008
#define JGD_ST_PACKET_START 0x0010
#define JGD_ST_LATCH_FULL   0x0020

#define JGD_PARAM_MAX 64

static uint8_t  spiState = JGD_SPI_IDLE;
static uint8_t  spiHaveData = 0;
static uint8_t  spiLatchFull = 0;
static uint8_t  spiRxLatch = 0;
static uint8_t  spiHdrCount = 0;
static uint16_t spiCmd = 0;
static uint16_t spiParamLen = 0;
static uint16_t spiParamCount = 0;
static uint8_t  spiParams[JGD_PARAM_MAX];
static uint16_t spiFifoLen = 0;
static uint16_t spiFifoPos = 0;
static uint8_t  spiFifo[JGD_FIFO_SIZE];

/* Backdoor pages latch: op $9xxx stages pages 0-2, op $Axxx applies
 * pages 3-5 together with the staged half. */
static uint16_t pagesLatch = 0;

/* ------------------------------------------------------------------ */
/* The served GDBIOS blob                                             */
/* ------------------------------------------------------------------ */
/*
 * Hand-assembled 68000, position independent (bra.w only), poking the
 * $F16006 backdoor for banking / write enable.  Layout per the blob
 * ABI in docs/jgd-interface-notes.md section 4:
 *   +0  u16 version ($0100), +2 u16 highest function (26),
 *   +N*4 four-byte entry slot for function N (jsr N*4(a6)).
 *
 * Backdoor word encoding (JGDControlWriteWord):
 *   $8000 | page<<4 | bank   set one page          (GD_ROMSetPage)
 *   $9000 | nibbles 0-2      stage pages 0-2       (GD_ROMSetPages)
 *   $A000 | nibbles 3-5      apply pages 3-5 + staged half
 *   $B000 | enable bit 0     ROM write enable      (GD_ROMWriteEnable)
 */
static const uint8_t jgdBlob[] =
{
   /* 000 */ 0x01, 0x00,                     /* dc.w $0100  version        */
   /* 002 */ 0x00, 0x1A,                     /* dc.w 26     function count */
   /* 004 */ 0x4E, 0x75, 0x4E, 0x71,         /* f1  GD_Init:        rts    */
   /* 008 */ 0x4E, 0x75, 0x4E, 0x71,         /* f2  GD_InitGPURead: rts    */
   /* 00C */ 0x60, 0x00, 0x00, 0x5E,         /* f3  bra.w biosver (06C)    */
   /* 010 */ 0x60, 0x00, 0x00, 0x60,         /* f4  bra.w wren    (072)    */
   /* 014 */ 0x60, 0x00, 0x00, 0x6C,         /* f5  bra.w setpage (082)    */
   /* 018 */ 0x60, 0x00, 0x00, 0x84,         /* f6  bra.w setpages(09E)    */
   /* 01C */ 0x60, 0x00, 0x00, 0xA6,         /* f7  bra.w serial  (0C4)    */
   /* 020 */ 0x60, 0x00, 0x00, 0xA2,         /* f8  bra.w serial  (0C4)    */
   /* 024 */ 0x70, 0x00, 0x4E, 0x75,         /* f9  GD_CardIn: moveq #0,d0; rts */
   /* 028 */ 0x70, 0xFF, 0x4E, 0x75,         /* f10 GD_FileOpen:  moveq #-1,d0; rts */
   /* 02C */ 0x70, 0xFF, 0x4E, 0x75,         /* f11 GD_FileClose */
   /* 030 */ 0x70, 0xFF, 0x4E, 0x75,         /* f12 GD_FileSeek  */
   /* 034 */ 0x70, 0xFF, 0x4E, 0x75,         /* f13 GD_FileRead  */
   /* 038 */ 0x70, 0xFF, 0x4E, 0x75,         /* f14 GD_FileWrite */
   /* 03C */ 0x70, 0xFF, 0x4E, 0x75,         /* f15 GD_FileTell  */
   /* 040 */ 0x70, 0xFF, 0x4E, 0x75,         /* f16 GD_FileSize  */
   /* 044 */ 0x70, 0xFF, 0x4E, 0x75,         /* f17 GD_FileAsyncPos    */
   /* 048 */ 0x70, 0xFF, 0x4E, 0x75,         /* f18 GD_FileAsyncWait   */
   /* 04C */ 0x70, 0xFF, 0x4E, 0x75,         /* f19 GD_FileAsyncActive */
   /* 050 */ 0x70, 0xFF, 0x4E, 0x75,         /* f20 GD_FileInfo  */
   /* 054 */ 0x70, 0xFF, 0x4E, 0x75,         /* f21 GD_DirOpen   */
   /* 058 */ 0x70, 0xFF, 0x4E, 0x75,         /* f22 GD_DirRead   */
   /* 05C */ 0x70, 0xFF, 0x4E, 0x75,         /* f23 GD_DirClose  */
   /* 060 */ 0x4E, 0x75, 0x4E, 0x71,         /* f24 GD_Reset:       rts */
   /* 064 */ 0x4E, 0x75, 0x4E, 0x71,         /* f25 GD_SetLED:      rts */
   /* 068 */ 0x4E, 0x75, 0x4E, 0x71,         /* f26 GD_DebugString: rts */
   /* biosver: */
   /* 06C */ 0x30, 0x3C, 0x01, 0x00,         /* move.w #$0100,d0 */
   /* 070 */ 0x4E, 0x75,                     /* rts */
   /* wren: (d0 = 1 enable / 0 disable) */
   /* 072 */ 0x02, 0x40, 0x00, 0x01,         /* andi.w #1,d0     */
   /* 076 */ 0x00, 0x40, 0xB0, 0x00,         /* ori.w #$B000,d0  */
   /* 07A */ 0x33, 0xC0, 0x00, 0xF1, 0x60, 0x06, /* move.w d0,$F16006 */
   /* 080 */ 0x4E, 0x75,                     /* rts */
   /* setpage: (d0 = page<<16 | bank) */
   /* 082 */ 0x32, 0x00,                     /* move.w d0,d1     */
   /* 084 */ 0x02, 0x41, 0x00, 0x0F,         /* andi.w #$F,d1    */
   /* 088 */ 0x48, 0x40,                     /* swap d0          */
   /* 08A */ 0xE9, 0x48,                     /* lsl.w #4,d0      */
   /* 08C */ 0x02, 0x40, 0x00, 0x70,         /* andi.w #$70,d0   */
   /* 090 */ 0x82, 0x40,                     /* or.w d0,d1       */
   /* 092 */ 0x00, 0x41, 0x80, 0x00,         /* ori.w #$8000,d1  */
   /* 096 */ 0x33, 0xC1, 0x00, 0xF1, 0x60, 0x06, /* move.w d1,$F16006 */
   /* 09C */ 0x4E, 0x75,                     /* rts */
   /* setpages: (d0 = u32, one bank nibble per page, LSN = page 0) */
   /* 09E */ 0x22, 0x00,                     /* move.l d0,d1     */
   /* 0A0 */ 0x02, 0x41, 0x0F, 0xFF,         /* andi.w #$FFF,d1  */
   /* 0A4 */ 0x00, 0x41, 0x90, 0x00,         /* ori.w #$9000,d1  */
   /* 0A8 */ 0x33, 0xC1, 0x00, 0xF1, 0x60, 0x06, /* move.w d1,$F16006 */
   /* 0AE */ 0x22, 0x00,                     /* move.l d0,d1     */
   /* 0B0 */ 0xE0, 0x89,                     /* lsr.l #8,d1      */
   /* 0B2 */ 0xE8, 0x89,                     /* lsr.l #4,d1      */
   /* 0B4 */ 0x02, 0x41, 0x0F, 0xFF,         /* andi.w #$FFF,d1  */
   /* 0B8 */ 0x00, 0x41, 0xA0, 0x00,         /* ori.w #$A000,d1  */
   /* 0BC */ 0x33, 0xC1, 0x00, 0xF1, 0x60, 0x06, /* move.w d1,$F16006 */
   /* 0C2 */ 0x4E, 0x75,                     /* rts */
   /* serial: (a0 = 16-byte buffer; zeros-with-success) */
   /* 0C4 */ 0x72, 0x03,                     /* moveq #3,d1      */
   /* 0C6 */ 0x42, 0x98,                     /* .l: clr.l (a0)+  */
   /* 0C8 */ 0x51, 0xC9, 0xFF, 0xFC,         /* dbra d1,.l       */
   /* 0CC */ 0x70, 0x00,                     /* moveq #0,d0      */
   /* 0CE */ 0x4E, 0x75                      /* rts */
};

#define JGD_BLOB_SIZE ((uint16_t)sizeof(jgdBlob))

/* HW version response (cmd 12): high word firmware, low word ASIC,
 * BCD.  Any firmware >= $0111 satisfies the GD_Install gate. */
#define JGD_FW_VERSION   0x0300
#define JGD_ASIC_VERSION 0x0102

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void JGDSetMode(int mode)
{
   jgdMode = mode;
}


int JGDGetMode(void)
{
   return jgdMode;
}


static void JGDSpiReset(void)
{
   spiState = JGD_SPI_IDLE;
   spiHaveData = 0;
   spiLatchFull = 0;
   spiRxLatch = 0;
   spiHdrCount = 0;
   spiCmd = 0;
   spiParamLen = 0;
   spiParamCount = 0;
   spiFifoLen = 0;
   spiFifoPos = 0;
   memset(spiParams, 0, sizeof(spiParams));
   memset(spiFifo, 0, sizeof(spiFifo));
   pagesLatch = 0;
}


void JGDReset(void)
{
   unsigned i;

   for (i = 0; i < 6; i++)
      jgdPage[i] = (uint8_t)i;
   jgdWriteEnabled = 0;
   JGDSpiReset();
}


void JGDUnload(void)
{
   JGDReset();
   jgdActive = 0;
   if (jgdROM)
   {
      free(jgdROM);
      jgdROM = NULL;
   }
}


void JGDDone(void)
{
   /* iOS cannot dlclose cores: every static must return to its
    * power-on value here (see CLAUDE.md / feedback_ios_static_state). */
   JGDUnload();
   jgdMode = JGD_MODE_AUTO;
}


void JGDLoadROM(const uint8_t *buffer, uint32_t size)
{
   int wantActive;

   JGDUnload();

   if (!buffer || size == 0 || size > JGD_ROM_SIZE)
      return;

   wantActive = (jgdMode == JGD_MODE_ENABLED)
      || (jgdMode == JGD_MODE_AUTO && size > JGD_AUTO_THRESHOLD);

   if (!wantActive)
      return;

   jgdROM = (uint8_t *)malloc(JGD_ROM_SIZE);
   if (!jgdROM)
   {
      LOG_ERR("[JGD] failed to allocate 16 MB SDRAM image; GameDrive disabled\n");
      return;
   }

   memcpy(jgdROM, buffer, size);
   if (size < JGD_ROM_SIZE)
      memset(jgdROM + size, 0, JGD_ROM_SIZE - size);

   jgdActive = 1;
   LOG_INF("[JGD] GameDrive emulation active (%s, image %u bytes)\n",
           jgdMode == JGD_MODE_ENABLED ? "forced" : "auto", (unsigned)size);
}


void JGDWriteROM8(uint32_t off, uint8_t v)
{
   jgdROM[((uint32_t)jgdPage[off >> 20] << 20) | (off & 0xFFFFF)] = v;
}

/* ------------------------------------------------------------------ */
/* SPI engine                                                         */
/* ------------------------------------------------------------------ */

static void JGDFifoPush(uint8_t b)
{
   if (spiFifoLen < JGD_FIFO_SIZE)
      spiFifo[spiFifoLen++] = b;
}


static uint8_t JGDFifoPop(void)
{
   if (spiFifoPos < spiFifoLen)
      return spiFifo[spiFifoPos++];
   return 0;
}


/* Fill the response FIFO for the completed command.  Responses stream
 * high byte first (GDExchangeWord reassembles rx1<<8 | rx2). */
static void JGDProcessCommand(void)
{
   uint16_t i;

   spiFifoLen = 0;
   spiFifoPos = 0;

   switch (spiCmd)
   {
      case 12:  /* HW version: u32, firmware word then ASIC word */
         JGDFifoPush((uint8_t)(JGD_FW_VERSION >> 8));
         JGDFifoPush((uint8_t)(JGD_FW_VERSION & 0xFF));
         JGDFifoPush((uint8_t)(JGD_ASIC_VERSION >> 8));
         JGDFifoPush((uint8_t)(JGD_ASIC_VERSION & 0xFF));
         break;

      case 0x80: /* Fetch GDBIOS: u16 size, then the blob */
         JGDFifoPush((uint8_t)(JGD_BLOB_SIZE >> 8));
         JGDFifoPush((uint8_t)(JGD_BLOB_SIZE & 0xFF));
         for (i = 0; i < JGD_BLOB_SIZE; i++)
            JGDFifoPush(jgdBlob[i]);
         break;

      default:  /* unknown command: zero-filled response */
         break;
   }
}


/* ASIC_SPI_STATUS write.  Decoded per the gdbios_bindings.s sequences:
 *
 *   $0000            end of packet / deselect     -> engine idle
 *   $0010 (START)    "attention": begin a packet, or "process it GD!"
 *                    after the header/params, or a block boundary in
 *                    the response -> HAVE_DATA drops
 *   $0011 (START|SS) GDWaitData handshake ack -> HAVE_DATA rises
 */
static void JGDStatusWrite(uint16_t v)
{
   if (v == 0)
   {
      /* clr.w ASIC_SPI_STATUS: end of packet. */
      spiState = JGD_SPI_IDLE;
      spiHaveData = 0;
      spiFifoLen = 0;
      spiFifoPos = 0;
      spiHdrCount = 0;
      return;
   }

   if (!(v & JGD_ST_PACKET_START))
      return;

   if (v & JGD_ST_SLAVE_SELECT)
   {
      /* Handshake ack: slave raises HAVE_DATA. */
      spiHaveData = 1;
      if (spiState == JGD_SPI_START || spiState == JGD_SPI_IDLE)
      {
         spiState = JGD_SPI_CMD;
         spiHdrCount = 0;
      }
      /* JGD_SPI_RESP: stay -- next block ready. */
      return;
   }

   /* Plain PACKET_START. */
   spiHaveData = 0;
   if (spiState == JGD_SPI_IDLE)
   {
      spiState = JGD_SPI_START;
      spiFifoLen = 0;
      spiFifoPos = 0;
      spiHdrCount = 0;
   }
   else if (spiState == JGD_SPI_PROC)
   {
      JGDProcessCommand();
      spiState = JGD_SPI_RESP;
   }
   /* JGD_SPI_RESP / others: block boundary, nothing to do. */
}


/* ASIC_SPI_DATA write: clock one byte exchange (tx = low byte). */
static void JGDDataWrite(uint16_t v)
{
   uint8_t tx = (uint8_t)(v & 0xFF);
   uint8_t rx = 0;

   switch (spiState)
   {
      case JGD_SPI_CMD:
         switch (spiHdrCount)
         {
            case 0: spiCmd = tx;                            break;
            case 1: spiCmd |= (uint16_t)tx << 8;            break;
            case 2: spiParamLen = tx;                       break;
            default:
               spiParamLen |= (uint16_t)tx << 8;
               spiParamCount = 0;
               spiState = spiParamLen ? JGD_SPI_PARAMS : JGD_SPI_PROC;
               break;
         }
         if (spiHdrCount < 3)
            spiHdrCount++;
         break;

      case JGD_SPI_PARAMS:
         if (spiParamCount < JGD_PARAM_MAX)
            spiParams[spiParamCount] = tx;
         spiParamCount++;
         if (spiParamCount >= spiParamLen)
            spiState = JGD_SPI_PROC;
         break;

      case JGD_SPI_RESP:
         rx = JGDFifoPop();
         break;

      default:
         break;
   }

   spiRxLatch = rx;
   spiLatchFull = 1;
}


/* Backdoor register (see the blob's op encoding above). */
static void JGDBackdoorWrite(uint16_t v)
{
   unsigned page;

   switch (v & 0xF000)
   {
      case 0x8000:
         page = (v >> 4) & 0x7;
         if (page < 6)
            jgdPage[page] = (uint8_t)(v & 0xF);
         break;
      case 0x9000:
         pagesLatch = v & 0x0FFF;
         break;
      case 0xA000:
         jgdPage[0] = (uint8_t)(pagesLatch & 0xF);
         jgdPage[1] = (uint8_t)((pagesLatch >> 4) & 0xF);
         jgdPage[2] = (uint8_t)((pagesLatch >> 8) & 0xF);
         jgdPage[3] = (uint8_t)(v & 0xF);
         jgdPage[4] = (uint8_t)((v >> 4) & 0xF);
         jgdPage[5] = (uint8_t)((v >> 8) & 0xF);
         break;
      case 0xB000:
         jgdWriteEnabled = (uint8_t)(v & 1);
         break;
      default:
         break;
   }
}

/* ------------------------------------------------------------------ */
/* JERRY register window                                              */
/* ------------------------------------------------------------------ */

uint16_t JGDControlReadWord(uint32_t offset)
{
   switch (offset & 0xFFFFFFFE)
   {
      case JGD_ASIC_SPI_STATUS:
         /* Busy (bit 15) never set: exchanges complete instantly. */
         return (uint16_t)((spiHaveData ? JGD_ST_HAVE_DATA : 0)
                           | (spiLatchFull ? JGD_ST_LATCH_FULL : 0));
      case JGD_ASIC_SPI_DATA:
         /* Word read drains the rx latch (the pre-install "in case of
          * FIFO DMA termination" drain loop in GD_Install). */
         spiLatchFull = 0;
         return spiRxLatch;
      default:
         return 0;
   }
}


uint8_t JGDControlReadByte(uint32_t offset)
{
   uint16_t w;

   if (offset == JGD_ASIC_SPI_DATA + 1)
   {
      /* ASIC_SPI_DATA_BYTE: the received byte. */
      spiLatchFull = 0;
      return spiRxLatch;
   }

   w = JGDControlReadWord(offset & 0xFFFFFFFE);
   return (offset & 1) ? (uint8_t)(w & 0xFF) : (uint8_t)(w >> 8);
}


void JGDControlWriteWord(uint32_t offset, uint16_t data)
{
   switch (offset & 0xFFFFFFFE)
   {
      case JGD_ASIC_SPI_STATUS:
         JGDStatusWrite(data);
         break;
      case JGD_ASIC_SPI_DATA:
         JGDDataWrite(data);
         break;
      case JGD_BACKDOOR:
         JGDBackdoorWrite(data);
         break;
      default:
         break;
   }
}


void JGDControlWriteByte(uint32_t offset, uint8_t data)
{
   /* Byte writes are not used by the published bindings; approximate
    * them as a word write carrying the byte in the low lane. */
   JGDControlWriteWord(offset & 0xFFFFFFFE, data);
}

/* ------------------------------------------------------------------ */
/* Savestate                                                          */
/* ------------------------------------------------------------------ */

#include "state.h"

/* Fixed-size chunk.  Deliberately all-zero while inactive so states of
 * non-GD content keep a zero tail (test_state_compat's dac_block_is_last
 * structural check relies on trailing chunks being zero there). */

size_t JGDStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   uint8_t zero8 = 0;
   uint16_t zero16 = 0;
   unsigned i;

   if (!jgdActive)
   {
      static const uint8_t zeros[16] = { 0 };
      STATE_SAVE_BUF(buf, zeros, 16);            /* flags/pages/spi bytes */
      for (i = 0; i < 5; i++)
         STATE_SAVE_VAR(buf, zero16);            /* latch/cmd/len/count/fifoLen */
      STATE_SAVE_VAR(buf, zero16);               /* fifoPos */
      {
         uint8_t zfifo[JGD_FIFO_SIZE];
         memset(zfifo, 0, sizeof(zfifo));
         STATE_SAVE_BUF(buf, zfifo, JGD_FIFO_SIZE);
      }
      return (size_t)(buf - start);
   }

   STATE_SAVE_VAR(buf, jgdActive);
   STATE_SAVE_VAR(buf, jgdWriteEnabled);
   STATE_SAVE_BUF(buf, jgdPage, 6);
   STATE_SAVE_VAR(buf, spiState);
   STATE_SAVE_VAR(buf, spiHaveData);
   STATE_SAVE_VAR(buf, spiLatchFull);
   STATE_SAVE_VAR(buf, spiRxLatch);
   STATE_SAVE_VAR(buf, spiHdrCount);
   STATE_SAVE_VAR(buf, zero8);                   /* pad */
   STATE_SAVE_VAR(buf, zero8);                   /* pad */
   STATE_SAVE_VAR(buf, zero8);                   /* pad */
   STATE_SAVE_VAR(buf, pagesLatch);
   STATE_SAVE_VAR(buf, spiCmd);
   STATE_SAVE_VAR(buf, spiParamLen);
   STATE_SAVE_VAR(buf, spiParamCount);
   STATE_SAVE_VAR(buf, spiFifoLen);
   STATE_SAVE_VAR(buf, spiFifoPos);
   STATE_SAVE_BUF(buf, spiFifo, JGD_FIFO_SIZE);

   return (size_t)(buf - start);
}


size_t JGDStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   uint8_t active, pad;
   unsigned i;

   STATE_LOAD_VAR(buf, active);
   STATE_LOAD_VAR(buf, jgdWriteEnabled);
   STATE_LOAD_BUF(buf, jgdPage, 6);
   STATE_LOAD_VAR(buf, spiState);
   STATE_LOAD_VAR(buf, spiHaveData);
   STATE_LOAD_VAR(buf, spiLatchFull);
   STATE_LOAD_VAR(buf, spiRxLatch);
   STATE_LOAD_VAR(buf, spiHdrCount);
   STATE_LOAD_VAR(buf, pad);
   STATE_LOAD_VAR(buf, pad);
   STATE_LOAD_VAR(buf, pad);
   STATE_LOAD_VAR(buf, pagesLatch);
   STATE_LOAD_VAR(buf, spiCmd);
   STATE_LOAD_VAR(buf, spiParamLen);
   STATE_LOAD_VAR(buf, spiParamCount);
   STATE_LOAD_VAR(buf, spiFifoLen);
   STATE_LOAD_VAR(buf, spiFifoPos);
   STATE_LOAD_BUF(buf, spiFifo, JGD_FIFO_SIZE);

   if (!active || !jgdROM)
   {
      /* State written without GD banking (or the current session has
       * no 16 MB image to bank): back to the reset mapping. */
      JGDReset();
   }
   else
   {
      /* Sanitize untrusted fields so a corrupt state cannot index out
       * of range. */
      for (i = 0; i < 6; i++)
         jgdPage[i] &= 0xF;
      jgdWriteEnabled &= 1;
      if (spiState > JGD_SPI_RESP)
         spiState = JGD_SPI_IDLE;
      if (spiFifoLen > JGD_FIFO_SIZE)
         spiFifoLen = JGD_FIFO_SIZE;
      if (spiFifoPos > spiFifoLen)
         spiFifoPos = spiFifoLen;
      if (spiParamCount > JGD_PARAM_MAX)
         spiParamCount = JGD_PARAM_MAX;
      if (spiHdrCount > 3)
         spiHdrCount = 3;
   }

   return (size_t)(buf - start);
}

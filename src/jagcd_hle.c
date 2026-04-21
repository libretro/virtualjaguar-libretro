/*
 * jagcd_hle.c — HLE (High-Level Emulation) Jaguar CD BIOS
 *
 * Replaces the real CD BIOS when no BIOS ROM is available.  Handles the
 * entire CD boot sequence in C and intercepts BIOS jump table calls to
 * transfer CD sectors directly from the disc image into Jaguar RAM.
 *
 * The BIOS jump table lives at $3000-$306B (18 entries, 6 bytes each).
 * Each entry on real hardware is BRA.W <handler> + NOP.  In HLE we fill
 * the table with RTS ($4E75) and intercept before execution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "jagcd_hle.h"
#include "cdintf.h"
#include "log.h"
#include "vjag_memory.h"
#include "gpu.h"
#include "m68000/m68kinterface.h"

/* file_stream_transforms.h redefines fprintf; restore real stdio. */
#undef fprintf

/* HLE debug tracing — set to 1 for verbose CD HLE logging */
#define HLE_DEBUG 1
#if HLE_DEBUG
#define HLE_LOG(...) LOG_DBG("[CD-HLE] " __VA_ARGS__)
#else
#define HLE_LOG(...) ((void)0)
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define BIOS_JUMPTABLE_BASE  0x003000
#define BIOS_JUMPTABLE_SIZE  0x0E00

/* BIOS jump table entries (18 entries, 6 bytes apart).
 * Names from retail CD BIOS disassembly (docs/cd-bios-calling-convention.md). */
#define JT_CD_SETUP_AUDIO_ISR  0x003000  /* entry 0  */
#define JT_CD_WAIT_RESPONSE    0x003006  /* entry 1  */
#define JT_CD_WAIT_RESPONSE2   0x00300C  /* entry 2  */
#define JT_CD_I2S_ENABLE       0x003012  /* entry 3  */
#define JT_CD_SPIN_UP          0x003018  /* entry 4  */
#define JT_CD_STOP_DRIVE       0x00301E  /* entry 5  */
#define JT_CD_SET_VOL_MUTE     0x003024  /* entry 6  */
#define JT_CD_SET_VOL_MAX      0x00302A  /* entry 7  */
#define JT_CD_PAUSE            0x003030  /* entry 8  */
#define JT_CD_UNPAUSE          0x003036  /* entry 9  */
#define JT_CD_READ             0x00303C  /* entry 10 */
#define JT_CD_FIFO_DISABLE     0x003042  /* entry 11 */
#define JT_CD_HW_RESET         0x003048  /* entry 12 */
#define JT_CD_POLL             0x00304E  /* entry 13 */
#define JT_CD_SET_DAC_MODE     0x003054  /* entry 14 */
#define JT_CD_READ_TOC         0x00305A  /* entry 15 */
#define JT_CD_SETUP_CDROM_ISR  0x003060  /* entry 16 */
#define JT_CD_SETUP_DATA_ISR   0x003066  /* entry 17 */

#define CD_READY_ADDR  0x03727C
#define GPU_AUTH_ADDR  0xF03000
#define GPU_AUTH_MAGIC 0x03D0DEAD
#define M68K_RTS       0x4E75

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static bool hle_active = false;

/* Saved from the last CD_read call so CD_poll can report completion. */
static uint32_t hle_read_dest      = 0;
static uint32_t hle_read_end_addr  = 0;
static uint32_t hle_read_progress  = 0;
static bool     hle_read_pending   = false;

/* GPU data area base from the $3060/$3066/$3000 ISR setup call.
 * The boot stub reads [$3074] to find this pointer, then checks
 * the transfer state structure there. */
static uint32_t hle_gpu_data_base  = 0;


bool JaguarCDHLEActive(void)
{
   return hle_active;
}

void JaguarCDHLESetActive(bool active)
{
   hle_active = active;
}

/* ------------------------------------------------------------------ */
/* TOC table at $2C00                                                  */
/*                                                                     */
/* The boot stub at $0803E2 scans 8-byte entries looking for           */
/* byte[4]==1 (session boundary marker), then takes the NEXT entry's   */
/* bytes [1],[2],[3] as {min, sec, frm} of the first session-2 track.  */
/* We write a minimal table that satisfies this search.                */
/* ------------------------------------------------------------------ */

static void HLEPopulateTOC(uint32_t addr)
{
   uint32_t numTracks = CDIntfGetNumTracks();
   uint32_t t;
   bool wroteSessionMarker = false;
   uint32_t base = addr;

   if (addr + 0x400 > 0x200000)
      addr = 0x2C00;

   memset(&jaguarMainRAM[addr], 0, 0x400);

   for (t = 1; t <= numTracks && addr < base + 0x3F8; t++)
   {
      uint8_t min  = CDIntfGetTrackInfo(t, 0);
      uint8_t sec  = CDIntfGetTrackInfo(t, 1);
      uint8_t frm  = CDIntfGetTrackInfo(t, 2);
      uint8_t sess = CDIntfGetTrackSession(t);

      if (sess >= 2 && !wroteSessionMarker)
      {
         HLE_LOG("TOC: session marker at $%04X (before track %u)\n",
                addr, t);
         jaguarMainRAM[addr + 0] = 0x00;
         jaguarMainRAM[addr + 1] = 0x00;
         jaguarMainRAM[addr + 2] = 0x00;
         jaguarMainRAM[addr + 3] = 0x00;
         jaguarMainRAM[addr + 4] = 0x01;
         jaguarMainRAM[addr + 5] = 0x00;
         jaguarMainRAM[addr + 6] = 0x00;
         jaguarMainRAM[addr + 7] = 0x00;
         addr += 8;
         wroteSessionMarker = true;
      }

      if (sess >= 2 || t >= numTracks - 4)
         HLE_LOG("TOC: track %2u session=%u MSF=%02u:%02u:%02u at $%04X\n",
                t, sess, min, sec, frm, addr);

      jaguarMainRAM[addr + 0] = (uint8_t)t;
      jaguarMainRAM[addr + 1] = min;
      jaguarMainRAM[addr + 2] = sec;
      jaguarMainRAM[addr + 3] = frm;
      jaguarMainRAM[addr + 4] = 0x00;
      jaguarMainRAM[addr + 5] = 0x00;
      jaguarMainRAM[addr + 6] = 0x00;
      jaguarMainRAM[addr + 7] = 0x00;
      addr += 8;
   }

   HLE_LOG("Populated TOC at $%04X: %u tracks, marker=%s, end=$%04X\n",
           base, numTracks, wroteSessionMarker ? "yes" : "no", addr);
}

/* ------------------------------------------------------------------ */
/* Jump table setup                                                    */
/* ------------------------------------------------------------------ */

static void HLEInstallJumpTable(void)
{
   uint32_t i;
   for (i = 0; i < BIOS_JUMPTABLE_SIZE; i += 2)
   {
      jaguarMainRAM[BIOS_JUMPTABLE_BASE + i + 0] = 0x4E;
      jaguarMainRAM[BIOS_JUMPTABLE_BASE + i + 1] = 0x75;
   }

   HLE_LOG("Installed RTS stubs at $%06X-$%06X\n",
           BIOS_JUMPTABLE_BASE,
           BIOS_JUMPTABLE_BASE + BIOS_JUMPTABLE_SIZE - 1);
}

/* ------------------------------------------------------------------ */
/* $303C: CD_read — start CD data transfer                             */
/*                                                                     */
/* D0 = packed MSF: (min << 16) | (sec << 8) | frm.                   */
/*      Bit 31: re-seek flag (skip init, just seek).                   */
/* D1 = sync sentinel.  On real hardware the GPU ISR scans the I2S     */
/*      stream for this 4-byte pattern before starting the transfer.   */
/* A0 = destination buffer in Jaguar RAM.                              */
/* A1 = end address (dest + byte_count).                               */
/*                                                                     */
/* HLE: scan disc data from MSF for the D1 sentinel, then transfer    */
/* from the sentinel position into RAM with I2S un-swap.               */
/* ------------------------------------------------------------------ */

static void HLEHandleCDRead(void)
{
   uint32_t d0 = m68k_get_reg(NULL, M68K_REG_D0);
   uint32_t d1 = m68k_get_reg(NULL, M68K_REG_D1);
   uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
   uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);

   uint8_t frm = d0 & 0xFF;
   uint8_t sec = (d0 >> 8) & 0xFF;
   uint8_t min = (d0 >> 16) & 0x7F;
   uint32_t lba;
   uint32_t destAddr, byteCount;
   uint32_t bytesWritten, s;
   uint8_t sectorBuf[2352];
   uint32_t i;
   uint8_t pat[4];
   uint32_t scanLBA, scanOff;
   bool foundSentinel;

   lba = ((uint32_t)min * 60 + sec) * 75 + frm;
   if (lba >= 150)
      lba -= 150;

   destAddr  = a0;
   byteCount = (a1 > a0 && a1 < 0x200000) ? (a1 - a0) : 0;

   if (byteCount == 0 || byteCount > 0x200000)
      byteCount = 0x5BC00;

   HLE_LOG("CD_read: D0=$%08X D1=$%08X ('%c%c%c%c') "
           "MSF=%02u:%02u:%02u LBA=%u dest=$%06X end=$%06X size=$%X\n",
           d0, d1,
           (d1 >> 24) & 0x7F, (d1 >> 16) & 0x7F,
           (d1 >>  8) & 0x7F,  d1        & 0x7F,
           min, sec, frm, lba, destAddr, a1, byteCount);

   if (destAddr == 0 || destAddr >= 0x200000)
   {
      HLE_LOG("CD_read: invalid dest=$%06X — skipping\n", destAddr);
      hle_read_pending = false;
      return;
   }

   /* Scan for the D1 sentinel sync block in the byte-swapped disc data.
    *
    * On real hardware the I2S path byte-swaps each 16-bit word, and the
    * sentinel pattern (e.g. DDL9 = $44444C39) appears as a BLOCK of
    * repeated 4-byte patterns preceding the actual game data.  The GPU
    * ISR scans the stream for this pattern, skips the entire sync block,
    * and begins DMA from the first non-sentinel data.
    *
    * A stray single-match can occur inside the boot stub track (the boot
    * stub embeds the sentinel list DDL1-DDL9 in its data section).  We
    * reject isolated matches by requiring at least MIN_SYNC consecutive
    * sentinel words before accepting. */
   pat[0] = (d1 >> 24) & 0xFF;
   pat[1] = (d1 >> 16) & 0xFF;
   pat[2] = (d1 >>  8) & 0xFF;
   pat[3] =  d1        & 0xFF;

   #define MIN_SYNC_MATCHES 3

   foundSentinel = false;
   scanLBA = lba;
   scanOff = 0;

   for (s = 0; s < 2000 && !foundSentinel; s++)
   {
      if (!CDIntfReadBlock(lba + s, sectorBuf))
         continue;

      /* I2S un-swap: real hardware swaps bytes within 16-bit words */
      for (i = 0; i + 1 < 2352; i += 2)
      {
         uint8_t tmp = sectorBuf[i];
         sectorBuf[i]     = sectorBuf[i + 1];
         sectorBuf[i + 1] = tmp;
      }

      for (i = 0; i + 3 < 2352; i++)
      {
         if (sectorBuf[i]   != pat[0] || sectorBuf[i+1] != pat[1] ||
             sectorBuf[i+2] != pat[2] || sectorBuf[i+3] != pat[3])
            continue;

         /* Found a candidate.  Count consecutive matches. */
         {
            uint32_t matchCount = 1;
            uint32_t j = i + 4;
            while (j + 3 < 2352 &&
                   sectorBuf[j]   == pat[0] && sectorBuf[j+1] == pat[1] &&
                   sectorBuf[j+2] == pat[2] && sectorBuf[j+3] == pat[3])
            {
               matchCount++;
               j += 4;
            }
            HLE_LOG("sentinel match: %u consecutive at LBA %u off %u (sector %u from seek)\n",
                   matchCount, lba + s, i, s);
            if (matchCount < MIN_SYNC_MATCHES)
               continue;  /* stray match — keep searching */

            /* Sync block confirmed.  Scan forward across sector boundaries
             * to find where the sentinel pattern ends. */
            scanLBA = lba + s;
            scanOff = j;  /* first non-sentinel byte in current sector */

            /* If the sync block extends to the end of this sector, keep
             * scanning subsequent sectors. */
            while (scanOff >= 2352)
            {
               scanLBA++;
               scanOff = 0;
               if (!CDIntfReadBlock(scanLBA, sectorBuf))
                  break;
               for (i = 0; i + 1 < 2352; i += 2)
               {
                  uint8_t tmp2 = sectorBuf[i];
                  sectorBuf[i]     = sectorBuf[i + 1];
                  sectorBuf[i + 1] = tmp2;
               }
               /* Advance past continuing sentinel matches */
               while (scanOff + 3 < 2352 &&
                      sectorBuf[scanOff]   == pat[0] && sectorBuf[scanOff+1] == pat[1] &&
                      sectorBuf[scanOff+2] == pat[2] && sectorBuf[scanOff+3] == pat[3])
                  scanOff += 4;
               if (scanOff < 2352)
                  break;  /* found non-sentinel data in this sector */
            }
            foundSentinel = true;
            HLE_LOG("CD_read: sync block (%u+ matches) ends at "
                   "LBA %u offset %u (scanned %u sectors from seek)\n",
                   matchCount, scanLBA, scanOff, scanLBA - lba + 1);
            break;
         }
      }
   }

   if (!foundSentinel)
   {
      HLE_LOG("CD_read: sentinel NOT found — reading from LBA %u\n", lba);
      scanLBA = lba;
      scanOff = 0;
   }

   /* Transfer data from the sentinel position into Jaguar RAM */
   bytesWritten = 0;
   s = 0;

   while (bytesWritten < byteCount)
   {
      uint32_t copyStart, copyLen, dst;

      if (!CDIntfReadBlock(scanLBA + s, sectorBuf))
         memset(sectorBuf, 0, 2352);

      /* I2S un-swap */
      for (i = 0; i + 1 < 2352; i += 2)
      {
         uint8_t tmp = sectorBuf[i];
         sectorBuf[i]     = sectorBuf[i + 1];
         sectorBuf[i + 1] = tmp;
      }

      copyStart = (s == 0) ? scanOff : 0;
      copyLen = 2352 - copyStart;
      if (copyLen > byteCount - bytesWritten)
         copyLen = byteCount - bytesWritten;

      dst = destAddr + bytesWritten;
      for (i = 0; i < copyLen && (dst + i) < 0x200000; i++)
         jaguarMainRAM[dst + i] = sectorBuf[copyStart + i];

      bytesWritten += copyLen;
      s++;
   }

   hle_read_dest     = destAddr;
   hle_read_end_addr = destAddr + byteCount;
   hle_read_progress = byteCount;
   hle_read_pending  = true;

   /* Write $FFFF sentinel padding after the transferred data.
    *
    * Game code (e.g. Primal Rage) scans DDL directory tables for a $FFFF
    * terminator using 16-bit signed index math that wraps the effective
    * address into a ~64K RAM window.  On real hardware, uninitialized DRAM
    * contains random values — some of which happen to be $FFFF — providing
    * the terminator naturally.  Our emulator zeroes RAM at init, so the
    * loop never finds $FFFF and hangs.
    *
    * Padding 8 bytes of $FF after each transfer matches the expected
    * end-of-list sentinel without overwriting useful data (the game's
    * dest/end range is respected; the padding goes just past it). */
   {
      uint32_t padEnd = destAddr + byteCount + 8;
      if (padEnd <= 0x200000)
      {
         uint32_t p;
         for (p = destAddr + byteCount; p < padEnd; p++)
            jaguarMainRAM[p] = 0xFF;
      }
   }

   /* Write completion state to the GPU data area.
    * The boot stub reads [$3074] to find this structure, then checks
    * [+0] (current write pos) against [+4] (end addr) for completion.
    * The real GPU ISR pre-decrements dest by 4, so [+0] = A0-4. */
   if (hle_gpu_data_base != 0)
   {
      GPUWriteLong(hle_gpu_data_base + 0,  destAddr + byteCount, 0);
      GPUWriteLong(hle_gpu_data_base + 4,  destAddr + byteCount, 0);
      GPUWriteLong(hle_gpu_data_base + 8,  byteCount, 0);
      GPUWriteLong(hle_gpu_data_base + 16, d1, 0);
   }

   HLE_LOG("CD_read: transferred %u bytes (%u sectors) "
           "to $%06X-$%06X\n",
           byteCount, s, destAddr, hle_read_end_addr - 1);

}

/* ------------------------------------------------------------------ */
/* $304E: CD_poll — return current transfer position                   */
/*                                                                     */
/* Returns:                                                            */
/*   A0 = current write position (= end when done)                     */
/*   A1 = bytes transferred so far                                     */
/* ------------------------------------------------------------------ */

static void HLEHandleCDPoll(void)
{
   static uint32_t pollCount = 0;
   pollCount++;
   if (pollCount <= 5 || (pollCount % 100000) == 0)
      HLE_LOG("CD_poll #%u: pending=%d\n", pollCount, hle_read_pending);

   if (hle_read_pending)
   {
      m68k_set_reg(M68K_REG_A0, hle_read_end_addr);
      m68k_set_reg(M68K_REG_A1, 0);
      hle_read_pending = false;
   }
   else
   {
      m68k_set_reg(M68K_REG_A0, 0);
      m68k_set_reg(M68K_REG_A1, 0);
   }
}

/* ------------------------------------------------------------------ */
/* $305A: CD_read_toc — read TOC into buffer at A0                     */
/* ------------------------------------------------------------------ */

static void HLEHandleReadTOC(void)
{
   uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);

   HLE_LOG("CD_read_toc: A0=$%06X\n", a0);

   if (a0 > 0 && a0 < 0x200000)
      HLEPopulateTOC(a0);
}

/* ------------------------------------------------------------------ */
/* $3006: CD_wait_response — return DSA response in D1                 */
/*                                                                     */
/* Real BIOS polls BUTCH bit 13 and reads DS_DATA.  HLE returns        */
/* $0000 (idle/ready) to avoid infinite poll loops.                     */
/* ------------------------------------------------------------------ */

static void HLEHandleWaitResponse(void)
{
   m68k_set_reg(M68K_REG_D1, 0x0000);
}

/* ------------------------------------------------------------------ */
/* ISR setup — save GPU data area pointer                              */
/*                                                                     */
/* $3000/$3060/$3066 setup calls pass A0 = GPU RAM base.  The boot     */
/* stub later reads [$3074] to find this pointer, then checks the      */
/* transfer state structure there.                                     */
/*                                                                     */
/* GPU data area layout (relative to base):                            */
/*   [+0]  dest pointer  (A0 from CD_read, decremented by 4)          */
/*   [+4]  end address   (A1 from CD_read)                             */
/*   [+8]  progress      (bytes transferred, 0 initially)             */
/*   [+16] sentinel      (D1 from CD_read)                            */
/* ------------------------------------------------------------------ */

static void HLEHandleISRSetup(uint8_t mode)
{
   uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);

   hle_gpu_data_base = a0;

   /* $3072: ISR mode flag */
   jaguarMainRAM[0x3072] = mode;
   jaguarMainRAM[0x3073] = 0x00;

   /* $3074: pointer to GPU data area */
   SET32(jaguarMainRAM, 0x3074, a0);

   HLE_LOG("ISR setup: mode=$%02X GPU_DATA=$%06X\n", mode, a0);
}

/* ------------------------------------------------------------------ */
/* GPU data phase intercept (safety net)                               */
/*                                                                     */
/* If the GPU somehow starts running the BIOS CD ISR despite our HLE,  */
/* intercept it to prevent hangs from broken BUTCH emulation.           */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEGPUDataPhase(void)
{
   if (!hle_active)
      return false;

   HLE_LOG("GPU data phase intercepted (safety net)\n");
   return true;
}

/* ------------------------------------------------------------------ */
/* Boot                                                                */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEBoot(void)
{
   static uint8_t stubBuf[256 * 1024];
   uint32_t loadAddr = 0, length = 0;
   uint32_t i;

   hle_active        = false;
   hle_read_pending  = false;
   hle_read_end_addr = 0;
   hle_read_dest     = 0;
   hle_read_progress = 0;

   if (!CDIntfIsImageLoaded())
   {
      LOG_ERR("[CD-HLE] No disc image loaded — HLE boot aborted\n");
      return false;
   }

   /* Extract boot stub from session 2 */
   if (!CDIntfExtractBootStub(stubBuf, sizeof(stubBuf), &loadAddr, &length))
   {
      LOG_ERR("[CD-HLE] Boot stub extraction failed\n");
      return false;
   }

   /* Inject boot stub into Jaguar RAM */
   for (i = 0; i < length && (loadAddr + i) < 0x200000; i++)
      jaguarMainRAM[loadAddr + i] = stubBuf[i];

   LOG_INF("[CD-HLE] Injected boot stub: $%X bytes at $%06X\n",
           length, loadAddr);

   HLEInstallJumpTable();
   HLEPopulateTOC(0x2C00);

   /* CD-ready flag at $3727C */
   jaguarMainRAM[CD_READY_ADDR + 0] = 0xFF;
   jaguarMainRAM[CD_READY_ADDR + 1] = 0xFF;

   /* GPU auth magic ($03D0DEAD at $F03000) */
   GPUWriteLong(GPU_AUTH_ADDR, GPU_AUTH_MAGIC, 0);

   /* Install safe interrupt vectors.  JaguarReset() randomizes RAM, so
    * the 68K vector table ($000-$3FF) contains garbage.  When TOM fires
    * a VBLANK IRQ (autovector level 2 → vector $68), the CPU would jump
    * to a random address and crash.  Write an RTE at $400 and point all
    * exception vectors there so interrupts return harmlessly until the
    * boot stub installs its own handlers. */
   SET16(jaguarMainRAM, 0x400, 0x4E73);  /* RTE */
   for (i = 2; i < 256; i++)
      SET32(jaguarMainRAM, i * 4, 0x00000400);

   /* Set initial stack pointer and PC */
   SET32(jaguarMainRAM, 0, 0x00200000);
   SET32(jaguarMainRAM, 4, loadAddr);
   m68k_set_reg(M68K_REG_SP, 0x00200000);
   m68k_set_reg(M68K_REG_PC, loadAddr);

   hle_active = true;

   LOG_INF("[CD-HLE] Boot complete — PC=$%06X SP=$%06X\n",
           loadAddr, 0x200000);
   return true;
}

/* ------------------------------------------------------------------ */
/* Instruction hook — intercept all 18 BIOS jump table entries         */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEHook(uint32_t pc)
{
   if (!hle_active)
      return false;

   /* Fast rejection: jump table is $3000-$306B */
   if (pc < BIOS_JUMPTABLE_BASE || pc > 0x00306B)
      return false;

   switch (pc)
   {
   case JT_CD_READ:
      HLEHandleCDRead();
      return true;

   case JT_CD_POLL:
      HLEHandleCDPoll();
      return true;

   case JT_CD_READ_TOC:
      HLEHandleReadTOC();
      return true;

   case JT_CD_WAIT_RESPONSE:
   case JT_CD_WAIT_RESPONSE2:
      HLEHandleWaitResponse();
      return true;

   /* ISR setup: save GPU data area pointer from A0 */
   case JT_CD_SETUP_AUDIO_ISR:
      HLEHandleISRSetup(0x00);
      return true;
   case JT_CD_SETUP_CDROM_ISR:
      HLEHandleISRSetup(0xFF);
      return true;
   case JT_CD_SETUP_DATA_ISR:
      HLEHandleISRSetup(0x01);
      return true;

   /* No-ops: these control hardware state that doesn't exist in HLE */
   case JT_CD_I2S_ENABLE:
   case JT_CD_SPIN_UP:
   case JT_CD_STOP_DRIVE:
   case JT_CD_SET_VOL_MUTE:
   case JT_CD_SET_VOL_MAX:
   case JT_CD_PAUSE:
   case JT_CD_UNPAUSE:
   case JT_CD_FIFO_DISABLE:
   case JT_CD_HW_RESET:
   case JT_CD_SET_DAC_MODE:
   {
      static uint32_t noop_count = 0;
      noop_count++;
      if (noop_count <= 20 || (noop_count % 10000) == 0)
         HLE_LOG("No-op $%06X (call #%u)\n", pc, noop_count);
      return true;
   }

   default:
      break;
   }

   return false;
}

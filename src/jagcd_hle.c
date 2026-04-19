/*
 * jagcd_hle.c — HLE (High-Level Emulation) Jaguar CD BIOS
 *
 * Replaces the real CD BIOS when no BIOS ROM is available.  Handles the
 * entire CD boot sequence in C and intercepts BIOS jump table calls to
 * transfer CD sectors directly from the disc image into Jaguar RAM.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "jagcd_hle.h"
#include "cdintf.h"
#include "vjag_memory.h"
#include "gpu.h"
#include "m68000/m68kinterface.h"

/* file_stream_transforms.h redefines fprintf; restore real stdio. */
#undef fprintf

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define BIOS_JUMPTABLE_BASE  0x003000
#define BIOS_JUMPTABLE_SIZE  0x0E00

/* BIOS jump table entries used by the boot stub:
 *   $3006: CD_init  (D0 = mode)
 *   $301E: CD_stop
 *   $303C: CD_read  (D0 = packed MSF, A0 = dest, A1 = end)
 *   $3042: CD_reset
 *   $304E: CD_poll  (returns A0 = current pos, A1 = error)
 *   $3060: GPU ISR setup */
#define BIOS_CD_INIT   0x003006
#define BIOS_CD_STOP   0x00301E
#define BIOS_CD_READ   0x00303C
#define BIOS_CD_RESET  0x003042
#define BIOS_CD_POLL   0x00304E
#define BIOS_GPU_SETUP 0x003060

#define CD_READY_ADDR  0x03727C
#define GPU_AUTH_ADDR  0xF03000
#define GPU_AUTH_MAGIC 0x03D0DEAD
#define M68K_RTS       0x4E75

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static bool hle_active = false;

/* Saved from the last CD_read ($303C) call so CD_poll ($304E) can
 * report completion. */
static uint32_t hle_read_end_addr = 0;
static bool     hle_read_pending  = false;

bool JaguarCDHLEActive(void)
{
   return hle_active;
}

/* ------------------------------------------------------------------ */
/* TOC table at $2C00                                                  */
/*                                                                     */
/* The boot stub at $0803E2 scans 8-byte entries looking for           */
/* byte[4]==1 (session boundary marker), then takes the NEXT entry's   */
/* bytes [1],[2],[3] as {min, sec, frm} of the first session-2 track.  */
/* We write a minimal table that satisfies this search.                */
/* ------------------------------------------------------------------ */

static void HLEPopulateTOC(void)
{
   uint32_t numTracks = CDIntfGetNumTracks();
   uint32_t addr = 0x2C00;
   uint32_t t;
   bool wroteSessionMarker = false;

   memset(&jaguarMainRAM[0x2C00], 0, 0x400);

   for (t = 1; t <= numTracks && addr < 0x2FF8; t++)
   {
      uint8_t min  = CDIntfGetTrackInfo(t, 0);
      uint8_t sec  = CDIntfGetTrackInfo(t, 1);
      uint8_t frm  = CDIntfGetTrackInfo(t, 2);
      uint8_t sess = CDIntfGetTrackSession(t);

      if (sess >= 2 && !wroteSessionMarker)
      {
         fprintf(stderr, "[CD-HLE] TOC: session marker at $%04X (before track %u)\n",
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
         fprintf(stderr, "[CD-HLE] TOC: track %2u session=%u MSF=%02u:%02u:%02u at $%04X\n",
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

   fprintf(stderr, "[CD-HLE] Populated $2C00 TOC: %u tracks, marker=%s, end=$%04X\n",
           numTracks, wroteSessionMarker ? "yes" : "no", addr);
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

   fprintf(stderr, "[CD-HLE] Installed RTS stubs at $%06X-$%06X\n",
           BIOS_JUMPTABLE_BASE,
           BIOS_JUMPTABLE_BASE + BIOS_JUMPTABLE_SIZE - 1);
}

/* ------------------------------------------------------------------ */
/* Find game data on disc                                              */
/*                                                                     */
/* The boot stub's TOC scan points to the first session-2 track (the   */
/* boot stub track itself), which contains only auth pattern + zeros.  */
/* The actual game data is in a later track (typically track 32 for     */
/* Primal Rage).  This function scans session-2 tracks to find where   */
/* the game data begins: past pregap silence, past auth pattern +      */
/* header text, at the first sector with non-ASCII binary data.        */
/* Returns the LBA of the first game data sector, or 0 on failure.     */
/* ------------------------------------------------------------------ */

static uint32_t HLEFindGameDataLBA(void)
{
   uint32_t numTracks = CDIntfGetNumTracks();
   uint32_t t, bestTrack = 0;
   uint32_t bestSize = 0;
   bool skippedBootStub = false;

   /* Find the largest session-2 track (after skipping the boot stub
    * track).  The game data track is typically much larger than the
    * boot stub or padding tracks. */
   for (t = 1; t <= numTracks; t++)
   {
      uint32_t trackSize;
      if (CDIntfGetTrackSession(t) < 2)
         continue;
      if (!skippedBootStub)
      {
         skippedBootStub = true;
         continue;
      }

      /* Approximate track size from MSF difference to next track */
      {
         uint8_t tm = CDIntfGetTrackInfo(t, 0);
         uint8_t ts = CDIntfGetTrackInfo(t, 1);
         uint8_t tf = CDIntfGetTrackInfo(t, 2);
         uint32_t lba = ((uint32_t)tm * 60 + ts) * 75 + tf;

         if (t < numTracks)
         {
            uint8_t nm = CDIntfGetTrackInfo(t+1, 0);
            uint8_t ns = CDIntfGetTrackInfo(t+1, 1);
            uint8_t nf = CDIntfGetTrackInfo(t+1, 2);
            uint32_t nextLba = ((uint32_t)nm * 60 + ns) * 75 + nf;
            trackSize = (nextLba > lba) ? nextLba - lba : 0;
         }
         else
         {
            trackSize = 10000;
         }
      }

      if (trackSize > bestSize)
      {
         bestSize = trackSize;
         bestTrack = t;
      }
   }

   if (bestTrack == 0)
      return 0;

   /* Scan the largest track for the first non-empty, non-auth,
    * non-padding sector (the actual game data). */
   {
      uint8_t tm = CDIntfGetTrackInfo(bestTrack, 0);
      uint8_t ts = CDIntfGetTrackInfo(bestTrack, 1);
      uint8_t tf = CDIntfGetTrackInfo(bestTrack, 2);
      uint32_t absBlock = ((uint32_t)tm * 60 + ts) * 75 + tf;
      uint32_t trackLBA = (absBlock >= 150) ? absBlock - 150 : 0;
      uint32_t sec;
      uint8_t buf[2352];

      for (sec = 0; sec < 500; sec++)
      {
         uint32_t nonzero = 0, binary = 0;
         uint32_t j;
         bool has_auth = false;

         if (!CDIntfReadBlock(trackLBA + sec, buf))
            continue;

         for (j = 0; j < 2352; j++)
         {
            if (buf[j] != 0)
               nonzero++;
            if (buf[j] > 0x7F || (buf[j] < 0x20 && buf[j] != 0))
               binary++;
         }

         if (nonzero == 0)
            continue;

         for (j = 0; j + 3 < 2352; j++)
         {
            if ((buf[j] == 'T' && buf[j+1] == 'A' && buf[j+2] == 'I' && buf[j+3] == 'R') ||
                (buf[j] == 'A' && buf[j+1] == 'T' && buf[j+2] == 'R' && buf[j+3] == 'I'))
            { has_auth = true; break; }
         }
         if (has_auth)
            continue;

         if (binary > 100)
         {
            fprintf(stderr, "[CD-HLE] Game data found: track %u sector %u "
                    "LBA=%u (%u sectors into track, binary=%u)\n",
                    bestTrack, sec, trackLBA + sec, sec, binary);
            return trackLBA + sec;
         }
      }
   }

   return 0;
}

/* ------------------------------------------------------------------ */
/* $303C: CD_read — start CD data transfer                             */
/*                                                                     */
/* BIOS calling convention (from disassembly):                         */
/*   D0 = packed MSF: (minute << 16) | (second << 8) | frame          */
/*   A0 = destination address in Jaguar RAM                            */
/*   A1 = end address (dest + byte_count)                              */
/*                                                                     */
/* The real BIOS sets up a GPU ISR that reads from BUTCH FIFO.  Our    */
/* HLE does the full transfer synchronously, then $304E reports done.  */
/*                                                                     */
/* The boot stub's TOC scan always finds the first session-2 track     */
/* (the boot stub track) as the read target.  On multi-track session-2 */
/* discs the game data is in a later track.  We detect this and        */
/* redirect to the actual game data.                                   */
/* ------------------------------------------------------------------ */

static void HLEHandleCDRead(void)
{
   uint32_t d0 = m68k_get_reg(NULL, M68K_REG_D0);
   uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
   uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);

   uint8_t frm = d0 & 0xFF;
   uint8_t sec = (d0 >> 8) & 0xFF;
   uint8_t min = (d0 >> 16) & 0xFF;
   uint32_t lba;
   uint32_t destAddr, byteCount, numSectors;
   uint32_t s, i;
   uint8_t sectorBuf[2352];

   /* Convert absolute MSF to LBA (2-second / 150-frame lead-in) */
   lba = ((uint32_t)min * 60 + sec) * 75 + frm;
   if (lba >= 150)
      lba -= 150;

   /* Destination and size from A0/A1 */
   destAddr = a0;
   if (a1 > a0 && a1 < 0x200000)
      byteCount = a1 - a0;
   else
      byteCount = 0;

   /* Fallback: if A1 isn't useful, try the boot stub's stored end address
    * at $085D86 (set before $303C is called). */
   if (byteCount == 0 || byteCount > 0x200000)
   {
      uint32_t storedEnd = GET32(jaguarMainRAM, 0x085D86);
      if (storedEnd > a0 && storedEnd <= 0x200000)
         byteCount = storedEnd - a0;
      else
         byteCount = 0x5BC00;
   }

   numSectors = (byteCount + 2351) / 2352;

   fprintf(stderr, "[CD-HLE] CD_read: D0=$%08X MSF=%02u:%02u:%02u LBA=%u "
           "A0=$%06X A1=$%06X size=$%X (%u sectors)\n",
           d0, min, sec, frm, lba, a0, a1, byteCount, numSectors);

   /* Check if the requested LBA yields empty/auth data (boot stub track).
    * If so, scan forward to find the actual game data. */
   {
      uint8_t probe[2352];
      bool isEmpty = true;
      if (CDIntfReadBlock(lba, probe))
      {
         for (i = 0; i < 2352; i++)
            if (probe[i] != 0) { isEmpty = false; break; }
      }
      if (isEmpty)
      {
         uint32_t gameLBA = HLEFindGameDataLBA();
         if (gameLBA > 0)
         {
            fprintf(stderr, "[CD-HLE] CD_read: redirecting from empty LBA %u "
                    "to game data at LBA %u\n", lba, gameLBA);
            lba = gameLBA;
         }
      }
   }

   if (destAddr == 0 || destAddr >= 0x200000 || numSectors == 0)
   {
      fprintf(stderr, "[CD-HLE] CD_read: invalid dest or zero sectors\n");
      hle_read_pending = false;
      return;
   }

   /* Read sectors, I2S word-swap, and copy to Jaguar RAM */
   for (s = 0; s < numSectors; s++)
   {
      uint32_t bytesThisSector = 2352;
      uint32_t remaining = byteCount - (s * 2352);
      if (remaining < 2352)
         bytesThisSector = remaining;

      if (!CDIntfReadBlock(lba + s, sectorBuf))
      {
         fprintf(stderr, "[CD-HLE] CD_read: ReadBlock failed at LBA %u "
                 "(sector %u/%u)\n", lba + s, s, numSectors);
         memset(sectorBuf, 0, 2352);
      }

      /* I2S word-swap: disc stores bytes pre-swapped within 16-bit words */
      for (i = 0; i + 1 < bytesThisSector; i += 2)
      {
         uint8_t tmp = sectorBuf[i];
         sectorBuf[i] = sectorBuf[i + 1];
         sectorBuf[i + 1] = tmp;
      }

      {
         uint32_t dst = destAddr + s * 2352;
         uint32_t j;
         for (j = 0; j < bytesThisSector && (dst + j) < 0x200000; j++)
            jaguarMainRAM[dst + j] = sectorBuf[j];
      }
   }

   hle_read_end_addr = destAddr + byteCount;
   hle_read_pending = true;

   fprintf(stderr, "[CD-HLE] CD_read: transferred %u sectors to $%06X-$%06X\n",
           numSectors, destAddr, hle_read_end_addr - 1);

   /* Dump first 64 bytes at destination */
   {
      uint32_t a;
      fprintf(stderr, "[CD-HLE] Data at $%06X:\n", destAddr);
      for (a = destAddr; a < destAddr + 64 && a < 0x200000; a += 16)
         fprintf(stderr, "  %06X: %02X%02X%02X%02X %02X%02X%02X%02X "
                 "%02X%02X%02X%02X %02X%02X%02X%02X\n", a,
                 jaguarMainRAM[a+0], jaguarMainRAM[a+1],
                 jaguarMainRAM[a+2], jaguarMainRAM[a+3],
                 jaguarMainRAM[a+4], jaguarMainRAM[a+5],
                 jaguarMainRAM[a+6], jaguarMainRAM[a+7],
                 jaguarMainRAM[a+8], jaguarMainRAM[a+9],
                 jaguarMainRAM[a+10], jaguarMainRAM[a+11],
                 jaguarMainRAM[a+12], jaguarMainRAM[a+13],
                 jaguarMainRAM[a+14], jaguarMainRAM[a+15]);
   }
}

/* ------------------------------------------------------------------ */
/* $304E: CD_poll — return current transfer position                    */
/*                                                                     */
/* Returns:                                                            */
/*   A0 = current write position (= end address when done)             */
/*   A1 = error flag (0 = no error)                                    */
/*                                                                     */
/* The boot stub polls in a loop:                                      */
/*   .poll: JSR ($304E).w                                              */
/*          CMPA.L #0, A1    ; error?                                  */
/*          BNE error                                                  */
/*          CMPA.L A6, A0    ; A0 >= end?                              */
/*          BLT .poll                                                  */
/* ------------------------------------------------------------------ */

static void HLEHandleCDPoll(void)
{
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
/* GPU data phase intercept (safety net)                                */
/*                                                                     */
/* If the GPU somehow starts running the BIOS CD ISR despite our HLE,  */
/* intercept it to prevent hangs from broken BUTCH emulation.           */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEGPUDataPhase(void)
{
   if (!hle_active)
      return false;

   fprintf(stderr, "[CD-HLE] GPU data phase intercepted (safety net)\n");
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

   hle_active = false;
   hle_read_pending = false;
   hle_read_end_addr = 0;

   if (!CDIntfIsImageLoaded())
   {
      fprintf(stderr, "[CD-HLE] No disc image loaded — HLE boot aborted\n");
      return false;
   }

   /* Extract boot stub from session 2 */
   if (!CDIntfExtractBootStub(stubBuf, sizeof(stubBuf), &loadAddr, &length))
   {
      fprintf(stderr, "[CD-HLE] Boot stub extraction failed\n");
      return false;
   }

   /* Inject boot stub into Jaguar RAM */
   for (i = 0; i < length && (loadAddr + i) < 0x200000; i++)
      jaguarMainRAM[loadAddr + i] = stubBuf[i];

   fprintf(stderr, "[CD-HLE] Injected boot stub: $%X bytes at $%06X\n",
           length, loadAddr);

   HLEInstallJumpTable();
   HLEPopulateTOC();

   /* CD-ready flag at $3727C */
   jaguarMainRAM[CD_READY_ADDR + 0] = 0xFF;
   jaguarMainRAM[CD_READY_ADDR + 1] = 0xFF;

   /* GPU auth magic ($03D0DEAD at $F03000) */
   GPUWriteLong(GPU_AUTH_ADDR, GPU_AUTH_MAGIC, 0);

   /* Set initial stack pointer and PC */
   SET32(jaguarMainRAM, 0, 0x00200000);
   m68k_set_reg(M68K_REG_SP, 0x00200000);
   m68k_set_reg(M68K_REG_PC, loadAddr);

   hle_active = true;

   fprintf(stderr, "[CD-HLE] Boot complete — PC=$%06X SP=$%06X\n",
           loadAddr, 0x200000);
   return true;
}

/* ------------------------------------------------------------------ */
/* Instruction hook                                                    */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEHook(uint32_t pc)
{
   if (!hle_active)
      return false;

   switch (pc)
   {
   case BIOS_CD_READ:
      HLEHandleCDRead();
      return true;

   case BIOS_CD_POLL:
      HLEHandleCDPoll();
      return true;

   case BIOS_CD_INIT:
   case BIOS_CD_STOP:
   case BIOS_CD_RESET:
   case BIOS_GPU_SETUP:
      /* No-op — the RTS at these addresses is sufficient */
      return true;

   default:
      break;
   }

   return false;
}

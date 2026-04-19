//
// JAGUAR.CPP
//
// Originally by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Carwin Jones (BeOS)
// Cleanups and endian wrongness amelioration by James Hammons
// Note: Endian wrongness probably stems from the MAME origins of this emu and
//       the braindead way in which MAME handled memory when this was written. :-)
//
// JLH = James Hammons
//
// WHO  WHEN        WHAT
// ---  ----------  -----------------------------------------------------------
// JLH  11/25/2009  Major rewrite of memory subsystem and handlers
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "jaguar.h"

#include "cdintf.h"
#include "cdrom.h"
#include "jagcd_hle.h"
#include "dsp.h"
#include "eeprom.h"
#include "event.h"
#include "gpu.h"
#include "jerry.h"
#include "joystick.h"
#include "m68000/m68kinterface.h"
#include "memtrack.h"
#include "mmu.h"
#include "settings.h"
#include "tom.h"

static bool frameDone;

// Platform-independent xorshift32 PRNG for deterministic RAM initialization.
// libc rand() produces different sequences on different platforms (glibc vs
// macOS libsystem), which causes cross-platform baseline mismatches.
static uint32_t jaguar_prng_state = 12345;

void JaguarSeedPRNG(uint32_t seed)
{
   jaguar_prng_state = seed ? seed : 1;
}

uint32_t JaguarRand(void)
{
   jaguar_prng_state ^= jaguar_prng_state << 13;
   jaguar_prng_state ^= jaguar_prng_state >> 17;
   jaguar_prng_state ^= jaguar_prng_state << 5;
   return jaguar_prng_state;
}

#define ALPINE_FUNCTIONS

// Private function prototypes

unsigned jaguar_unknown_readbyte(unsigned address, uint32_t who)
{
   return 0xFF;
}

unsigned jaguar_unknown_readword(unsigned address, uint32_t who)
{
   return 0xFFFF;
}

// Unknown read/write byte/word routines

// It's hard to believe that developers would be sloppy with their memory writes, yet in
// some cases the developers screwed up royal. E.g., Club Drive has the following code:
//
// 807EC4: movea.l #$f1b000, A1
// 807ECA: movea.l #$8129e0, A0
// 807ED0: move.l  A0, D0
// 807ED2: move.l  #$f1bb94, D1
// 807ED8: sub.l   D0, D1
// 807EDA: lsr.l   #2, D1
// 807EDC: move.l  (A0)+, (A1)+
// 807EDE: dbra    D1, 807edc
//
// The problem is at $807ED0--instead of putting A0 into D0, they really meant to put A1
// in. This mistake causes it to try and overwrite approximately $700000 worth of address
// space! (That is, unless the 68K causes a bus error...)

void jaguar_unknown_writebyte(unsigned address, unsigned data, uint32_t who)
{
}

void jaguar_unknown_writeword(unsigned address, unsigned data, uint32_t who)
{
}

uint32_t JaguarGetHandler(uint32_t i)
{
   return JaguarReadLong(i * 4, UNKNOWN);
}


bool JaguarInterruptHandlerIsValid(uint32_t i) // Debug use only...
{
   uint32_t handler = JaguarGetHandler(i);
   return (handler && (handler != 0xFFFFFFFF) ? true : false);
}

// External variables

// Really, need to include memory.h for this, but it might interfere with some stuff...
extern uint8_t jagMemSpace[];

// Internal variables

uint32_t jaguarMainROMCRC32, jaguarROMSize, jaguarRunAddress;

bool jaguarCartInserted = false;
bool lowerField = false;


uint32_t pcQueue[0x400];
uint32_t a0Queue[0x400];
uint32_t a1Queue[0x400];
uint32_t a2Queue[0x400];
uint32_t a3Queue[0x400];
uint32_t a4Queue[0x400];
uint32_t a5Queue[0x400];
uint32_t a6Queue[0x400];
uint32_t a7Queue[0x400];
uint32_t d0Queue[0x400];
uint32_t d1Queue[0x400];
uint32_t d2Queue[0x400];
uint32_t d3Queue[0x400];
uint32_t d4Queue[0x400];
uint32_t d5Queue[0x400];
uint32_t d6Queue[0x400];
uint32_t d7Queue[0x400];
uint32_t pcQPtr = 0;
bool startM68KTracing = false;

void JaguarDumpPCHistoryStderr(int count)
{
   int n = (count > 0x400) ? 0x400 : count;
   int i;
   fprintf(stderr, "[CD-AUTH] 68K PC history (newest first, %d entries):\n", n);
   for (i = 0; i < n; i++)
   {
      /* pcQPtr has already been incremented past the last write, so
       * entry (pcQPtr - 1) is newest. */
      uint32_t idx = (pcQPtr - 1 - i) & 0x3FF;
      fprintf(stderr, "  [-%d] PC=$%06X\n", i, pcQueue[idx]);
   }
}

/* Populate the BIOS TOC table at $2C00 in main RAM.
 *
 * The CD BIOS normally reads the disc TOC during its auth/init sequence
 * and stores track info at $2C00 as 8-byte entries:
 *   +0: track number
 *   +1: absolute minutes (MSF)
 *   +2: absolute seconds (MSF)
 *   +3: absolute frames (MSF)
 *   +4: session number (1 or 2)
 *   +5-7: padding/duration
 *
 * When auth is bypassed, the TOC table is never populated.  The boot stub
 * at $0803E2 searches this table for the first session-2 track's MSF to
 * compute the CD_read seek target.  Without valid data, it reads garbage
 * and seeks to a nonsensical position. */
static void JaguarPopulateBIOSTocTable(void)
{
   uint32_t numTracks = CDIntfGetNumTracks();
   uint32_t addr = 0x2C00;
   uint32_t t;

   memset(&jaguarMainRAM[0x2C00], 0, 0x100);

   for (t = 1; t <= numTracks && addr < 0x2CF8; t++)
   {
      uint8_t min = CDIntfGetTrackInfo(t, 0);
      uint8_t sec = CDIntfGetTrackInfo(t, 1);
      uint8_t frm = CDIntfGetTrackInfo(t, 2);
      uint8_t sess = CDIntfGetTrackSession(t);

      jaguarMainRAM[addr + 0] = (uint8_t)t;
      jaguarMainRAM[addr + 1] = min;
      jaguarMainRAM[addr + 2] = sec;
      jaguarMainRAM[addr + 3] = frm;
      jaguarMainRAM[addr + 4] = sess;
      jaguarMainRAM[addr + 5] = 0;
      jaguarMainRAM[addr + 6] = 0;
      jaguarMainRAM[addr + 7] = 0;
      addr += 8;
   }

   fprintf(stderr, "[CD-TOC] Populated $2C00 table: %u tracks, %u bytes\n",
           numTracks, addr - 0x2C00);
}

/* CD BIOS audio-pregap authentication bypass.
 *
 * The Jaguar CD BIOS authenticates session 2 by reading 149 frames of
 * pregap audio (just before track 30 INDEX 01) and DSP-decoding them into
 * a checksum.  Redump-style BIN/CUE dumps strip this audio, so the BIOS
 * reads silence, the checksum mismatches,
 * and execution falls into the BNE.W $0504EC fail path -> STOP $0200 ->
 * "?" icon.  CDI dumps preserve the pregap and would not need this.
 *
 * The bypass:
 *   1. Patch BNE.W at $050AA0 -> 2x NOP, so the byte-compare mismatch
 *      falls through to the post-compare path.
 *   2. At PC=$050AB2 (DSP-result MOVE.L), pre-stuff F1B4C8 with
 *      $80010000 (done|pass response).
 *   3. At PC=$050B0C (post-BSR MOVE.L), pre-stuff $FB000 with $0A so the
 *      following BHI takes the success branch.
 *
 * Installed lazily on the first virtual-pregap read served by cdintf.c so
 * the BIOS has finished decrypting and copying its code into RAM. */
void JaguarInstallCDAuthBypass(void)
{
   static bool installed = false;
   const uint32_t bneAddr = 0x050AA0;
   if (installed)
      return;

   if (jaguarMainRAM[bneAddr]     != 0x66 || jaguarMainRAM[bneAddr + 1] != 0x00
    || jaguarMainRAM[bneAddr + 2] != 0xFA || jaguarMainRAM[bneAddr + 3] != 0x4A)
   {
      fprintf(stderr,
              "[CD-AUTH] Skip BNE patch: unexpected bytes at $%06X (%02X%02X %02X%02X)\n",
              bneAddr,
              jaguarMainRAM[bneAddr], jaguarMainRAM[bneAddr + 1],
              jaguarMainRAM[bneAddr + 2], jaguarMainRAM[bneAddr + 3]);
      installed = true;
      return;
   }
   jaguarMainRAM[bneAddr]     = 0x4E; jaguarMainRAM[bneAddr + 1] = 0x71;
   jaguarMainRAM[bneAddr + 2] = 0x4E; jaguarMainRAM[bneAddr + 3] = 0x71;
   fprintf(stderr, "[CD-AUTH] Installed BNE.W $0504EC -> 2x NOP at $%06X\n", bneAddr);
   installed = true;
}

void JaguarDumpMemWindow(uint32_t centerPC, uint32_t before, uint32_t after)
{
   uint32_t start = (centerPC > before) ? (centerPC - before) : 0;
   uint32_t end = centerPC + after;
   uint32_t addr;
   fprintf(stderr, "[CD-AUTH] 68K memory @ $%06X (-%u..+%u):\n",
           centerPC, before, after);
   for (addr = start & ~0xF; addr < end; addr += 16)
   {
      int i;
      fprintf(stderr, "  $%06X:", addr);
      for (i = 0; i < 16; i += 2)
      {
         uint32_t a = addr + i;
         if (a < 0x200000)
            fprintf(stderr, " %02X%02X",
                    jaguarMainRAM[a], jaguarMainRAM[a + 1]);
         else
            fprintf(stderr, " ----");
      }
      fprintf(stderr, "\n");
   }
}

// Breakpoint on memory access vars (exported)
bool bpmActive = false;
uint32_t bpmAddress1;


/* Callback function to detect illegal instructions */
static bool start = false;

void M68KInstructionHook(void)
{
   unsigned i;
   uint32_t m68kPC = m68k_get_reg(NULL, M68K_REG_PC);
   static bool savedAuthVector = false;
   static bool restoredAuthVector = false;
   static uint32_t savedAuthLong = 0;

   // For tracebacks...
   // Ideally, we'd save all the registers as well...
   pcQueue[pcQPtr] = m68kPC;
   a0Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A0);
   a1Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A1);
   a2Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A2);
   a3Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A3);
   a4Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A4);
   a5Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A5);
   a6Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A6);
   a7Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_A7);
   d0Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D0);
   d1Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D1);
   d2Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D2);
   d3Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D3);
   d4Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D4);
   d5Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D5);
   d6Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D6);
   d7Queue[pcQPtr] = m68k_get_reg(NULL, M68K_REG_D7);
   pcQPtr++;
   pcQPtr &= 0x3FF;

   if (m68kPC & 0x01)		// Oops! We're fetching an odd address!
      return;

   /* HLE CD BIOS: intercept BIOS jump table calls (CD_read, etc.)
    * and handle them entirely in C.  Skip real-BIOS hooks when active. */
   if (JaguarCDHLEHook(m68kPC))
      return;

   /* CD BIOS GPU auth bypass: The CD BIOS checks GPU RAM $F03000 for the
    * boot ROM authentication magic ($03D0DEAD) after the intro animation.
    * The real GPU auth code would have left this value, but in emulation
    * the GPU security code never converges and the BIOS animation uses
    * GPU RAM (overwriting any pre-loaded value).  Re-write the magic
    * right before the BIOS reads it. */
   if (vjs.useCDBIOS && m68kPC == 0x005E40)
   {
      if (!savedAuthVector)
      {
         savedAuthLong = GPUReadLong(0xF03000, UNKNOWN);
         savedAuthVector = true;
      }
      fprintf(stderr, "[CD-TRACE] Re-applying auth magic at $F03000 before boot ROM check\n");
      GPUWriteLong(0xF03000, 0x03D0DEAD, 0);
   }

   /* Auth bypass hooks. Belt-and-suspenders with the pregap redirect:
    *   - Redirect feeds real TAIRTAIR audio for the first auth sector
    *   - Bypass forces the post-auth checks to take the success path even
    *     when the DSP doesn't compute the expected checksum (which it
    *     can't, since redumped BIN/CUE only has the TAIRTAIR header in
    *     sector 0; the rest of the auth window is silence in the file). */
   if (vjs.useCDBIOS)
   {
      /* Hook at PC=$050A9C: install BNE NOP before the BIOS gets there. */
      if (m68kPC == 0x050A9C)
         JaguarInstallCDAuthBypass();

      /* Hook at PC=$050AB2 (DSP-result MOVE.L): pre-stuff F1B4C8 with
       * $80010000 = "DSP done, pass". */
      if (m68kPC == 0x050AB2)
         DSPWriteLong(0x00F1B4C8, 0x80010000, UNKNOWN);

      /* Hook at PC=$050B0C (post-BSR MOVE.L / SUBQ): pre-stuff $FB000 with
       * $0A so the following BHI takes the success branch. */
      if (m68kPC == 0x050B0C)
         JaguarWriteLong(0x000FB000, 0x0000000A, UNKNOWN);

      /* Hook at PC=$0505FA (CMP.L $1AE00C, D1 — wait for CD response magic).
       * On real hardware, $1AE00C is updated by an interrupt handler when
       * the CD response is ready. Locally that handler isn't writing the
       * expected value, so we stuff it directly. */
      if (m68kPC == 0x0505FA)
      {
         static uint32_t stuffed = 0;
         JaguarWriteLong(0x001AE00C, 0x20010001, UNKNOWN);
         if (stuffed++ < 3)
            fprintf(stderr, "[CD-AUTH] Stuffed $1AE00C = $20010001 at PC=$0505FA (#%u)\n", stuffed);
      }

      /* Hook at PC=$050176 (the BIOS's `JSR $00080000` to enter the boot
       * stub).  By this point the cart populator has already filled $080000
       * with the CD Player UI fallback (the BIOS never streams game data
       * from disc to RAM in our emulation).  Extract the universal-header +
       * boot loader from the start of session 2 ourselves and overwrite
       * $080000 with the *game's* code so the JSR enters the title instead
       * of the CD Player. */
      if (m68kPC == 0x050176)
      {
         static bool bootStubInjected = false;
         if (!bootStubInjected)
         {
            static uint8_t stub[256 * 1024];
            uint32_t loadAddr = 0, length = 0;
            bootStubInjected = true;
            if (CDIntfExtractBootStub(stub, sizeof(stub), &loadAddr, &length))
            {
               uint32_t i;

               /* Dump the BIOS-populated $2C00 table BEFORE we touch anything.
                * The DSP TOC reader should have filled this already. */
               fprintf(stderr, "[CD-TOC-DUMP] $2C00 table before boot stub injection:\n");
               for (i = 0; i < 0x80; i += 8)
               {
                  uint32_t a = 0x2C00 + i;
                  if (jaguarMainRAM[a] == 0 && jaguarMainRAM[a+1] == 0
                   && jaguarMainRAM[a+2] == 0 && jaguarMainRAM[a+3] == 0
                   && jaguarMainRAM[a+4] == 0 && jaguarMainRAM[a+5] == 0
                   && jaguarMainRAM[a+6] == 0 && jaguarMainRAM[a+7] == 0)
                     continue;
                  fprintf(stderr, "  $%04X: %02X %02X %02X %02X  %02X %02X %02X %02X\n",
                          a,
                          jaguarMainRAM[a+0], jaguarMainRAM[a+1],
                          jaguarMainRAM[a+2], jaguarMainRAM[a+3],
                          jaguarMainRAM[a+4], jaguarMainRAM[a+5],
                          jaguarMainRAM[a+6], jaguarMainRAM[a+7]);
               }

               for (i = 0; i < length && (loadAddr + i) < 0x200000; i++)
                  jaguarMainRAM[loadAddr + i] = stub[i];
               fprintf(stderr,
                       "[CD-BOOTSTUB] Injected $%X bytes at $%06X "
                       "(replacing CD Player UI fallback)\n",
                       length, loadAddr);

               /* Do NOT call JaguarPopulateBIOSTocTable() — the BIOS DSP
                * should have already populated $2C00 with the correct format.
                * Our previous format was wrong and destroyed the real data. */
            }
            else
            {
               fprintf(stderr,
                       "[CD-BOOTSTUB] Extraction failed — falling through to CD Player UI\n");
            }
         }
      }
   }

   /* Boot stub TOC diagnostic: log what $0803E2 found in the $2C00 table.
    * If the BIOS DSP populated $2C00 correctly, the boot stub's search
    * should have set valid MSF values at $085D80-$085D85. */
   if (vjs.useCDBIOS && m68kPC == 0x0802A0)
   {
      static bool tocLogged = false;
      if (!tocLogged)
      {
         uint16_t frm = (jaguarMainRAM[0x085D80] << 8) | jaguarMainRAM[0x085D81];
         uint16_t sec = (jaguarMainRAM[0x085D82] << 8) | jaguarMainRAM[0x085D83];
         uint16_t min = (jaguarMainRAM[0x085D84] << 8) | jaguarMainRAM[0x085D85];
         fprintf(stderr,
                 "[CD-TOC-DIAG] Boot stub $0803E2 result: $085D80=%02X%02X "
                 "$085D82=%02X%02X $085D84=%02X%02X → MSF %u:%u:%u\n",
                 jaguarMainRAM[0x085D80], jaguarMainRAM[0x085D81],
                 jaguarMainRAM[0x085D82], jaguarMainRAM[0x085D83],
                 jaguarMainRAM[0x085D84], jaguarMainRAM[0x085D85],
                 min, sec, frm);
         tocLogged = true;
      }
   }

   /* CD BIOS: $3727C is the "CD ready" flag tested in the BIOS main loop at $5010.
    * On real hardware, the GPU CD code sets this after drive communication.
    * Keep this path observable, but do not force the value here. */
   if (vjs.useCDBIOS)
   {
      static bool authDone = false;
      static uint32_t pc5010Count = 0;
      static uint32_t instrCount = 0;
      static bool logged50BA = false;

      if (m68kPC == 0x005E64)
      {
         authDone = true;
         /* Do NOT restore the saved GPU RAM value — leave $03D0DEAD in
          * place.  On real hardware the auth code writes $03D0DEAD to
          * $F03000 and the BIOS's post-auth GPU program expects to find
          * it there.  Restoring the pre-auth value ($12345678 or whatever
          * the GPU security calc left) corrupts the post-auth flow, which
          * causes cascading failures in CD setup (wrong seek targets,
          * missing GPU ISR reload, etc.). */
         restoredAuthVector = true;
         fprintf(stderr, "[CD-TRACE] Auth PASSED (leaving $03D0DEAD at $F03000 for post-auth GPU)\n");
      }
      /* Observe BIOS polling of the CD-ready flag without modifying it. */
      if (authDone && m68kPC == 0x005010)
      {
         uint16_t ready = (jaguarMainRAM[0x3727C] << 8) | jaguarMainRAM[0x3727D];
         pc5010Count++;
         if (pc5010Count <= 5 || (pc5010Count % 100000) == 0)
            fprintf(stderr, "[CD-TRACE] 68K at $5010 (hit #%u, $3727C=%04X)\n",
                    pc5010Count, ready);
      }
      /* Log when 68K enters CD code path */
      if (authDone && m68kPC == 0x0050BA && !logged50BA)
      {
         logged50BA = true;
         fprintf(stderr, "[CD-TRACE] 68K entered CD code at $50BA ($3727C=%04X)\n",
                 (jaguarMainRAM[0x3727C] << 8) | jaguarMainRAM[0x3727D]);
      }

      /* Trace key BIOS CD function entries (addresses in BIOS ROM at $800000+) */
      {
         static bool loggedCDRead = false, loggedCDCallback = false;
         static bool logged1FD418Write = false;
         static uint32_t cdReadCount = 0, cdCallbackCount = 0;

         /* CD callback at $817E3C — checks $1AE02A, sets $1FD418 */
         if (m68kPC == 0x817E3C)
         {
            cdCallbackCount++;
            if (!loggedCDCallback || cdCallbackCount <= 10 || (cdCallbackCount % 10000) == 0)
            {
               loggedCDCallback = true;
               uint16_t ae02a = (jaguarMainRAM[0x1AE02A] << 8) | jaguarMainRAM[0x1AE02B];
               uint16_t af06c = (jaguarMainRAM[0x1AF06C] << 8) | jaguarMainRAM[0x1AF06D];
               uint16_t fd418 = (jaguarMainRAM[0x1FD418] << 8) | jaguarMainRAM[0x1FD419];
               fprintf(stderr, "[CD-TRACE] CD callback $817E3C hit #%u ($1AE02A=%04X $1AF06C=%04X $1FD418=%04X)\n",
                       cdCallbackCount, ae02a, af06c, fd418);
            }
         }
         /* CD_read single-speed entry at $818056 */
         if (m68kPC == 0x818056)
         {
            cdReadCount++;
            if (!loggedCDRead || cdReadCount <= 10 || (cdReadCount % 1000) == 0)
            {
               loggedCDRead = true;
               uint16_t fd418 = (jaguarMainRAM[0x1FD418] << 8) | jaguarMainRAM[0x1FD419];
               fprintf(stderr, "[CD-TRACE] CD_read $818056 hit #%u ($1FD418=%04X)\n",
                       cdReadCount, fd418);
            }
         }
         /* Detect when $1FD418 is first written to 1 */
         if (!logged1FD418Write &&
             jaguarMainRAM[0x1FD418] == 0x00 && jaguarMainRAM[0x1FD419] == 0x01)
         {
            logged1FD418Write = true;
            fprintf(stderr, "[CD-TRACE] $1FD418 = 1 detected! (68K PC=$%06X)\n", m68kPC);
         }
         /* Formatter at $195E3A (in RAM) — where TST.W $1FD418 is.
          * If the formatter loops with $1FD418=0 but we have CD data,
          * force-set it. This is a safety net for when the full BUTCH
          * interrupt → GPU ISR → CD callback chain doesn't fire. */
         static uint32_t formatterCount = 0;
         if (m68kPC == 0x195E3A)
         {
            uint16_t fd418 = (jaguarMainRAM[0x1FD418] << 8) | jaguarMainRAM[0x1FD419];
            formatterCount++;
            if (formatterCount <= 5 || (formatterCount % 100000) == 0)
               fprintf(stderr, "[CD-TRACE] Formatter $195E3A hit #%u ($1FD418=%04X)\n",
                       formatterCount, fd418);

            /* Formatter bypass disabled — data injection removed.
             * The BIOS must set $1FD418 through its normal code path
             * (GPU ISR / CD callback). */
         }
      }

      /* Periodic PC sampling to see where 68K spends time */
      if (authDone && (++instrCount % 5000000) == 0)
         fprintf(stderr, "[CD-TRACE] 68K PC=$%06X (sample #%u)\n", m68kPC, instrCount / 5000000);


      /* $192E46 = `TST.W $001A6800` polled in a wait loop together with
       * $00198CAC. These are BIOS-internal completion mailboxes set by GPU
       * code that we don't fully emulate. Stuff $1A6800 = 1 every time the
       * loop is entered so the BIOS proceeds to the next phase. */
      if (m68kPC == 0x192E46)
      {
         static uint32_t stuffed192E46 = 0;
         if (++stuffed192E46 <= 3)
            fprintf(stderr, "[CD-AUTH] Stuffed $1A6800=$0001 at PC=$192E46 (#%u)\n",
                    stuffed192E46);
         JaguarWriteWord(0x001A6800, 0x0001, UNKNOWN);
      }

      /* Trace first entry into CD Player UI region ($080000-$08FFFF)
       * from BIOS/elsewhere. CD Player UI is copied from CD-BIOS cart
       * into main RAM. We want the first BIOS-area → CD-Player branch. */
      {
         static uint32_t prevPC = 0;
         static bool loggedFirstEntry = false;
         static bool loggedFirstWrite = false;
         /* Detect when $080000 first becomes non-zero — the BIOS copies
          * either game code (if loadable) or the CD Player UI there. */
         if (!loggedFirstWrite && jaguarMainRAM[0x080000] == 0x60
             && jaguarMainRAM[0x080001] == 0x00)
         {
            loggedFirstWrite = true;
            fprintf(stderr, "[CD-LOAD-DETECT] $080000 now has BRA.W — populated by PC=$%06X\n",
                    prevPC);
         }
         bool prevInPlayer = (prevPC >= 0x080000 && prevPC < 0x090000);
         bool curInPlayer  = (m68kPC >= 0x080000 && m68kPC < 0x090000);
         if (!loggedFirstEntry && curInPlayer && !prevInPlayer)
         {
            loggedFirstEntry = true;
            fprintf(stderr, "[CD-PLAYER-ENTRY] First entry into $080000 region at $%06X from PC=$%06X\n",
                    m68kPC, prevPC);
            fprintf(stderr, "[CD-PLAYER-ENTRY] 68K regs: A0=$%08X A1=$%08X D0=$%08X D1=$%08X SR=$%04X\n",
                    m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                    m68k_get_reg(NULL, M68K_REG_SR));
         }
         prevPC = m68kPC;
      }

      /* One-shot dump of the game's main poll function context once we
       * see the game executing at $081220. Helps decode the outer caller.
       * Periodic state sample of the BIOS CD registers so we can see
       * whether the BIOS service chain (at $00194D18) is ever making
       * progress while the game polls. Empirically, it is not — the
       * service is never called, and $1AE02A (BIOS-tracked mode) stays
       * zero even after the game issues Set Mode 1 ($1501). */
      if (m68kPC == 0x081220)
      {
         static bool dumpedGamePoll = false;
         static uint32_t pollCount = 0;
         if (!dumpedGamePoll)
         {
            dumpedGamePoll = true;
            fprintf(stderr, "[CD-DUMP] Game poll function context @ $081220:\n");
            JaguarDumpMemWindow(0x081200, 0x20, 0x80);
            fprintf(stderr, "[CD-DUMP] Game CD-event flag area @ $0008B380:\n");
            JaguarDumpMemWindow(0x0008B380, 0x00, 0x40);
         }
         if (++pollCount <= 5 || (pollCount % 1000) == 0)
         {
            uint32_t cur = ((uint32_t)jaguarMainRAM[0x1AE00C] << 24)
                         | ((uint32_t)jaguarMainRAM[0x1AE00D] << 16)
                         | ((uint32_t)jaguarMainRAM[0x1AE00E] <<  8)
                         |  (uint32_t)jaguarMainRAM[0x1AE00F];
            uint32_t e032 = ((uint32_t)jaguarMainRAM[0x1AE032] << 24)
                          | ((uint32_t)jaguarMainRAM[0x1AE033] << 16)
                          | ((uint32_t)jaguarMainRAM[0x1AE034] <<  8)
                          |  (uint32_t)jaguarMainRAM[0x1AE035];
            uint16_t e02a = ((uint16_t)jaguarMainRAM[0x1AE02A] << 8)
                          |  (uint16_t)jaguarMainRAM[0x1AE02B];
            fprintf(stderr, "[CD-POLL] #%u $1AE00C=$%08X $1AE02A=$%04X $1AE032(+E034)=$%08X\n",
                    pollCount, cur, e02a, e032);
         }
      }

      /* One-shot dump of the BIOS service routines the game calls into. */
      if (m68kPC == 0x196446)
      {
         static bool dumped196446 = false;
         if (!dumped196446)
         {
            dumped196446 = true;
            fprintf(stderr, "[CD-DUMP] BIOS service @ $00196446:\n");
            JaguarDumpMemWindow(0x196446, 0x10, 0x100);
         }
      }
      /* $194DBC is CMPI.W #1, $001AE02A — the mode check that gates the
       * kick path at $194DEE. Sample what the BIOS sees here. */
      if (m68kPC == 0x194DBC)
      {
         static uint32_t dbcCount = 0;
         if (++dbcCount <= 5 || (dbcCount % 1000) == 0)
         {
            uint32_t c00c = ((uint32_t)jaguarMainRAM[0x1AE00C] << 24)
                          | ((uint32_t)jaguarMainRAM[0x1AE00D] << 16)
                          | ((uint32_t)jaguarMainRAM[0x1AE00E] <<  8)
                          |  (uint32_t)jaguarMainRAM[0x1AE00F];
            uint16_t e02a = ((uint16_t)jaguarMainRAM[0x1AE02A] << 8)
                          |  (uint16_t)jaguarMainRAM[0x1AE02B];
            fprintf(stderr, "[CD-194DBC] #%u $1AE00C=$%08X $1AE02A=$%04X\n",
                    dbcCount, c00c, e02a);
         }
      }
      if (m68kPC == 0x194DEE)
      {
         static uint32_t kickReachCount = 0;
         kickReachCount++;
         if (kickReachCount <= 3 || (kickReachCount % 100) == 0)
            fprintf(stderr, "[CD-194DEE] Reached kick path #%u — filling $1AE032=$0100\n",
                    kickReachCount);
      }
      /* One-shot dump of the hot BIOS wait loop identified by histogram
       * at $050BE0. Dump 64 bytes at first entry so we can decode the
       * branch condition. */
      if (m68kPC >= 0x050BE0 && m68kPC < 0x050C00)
      {
         static bool dumped050BE0 = false;
         if (!dumped050BE0)
         {
            dumped050BE0 = true;
            fprintf(stderr, "[CD-DUMP] Hot BIOS wait loop @ $050BE0 (first entry PC=$%06X):\n", m68kPC);
            JaguarDumpMemWindow(0x050BC0, 0x00, 0x80);
            fprintf(stderr, "[CD-DUMP] BIOS jump table @ $003000:\n");
            JaguarDumpMemWindow(0x003000, 0x00, 0x80);
            fprintf(stderr, "[CD-DUMP] 68K regs: D0=$%08X D1=$%08X D2=$%08X A0=$%08X A1=$%08X A7=$%08X\n",
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                    m68k_get_reg(NULL, M68K_REG_D2),
                    m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                    m68k_get_reg(NULL, M68K_REG_A7));
         }
      }
      /* One-shot dump at first execution of CD_read at $303C (if installed)
       * or its originating JSR site. Track entries into the jump-table region. */
      if (m68kPC >= 0x003000 && m68kPC < 0x003070)
      {
         static bool firstJTHit = false;
         static uint32_t jtPrevPC = 0;
         if (!firstJTHit)
         {
            firstJTHit = true;
            fprintf(stderr, "[CD-DUMP] First jump-table entry at $%06X from PC=$%06X\n",
                    m68kPC, jtPrevPC);
            JaguarDumpMemWindow(0x003000, 0x00, 0x80);
         }
         jtPrevPC = m68kPC;
      }
      if (m68kPC == 0x00303C)
      {
         static uint32_t fn303CCalls = 0;
         fn303CCalls++;
         if (fn303CCalls <= 3)
         {
            fprintf(stderr, "[CD-BIOS10] $303C call #%u D0=$%08X D1=$%08X D2=$%08X A0=$%08X A1=$%08X [$3072]=$%02X\n",
                    fn303CCalls,
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                    m68k_get_reg(NULL, M68K_REG_D2),
                    m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                    JaguarReadByte(0x003072, UNKNOWN));
            if (fn303CCalls == 1)
               JaguarDumpMemWindow(0x003590, 0x00, 0xC0);
         }
      }
      /* Trace BIOS function at $3610 (JSR $304E → BRA.W $3610). */
      if (m68kPC == 0x003610)
      {
         static uint32_t fn3610Calls = 0;
         fn3610Calls++;
         if (fn3610Calls == 1)
         {
            fprintf(stderr, "[CD-DUMP] BIOS $3610 first entry — code:\n");
            JaguarDumpMemWindow(0x003610, 0x00, 0x20);
            fprintf(stderr, "[CD-DUMP] Boot stub setup code ($080360-$0803F0):\n");
            JaguarDumpMemWindow(0x080360, 0x00, 0xA0);
            fprintf(stderr, "[CD-DUMP] Boot stub data ($085D90-$085E00):\n");
            JaguarDumpMemWindow(0x085D90, 0x00, 0x70);
            uint32_t structAddr = JaguarReadLong(0x003074, UNKNOWN);
            fprintf(stderr, "[CD-DUMP] GPU buf struct ($F03118+): $%08X $%08X $%08X\n",
                    GPUReadLong(0xF03118, UNKNOWN),
                    GPUReadLong(0xF0311C, UNKNOWN),
                    GPUReadLong(0xF03120, UNKNOWN));
         }
         if (fn3610Calls <= 10 || (fn3610Calls % 200000) == 0)
            fprintf(stderr, "[CD-POLL] $3610 call #%u: A0=$%08X A1=$%08X D0=$%08X gpu[$118/$11C/$120]=$%08X/$%08X/$%08X\n",
                    fn3610Calls,
                    m68k_get_reg(NULL, M68K_REG_A0),
                    m68k_get_reg(NULL, M68K_REG_A1),
                    m68k_get_reg(NULL, M68K_REG_D0),
                    GPUReadLong(0xF03118, UNKNOWN),
                    GPUReadLong(0xF0311C, UNKNOWN),
                    GPUReadLong(0xF03120, UNKNOWN));
      }
      /* Dump CD_read implementation at $003624 on first entry. */
      if (m68kPC == 0x003624)
      {
         static uint32_t cdReadCalls = 0;
         cdReadCalls++;
         if (cdReadCalls == 1)
         {
            fprintf(stderr, "[CD-DUMP] CD_read first call — code @ $003624:\n");
            JaguarDumpMemWindow(0x003624, 0x00, 0x200);
            fprintf(stderr, "[CD-DUMP] CD_read regs: D0=$%08X D1=$%08X D2=$%08X A0=$%08X A1=$%08X A2=$%08X\n",
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                    m68k_get_reg(NULL, M68K_REG_D2),
                    m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                    m68k_get_reg(NULL, M68K_REG_A2));
            uint8_t flag3072 = JaguarReadByte(0x003072, UNKNOWN);
            uint32_t structAddr = JaguarReadLong(0x003074, UNKNOWN);
            fprintf(stderr, "[CD-DUMP] [$3072]=$%02X (bit7=%d) [$3074]=$%08X\n",
                    flag3072, (flag3072 >> 7) & 1, structAddr);
            fprintf(stderr, "[CD-DUMP] GPU saved regs $F03FE0-$F03FFF:\n");
            for (uint32_t i = 0xF03FE0; i < 0xF04000; i += 4)
               fprintf(stderr, "  $%06X: $%08X\n", i, GPUReadLong(i, UNKNOWN));
         }
         if (cdReadCalls <= 10 || (cdReadCalls % 1000) == 0)
            fprintf(stderr, "[CD-DUMP] CD_read call #%u D0=$%08X A0=$%08X A1=$%08X\n",
                    cdReadCalls, m68k_get_reg(NULL, M68K_REG_D0),
                    m68k_get_reg(NULL, M68K_REG_A0),
                    m68k_get_reg(NULL, M68K_REG_A1));
      }
      /* Trace 68K ISR at $080250 (boot stub BUTCH handler). */
      if (m68kPC == 0x080250)
      {
         static uint32_t isrCount = 0;
         isrCount++;
         if (isrCount <= 10 || (isrCount % 50000) == 0)
         {
            uint32_t df8 = JaguarReadLong(0x085DF8, UNKNOWN);
            uint32_t df0 = JaguarReadLong(0x085DF0, UNKNOWN);
            uint32_t df4 = JaguarReadLong(0x085DF4, UNKNOWN);
            uint32_t dfc = JaguarReadLong(0x085DFC, UNKNOWN);
            fprintf(stderr, "[CD-ISR] $080250 hit #%u: $085DF8=$%08X $085DF0=$%08X $085DF4=$%08X $085DFC=$%08X\n",
                    isrCount, df8, df0, df4, dfc);
            if (isrCount == 1)
            {
               fprintf(stderr, "[CD-ISR] Full ISR code at $080250:\n");
               JaguarDumpMemWindow(0x080250, 0x00, 0x60);
            }
         }
      }
      if (m68kPC == 0x0803AA)
      {
         static uint32_t hitCount = 0;
         hitCount++;
         if (hitCount <= 5 || (hitCount % 50000) == 0)
         {
            uint32_t structAddr = JaguarReadLong(0x003074, UNKNOWN);
            uint32_t bufPtr = structAddr ? JaguarReadLong(structAddr, UNKNOWN) : 0;
            fprintf(stderr, "[BOOTSTUB] $0803AA hit #%u: A0=$%08X A1=$%08X A6=$%08X bufStruct=$%08X SR=$%04X\n",
                    hitCount,
                    m68k_get_reg(NULL, M68K_REG_A0),
                    m68k_get_reg(NULL, M68K_REG_A1),
                    m68k_get_reg(NULL, M68K_REG_A6),
                    bufPtr,
                    m68k_get_reg(NULL, M68K_REG_SR) & 0xFFFF);
         }
      }
      /* Stub the DSP completion at $F1B4C8 when the BIOS stalls in the
       * wait loop at $050BE2. We fake the DSP finishing by writing a
       * negative value after ~1000 polls. Lets the BIOS proceed so we
       * can see the next stall point. */
      if (m68kPC == 0x050BE2)
      {
         static uint32_t waitCount = 0;
         static uint32_t lastKickAt = 0;
         waitCount++;
         if (waitCount <= 5 || (waitCount % 100000) == 0)
         {
            uint32_t b4c8 = JaguarReadLong(0x00F1B4C8, UNKNOWN);
            uint32_t fb080 = JaguarReadWord(0x000FB080, UNKNOWN);
            fprintf(stderr, "[CD-WAIT] $050BE2 hit #%u $F1B4C8=$%08X retryCount=$%04X\n",
                    waitCount, b4c8, fb080);
         }
         /* Kick the flag after 1000 polls (so BIOS exits inner wait). */
         if (waitCount - lastKickAt >= 1000)
         {
            uint32_t b4c8 = JaguarReadLong(0x00F1B4C8, UNKNOWN);
            if ((b4c8 & 0x80000000) == 0)
            {
               JaguarWriteLong(0x00F1B4C8, 0x80000008, UNKNOWN);
               lastKickAt = waitCount;
               static uint32_t kickCount = 0;
               kickCount++;
               if (kickCount <= 10)
                  fprintf(stderr, "[CD-KICK] Forced $F1B4C8=$80000008 (kick #%u at waitCount=%u)\n",
                          kickCount, waitCount);
            }
         }
      }
      /* Similarly dump $050210 and $050220 hot buckets. */
      if (m68kPC >= 0x050200 && m68kPC < 0x050240)
      {
         static bool dumped050200 = false;
         if (!dumped050200)
         {
            dumped050200 = true;
            fprintf(stderr, "[CD-DUMP] Hot BIOS loop @ $050200 (first entry PC=$%06X):\n", m68kPC);
            JaguarDumpMemWindow(0x050200, 0x00, 0x60);
         }
      }
      /* Dump $050860 area (3rd hottest). */
      if (m68kPC >= 0x050860 && m68kPC < 0x050880)
      {
         static bool dumped050860 = false;
         if (!dumped050860)
         {
            dumped050860 = true;
            fprintf(stderr, "[CD-DUMP] Hot BIOS loop @ $050860 (first entry PC=$%06X):\n", m68kPC);
            JaguarDumpMemWindow(0x050860, 0x00, 0x40);
         }
      }
      /* Fine-grained PC histogram for $050000-$050FFF and $083000-$083FFF.
       * 16-byte buckets to pinpoint the tight wait loop. */
      {
         static uint32_t bios5k[0x100] = {0};
         static uint32_t cdp83[0x100] = {0};
         static uint32_t histSample = 0;
         if (m68kPC >= 0x050000 && m68kPC < 0x051000)
            bios5k[(m68kPC >> 4) & 0xFF]++;
         else if (m68kPC >= 0x083000 && m68kPC < 0x084000)
            cdp83[(m68kPC >> 4) & 0xFF]++;
         if (++histSample >= 3000000)
         {
            histSample = 0;
            fprintf(stderr, "[CD-HIST-5K] $05xxx top 6 (16-byte buckets):\n");
            for (int rank = 0; rank < 6; rank++)
            {
               uint32_t best = 0; int bestIdx = -1;
               for (int i = 0; i < 0x100; i++)
                  if (bios5k[i] > best) { best = bios5k[i]; bestIdx = i; }
               if (!best) break;
               fprintf(stderr, "  $%06X: %u\n", 0x050000 + (bestIdx << 4), best);
               bios5k[bestIdx] = 0;
            }
            fprintf(stderr, "[CD-HIST-83] $083xxx top 6:\n");
            for (int rank = 0; rank < 6; rank++)
            {
               uint32_t best = 0; int bestIdx = -1;
               for (int i = 0; i < 0x100; i++)
                  if (cdp83[i] > best) { best = cdp83[i]; bestIdx = i; }
               if (!best) break;
               fprintf(stderr, "  $%06X: %u\n", 0x083000 + (bestIdx << 4), best);
               cdp83[bestIdx] = 0;
            }
            memset(bios5k, 0, sizeof(bios5k));
            memset(cdp83, 0, sizeof(cdp83));
         }
      }

      if (m68kPC == 0x194D18)
      {
         static bool dumped194D18 = false;
         if (!dumped194D18)
         {
            dumped194D18 = true;
            fprintf(stderr, "[CD-DUMP] BIOS service @ $00194D18:\n");
            JaguarDumpMemWindow(0x194D18, 0x40, 0x100);
         }
      }

   }
}

/* Custom UAE 68000 read/write/IRQ functions */

int irq_ack_handler(int level)
{
   // Tracing the IPL lines on the Jaguar schematic yields the following:
   // IPL1 is connected to INTL on TOM (OUT to 68K)
   // IPL0-2 are also tied to Vcc via 4.7K resistors!
   // (DINT on TOM goes into DINT on JERRY (IN Tom from Jerry))
   // There doesn't seem to be any other path to IPL0 or 2 on the schematic, which means
   // that *all* IRQs to the 68K are routed thru TOM at level 2. Which means they're all maskable.

   // The GPU/DSP/etc are probably *not* issuing an NMI, but it seems to work OK...
   // They aren't, and this causes problems with a, err, specific ROM. :-D

   if (level == 2)
   {
      m68k_set_irq(0);						// Clear the IRQ (NOTE: Without this, the BIOS fails)...
      return 64;								// Set user interrupt #0
   }

   return M68K_INT_ACK_AUTOVECTOR;
}


//#define USE_NEW_MMU

unsigned int m68k_read_memory_8(unsigned int address)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;
#ifndef USE_NEW_MMU
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFF))
      return jaguarMainRAM[address];
   else if ((address >= 0x800000) && (address <= 0xDFFEFF))
      return jaguarMainROM[address - 0x800000];
   else if ((address >= 0xE00000) && (address <= 0xE3FFFF))
      return jagMemSpace[address];
   else if ((address >= 0xDFFF00) && (address <= 0xDFFFFF))
      return CDROMReadByte(address, UNKNOWN);
   else if ((address >= 0xF00000) && (address <= 0xF0FFFF))
      return TOMReadByte(address, M68K);
   else if ((address >= 0xF10000) && (address <= 0xF1FFFF))
      return JERRYReadByte(address, M68K);
   else
      return jaguar_unknown_readbyte(address, M68K);

   return 0;
#else
   return MMURead8(address, M68K);
#endif
}


void gpu_dump_disassembly(void);
void gpu_dump_registers(void);

unsigned int m68k_read_memory_16(unsigned int address)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;

#ifndef USE_NEW_MMU
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFE))
      return GET16(jaguarMainRAM, address);
   else if ((address >= 0x800000) && (address <= 0xDFFEFE))
   {
      /* Memory Track reading... */
      if (((TOMGetMEMCON1() & 0x0006) == (2 << 1)) && (jaguarMainROMCRC32 == 0xFDF37F47))
         return MTReadWord(address);
      else
         return (jaguarMainROM[address - 0x800000] << 8)
            | jaguarMainROM[address - 0x800000 + 1];
   }
   else if ((address >= 0xE00000) && (address <= 0xE3FFFE))
      return (jagMemSpace[address] << 8) | jagMemSpace[address + 1];
   else if ((address >= 0xDFFF00) && (address <= 0xDFFFFE))
      return CDROMReadWord(address, M68K);
   else if ((address >= 0xF00000) && (address <= 0xF0FFFE))
      return TOMReadWord(address, M68K);
   else if ((address >= 0xF10000) && (address <= 0xF1FFFE))
      return JERRYReadWord(address, M68K);

   return jaguar_unknown_readword(address, M68K);
#else
   return MMURead16(address, M68K);
#endif
}


unsigned int m68k_read_memory_32(unsigned int address)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;

#ifndef USE_NEW_MMU
   if ((address >= 0x800000) && (address <= 0xDFFEFE))
   {
      // Memory Track reading...
      if (((TOMGetMEMCON1() & 0x0006) == (2 << 1)) && (jaguarMainROMCRC32 == 0xFDF37F47))
         return MTReadLong(address);

      return GET32(jaguarMainROM, address - 0x800000);
   }

   return (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
#else
   return MMURead32(address, M68K);
#endif
}


void m68k_write_memory_8(unsigned int address, unsigned int value)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;

#ifndef USE_NEW_MMU
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFF))
      jaguarMainRAM[address] = value;
   else if ((address >= 0xDFFF00) && (address <= 0xDFFFFF))
      CDROMWriteByte(address, value, M68K);
   else if ((address >= 0xF00000) && (address <= 0xF0FFFF))
      TOMWriteByte(address, value, M68K);
   else if ((address >= 0xF10000) && (address <= 0xF1FFFF))
      JERRYWriteByte(address, value, M68K);
   else
      jaguar_unknown_writebyte(address, value, M68K);
#else
   MMUWrite8(address, value, M68K);
#endif
}


void m68k_write_memory_16(unsigned int address, unsigned int value)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;

#ifndef USE_NEW_MMU
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFE))
   {
      SET16(jaguarMainRAM, address, value);
   }
   /* Memory Track device writes.... */
   else if ((address >= 0x800000) && (address <= 0x87FFFE))
   {
      if (((TOMGetMEMCON1() & 0x0006) == (2 << 1)) && (jaguarMainROMCRC32 == 0xFDF37F47))
         MTWriteWord(address, value);
   }
   else if ((address >= 0xDFFF00) && (address <= 0xDFFFFE))
      CDROMWriteWord(address, value, M68K);
   else if ((address >= 0xF00000) && (address <= 0xF0FFFE))
      TOMWriteWord(address, value, M68K);
   else if ((address >= 0xF10000) && (address <= 0xF1FFFE))
      JERRYWriteWord(address, value, M68K);
   else
   {
      jaguar_unknown_writeword(address, value, M68K);
   }
#else
   MMUWrite16(address, value, M68K);
#endif
}


void m68k_write_memory_32(unsigned int address, unsigned int value)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;

#ifndef USE_NEW_MMU
   m68k_write_memory_16(address, value >> 16);
   m68k_write_memory_16(address + 2, value & 0xFFFF);
#else
   MMUWrite32(address, value, M68K);
#endif
}

/* Disassemble M68K instructions at the given offset */

unsigned int m68k_read_disassembler_8(unsigned int address)
{
   return m68k_read_memory_8(address);
}


unsigned int m68k_read_disassembler_16(unsigned int address)
{
   return m68k_read_memory_16(address);
}


unsigned int m68k_read_disassembler_32(unsigned int address)
{
   return m68k_read_memory_32(address);
}

uint8_t JaguarReadByte(uint32_t offset, uint32_t who)
{
   offset &= 0xFFFFFF;

   // First 2M is mirrored in the $0 - $7FFFFF range
   if (offset < 0x800000)
      return jaguarMainRAM[offset & 0x1FFFFF];
   else if ((offset >= 0x800000) && (offset < 0xDFFF00))
   {
      if (CDROMIsBiosOverride())
         return CDROMReadFifoByte(who);
      return jaguarMainROM[offset - 0x800000];
   }
   else if ((offset >= 0xDFFF00) && (offset <= 0xDFFFFF))
      return CDROMReadByte(offset, who);
   else if ((offset >= 0xE00000) && (offset < 0xE40000))
      return jagMemSpace[offset];
   else if ((offset >= 0xF00000) && (offset < 0xF10000))
      return TOMReadByte(offset, who);
   else if ((offset >= 0xF10000) && (offset < 0xF20000))
      return JERRYReadByte(offset, who);
   else
      return jaguar_unknown_readbyte(offset, who);

   return 0x00;
}

uint16_t JaguarReadWord(uint32_t offset, uint32_t who)
{
   offset &= 0xFFFFFF;

   // First 2M is mirrored in the $0 - $7FFFFF range
   if (offset < 0x800000)
      return (jaguarMainRAM[(offset+0) & 0x1FFFFF] << 8) | jaguarMainRAM[(offset+1) & 0x1FFFFF];
   else if ((offset >= 0x800000) && (offset < 0xDFFF00))
   {
      if (CDROMIsBiosOverride())
         return (CDROMReadFifoByte(who) << 8) | CDROMReadFifoByte(who);
      offset -= 0x800000;
      return (jaguarMainROM[offset+0] << 8) | jaguarMainROM[offset+1];
   }
   //	else if ((offset >= 0xDFFF00) && (offset < 0xDFFF00))
   else if ((offset >= 0xDFFF00) && (offset <= 0xDFFFFE))
      return CDROMReadWord(offset, who);
   else if ((offset >= 0xE00000) && (offset <= 0xE3FFFE))
      return (jagMemSpace[offset + 0] << 8) | jagMemSpace[offset + 1];
   else if ((offset >= 0xF00000) && (offset <= 0xF0FFFE))
      return TOMReadWord(offset, who);
   else if ((offset >= 0xF10000) && (offset <= 0xF1FFFE))
      return JERRYReadWord(offset, who);

   return jaguar_unknown_readword(offset, who);
}


void JaguarWriteByte(uint32_t offset, uint8_t data, uint32_t who)
{
   offset &= 0xFFFFFF;

   // First 2M is mirrored in the $0 - $7FFFFF range
   if (offset < 0x800000)
   {
      jaguarMainRAM[offset & 0x1FFFFF] = data;
      return;
   }
   else if ((offset >= 0xDFFF00) && (offset <= 0xDFFFFF))
   {
      CDROMWriteByte(offset, data, who);
      return;
   }
   else if ((offset >= 0xF00000) && (offset <= 0xF0FFFF))
   {
      TOMWriteByte(offset, data, who);
      return;
   }
   else if ((offset >= 0xF10000) && (offset <= 0xF1FFFF))
   {
      JERRYWriteByte(offset, data, who);
      return;
   }

   jaguar_unknown_writebyte(offset, data, who);
}


void JaguarWriteWord(uint32_t offset, uint16_t data, uint32_t who)
{
   offset &= 0xFFFFFF;

   // First 2M is mirrored in the $0 - $7FFFFF range
   if (offset <= 0x7FFFFE)
   {
      uint32_t ramOff = (offset + 0) & 0x1FFFFF;
      /* GPU-scoped trace: log writes to main RAM while the GPU is running,
       * restricted to the CD BIOS workspace range ($30000-$200000).  Rate-limit
       * per unique address so the first few writes to each slot are logged. */
      /* Exclude blitter-sourced writes — the blitter is used for bulk memory
       * clears and would drown the log.  Keep 68K / GPU / DSP writes. */
      if (vjs.useCDBIOS && GPUIsRunning() && who != BLITTER
          && ramOff >= 0x30000 && ramOff < 0x200000)
      {
         static uint32_t seen_addrs[64] = {0};
         static uint32_t seen_hits[64] = {0};
         static unsigned seen_n = 0;
         unsigned i;
         int idx = -1;
         for (i = 0; i < seen_n; i++)
            if (seen_addrs[i] == ramOff) { idx = (int)i; break; }
         if (idx < 0 && seen_n < 64)
         {
            seen_addrs[seen_n] = ramOff;
            seen_hits[seen_n] = 0;
            idx = (int)seen_n++;
         }
         if (idx >= 0 && seen_hits[idx] < 3)
         {
            seen_hits[idx]++;
            fprintf(stderr,
                    "[GPU-WRITE] $%06X = $%04X (GPU_PC=$%06X who=%u)\n",
                    ramOff, data, GPUGetPC(), who);
         }
      }
      /* Track writes to the game's CD-event flag at $0008B398.
       * Game's poll function at $081220 returns RTS unless either
       * BUTCH bit13 (DSARX) or this longword is non-zero. We never
       * deliver BUTCH IRQs (game uses polling), so this flag is the
       * only path that wakes the game's main loop. */
      if (vjs.useCDBIOS && (ramOff == 0x08B398 || ramOff == 0x08B39A))
      {
         static uint32_t b398Count = 0;
         if (++b398Count <= 20)
            fprintf(stderr, "[CD-FLAG] $%06X = $%04X who=%u 68K_PC=$%06X GPU_PC=$%06X\n",
                    ramOff, data, who,
                    m68k_get_reg(NULL, M68K_REG_PC), GPUGetPC());
      }
      jaguarMainRAM[(offset+0) & 0x1FFFFF] = data >> 8;
      jaguarMainRAM[(offset+1) & 0x1FFFFF] = data & 0xFF;
      return;
   }
   else if (offset >= 0xDFFF00 && offset <= 0xDFFFFE)
   {
      CDROMWriteWord(offset, data, who);
      return;
   }
   else if (offset >= 0xF00000 && offset <= 0xF0FFFE)
   {
      TOMWriteWord(offset, data, who);
      return;
   }
   else if (offset >= 0xF10000 && offset <= 0xF1FFFE)
   {
      JERRYWriteWord(offset, data, who);
      return;
   }
   // Don't bomb on attempts to write to ROM
   else if (offset >= 0x800000 && offset <= 0xEFFFFF)
      return;

   jaguar_unknown_writeword(offset, data, who);
}


// We really should re-do this so that it does *real* 32-bit access... !!! FIX !!!
uint32_t JaguarReadLong(uint32_t offset, uint32_t who)
{
   return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset+2, who);
}


// We really should re-do this so that it does *real* 32-bit access... !!! FIX !!!
void JaguarWriteLong(uint32_t offset, uint32_t data, uint32_t who)
{
   JaguarWriteWord(offset, data >> 16, who);
   JaguarWriteWord(offset+2, data & 0xFFFF, who);
}


void JaguarSetScreenBuffer(uint32_t * buffer)
{
   // This is in TOM, but we set it here...
   screenBuffer = buffer;
}


void JaguarSetScreenPitch(uint32_t pitch)
{
   // This is in TOM, but we set it here...
   screenPitch = pitch;
}

/* Jaguar console initialization */
void JaguarInit(void)
{
   unsigned i;
   // Reset the platform-independent PRNG for deterministic RAM fill
   JaguarSeedPRNG(12345);

   // Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents
   for(i=0; i<0x200000; i+=4)
      *((uint32_t *)(&jaguarMainRAM[i])) = JaguarRand();

   lowerField = false;							// Reset the lower field flag
   memset(jaguarMainRAM + 0x804, 0xFF, 4);

   m68k_pulse_reset();							// Need to do this so UAE disasm doesn't segfault on exit
   GPUInit();
   DSPInit();
   TOMInit();
   JERRYInit();
   CDROMInit();

}

/* New timer based code stuffola... */

// The thing to keep in mind is that the VC is advanced every HALF line, regardless
// of whether the display is interlaced or not. The only difference with an
// interlaced display is that the high bit of VC will be set when the lower
// field is being rendered. (NB: The high bit of VC is ALWAYS set on the lower field,
// regardless of whether it's in interlace mode or not.
// NB2: Seems it doens't always, not sure what the constraint is...)
//
// Normally, TVs will render a full frame in 1/30s (NTSC) or 1/25s (PAL) by
// rendering two fields that are slighty vertically offset from each other.
// Each field is created in 1/60s (NTSC) or 1/50s (PAL), and every other line
// is rendered in this mode so that each field, when overlaid on each other,
// will yield the final picture at the full resolution for the full frame.
//
// We execute a half frame in each timeslice (1/60s NTSC, 1/50s PAL).
// Since the number of lines in a FULL frame is 525 for NTSC, 625 for PAL,
// it will be half this number for a half frame. BUT, since we're counting
// HALF lines, we double this number and we're back at 525 for NTSC, 625 for PAL.
//
// Scanline times are 63.5555... μs in NTSC and 64 μs in PAL
// Half line times are, naturally, half of this. :-P
void HalflineCallback(void)
{
   uint16_t vc           = TOMReadWord(0xF00006, JAGUAR);
   uint16_t vp           = TOMReadWord(0xF0003E, JAGUAR) + 1;
   uint16_t vi           = TOMReadWord(0xF0004E, JAGUAR);
   // Each # of lines is for a full frame == 1/30s (NTSC), 1/25s (PAL).
   // So we cut the number of half-lines in a frame in half. :-P
   uint16_t numHalfLines = ((vjs.hardwareTypeNTSC ? 525 : 625) * 2) / 2;

   vc++;

   if ((vc & 0x7FF) >= numHalfLines)
   {
      lowerField = !lowerField;
      // If we're rendering the lower field, set the high bit (#11, counting
      // from 0) of VC
      vc         = (lowerField ? 0x0800 : 0x0000);
   }

   TOMWriteWord(0xF00006, vc, JAGUAR);

   // Time for Vertical Interrupt?
   if ((vc & 0x7FF) == vi && (vc & 0x7FF) > 0 && TOMIRQEnabled(IRQ_VIDEO))
   {
      // We don't have to worry about autovectors & whatnot because the Jaguar
      // tells you through its HW registers who sent the interrupt...
      TOMSetPendingVideoInt();
      m68k_set_irq(2);
   }

   TOMExecHalfline(vc, true);

   //Change this to VBB???
   //Doesn't seem to matter (at least for Flip Out & I-War)
   if ((vc & 0x7FF) == 0)
   {
      JoystickExec();
      frameDone = true;
   }

   SetCallbackTime(HalflineCallback, (vjs.hardwareTypeNTSC ? 31.777777777 : 32.0), EVENT_MAIN);
}

void JaguarReset(void)
{
   unsigned i;

   // Only problem with this approach: It wipes out RAM loaded files...!
   // Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents
   JaguarSeedPRNG(12345);
   for(i=8; i<0x200000; i+=4)
      *((uint32_t *)(&jaguarMainRAM[i])) = JaguarRand();

   // New timer base code stuffola...
   InitializeEventList();
   //Need to change this so it uses the single RAM space and load the BIOS
   //into it somewhere...
   //Also, have to change this here and in JaguarReadXX() currently
   // Only use the system BIOS if it's available...! (it's always available now!)
   // AND only if a jaguar cartridge has been inserted.
   if (vjs.useJaguarBIOS && jaguarCartInserted && !vjs.hardwareTypeAlpine)
      memcpy(jaguarMainRAM, jagMemSpace + 0xE00000, 8);
   else
      SET32(jaguarMainRAM, 4, jaguarRunAddress);

   TOMReset();
   JERRYReset();
   GPUReset();
   DSPReset();
   CDROMReset();
   m68k_pulse_reset();								// Reset the 68000

   lowerField = false;								// Reset the lower field flag
   SetCallbackTime(HalflineCallback, (vjs.hardwareTypeNTSC ? 31.777777777 : 32.0), EVENT_MAIN);
}


void JaguarDone(void)
{
   CDROMDone();
   DSPDone();
   TOMDone();
   JERRYDone();
}

uint8_t * GetRamPtr(void)
{
   return jaguarMainRAM;
}


/* New Jaguar execution stack
 * This executes 1 frame's worth of code. */

void JaguarExecuteNew(void)
{
   frameDone = false;

   do
   {
      double timeToNextEvent = GetTimeToNextEvent(EVENT_MAIN);
      m68k_execute(USEC_TO_M68K_CYCLES(timeToNextEvent));
      GPUExec(USEC_TO_RISC_CYCLES(timeToNextEvent));
      BUTCHExec(USEC_TO_RISC_CYCLES(timeToNextEvent));
      HandleNextEvent(EVENT_MAIN);
   } while(!frameDone);
}

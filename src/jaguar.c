#define HLE_DIAG 0
#ifndef LLDB_TRAP
#define LLDB_TRAP 0
#endif
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

#include "log.h"
#include "cdintf.h"
#include "cdrom.h"
#include "jagcd_boot.h"
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

#if LLDB_TRAP
#include <signal.h>
static uint32_t lldb_last_good_pc = 0;
static int lldb_trap_armed = 0;
static unsigned lldb_trap_frame = 0;
#endif

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
uint32_t jaguarLoadedRAMStart, jaguarLoadedRAMEnd;

bool jaguarCartInserted = false;
bool lowerField = false;


uint32_t pcQueue[0x400];
uint32_t pcQPtr = 0;

void JaguarDumpPCHistoryStderr(int count)
{
   int n = (count > 0x400) ? 0x400 : count;
   int i;
   LOG_DBG("[CD-AUTH] 68K PC history (newest first, %d entries):\n", n);
   for (i = 0; i < n; i++)
   {
      /* pcQPtr has already been incremented past the last write, so
       * entry (pcQPtr - 1) is newest. */
      uint32_t idx = (pcQPtr - 1 - i) & 0x3FF;
      LOG_DBG("  [-%d] PC=$%06X\n", i, pcQueue[idx]);
   }
}

void JaguarDumpMemWindow(uint32_t centerPC, uint32_t before, uint32_t after)
{
   uint32_t start = (centerPC > before) ? (centerPC - before) : 0;
   uint32_t end = centerPC + after;
   uint32_t addr;
   LOG_DBG("[CD-AUTH] 68K memory @ $%06X (-%u..+%u):\n",
           centerPC, before, after);
   for (addr = start & ~0xF; addr < end; addr += 16)
   {
      int i;
      LOG_DBG("  $%06X:", addr);
      for (i = 0; i < 16; i += 2)
      {
         uint32_t a = addr + i;
         if (a < 0x200000)
            LOG_DBG(" %02X%02X",
                    jaguarMainRAM[a], jaguarMainRAM[a + 1]);
         else
            LOG_DBG(" ----");
      }
      LOG_DBG("\n");
   }
}

// Breakpoint on memory access vars (exported)
bool bpmActive = false;
uint32_t bpmAddress1;


/* Callback function to detect illegal instructions */
static bool start = false;

void M68KInstructionHook(void)
{
   uint32_t m68kPC = m68k_get_reg(NULL, M68K_REG_PC);

   pcQueue[pcQPtr] = m68kPC;
   pcQPtr = (pcQPtr + 1) & 0x3FF;

#if LLDB_TRAP
   if (lldb_trap_armed && m68kPC == 0x418E)
   {
      LOG_WRN("[TRAP] PC=$%06X entered data zone! last_good=$%06X frame=%u\n",
              m68kPC, lldb_last_good_pc, lldb_trap_frame);
      LOG_DBG("[TRAP] SP=$%08X SR=$%04X\n",
              m68k_get_reg(NULL, M68K_REG_A7),
              m68k_get_reg(NULL, M68K_REG_SR));
      for (int r = 0; r <= 7; r++)
         LOG_DBG("[TRAP] D%d=$%08X A%d=$%08X\n", r,
                 m68k_get_reg(NULL, M68K_REG_D0 + r), r,
                 m68k_get_reg(NULL, M68K_REG_A0 + r));
      LOG_DBG("[TRAP] PC history (last 64):\n");
      for (int i = 63; i >= 0; i--)
         LOG_DBG("  %2d: $%06X\n", i, pcQueue[(pcQPtr - 1 - i) & 0x3FF]);
      /* Dump code at the call site and destination */
      LOG_DBG("[TRAP] Code at $005410-$005430 (JSR source):\n");
      for (uint32_t a = 0x5410; a < 0x5430; a += 2)
         LOG_DBG("  $%06X: %02X%02X\n", a, jaguarMainRAM[a], jaguarMainRAM[a+1]);
      LOG_DBG("[TRAP] Code at $0053C0-$005430 (loop):\n");
      for (uint32_t a = 0x53C0; a < 0x5430; a += 2)
         LOG_DBG("  $%06X: %02X%02X\n", a, jaguarMainRAM[a], jaguarMainRAM[a+1]);
      LOG_DBG("[TRAP] Code at $010890-$0108C0 (DDL target):\n");
      for (uint32_t a = 0x10890; a < 0x108C0; a += 2)
         LOG_DBG("  $%06X: %02X%02X\n", a, jaguarMainRAM[a], jaguarMainRAM[a+1]);
      LOG_DBG("[TRAP] Code at $005190-$0051F0 (return path):\n");
      for (uint32_t a = 0x5190; a < 0x51F0; a += 2)
         LOG_DBG("  $%06X: %02X%02X\n", a, jaguarMainRAM[a], jaguarMainRAM[a+1]);
      LOG_DBG("[TRAP] Code at $006900-$006920 (pre-call):\n");
      for (uint32_t a = 0x6900; a < 0x6920; a += 2)
         LOG_DBG("  $%06X: %02X%02X\n", a, jaguarMainRAM[a], jaguarMainRAM[a+1]);
      LOG_DBG("[TRAP] Code at $039690-$039720 (error check):\n");
      for (uint32_t a = 0x39690; a < 0x39720; a += 2)
         LOG_DBG("  $%06X: %02X%02X\n", a, jaguarMainRAM[a], jaguarMainRAM[a+1]);
      LOG_DBG("[TRAP] Code at $004180-$0041A0 (error handler):\n");
      for (uint32_t a = 0x4180; a < 0x41A0; a += 2)
         LOG_DBG("  $%06X: %02X%02X\n", a, jaguarMainRAM[a], jaguarMainRAM[a+1]);
      LOG_DBG("[TRAP] Stack ($1FFFD0-$200000):\n");
      for (uint32_t a = 0x1FFFD0; a < 0x200000; a += 4)
         LOG_DBG("  $%06X: $%08X\n", a, GET32(jaguarMainRAM, a));
      raise(SIGTRAP);
   }
   if (m68kPC < 0x200000 || (m68kPC >= 0x800000 && m68kPC < 0xE00000))
      lldb_last_good_pc = m68kPC;
#endif

   if (m68kPC & 0x01)
      return;

   if (bootConfig.strategy && bootConfig.strategy->instruction_hook(m68kPC))
      return;

#if HLE_DIAG
      /* Lightweight PC histogram: bucket by 256-byte range, dump periodically */
      {
         #define HIST_BUCKETS 8192  /* covers 0-$1FFFFF (2MB RAM) */
         static uint32_t pcHistBuf[HIST_BUCKETS];
         static uint64_t histTotal = 0;
         static uint32_t histDumps = 0;

         if (m68kPC < 0x200000)
            pcHistBuf[m68kPC >> 8]++;
         else if (m68kPC >= 0x800000 && m68kPC < 0xA00000)
            pcHistBuf[(m68kPC - 0x800000 + 0x200000) >> 8]++;
         histTotal++;

         if (histTotal == 5000000 || histTotal == 20000000)
         {
            LOG_DBG("[HLE-HIST] After %lluM instructions, top 20 PC ranges:\n",
                    (unsigned long long)(histTotal / 1000000));
            /* Find top 20 */
            for (int t = 0; t < 20; t++)
            {
               uint32_t maxIdx = 0, maxVal = 0;
               for (uint32_t b = 0; b < HIST_BUCKETS; b++)
                  if (pcHistBuf[b] > maxVal) { maxVal = pcHistBuf[b]; maxIdx = b; }
               if (maxVal == 0) break;
               uint32_t addr = maxIdx < (0x200000 >> 8) ?
                  (maxIdx << 8) : ((maxIdx << 8) - 0x200000 + 0x800000);
               LOG_DBG("  $%06X-$%06X: %u (%.1f%%)\n",
                       addr, addr + 0xFF, maxVal, 100.0 * maxVal / histTotal);
               pcHistBuf[maxIdx] = 0; /* remove from further search */
            }
            histDumps++;
         }

         /* One-shot GPU state dump when we first enter the VSync spin at $4550-$4580 */
         {
            static bool dumpedVSync = false;
            if (!dumpedVSync && m68kPC >= 0x4550 && m68kPC < 0x4580)
            {
               dumpedVSync = true;
               LOG_DBG("[HLE-VSYNC] First VSync entry at PC=$%06X\n", m68kPC);
               GPUDumpState("VSync-entry");
               /* Dump the frame counter at $063780 */
               LOG_DBG("[HLE-VSYNC] Frame counter ($063780): $%04X\n",
                       GET16(jaguarMainRAM, 0x063780));
               /* Dump GPU RAM code at the GPU PC area */
               uint32_t gpc = GPUGetPC();
               if (gpc >= 0xF03000 && gpc < 0xF03FF0)
               {
                  LOG_DBG("[HLE-VSYNC] GPU code at PC=$%06X:\n", gpc);
                  for (uint32_t a = gpc; a < gpc + 64 && a < 0xF04000; a += 4)
                     LOG_DBG("  %06X: %08X\n", a, GPUReadLong(a, M68K));
               }
            }
         }

         /* Periodic GPU state check while in VSync spin */
         {
            static uint32_t vsyncSpinCount = 0;
            if (m68kPC >= 0x456E && m68kPC <= 0x4572)
            {
               vsyncSpinCount++;
               if (vsyncSpinCount == 1000 || vsyncSpinCount == 100000 || vsyncSpinCount == 1000000)
               {
                  LOG_DBG("[HLE-VSYNC] Spin #%u at PC=$%06X, GPU running=%d GPU_PC=$%06X\n",
                          vsyncSpinCount, m68kPC, GPUIsRunning(), GPUGetPC());
                  LOG_DBG("[HLE-VSYNC]   $063780=$%04X mailbox($F03E9C)=$%08X\n",
                          GET16(jaguarMainRAM, 0x063780),
                          GPUReadLong(0xF03E9C, M68K));
               }
            }
         }

         /* One-shot dump when we first enter the $4100-$41FF loading loop */
         {
            static bool dumped4100Loop = false;
            if (!dumped4100Loop && m68kPC >= 0x4100 && m68kPC < 0x4200)
            {
               dumped4100Loop = true;
               LOG_DBG("[HLE-LOOP2] First entry to $%06X — dumping code $4100-$4220:\n", m68kPC);
               for (uint32_t a = 0x4100; a < 0x4220; a += 16)
                  LOG_DBG("  %06X: %02X%02X %02X%02X %02X%02X %02X%02X "
                          "%02X%02X %02X%02X %02X%02X %02X%02X\n", a,
                          jaguarMainRAM[a+0],  jaguarMainRAM[a+1],
                          jaguarMainRAM[a+2],  jaguarMainRAM[a+3],
                          jaguarMainRAM[a+4],  jaguarMainRAM[a+5],
                          jaguarMainRAM[a+6],  jaguarMainRAM[a+7],
                          jaguarMainRAM[a+8],  jaguarMainRAM[a+9],
                          jaguarMainRAM[a+10], jaguarMainRAM[a+11],
                          jaguarMainRAM[a+12], jaguarMainRAM[a+13],
                          jaguarMainRAM[a+14], jaguarMainRAM[a+15]);
               LOG_DBG("[HLE-LOOP2] Also dumping $4500-$4620:\n");
               for (uint32_t a = 0x4500; a < 0x4620; a += 16)
                  LOG_DBG("  %06X: %02X%02X %02X%02X %02X%02X %02X%02X "
                          "%02X%02X %02X%02X %02X%02X %02X%02X\n", a,
                          jaguarMainRAM[a+0],  jaguarMainRAM[a+1],
                          jaguarMainRAM[a+2],  jaguarMainRAM[a+3],
                          jaguarMainRAM[a+4],  jaguarMainRAM[a+5],
                          jaguarMainRAM[a+6],  jaguarMainRAM[a+7],
                          jaguarMainRAM[a+8],  jaguarMainRAM[a+9],
                          jaguarMainRAM[a+10], jaguarMainRAM[a+11],
                          jaguarMainRAM[a+12], jaguarMainRAM[a+13],
                          jaguarMainRAM[a+14], jaguarMainRAM[a+15]);
               LOG_DBG("[HLE-LOOP2] Regs: D0=$%08X D1=$%08X D2=$%08X D3=$%08X "
                       "D4=$%08X D5=$%08X D6=$%08X D7=$%08X\n",
                       m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                       m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
                       m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
                       m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
               LOG_DBG("[HLE-LOOP2] A0=$%08X A1=$%08X A2=$%08X A3=$%08X "
                       "A4=$%08X A5=$%08X A6=$%08X A7=$%08X SR=$%04X\n",
                       m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                       m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3),
                       m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
                       m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7),
                       m68k_get_reg(NULL, M68K_REG_SR));
               /* Dump key RAM areas the loop might reference */
               LOG_DBG("[HLE-LOOP2] RAM $0000-$003F:\n");
               for (uint32_t a = 0x0000; a < 0x0040; a += 16)
                  LOG_DBG("  %06X: %02X%02X %02X%02X %02X%02X %02X%02X "
                          "%02X%02X %02X%02X %02X%02X %02X%02X\n", a,
                          jaguarMainRAM[a+0],  jaguarMainRAM[a+1],
                          jaguarMainRAM[a+2],  jaguarMainRAM[a+3],
                          jaguarMainRAM[a+4],  jaguarMainRAM[a+5],
                          jaguarMainRAM[a+6],  jaguarMainRAM[a+7],
                          jaguarMainRAM[a+8],  jaguarMainRAM[a+9],
                          jaguarMainRAM[a+10], jaguarMainRAM[a+11],
                          jaguarMainRAM[a+12], jaguarMainRAM[a+13],
                          jaguarMainRAM[a+14], jaguarMainRAM[a+15]);
            }
         }

         /* One-shot dump of the hot loop code when we first enter $6400 */
         {
            static bool dumpedHotLoop = false;
            if (!dumpedHotLoop && m68kPC >= 0x6400 && m68kPC < 0x6600)
            {
               dumpedHotLoop = true;
               LOG_DBG("[HLE-LOOP] First entry to $%06X — dumping code $6400-$6520:\n", m68kPC);
               for (uint32_t a = 0x6400; a < 0x6520; a += 16)
                  LOG_DBG("  %06X: %02X%02X %02X%02X %02X%02X %02X%02X "
                          "%02X%02X %02X%02X %02X%02X %02X%02X\n", a,
                          jaguarMainRAM[a+0],  jaguarMainRAM[a+1],
                          jaguarMainRAM[a+2],  jaguarMainRAM[a+3],
                          jaguarMainRAM[a+4],  jaguarMainRAM[a+5],
                          jaguarMainRAM[a+6],  jaguarMainRAM[a+7],
                          jaguarMainRAM[a+8],  jaguarMainRAM[a+9],
                          jaguarMainRAM[a+10], jaguarMainRAM[a+11],
                          jaguarMainRAM[a+12], jaguarMainRAM[a+13],
                          jaguarMainRAM[a+14], jaguarMainRAM[a+15]);
               LOG_DBG("[HLE-LOOP] Regs: D0=$%08X D1=$%08X D2=$%08X D3=$%08X "
                       "D4=$%08X D5=$%08X D6=$%08X D7=$%08X\n",
                       m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                       m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
                       m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
                       m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
               LOG_DBG("[HLE-LOOP] A0=$%08X A1=$%08X A2=$%08X A3=$%08X "
                       "A4=$%08X A5=$%08X A6=$%08X A7=$%08X SR=$%04X\n",
                       m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                       m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3),
                       m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
                       m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7),
                       m68k_get_reg(NULL, M68K_REG_SR));
            }
         }

         /* Dump DDL2 boundary when function processes DDL2 table */
         {
            static bool dumpedDDL2Boundary = false;
            if (!dumpedDDL2Boundary && m68kPC >= 0x64C0 && m68kPC < 0x64D0)
            {
               uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);
               if (a1 >= 0x0A8C00 && a1 < 0x0A9000)
               {
                  dumpedDDL2Boundary = true;
                  LOG_DBG("[HLE-DDL2] Function $64C0 called with A1=$%06X, "
                          "dumping DDL2 data + boundary:\n", a1);
                  for (uint32_t a = a1; a < a1 + 112 && a + 15 < 0x200000; a += 16)
                     LOG_DBG("  %06X: %02X%02X %02X%02X %02X%02X %02X%02X "
                             "%02X%02X %02X%02X %02X%02X %02X%02X\n", a,
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
         }

         /* Periodic register dump while in hot loops */
         {
            static uint32_t loopSamples6400 = 0;
            static uint32_t loopSamples4100 = 0;
            if (m68kPC >= 0x6400 && m68kPC < 0x6600)
            {
               loopSamples6400++;
               if (loopSamples6400 == 100000 || loopSamples6400 == 1000000 || loopSamples6400 == 5000000)
                  LOG_DBG("[HLE-LOOP] sample #%u PC=$%06X D0=$%08X D1=$%08X "
                          "D2=$%08X A0=$%08X A1=$%08X A2=$%08X\n",
                          loopSamples6400, m68kPC,
                          m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                          m68k_get_reg(NULL, M68K_REG_D2),
                          m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                          m68k_get_reg(NULL, M68K_REG_A2));
            }
            if (m68kPC >= 0x4100 && m68kPC < 0x4600)
            {
               loopSamples4100++;
               if (loopSamples4100 == 100000 || loopSamples4100 == 500000 || loopSamples4100 == 2000000)
                  LOG_DBG("[HLE-LOOP2] sample #%u PC=$%06X D0=$%08X D1=$%08X "
                          "D2=$%08X D3=$%08X A0=$%08X A1=$%08X A2=$%08X A3=$%08X\n",
                          loopSamples4100, m68kPC,
                          m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                          m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
                          m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                          m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3));
            }
         }
      }
#endif

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

   BUTCHExec(USEC_TO_RISC_CYCLES(vjs.hardwareTypeNTSC ? 31.777777777 : 32.0));

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

   if (bootConfig.strategy && bootConfig.strategy->reset)
      bootConfig.strategy->reset();

   JaguarSeedPRNG(12345);
   for(i=8; i<0x200000; i+=4)
   {
      uint32_t r = JaguarRand();
      if (jaguarLoadedRAMEnd > jaguarLoadedRAMStart
          && i >= jaguarLoadedRAMStart && i < jaguarLoadedRAMEnd)
         continue;
      *((uint32_t *)(&jaguarMainRAM[i])) = r;
   }

   // New timer base code stuffola...
   InitializeEventList();
   //Need to change this so it uses the single RAM space and load the BIOS
   //into it somewhere...
   //Also, have to change this here and in JaguarReadXX() currently
   if (bootConfig.showBootROM && !vjs.hardwareTypeAlpine)
   {
      memcpy(jaguarMainRAM, jagMemSpace + 0xE00000, 8);

      /* The boot ROM sets up its own vector table, but IRQs can fire
       * before that happens (e.g. TOM video interrupts). Install an
       * RTE trampoline so early exceptions return safely instead of
       * dispatching through PRNG garbage. The BIOS will overwrite
       * these with real handlers during init. */
      SET16(jaguarMainRAM, 0x400, 0x4E73);  /* RTE */
      for (i = 2; i < 256; i++)
         SET32(jaguarMainRAM, i * 4, 0x400);
   }
   else
   {
      SET32(jaguarMainRAM, 4, jaguarRunAddress);

      if (jaguarLoadedRAMEnd > jaguarLoadedRAMStart
          && jaguarLoadedRAMStart > 0x400)
      {
         SET16(jaguarMainRAM, 0x400, 0x4E73);  /* RTE */
         for (i = 2; i < 256; i++)
            SET32(jaguarMainRAM, i * 4, 0x400);
      }
   }

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

#if LLDB_TRAP
   lldb_trap_frame++;
   if (lldb_trap_frame >= 400)
      lldb_trap_armed = 1;
#endif

   do
   {
      double timeToNextEvent = GetTimeToNextEvent(EVENT_MAIN);
      m68k_execute(USEC_TO_M68K_CYCLES(timeToNextEvent));
      GPUExec(USEC_TO_RISC_CYCLES(timeToNextEvent));
      HandleNextEvent(EVENT_MAIN);
   } while(!frameDone);
}

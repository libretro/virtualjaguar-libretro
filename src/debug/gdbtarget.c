/*
 * gdbtarget.c -- adapts the Jaguar to the GDB stub's target vtable, owns
 * the per-target breakpoint/watchpoint state, and runs the halt loop.
 * Design: docs/gdb-stub-design.md (issue #652).
 *
 * NOTE on the "who" argument to JaguarReadByte()/JaguarWriteByte(): the
 * plan that specified this file named the constant DEBUG, but
 * src/core/vjag_memory.h's who enum spells this enumerator DEBUGGER, not
 * DEBUG -- deliberately, per that header's comment: Xcode's stock Debug
 * configuration defines the preprocessor macro DEBUG=1, which would
 * otherwise mangle the enumerator list on every Xcode/SwiftPM consumer.
 * The wire value (9) is unchanged. Verified for RAM/ROM and for every
 * who-GATED side effect this audit found (DSP HLE sound-engine auto-ack
 * and DSPGO auto-clear in src/jerry/dsp.c, the busArbiter OP charge in
 * jaguar.c, the GPU/DSP-specific branches in vjtrace.c): all of them test
 * who == M68K / GPU / DSP / OP specifically, none of which equals
 * DEBUGGER (9), so a debugger read/write triggers none of them.
 *
 * NOT claimed: that every memory-mapped register in the map is free of
 * read/write side effects regardless of who asks. A handful of hardware
 * registers are read/write-sensitive by design on real silicon (e.g. CD
 * BUTCH+2's DSCNTRL ack semantics in src/cd/cdrom.c, reached only via the
 * 16/32-bit read path, not the byte path this file calls) -- a debugger
 * poking at those would legitimately perturb interrupt/FIFO state
 * exactly as touching them from any processor does.
 */
#include <string.h>
#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <compat/msvc.h>  /* snprintf shim for MSVC < 2015 (buildbot msvc05/10) */

#include "gdbstub.h"
#include "gdbdisasm.h"
#include "m68kinterface.h"
#include "jaguar.h"
#include "gpu.h"
#include "dsp.h"
#include "log.h"

/* pcQueue/pcQPtr (src/core/jaguar.c, issue #542's 68K PC traceback ring)
 * have no header declaration -- test/tools dlsym them by name instead.
 * gdbtarget.c is linked into the same binary (not dlsym-based), so it
 * needs a plain extern, same ad hoc pattern src/core/vjtrace.c already
 * uses for GPUGetReg/DSPGetReg. */
extern uint32_t pcQueue[0x400];
extern uint32_t pcQPtr;

static const char gdbHex[] = "0123456789abcdef";

static void GDBPutHex32(char *out, unsigned int v)
{
   int i;

   for (i = 0; i < 8; i++)
      out[i] = gdbHex[(v >> ((7 - i) * 4)) & 0xF];
}

static int GDBGetHexVal(char c)
{
   if (c >= '0' && c <= '9')
      return c - '0';
   if (c >= 'a' && c <= 'f')
      return (c - 'a') + 10;
   if (c >= 'A' && c <= 'F')
      return (c - 'A') + 10;
   return -1;
}

/* ------------------------------------------------------------------ */
/* 68000 registers and memory                                          */
/* ------------------------------------------------------------------ */

static int GDBReadRegs68K(void *user, char *out, int outMax)
{
   int i;

   (void)user;

   if (outMax < 144)
      return -1;

   for (i = 0; i < 8; i++)
      GDBPutHex32(out + (i * 8),
                  m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i)));

   for (i = 0; i < 8; i++)
      GDBPutHex32(out + ((8 + i) * 8),
                  m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i)));

   GDBPutHex32(out + (16 * 8), m68k_get_reg(NULL, M68K_REG_SR));
   GDBPutHex32(out + (17 * 8), m68k_get_reg(NULL, M68K_REG_PC));

   return 144;
}

/* "G" -- write all 18 68K registers back, same layout readRegisters
 * produced (D0-D7, A0-A7, SR, PC; 8 hex chars each, no separators). A
 * raw poke via m68k_set_reg(), same as any debugger register write --
 * no simulated instruction, no side effect beyond the register itself. */
static int GDBWriteRegs68K(void *user, const char *hex, int hexLen)
{
   unsigned int v;
   int i;

   (void)user;

   if (hexLen < 144)
      return -1;

   for (i = 0; i < 8; i++)
   {
      if (GDBParseHexU32(hex + (i * 8), 8, &v) < 0)
         return -1;
      m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), v);
   }

   for (i = 0; i < 8; i++)
   {
      if (GDBParseHexU32(hex + ((8 + i) * 8), 8, &v) < 0)
         return -1;
      m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), v);
   }

   if (GDBParseHexU32(hex + (16 * 8), 8, &v) < 0)
      return -1;
   m68k_set_reg(M68K_REG_SR, v);

   if (GDBParseHexU32(hex + (17 * 8), 8, &v) < 0)
      return -1;
   m68k_set_reg(M68K_REG_PC, v);

   return 0;
}

/*
 * The Jaguar bus is 24-bit. Anything above 0xFFFFFF cannot be addressed
 * and is refused outright rather than silently wrapped -- a debugger that
 * asks for a wild address must be told no, not handed mirrored data.
 */
#define GDB_GUEST_LIMIT 0x1000000U

static int GDBReadMem68K(void *user, unsigned int addr, int len,
                         char *out, int outMax)
{
   int i;

   (void)user;

   if (len <= 0)
      return -1;
   if (addr >= GDB_GUEST_LIMIT)
      return -1;
   if ((unsigned int)len > (GDB_GUEST_LIMIT - addr))
      return -1;
   if ((len * 2) > outMax)
      return -1;

   for (i = 0; i < len; i++)
   {
      unsigned int b = JaguarReadByte(addr + (unsigned int)i, DEBUGGER);

      out[i * 2]       = gdbHex[(b >> 4) & 0xF];
      out[(i * 2) + 1] = gdbHex[b & 0xF];
   }

   return len * 2;
}

/* "M" -- write len bytes at addr from 2*len hex chars. Bounds-checked
 * identically to GDBReadMem68K -- this is the single most important
 * invariant in the whole stub (docs/gdb-stub-design.md "Security"): a
 * malformed or hostile M/X packet must never write outside emulated
 * space. GPU and DSP threads share this same function (GDBGpuOps()/
 * GDBDspOps() below point readMemory/writeMemory at the identical
 * functions the 68K uses) because the local RISC RAMs are memory-mapped
 * into the same unified 24-bit bus JaguarReadByte/JaguarWriteByte already
 * decode -- there is no separate GPU/DSP address space to model. */
static int GDBWriteMem68K(void *user, unsigned int addr, int len,
                          const char *hex, int hexLen)
{
   int i;

   (void)user;

   if (len <= 0)
      return -1;
   if (addr >= GDB_GUEST_LIMIT)
      return -1;
   if ((unsigned int)len > (GDB_GUEST_LIMIT - addr))
      return -1;
   if (hexLen != len * 2)
      return -1;

   for (i = 0; i < len; i++)
   {
      int hi = GDBGetHexVal(hex[i * 2]);
      int lo = GDBGetHexVal(hex[(i * 2) + 1]);

      if (hi < 0 || lo < 0)
         return -1;

      JaguarWriteByte(addr + (unsigned int)i,
                      (uint8_t)((hi << 4) | lo), DEBUGGER);
   }

   return 0;
}

/* ------------------------------------------------------------------ */
/* Per-target breakpoint tables, direct-mapped PC cache, armed counters */
/* docs/gdb-stub-design.md "Breakpoint detection"                       */
/* ------------------------------------------------------------------ */

#define GDB_MAX_BP        64
#define GDB_BP_CACHE_SIZE 256
#define GDB_BP_CACHE_MASK (GDB_BP_CACHE_SIZE - 1)
#define GDB_BP_EMPTY      0xFFFFFFFFu

struct GDBBpTarget
{
   unsigned int addr[GDB_MAX_BP];
   int used[GDB_MAX_BP];
   int count;
   unsigned int cache[GDB_BP_CACHE_SIZE];
   /* Armed for exactly the next instruction on this processor -- used by
    * both single-step (`s`/`vCont;s`) and `monitor halt <target>`/the
    * gdb_wait boot halt. Consumed (cleared) the moment it matches. */
   int oneShot;
   int oneShotReason;
};

static struct GDBBpTarget gdbBp[GDB_NUM_TARGETS];
static int gdbLastReason[GDB_NUM_TARGETS];

int gdbArmed68K = 0;
int gdbArmedGPU = 0;
int gdbArmedDSP = 0;

static void GDBRecomputeArmed(int target)
{
   int n = gdbBp[target].count + (gdbBp[target].oneShot ? 1 : 0);

   if (target == GDB_TGT_68K)
      gdbArmed68K = n;
   else if (target == GDB_TGT_GPU)
      gdbArmedGPU = n;
   else if (target == GDB_TGT_DSP)
      gdbArmedDSP = n;
}

int GDBCheckPC(int target, unsigned int pc)
{
   struct GDBBpTarget *t;
   unsigned int idx;
   int i;

   if (target < 0 || target >= GDB_NUM_TARGETS)
      return 0;

   t = &gdbBp[target];

   if (t->oneShot)
   {
      t->oneShot = 0;
      gdbLastReason[target] = t->oneShotReason;
      GDBRecomputeArmed(target);
      return 1;
   }

   idx = (pc >> 1) & GDB_BP_CACHE_MASK;
   if (t->cache[idx] == pc)
   {
      gdbLastReason[target] = GDB_STOP_BREAKPOINT;
      return 1;
   }

   for (i = 0; i < GDB_MAX_BP; i++)
   {
      if (t->used[i] && t->addr[i] == pc)
      {
         t->cache[idx] = pc;
         gdbLastReason[target] = GDB_STOP_BREAKPOINT;
         return 1;
      }
   }

   return 0;
}

static int GDBInsertExecBp(int target, unsigned int addr)
{
   struct GDBBpTarget *t = &gdbBp[target];
   int i;

   for (i = 0; i < GDB_MAX_BP; i++)
   {
      if (t->used[i] && t->addr[i] == addr)
         return 0;   /* already armed: idempotent */
   }

   for (i = 0; i < GDB_MAX_BP; i++)
   {
      if (!t->used[i])
      {
         t->used[i] = 1;
         t->addr[i] = addr;
         t->count++;
         GDBRecomputeArmed(target);
         return 0;
      }
   }

   return -2;   /* table full */
}

static int GDBRemoveExecBp(int target, unsigned int addr)
{
   struct GDBBpTarget *t = &gdbBp[target];
   int i;

   for (i = 0; i < GDB_MAX_BP; i++)
   {
      if (t->used[i] && t->addr[i] == addr)
      {
         t->used[i] = 0;
         t->count--;
         /* Direct-mapped: this address can only ever have landed in one
          * cache line. Clearing it is enough -- a stale hit for a
          * DIFFERENT still-armed address at the same line would simply
          * be re-populated on its next miss. */
         t->cache[(addr >> 1) & GDB_BP_CACHE_MASK] = GDB_BP_EMPTY;
         GDBRecomputeArmed(target);
         return 0;
      }
   }

   return 0;   /* not found: idempotent, per RSP convention */
}

/* ------------------------------------------------------------------ */
/* Memory watchpoints -- single slot, ridden on jaguar.c's bpmActive/    */
/* bpmAddress1 (docs/gdb-stub-design.md "Watchpoints"). This module is   */
/* their only writer anywhere in the tree.                              */
/* ------------------------------------------------------------------ */

static int gdbWatchArmed    = 0;
static unsigned int gdbWatchAddr = 0;
static int gdbWatchKindMask = 0;   /* bit0 = write, bit1 = read */

static int GDBWatchMaskForType(int type)
{
   if (type == GDB_BP_WATCH_WR)
      return 1;
   if (type == GDB_BP_WATCH_RD)
      return 2;
   return 3;   /* GDB_BP_WATCH_RW (access) */
}

static int GDBInsertWatch(unsigned int addr, int type)
{
   int mask = GDBWatchMaskForType(type);

   if (gdbWatchArmed && gdbWatchAddr != addr)
      return -2;   /* single slot -- see docs/gdb-stub-design.md */

   gdbWatchAddr      = addr;
   gdbWatchKindMask  |= mask;
   gdbWatchArmed     = 1;
   bpmAddress1       = addr;
#ifndef ALPINE_FUNCTIONS
   /* The six GDBMemWatchHit() call sites in src/core/jaguar.c all sit
    * inside #ifdef ALPINE_FUNCTIONS, and no build file in this repo
    * defines it -- so on every shipped target a watchpoint can never
    * fire.  Returning success here would make GDB report the watchpoint
    * as set and then silently never stop, which is the worst failure a
    * debugger can have: the user concludes the address is never touched.
    *
    * Refuse instead, so GDB says "not supported" and the user knows.
    * Found by the Kimi review on PR #724.  Making watchpoints actually
    * work means moving those call sites out of the ALPINE gate, which
    * puts six branches in the hottest memory path -- a change that needs
    * its own perf measurement, not a release-eve edit. */
   return -1;
#else
   bpmActive         = true;

   return 0;
#endif
}

static int GDBRemoveWatch(unsigned int addr, int type)
{
   if (!gdbWatchArmed || gdbWatchAddr != addr)
      return 0;   /* idempotent */

   gdbWatchKindMask &= ~GDBWatchMaskForType(type);
   if (gdbWatchKindMask == 0)
   {
      gdbWatchArmed = 0;
      bpmActive     = false;
   }

   return 0;
}

void GDBMemWatchHit(unsigned int address, int isWrite)
{
   int need = isWrite ? 1 : 2;

   if (!gdbWatchArmed || address != gdbWatchAddr)
      return;
   if (!(gdbWatchKindMask & need))
      return;

   GDBHalt(GDB_TGT_68K, GDB_STOP_WATCHPOINT, address);
}

/* ------------------------------------------------------------------ */
/* Per-target insertBreak/removeBreak (Z/z packet backends)             */
/* ------------------------------------------------------------------ */

static int GDB68KInsertBreak(void *user, int type, unsigned int addr, unsigned int kind)
{
   (void)user;
   (void)kind;

   if (type == GDB_BP_SOFTWARE || type == GDB_BP_HARDWARE)
      return GDBInsertExecBp(GDB_TGT_68K, addr);
   if (type == GDB_BP_WATCH_WR || type == GDB_BP_WATCH_RD || type == GDB_BP_WATCH_RW)
      return GDBInsertWatch(addr, type);

   return -1;
}

static int GDB68KRemoveBreak(void *user, int type, unsigned int addr, unsigned int kind)
{
   (void)user;
   (void)kind;

   if (type == GDB_BP_SOFTWARE || type == GDB_BP_HARDWARE)
      return GDBRemoveExecBp(GDB_TGT_68K, addr);
   if (type == GDB_BP_WATCH_WR || type == GDB_BP_WATCH_RD || type == GDB_BP_WATCH_RW)
      return GDBRemoveWatch(addr, type);

   return -1;
}

/* GPU/DSP execution breakpoints only -- this stub's memory watchpoints
 * ride the 68K bus access functions exclusively (see the comment above
 * GDBMemWatchHit / docs/gdb-stub-design.md), so a Z2/Z3/Z4 against GDB
 * thread 2 or 3 is refused as unsupported rather than silently aliased
 * onto the 68K's single watch slot. */
static int GDBGpuInsertBreak(void *user, int type, unsigned int addr, unsigned int kind)
{
   (void)user;
   (void)kind;
   if (type == GDB_BP_SOFTWARE || type == GDB_BP_HARDWARE)
      return GDBInsertExecBp(GDB_TGT_GPU, addr);
   return -1;
}

static int GDBGpuRemoveBreak(void *user, int type, unsigned int addr, unsigned int kind)
{
   (void)user;
   (void)kind;
   if (type == GDB_BP_SOFTWARE || type == GDB_BP_HARDWARE)
      return GDBRemoveExecBp(GDB_TGT_GPU, addr);
   return -1;
}

static int GDBDspInsertBreak(void *user, int type, unsigned int addr, unsigned int kind)
{
   (void)user;
   (void)kind;
   if (type == GDB_BP_SOFTWARE || type == GDB_BP_HARDWARE)
      return GDBInsertExecBp(GDB_TGT_DSP, addr);
   return -1;
}

static int GDBDspRemoveBreak(void *user, int type, unsigned int addr, unsigned int kind)
{
   (void)user;
   (void)kind;
   if (type == GDB_BP_SOFTWARE || type == GDB_BP_HARDWARE)
      return GDBRemoveExecBp(GDB_TGT_DSP, addr);
   return -1;
}

/* ------------------------------------------------------------------ */
/* GPU/DSP register files (org.atari.jaguar.risc: R0-R31, PC, FLAGS)    */
/* ------------------------------------------------------------------ */

static int GDBReadRegsRISC(uint32_t (*getReg)(int), uint32_t pc, uint32_t flags,
                           char *out, int outMax)
{
   int i;

   if (outMax < GDB_RISC_NUM_REGS * 8)
      return -1;

   for (i = 0; i < 32; i++)
      GDBPutHex32(out + (i * 8), getReg(i));

   GDBPutHex32(out + (GDB_RISC_REG_PC * 8), pc);
   GDBPutHex32(out + (GDB_RISC_REG_FLAGS * 8), flags);

   return GDB_RISC_NUM_REGS * 8;
}

static int GDBWriteRegsRISC(void (*setReg)(int, uint32_t),
                            void (*setPC)(uint32_t), void (*setFlags)(uint32_t),
                            const char *hex, int hexLen)
{
   unsigned int v;
   int i;

   if (hexLen < GDB_RISC_NUM_REGS * 8)
      return -1;

   for (i = 0; i < 32; i++)
   {
      if (GDBParseHexU32(hex + (i * 8), 8, &v) < 0)
         return -1;
      setReg(i, v);
   }

   if (GDBParseHexU32(hex + (GDB_RISC_REG_PC * 8), 8, &v) < 0)
      return -1;
   setPC(v);

   if (GDBParseHexU32(hex + (GDB_RISC_REG_FLAGS * 8), 8, &v) < 0)
      return -1;
   setFlags(v);

   return 0;
}

static int GDBReadRegsGPU(void *user, char *out, int outMax)
{
   (void)user;
   return GDBReadRegsRISC(GPUGetReg, GPUGetPC(), GPUGetFlags(), out, outMax);
}

static int GDBWriteRegsGPU(void *user, const char *hex, int hexLen)
{
   (void)user;
   return GDBWriteRegsRISC(GPUSetReg, GPUSetPC, GPUSetFlags, hex, hexLen);
}

static int GDBReadRegsDSP(void *user, char *out, int outMax)
{
   (void)user;
   return GDBReadRegsRISC(DSPGetReg, DSPGetPC(), DSPGetFlags(), out, outMax);
}

static int GDBWriteRegsDSP(void *user, const char *hex, int hexLen)
{
   (void)user;
   return GDBWriteRegsRISC(DSPSetReg, DSPSetPC, DSPSetFlags, hex, hexLen);
}

/* ------------------------------------------------------------------ */
/* qXfer:features:read -- custom target descriptions for GPU/DSP        */
/* Register numbering (R0-R31=0-31, PC=32, FLAGS=33) is fixed forever   */
/* once shipped -- docs/gdb-stub-design.md Open Question 3.             */
/* ------------------------------------------------------------------ */

static char gdbGpuXML[2048];
static char gdbDspXML[2048];
static int gdbGpuXMLLen = -1;
static int gdbDspXMLLen = -1;

static int GDBBuildRiscXML(const char *featureName, char *buf, int bufMax)
{
   int n = 0;
   int i;

   n += snprintf(buf + n, (size_t)(bufMax - n),
      "<?xml version=\"1.0\"?>\n"
      "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
      "<target>\n"
      "  <feature name=\"%s\">\n", featureName);

   for (i = 0; i < 32; i++)
      n += snprintf(buf + n, (size_t)(bufMax - n),
         "    <reg name=\"r%d\" bitsize=\"32\" type=\"uint32\"/>\n", i);

   n += snprintf(buf + n, (size_t)(bufMax - n),
      "    <reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>\n"
      "    <reg name=\"flags\" bitsize=\"32\" type=\"uint32\"/>\n"
      "  </feature>\n"
      "</target>\n");

   if (n < 0)
      return 0;
   return (n >= bufMax) ? bufMax - 1 : n;
}

static const char *GDBGpuTargetXML(void *user, int *xmlLen)
{
   (void)user;
   if (gdbGpuXMLLen < 0)
      gdbGpuXMLLen = GDBBuildRiscXML("org.atari.jaguar.gpu", gdbGpuXML,
                                     (int)sizeof(gdbGpuXML));
   *xmlLen = gdbGpuXMLLen;
   return gdbGpuXML;
}

static const char *GDBDspTargetXML(void *user, int *xmlLen)
{
   (void)user;
   if (gdbDspXMLLen < 0)
      gdbDspXMLLen = GDBBuildRiscXML("org.atari.jaguar.dsp", gdbDspXML,
                                     (int)sizeof(gdbDspXML));
   *xmlLen = gdbDspXMLLen;
   return gdbDspXML;
}

/* ------------------------------------------------------------------ */
/* monitor commands (qRcmd) -- 68K ops only; internally dispatches to   */
/* whichever processor the command names. docs/gdb-stub-design.md       */
/* "monitor commands": reuses the ONE disassembler (gdbdisasm.h), does  */
/* not write a second one.                                              */
/* ------------------------------------------------------------------ */

static uint16_t GDBRiscReadWord(int target, uint32_t addr)
{
   if (target == GDB_TGT_GPU)
      return GPUReadWord(addr, DEBUGGER);
   return DSPReadWord(addr, DEBUGGER);
}

static int GDBMonitorDisasm(const char *args, char *out, int outMax)
{
   char which[8];
   unsigned int addr = 0, count = 4;
   int target;
   const char * const *mnemonics;
   int n = 0;
   unsigned int i;

   if (sscanf(args, "%7s %x %u", which, &addr, &count) < 2)
      return snprintf(out, (size_t)outMax,
         "usage: monitor disasm <gpu|dsp> <addr> [count]\n");

   if (strcmp(which, "gpu") == 0)
   {
      target = GDB_TGT_GPU;
      mnemonics = GDBDisasmGPUMnemonics;
   }
   else if (strcmp(which, "dsp") == 0)
   {
      target = GDB_TGT_DSP;
      mnemonics = GDBDisasmDSPMnemonics;
   }
   else
      return snprintf(out, (size_t)outMax, "unknown target '%s' (want gpu or dsp)\n", which);

   if (count > 64)
      count = 64;

   for (i = 0; i < count && n < outMax - 2; i++)
   {
      uint32_t a = addr + i * 2;
      uint16_t w  = GDBRiscReadWord(target, a);
      uint16_t w2 = ((w >> 10) == 38) ? GDBRiscReadWord(target, a + 2) : 0;
      uint16_t w3 = ((w >> 10) == 38) ? GDBRiscReadWord(target, a + 4) : 0;

      n += GDBDisasmOne(mnemonics, a, w, w2, w3, out + n, outMax - n - 1);
      if (n < outMax - 1)
         out[n++] = '\n';
   }

   return n;
}

static int GDBMonitorRegs(const char *args, char *out, int outMax)
{
   char which[8];

   if (sscanf(args, "%7s", which) < 1)
      which[0] = '\0';

   if (strcmp(which, "gpu") == 0)
   {
      int i, n = 0;

      n += snprintf(out + n, (size_t)(outMax - n), "GPU PC=$%06X FLAGS=$%08X CTRL=$%08X\n",
                    GPUGetPC(), GPUGetFlags(), GPUGetControl());
      for (i = 0; i < 32; i++)
         n += snprintf(out + n, (size_t)(outMax - n), "r%-2d=$%08X%s", i,
                       GPUGetReg(i), (i % 4 == 3) ? "\n" : "  ");
      return n;
   }

   if (strcmp(which, "dsp") == 0)
   {
      int i, n = 0;

      n += snprintf(out + n, (size_t)(outMax - n), "DSP PC=$%06X FLAGS=$%08X CTRL=$%08X\n",
                    DSPGetPC(), DSPGetFlags(), DSPGetControl());
      for (i = 0; i < 32; i++)
         n += snprintf(out + n, (size_t)(outMax - n), "r%-2d=$%08X%s", i,
                       DSPGetReg(i), (i % 4 == 3) ? "\n" : "  ");
      return n;
   }

   return snprintf(out, (size_t)outMax,
      "68K PC=$%06X SR=$%04X\n", m68k_get_reg(NULL, M68K_REG_PC),
      m68k_get_reg(NULL, M68K_REG_SR));
}

static int GDBMonitorHalt(const char *args, char *out, int outMax)
{
   char which[8];
   int target;

   if (sscanf(args, "%7s", which) < 1)
      return snprintf(out, (size_t)outMax, "usage: monitor halt <68k|gpu|dsp>\n");

   if (strcmp(which, "68k") == 0)
      target = GDB_TGT_68K;
   else if (strcmp(which, "gpu") == 0)
      target = GDB_TGT_GPU;
   else if (strcmp(which, "dsp") == 0)
      target = GDB_TGT_DSP;
   else
      return snprintf(out, (size_t)outMax, "unknown target '%s'\n", which);

   /* Arms unconditionally for the very next instruction on that
    * processor -- takes effect next time it actually runs, same one-shot
    * primitive `s`/vCont;s and the gdb_wait boot halt use. */
   gdbBp[target].oneShot       = 1;
   gdbBp[target].oneShotReason = GDB_STOP_USER;
   GDBRecomputeArmed(target);

   return snprintf(out, (size_t)outMax,
      "%s will halt on its next instruction\n", which);
}

static int GDBMonitorTrace(const char *args, char *out, int outMax)
{
   int n = 0;
   unsigned i;
   unsigned start;

   (void)args;

   /* The #542 traceback ring: 0x400 entries, pcQPtr is the NEXT slot to
    * write, so the oldest live entry is right there and the newest is
    * one behind it. Dump oldest-to-newest, capped so a 4096-byte reply
    * buffer never overflows (each line is at most ~9 chars). */
   start = pcQPtr;
   for (i = 0; i < 0x400 && n < outMax - 16; i++)
   {
      unsigned idx = (start + i) & 0x3FF;

      n += snprintf(out + n, (size_t)(outMax - n), "$%06X\n", pcQueue[idx]);
   }

   return n;
}

static int GDBMonitorWatch(const char *args, char *out, int outMax)
{
   char kindStr[8];
   unsigned int addr;
   int type = GDB_BP_WATCH_RW;
   int scanned;
   int rc;

   kindStr[0] = '\0';
   scanned = sscanf(args, "%x %7s", &addr, kindStr);

   if (scanned >= 1)
   {
      if (strcmp(kindStr, "w") == 0)
         type = GDB_BP_WATCH_WR;
      else if (strcmp(kindStr, "r") == 0)
         type = GDB_BP_WATCH_RD;

      rc = GDBInsertWatch(addr, type);
      if (rc == -2)
         return snprintf(out, (size_t)outMax,
            "watch slot already in use by a different address\n");

      return snprintf(out, (size_t)outMax, "watchpoint armed at $%06X\n", addr);
   }

   gdbWatchArmed    = 0;
   gdbWatchKindMask = 0;
   bpmActive        = false;

   return snprintf(out, (size_t)outMax, "watchpoint cleared\n");
}

static int GDBMonitorCmd(void *user, const char *cmd, char *out, int outMax)
{
   (void)user;

   if (strncmp(cmd, "disasm", 6) == 0)
      return GDBMonitorDisasm(cmd + 6, out, outMax);
   if (strncmp(cmd, "regs", 4) == 0)
      return GDBMonitorRegs(cmd + 4, out, outMax);
   if (strncmp(cmd, "halt", 4) == 0)
      return GDBMonitorHalt(cmd + 4, out, outMax);
   if (strncmp(cmd, "trace", 5) == 0)
      return GDBMonitorTrace(cmd + 5, out, outMax);
   if (strncmp(cmd, "watch", 5) == 0)
      return GDBMonitorWatch(cmd + 5, out, outMax);

   return snprintf(out, (size_t)outMax,
      "unknown monitor command '%s'\n"
      "commands: disasm <gpu|dsp> <addr> [count], regs [gpu|dsp],\n"
      "          halt <68k|gpu|dsp>, trace, watch [<addr> [r|w]]\n", cmd);
}

/* ------------------------------------------------------------------ */
/* Target ops tables                                                    */
/* ------------------------------------------------------------------ */

const struct GDBTargetOps *GDBJaguarOps(void)
{
   static struct GDBTargetOps ops;

   ops.readRegisters  = GDBReadRegs68K;
   ops.writeRegisters = GDBWriteRegs68K;
   ops.readMemory     = GDBReadMem68K;
   ops.writeMemory    = GDBWriteMem68K;
   ops.insertBreak    = GDB68KInsertBreak;
   ops.removeBreak    = GDB68KRemoveBreak;
   ops.targetXML      = NULL;   /* thread 1: GDB's native m68k description */
   ops.monitorCmd     = GDBMonitorCmd;
   return &ops;
}

const struct GDBTargetOps *GDBGpuOps(void)
{
   static struct GDBTargetOps ops;

   ops.readRegisters  = GDBReadRegsGPU;
   ops.writeRegisters = GDBWriteRegsGPU;
   /* Memory is the same unified 24-bit bus for every processor -- see
    * the comment above GDBWriteMem68K. */
   ops.readMemory     = GDBReadMem68K;
   ops.writeMemory    = GDBWriteMem68K;
   ops.insertBreak    = GDBGpuInsertBreak;
   ops.removeBreak    = GDBGpuRemoveBreak;
   ops.targetXML      = GDBGpuTargetXML;
   ops.monitorCmd     = NULL;   /* only thread 1's ops.monitorCmd is called */
   return &ops;
}

const struct GDBTargetOps *GDBDspOps(void)
{
   static struct GDBTargetOps ops;

   ops.readRegisters  = GDBReadRegsDSP;
   ops.writeRegisters = GDBWriteRegsDSP;
   ops.readMemory     = GDBReadMem68K;
   ops.writeMemory    = GDBWriteMem68K;
   ops.insertBreak    = GDBDspInsertBreak;
   ops.removeBreak    = GDBDspRemoveBreak;
   ops.targetXML      = GDBDspTargetXML;
   ops.monitorCmd     = NULL;
   return &ops;
}

/* ------------------------------------------------------------------ */
/* Session, socket pump and the blocking halt loop                      */
/* ------------------------------------------------------------------ */

static struct GDBSession gdbSession;
static char gdbRxBuf[GDB_PACKET_MAX];
static int  gdbRxLen = 0;
static int  gdbHaltedTarget = -1;
static int  gdbHaltTimeoutSeconds = 0;
static int  gdbClientWasPresent = 0;
static int  gdbClientAttachEventPending = 0;

static void GDBSleepMs(int ms)
{
#if defined(_WIN32)
   Sleep((DWORD)ms);
#else
   usleep((unsigned int)(ms * 1000));
#endif
}

/*
 * One non-blocking pass: accept, drain whatever the socket has into
 * gdbRxBuf, dispatch every complete "$...#cs" packet currently buffered.
 * Handles the low-level +/- ack byte (docs/gdb-stub-design.md Testing
 * item 5's "off-by-one": the packet that flips into no-ack mode is
 * itself still acked; nothing received after is). Returns 1 if the
 * client disconnected during this pass (everything already disarmed by
 * the time it returns), 0 otherwise.
 */
static int GDBPumpOnce(int *resumeRequested, int *resumeIsStep)
{
   int wasClientPresent;

   GDBSockPoll();

   wasClientPresent = gdbClientWasPresent;
   gdbClientWasPresent = GDBSockHasClient();
   if (!wasClientPresent && gdbClientWasPresent)
   {
      LOG_INF("[GDB] client attached\n");
      gdbClientAttachEventPending = 1;
   }

   for (;;)
   {
      int room = (int)sizeof(gdbRxBuf) - gdbRxLen;
      int n;

      if (room <= 0)
      {
         gdbRxLen = 0;
         break;
      }

      n = GDBSockRecv(gdbRxBuf + gdbRxLen, room);
      if (n < 0)
      {
         gdbRxLen = 0;
         gdbClientWasPresent = 0;
         GDBTargetResetState();
         return 1;
      }
      if (n == 0)
         break;

      gdbRxLen += n;
   }

   for (;;)
   {
      static char payload[GDB_PACKET_MAX];
      static char reply[GDB_PACKET_MAX];
      static char encoded[GDB_PACKET_MAX + 8];
      int i;
      int dollarAt = -1;
      int hashAt   = -1;
      int payLen, consumed, remaining;

      for (i = 0; i < gdbRxLen; i++)
      {
         if (gdbRxBuf[i] == '$')
         {
            dollarAt = i;
            break;
         }
      }

      if (dollarAt < 0)
      {
         /* Nothing but stray +/- ack bytes (or garbage) buffered. */
         gdbRxLen = 0;
         break;
      }

      for (i = dollarAt + 1; i < gdbRxLen; i++)
      {
         if (gdbRxBuf[i] == '#')
         {
            hashAt = i;
            break;
         }
      }

      if (hashAt < 0 || (hashAt + 2) >= gdbRxLen)
      {
         if (dollarAt > 0)
         {
            remaining = gdbRxLen - dollarAt;
            memmove(gdbRxBuf, gdbRxBuf + dollarAt, (size_t)remaining);
            gdbRxLen = remaining;
         }
         break;
      }

      payLen = GDBDecodePacket(gdbRxBuf + dollarAt, hashAt + 3 - dollarAt,
                               payload, (int)sizeof(payload));

      if (payLen >= 0)
      {
         int wasAckMode = !gdbSession.noAckMode;
         int replyLen, encLen;
         int myResume = 0, myStep = 0;

         if (wasAckMode)
            GDBSockSend("+", 1);

         replyLen = GDBHandlePacket(&gdbSession, payload, payLen,
                                    reply, (int)sizeof(reply),
                                    &myResume, &myStep);

         /* RSP: an EMPTY packet ("$#00") is how a stub says "I do not
          * implement this".  Silence is not the same thing -- the client
          * waits for a reply that never comes, which is what hung LLDB's
          * attach.  GDBHandlePacket() returning 0 means exactly that,
          * EXCEPT for c/s/vCont, which also return 0 but set myResume:
          * there the next packet the client should see is the stop reply
          * once the machine halts, not an empty one now. */
         if (replyLen > 0)
         {
            encLen = GDBEncodePacket(reply, replyLen, encoded,
                                     (int)sizeof(encoded));
            if (encLen > 0)
               GDBSockSend(encoded, encLen);
         }
         else if (replyLen == 0 && !myResume)
         {
            GDBSockSend("$#00", 4);
         }

         if (myResume)
         {
            *resumeRequested = 1;
            *resumeIsStep     = myStep;
         }
      }
      else if (payLen == -2 && !gdbSession.noAckMode)
      {
         /* Checksum mismatch: ask the client to retransmit. */
         GDBSockSend("-", 1);
      }

      consumed  = hashAt + 3;
      remaining = gdbRxLen - consumed;
      memmove(gdbRxBuf, gdbRxBuf + consumed, (size_t)remaining);
      gdbRxLen = remaining;
   }

   return 0;
}

void GDBTargetServicePoll(void)
{
   int resumeRequested = 0, resumeIsStep = 0;

   /* Not halted: a stray c/s/vCont (a client racing an unsolicited
    * continue, or simple client bugs) has nothing to resume. Discard it
    * rather than let it leak into a future real halt. */
   GDBPumpOnce(&resumeRequested, &resumeIsStep);
}

int GDBSockHasClientAttachEvent(void)
{
   if (!gdbClientAttachEventPending)
      return 0;
   gdbClientAttachEventPending = 0;
   return 1;
}

void GDBTargetSetHaltTimeout(int seconds)
{
   gdbHaltTimeoutSeconds = seconds;
}

void GDBTargetArmWaitAtBoot(void)
{
   gdbBp[GDB_TGT_68K].oneShot       = 1;
   gdbBp[GDB_TGT_68K].oneShotReason = GDB_STOP_USER;
   GDBRecomputeArmed(GDB_TGT_68K);
}

void GDBHalt(int target, int reason, unsigned int pc)
{
   time_t startTime;
   char stopMsg[32];
   char encoded[48];
   int encLen;
   int effectiveReason;

   if (target < 0 || target >= GDB_NUM_TARGETS)
      return;

   effectiveReason = (reason == GDB_STOP_BREAKPOINT) ? gdbLastReason[target] : reason;
   gdbHaltedTarget = target;
   startTime = time(NULL);

   LOG_INF("[GDB] halt: target=%d reason=%d pc=$%06X\n", target, effectiveReason, pc);

   sprintf(stopMsg, "T05thread:%x;", target + 1);
   encLen = GDBEncodePacket(stopMsg, (int)strlen(stopMsg), encoded, (int)sizeof(encoded));
   if (encLen > 0)
      GDBSockSend(encoded, encLen);

   for (;;)
   {
      int resumeRequested = 0, resumeIsStep = 0;

      if (GDBPumpOnce(&resumeRequested, &resumeIsStep))
         break;   /* client disconnected -- GDBTargetResetState() already ran */

      if (resumeRequested)
      {
         if (resumeIsStep)
         {
            gdbBp[target].oneShot       = 1;
            gdbBp[target].oneShotReason = GDB_STOP_STEP;
            GDBRecomputeArmed(target);
         }
         break;
      }

      if (gdbHaltTimeoutSeconds > 0 &&
          (int)difftime(time(NULL), startTime) >= gdbHaltTimeoutSeconds)
      {
         LOG_WRN("[GDB] halt-timeout (%ds) with no continue -- auto-resuming "
                 "(breakpoints stay armed; expect this to repeat if the same "
                 "PC is hit again)\n", gdbHaltTimeoutSeconds);
         break;
      }

      GDBSleepMs(2);
   }

   gdbHaltedTarget = -1;
}

void GDBTargetResetState(void)
{
   int t;

   for (t = 0; t < GDB_NUM_TARGETS; t++)
   {
      int i;

      gdbBp[t].count    = 0;
      gdbBp[t].oneShot  = 0;
      for (i = 0; i < GDB_MAX_BP; i++)
         gdbBp[t].used[i] = 0;
      for (i = 0; i < GDB_BP_CACHE_SIZE; i++)
         gdbBp[t].cache[i] = GDB_BP_EMPTY;
      GDBRecomputeArmed(t);
   }

   gdbWatchArmed    = 0;
   gdbWatchKindMask = 0;
   bpmActive        = false;

   /* A fresh client always starts in ack mode: QStartNoAckMode is
    * negotiated per connection.  Leaving this set meant the NEXT client
    * connected expecting acks and got none, and stalled. */
   gdbSession.noAckMode    = 0;

   gdbHaltedTarget         = -1;
   gdbRxLen                = 0;
   gdbClientWasPresent     = 0;
   gdbClientAttachEventPending = 0;
   gdbSession.threadG = 1;
   gdbSession.threadC = 1;
   /* RSP mandates that every connection starts in ack mode: a new
    * client's first reply must carry the leading '+' until IT has
    * negotiated QStartNoAckMode itself. Leaving this latched from a
    * dead client makes the next client's very first qSupported reply a
    * protocol violation -- gdb/lldb block waiting for an ack that never
    * comes. This mirrors GDBSessionInit() (src/debug/gdbstub.c), which
    * only ever runs once per content load. */
   gdbSession.noAckMode = 0;
}

void GDBTargetOpen(void)
{
   GDBSessionInit(&gdbSession, GDBJaguarOps(), NULL);
   GDBSessionSetTargetOps(&gdbSession, GDB_TGT_GPU, GDBGpuOps(), NULL);
   GDBSessionSetTargetOps(&gdbSession, GDB_TGT_DSP, GDBDspOps(), NULL);
   GDBTargetResetState();
}

void GDBTargetClose(void)
{
   GDBTargetResetState();
   gdbHaltTimeoutSeconds = 0;
}

/*
 * vjdebug.c -- the core side of the app's debug interface (struct
 * vj_debug_iface, src/debug/vj_debug_iface.h).
 *
 * Everything runs on the emulation thread: either between two retro_run()
 * calls, or from inside the host's halt_pump while GDBHalt() has the core
 * parked (src/debug/gdbtarget.c).
 *
 * Memory is read through JaguarReadByte(..., DEBUGGER) -- the byte path the
 * GDB stub uses, which has no side effect on the emulated machine (the 16/
 * 32-bit paths do on a few CD registers; see the note at the top of
 * gdbtarget.c).  Writes go through JaguarWrite{Byte,Word}(..., DEBUGGER)
 * under the policy documented at VJ_DBG_POKE_* in the header.
 *
 * Strict C89 (the core builds under MSVC 2005): declarations at the top of
 * each block, no inline, no long long literals.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vjdebug.h"
#include "gdbstub.h"
#include "gdbdisasm.h"
#include "jaguar.h"
#include "vjag_memory.h"
#include "m68kinterface.h"
#include "cpudefs.h"
#include "gpu.h"
#include "dsp.h"
#include "op.h"
#include "blitter.h"
#include "texreplace.h"
#include "settings.h"

/* The stock CRY -> XRGB table the renderer uses (tom.c). */
extern uint32_t CRY16ToRGB32[0x10000];

#define VJDBG_ADDR_LIMIT 0x1000000u
#define VJDBG_MAX_BP     64

unsigned vjDebugFrame = 0;

static int vjdbg_loaded = 0;
static int vjdbg_have_host = 0;
static struct vj_debug_host vjdbg_host;   /* copy of the app's host */
static struct vj_debug_host vjdbg_wrap;   /* what gdbtarget.c pumps  */
static int vjdbg_wrap_ready = 0;

/* Breakpoints armed through this interface (so detach removes exactly
 * those and leaves a TCP client's alone). */
static unsigned int vjdbg_bp_addr[VJ_DBG_NUM_TARGETS][VJDBG_MAX_BP];
static int vjdbg_bp_used[VJ_DBG_NUM_TARGETS][VJDBG_MAX_BP];

/* ------------------------------------------------------------------ */
/* Regions and the poke policy                                          */
/* ------------------------------------------------------------------ */

static const struct vj_debug_region vjdbg_regions[] =
{
   { "DRAM",             0x000000u, 0x200000u, VJ_DBG_REGION_RAM },
   { "Cartridge",        0x800000u, 0x5FFF00u, VJ_DBG_REGION_ROM },
   { "BUTCH",            0xDFFF00u, 0x000100u, VJ_DBG_REGION_REG | VJ_DBG_REGION_BLOCKED },
   { "BIOS",             0xE00000u, 0x020000u, VJ_DBG_REGION_ROM },
   { "TOM regs",         0xF00000u, 0x000400u, VJ_DBG_REGION_REG },
   { "CLUT",             0xF00400u, 0x000400u, VJ_DBG_REGION_RAM },
   { "TOM LBUF",         0xF00800u, 0x001800u, VJ_DBG_REGION_RAM },
   { "GPU/blitter regs", 0xF02000u, 0x001000u, VJ_DBG_REGION_REG },
   { "GPU RAM",          0xF03000u, 0x001000u, VJ_DBG_REGION_RAM },
   { "JERRY regs",       0xF10000u, 0x00B000u, VJ_DBG_REGION_REG },
   { "DSP RAM",          0xF1B000u, 0x002000u, VJ_DBG_REGION_RAM }
};
#define VJDBG_NUM_REGIONS ((int)(sizeof(vjdbg_regions) / sizeof(vjdbg_regions[0])))

/* Registers whose write starts or stops something; each is 4 bytes wide:
 * OLP, G_CTRL, B_CMD, D_CTRL, LTXD, RTXD. */
static const unsigned int vjdbg_dangerous[] =
{
   0xF00020u, 0xF02114u, 0xF02238u, 0xF1A114u, 0xF1A148u, 0xF1A14Cu
};
#define VJDBG_NUM_DANGEROUS ((int)(sizeof(vjdbg_dangerous) / sizeof(vjdbg_dangerous[0])))

static const struct vj_debug_region *VJDbgRegionOf(unsigned int addr)
{
   int i;

   for (i = 0; i < VJDBG_NUM_REGIONS; i++)
   {
      if (addr >= vjdbg_regions[i].start
            && (addr - vjdbg_regions[i].start) < vjdbg_regions[i].len)
         return &vjdbg_regions[i];
   }
   return NULL;
}

static int VJDbgIsDangerous(unsigned int addr)
{
   int i;

   for (i = 0; i < VJDBG_NUM_DANGEROUS; i++)
   {
      if (addr >= vjdbg_dangerous[i] && addr < vjdbg_dangerous[i] + 4u)
         return 1;
   }
   return 0;
}

static int VJDbgRegion(int idx, struct vj_debug_region *o)
{
   if (!o || idx < 0 || idx >= VJDBG_NUM_REGIONS)
      return 0;
   *o = vjdbg_regions[idx];
   return 1;
}

/* ------------------------------------------------------------------ */
/* Memory                                                               */
/* ------------------------------------------------------------------ */

static int VJDbgPeek(unsigned int addr, unsigned char *dst, unsigned int n)
{
   unsigned int i;

   if (!vjdbg_loaded)
      return VJ_DBG_ERR_STATE;
   if (!dst || n == 0 || addr >= VJDBG_ADDR_LIMIT || n > VJDBG_ADDR_LIMIT - addr)
      return VJ_DBG_ERR_RANGE;

   /* Main RAM: byte-for-byte what JaguarReadByte() returns there. */
   if (addr + n <= 0x200000u)
   {
      memcpy(dst, jaguarMainRAM + addr, n);
      return (int)n;
   }

   for (i = 0; i < n; i++)
      dst[i] = JaguarReadByte(addr + i, DEBUGGER);
   return (int)n;
}

static int VJDbgPokeCheck(unsigned int addr, unsigned int n, unsigned int flags)
{
   unsigned int i;

   if (n == 0 || addr >= VJDBG_ADDR_LIMIT || n > VJDBG_ADDR_LIMIT - addr)
      return VJ_DBG_ERR_RANGE;

   for (i = 0; i < n; i++)
   {
      unsigned int a = addr + i;
      const struct vj_debug_region *r = VJDbgRegionOf(a);

      if (!r)
         return VJ_DBG_ERR_RANGE;
      if (r->flags & VJ_DBG_REGION_BLOCKED)
         return VJ_DBG_ERR_POLICY;
      if (r->flags & VJ_DBG_REGION_ROM)
         return VJ_DBG_ERR_READONLY;
      if (r->flags & VJ_DBG_REGION_REG)
      {
         if (!(flags & VJ_DBG_POKE_REGS))
            return VJ_DBG_ERR_POLICY;
         if (VJDbgIsDangerous(a) && !(flags & VJ_DBG_POKE_DANGEROUS))
            return VJ_DBG_ERR_POLICY;
      }
   }
   return (int)n;
}

static int VJDbgPoke(unsigned int addr, const unsigned char *src,
                     unsigned int n, unsigned int flags)
{
   unsigned int i;
   int rc;

   if (!vjdbg_loaded)
      return VJ_DBG_ERR_STATE;
   rc = VJDbgPokeCheck(addr, n, flags);
   if (rc < 0)
      return rc;
   if (!src)
      return VJ_DBG_ERR_RANGE;

   /* All-or-nothing was checked above.  Register space is written a
    * 16-bit word at a time when aligned (the Jaguar's registers are
    * word/long wide; byte writes into them are not what a program does);
    * everything else byte by byte through the same path the GDB stub uses. */
   i = 0;
   while (i < n)
   {
      unsigned int a = addr + i;
      const struct vj_debug_region *r = VJDbgRegionOf(a);

      if (r && (r->flags & VJ_DBG_REGION_REG) && (a & 1u) == 0 && (n - i) >= 2)
      {
         JaguarWriteWord(a, (uint16_t)(((unsigned int)src[i] << 8) | src[i + 1]), DEBUGGER);
         i += 2;
      }
      else
      {
         /* Same self-modifying-code rule as the 68K's own write path (#811):
          * a write next to the PC must reach the prefetch queue. */
         if (a < 0x200000u && M68K_PF_NEAR(a, 1))
            M68KPrefetchSnoop(a, 1);
         JaguarWriteByte(a, src[i], DEBUGGER);
         i += 1;
      }
   }
   return (int)n;
}

/* ------------------------------------------------------------------ */
/* Registers and code                                                   */
/* ------------------------------------------------------------------ */

static int VJDbgGetRegs(int target, struct vj_debug_regs *o)
{
   int i;

   if (!vjdbg_loaded || !o)
      return 0;
   memset(o, 0, sizeof(*o));

   if (target == VJ_DBG_TARGET_M68K)
   {
      unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);   /* also refreshes regs.s */

      for (i = 0; i < 8; i++)
      {
         o->d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
         o->a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
      }
      o->pc = m68k_get_reg(NULL, M68K_REG_PC);
      o->sr = sr;
      /* UAE convention: A7 is the CURRENT stack pointer; the other one is parked in regs. */
      if (sr & 0x2000u)
      {
         o->ssp = regs.regs[15];
         o->usp = regs.usp;
      }
      else
      {
         o->usp = regs.regs[15];
         o->ssp = regs.isp;
      }
      return 1;
   }

   if (target == VJ_DBG_TARGET_GPU)
   {
      for (i = 0; i < 32; i++)
      {
         o->bank0[i] = gpu_reg_bank_0[i];
         o->bank1[i] = gpu_reg_bank_1[i];
      }
      o->risc_pc   = GPUGetPC();
      o->flags     = GPUGetFlags();
      o->ctrl      = GPUGetControl();
      o->cur_bank  = (o->flags & 0x4000u) ? 1u : 0u;   /* REGPAGE */
      return 1;
   }

   if (target == VJ_DBG_TARGET_DSP)
   {
      for (i = 0; i < 32; i++)
      {
         o->bank0[i] = dsp_reg_bank_0[i];
         o->bank1[i] = dsp_reg_bank_1[i];
      }
      o->risc_pc   = DSPGetPC();
      o->flags     = DSPGetFlags();
      o->ctrl      = DSPGetControl();
      o->cur_bank  = (o->flags & 0x4000u) ? 1u : 0u;   /* REGPAGE */
      return 1;
   }

   return 0;
}

static int VJDbgSetReg(int target, int reg, unsigned int v)
{
   if (!vjdbg_loaded || reg < 0)
      return 0;

   if (target == VJ_DBG_TARGET_M68K)
   {
      unsigned int sr;

      if (reg < 8)
         m68k_set_reg((m68k_register_t)(M68K_REG_D0 + reg), v);
      else if (reg < 16)
         m68k_set_reg((m68k_register_t)(M68K_REG_A0 + (reg - 8)), v);
      else if (reg == VJ_DBG_REG_68K_PC)
         m68k_set_reg(M68K_REG_PC, v);
      else if (reg == VJ_DBG_REG_68K_SR)
         m68k_set_reg(M68K_REG_SR, v);
      else if (reg == VJ_DBG_REG_68K_USP || reg == VJ_DBG_REG_68K_SSP)
      {
         sr = m68k_get_reg(NULL, M68K_REG_SR);
         if (((sr & 0x2000u) != 0) == (reg == VJ_DBG_REG_68K_SSP))
            regs.regs[15] = v;   /* the one A7 currently is */
         else if (reg == VJ_DBG_REG_68K_USP)
            regs.usp = v;
         else
            regs.isp = v;
      }
      else
         return 0;
      return 1;
   }

   if (target == VJ_DBG_TARGET_GPU)
   {
      if (reg < 32)
         gpu_reg_bank_0[reg] = v;
      else if (reg < 64)
         gpu_reg_bank_1[reg - 32] = v;
      else if (reg == VJ_DBG_REG_RISC_PC)
         GPUSetPC(v);
      else if (reg == VJ_DBG_REG_RISC_FLAGS)
         GPUSetFlags(v);
      else
         return 0;
      return 1;
   }

   if (target == VJ_DBG_TARGET_DSP)
   {
      if (reg < 32)
         dsp_reg_bank_0[reg] = v;
      else if (reg < 64)
         dsp_reg_bank_1[reg - 32] = v;
      else if (reg == VJ_DBG_REG_RISC_PC)
         DSPSetPC(v);
      else if (reg == VJ_DBG_REG_RISC_FLAGS)
         DSPSetFlags(v);
      else
         return 0;
      return 1;
   }

   return 0;
}

static unsigned short VJDbgRiscWord(int target, unsigned int addr)
{
   if (target == VJ_DBG_TARGET_GPU)
      return (unsigned short)GPUReadWord(addr, DEBUGGER);
   return (unsigned short)DSPReadWord(addr, DEBUGGER);
}

static int VJDbgDisasmRisc(int target, unsigned int addr, char *out, int max,
                           unsigned int *len)
{
   unsigned short w, w2, w3;
   const char * const *mnemonics;
   int n;

   if (!vjdbg_loaded || !out || max < 2)
      return 0;
   if (target == VJ_DBG_TARGET_GPU)
      mnemonics = GDBDisasmGPUMnemonics;
   else if (target == VJ_DBG_TARGET_DSP)
      mnemonics = GDBDisasmDSPMnemonics;
   else
      return 0;

   w  = VJDbgRiscWord(target, addr);
   w2 = 0;
   w3 = 0;
   if ((w >> 10) == 38)   /* movei: a 32-bit immediate follows */
   {
      w2 = VJDbgRiscWord(target, addr + 2);
      w3 = VJDbgRiscWord(target, addr + 4);
   }
   n = GDBDisasmOne(mnemonics, addr, w, w2, w3, out, max - 1);
   if (n < 0)
      n = 0;
   if (n > max - 1)
      n = max - 1;
   out[n] = '\0';
   if (len)
      *len = ((w >> 10) == 38) ? 6u : 2u;
   return 1;
}

/* ------------------------------------------------------------------ */
/* Attach / halt / breakpoints                                          */
/* ------------------------------------------------------------------ */

static void VJDbgClearBreakpoints(void)
{
   int t, i;

   for (t = 0; t < VJ_DBG_NUM_TARGETS; t++)
   {
      for (i = 0; i < VJDBG_MAX_BP; i++)
      {
         if (vjdbg_bp_used[t][i])
            GDBTargetBreakClear(t, vjdbg_bp_addr[t][i]);
         vjdbg_bp_used[t][i] = 0;
      }
   }
}

static void VJDbgDetach(void)
{
   if (!vjdbg_have_host)
      return;
   VJDbgClearBreakpoints();
   vjdbg_have_host = 0;
   memset(&vjdbg_host, 0, sizeof(vjdbg_host));
   GDBTargetSetDebugHost(NULL);
}

/* What GDBHalt() pumps: forwards to the app, and performs the detach
 * bookkeeping the instant the app asks for it. */
static int VJDbgPump(void *user, int target, unsigned int pc)
{
   int verdict;

   (void)user;
   if (!vjdbg_have_host || !vjdbg_host.halt_pump)
      return VJ_DBG_RESUME_CONTINUE;

   verdict = vjdbg_host.halt_pump(vjdbg_host.user, target, pc);
   if (verdict == VJ_DBG_RESUME_DETACH)
      VJDbgDetach();
   return verdict;
}

static int VJDbgAttach(const struct vj_debug_host *h)
{
   unsigned int n;

   if (!vjdbg_loaded || !h || vjdbg_have_host)
      return 0;
   if (h->size < offsetof(struct vj_debug_host, halt_pump) + sizeof(h->halt_pump) || !h->halt_pump)
      return 0;

   memset(&vjdbg_host, 0, sizeof(vjdbg_host));
   n = h->size < sizeof(vjdbg_host) ? h->size : (unsigned int)sizeof(vjdbg_host);
   memcpy(&vjdbg_host, h, n);

   if (!vjdbg_wrap_ready)
   {
      memset(&vjdbg_wrap, 0, sizeof(vjdbg_wrap));
      vjdbg_wrap.size      = sizeof(vjdbg_wrap);
      vjdbg_wrap.user      = NULL;
      vjdbg_wrap.halt_pump = VJDbgPump;
      vjdbg_wrap_ready = 1;
   }

   GDBTargetEnsureTables();
   vjdbg_have_host = 1;
   GDBTargetSetDebugHost(&vjdbg_wrap);
   return 1;
}

static void VJDbgDetachIface(void)
{
   VJDbgDetach();
}

static int VJDbgBreakSet(int target, unsigned int addr)
{
   int i, free_slot = -1;

   if (!vjdbg_have_host || target < 0 || target >= VJ_DBG_NUM_TARGETS)
      return 0;
   for (i = 0; i < VJDBG_MAX_BP; i++)
   {
      if (vjdbg_bp_used[target][i] && vjdbg_bp_addr[target][i] == addr)
         return 1;   /* already armed by us: idempotent */
      if (!vjdbg_bp_used[target][i] && free_slot < 0)
         free_slot = i;
   }
   if (free_slot < 0)
      return 0;
   if (GDBTargetBreakSet(target, addr) != 0)
      return 0;
   vjdbg_bp_addr[target][free_slot] = addr;
   vjdbg_bp_used[target][free_slot] = 1;
   return 1;
}

static int VJDbgBreakClear(int target, unsigned int addr)
{
   int i;

   if (target < 0 || target >= VJ_DBG_NUM_TARGETS)
      return 0;
   for (i = 0; i < VJDBG_MAX_BP; i++)
   {
      if (vjdbg_bp_used[target][i] && vjdbg_bp_addr[target][i] == addr)
      {
         vjdbg_bp_used[target][i] = 0;
         GDBTargetBreakClear(target, addr);
         return 1;
      }
   }
   return 1;   /* idempotent */
}

static void VJDbgHaltNext(int target)
{
   if (!vjdbg_have_host)
      return;
   GDBTargetHaltNext(target);
}

static int VJDbgHaltedTarget(void)
{
   return GDBTargetHaltedTarget();
}

static int VJDbgHaltReason(void)
{
   int r = GDBTargetHaltReason();

   if (r < 0 || GDBTargetHaltedTarget() < 0)
      return VJ_DBG_STOP_NONE;
   if (r == GDB_STOP_STEP)
      return VJ_DBG_STOP_STEP;
   if (r == GDB_STOP_USER)
      return VJ_DBG_STOP_USER;
   return VJ_DBG_STOP_BREAKPOINT;
}

/* ------------------------------------------------------------------ */
/* Object Processor, video, blitter, textures, misc                     */
/* ------------------------------------------------------------------ */

static void VJDbgOpArm(void)
{
   if (vjdbg_loaded)
      OPDbgSnapArm();
}

static int VJDbgOpGet(struct vj_debug_op_obj *o, int max, unsigned int *halfline)
{
   if (!vjdbg_loaded)
      return -1;
   return OPDbgSnapGet(o, max, halfline);
}

static unsigned int VJDbgCryToRgb(unsigned int v16)
{
   return CRY16ToRGB32[v16 & 0xFFFFu] & 0x00FFFFFFu;
}

static void VJDbgBlitEnable(int on)
{
   BlitterDbgLogEnable(on, &vjDebugFrame);
}

static unsigned int VJDbgBlitHead(void)
{
   return BlitterDbgLogHead();
}

static int VJDbgBlitGet(unsigned int i, struct vj_debug_blit *o)
{
   return BlitterDbgLogGet(i, o);
}

static int VJDbgTexReload(void)
{
   if (!vjdbg_loaded)
      return 0;
   TexReplaceReload();
   return texReplaceEnabled ? 1 : 0;
}

static unsigned int VJDbgFrame(void)
{
   return vjDebugFrame;
}

/* ------------------------------------------------------------------ */
/* The interface and its lifecycle                                      */
/* ------------------------------------------------------------------ */

static struct vj_debug_iface vjdbg_iface;
static int vjdbg_iface_ready = 0;

const struct vj_debug_iface *VJDebugIface(void)
{
   if (!vjdbg_iface_ready)
   {
      memset(&vjdbg_iface, 0, sizeof(vjdbg_iface));
      vjdbg_iface.size              = sizeof(vjdbg_iface);
      vjdbg_iface.version           = VJ_DEBUG_IFACE_VERSION;
      vjdbg_iface.region            = VJDbgRegion;
      vjdbg_iface.peek              = VJDbgPeek;
      vjdbg_iface.poke              = VJDbgPoke;
      vjdbg_iface.get_regs          = VJDbgGetRegs;
      vjdbg_iface.set_reg           = VJDbgSetReg;
      vjdbg_iface.disasm_risc       = VJDbgDisasmRisc;
      vjdbg_iface.attach            = VJDbgAttach;
      vjdbg_iface.detach            = VJDbgDetachIface;
      vjdbg_iface.break_set         = VJDbgBreakSet;
      vjdbg_iface.break_clear       = VJDbgBreakClear;
      vjdbg_iface.halt_next         = VJDbgHaltNext;
      vjdbg_iface.halted_target     = VJDbgHaltedTarget;
      vjdbg_iface.op_snapshot_arm   = VJDbgOpArm;
      vjdbg_iface.op_snapshot_get   = VJDbgOpGet;
      vjdbg_iface.cry_to_rgb        = VJDbgCryToRgb;
      vjdbg_iface.blit_log_enable   = VJDbgBlitEnable;
      vjdbg_iface.blit_log_head     = VJDbgBlitHead;
      vjdbg_iface.blit_log_get      = VJDbgBlitGet;
      vjdbg_iface.texreplace_reload = VJDbgTexReload;
      vjdbg_iface.frame_number      = VJDbgFrame;
      vjdbg_iface.halt_reason       = VJDbgHaltReason;
      vjdbg_iface_ready = 1;
   }
   return &vjdbg_iface;
}

void VJDebugSetLoaded(int loaded)
{
   vjdbg_loaded = loaded ? 1 : 0;
   vjDebugFrame = 0;
}

void VJDebugReset(void)
{
   /* The unload that follows resets the GDB-side tables (GDBTargetClose),
    * so only the host pointer and our own bookkeeping need dropping. */
   vjdbg_loaded = 0;
   vjdbg_have_host = 0;
   memset(&vjdbg_host, 0, sizeof(vjdbg_host));
   memset(vjdbg_bp_used, 0, sizeof(vjdbg_bp_used));
   memset(vjdbg_bp_addr, 0, sizeof(vjdbg_bp_addr));
   vjDebugFrame = 0;
   GDBTargetSetDebugHost(NULL);
   OPDbgSnapReset();
   BlitterDbgLogReset();
}

int VJDebugAttached(void)
{
   return vjdbg_have_host;
}

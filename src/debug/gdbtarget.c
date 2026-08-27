/*
 * gdbtarget.c -- adapts the Jaguar to the GDB stub's target vtable.
 * Design: docs/gdb-stub-design.md (issue #652).
 *
 * NOTE on the "who" argument to JaguarReadByte(): the plan that specified
 * this file named the constant DEBUG, but src/core/vjag_memory.h's who
 * enum spells this enumerator DEBUGGER, not DEBUG -- deliberately, per
 * that header's comment: Xcode's stock Debug configuration defines the
 * preprocessor macro DEBUG=1, which would otherwise mangle the enumerator
 * list on every Xcode/SwiftPM consumer. The wire value (9) is unchanged.
 * Verified non-perturbing: every who-gated side effect in the read path
 * (DSP HLE sound-engine auto-ack and DSPGO auto-clear in src/jerry/dsp.c,
 * the busArbiter OP charge in jaguar.c, the GPU/DSP-specific branches in
 * vjtrace.c) is gated on who == M68K / GPU / DSP / OP specifically, none
 * of which equals DEBUGGER (9), so a debugger read triggers none of them.
 */
#include <string.h>
#include "gdbstub.h"
#include "m68kinterface.h"
#include "jaguar.h"

static const char gdbHex[] = "0123456789abcdef";

static void GDBPutHex32(char *out, unsigned int v)
{
   int i;

   for (i = 0; i < 8; i++)
      out[i] = gdbHex[(v >> ((7 - i) * 4)) & 0xF];
}

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

const struct GDBTargetOps *GDBJaguarOps(void)
{
   static struct GDBTargetOps ops;

   ops.readRegisters = GDBReadRegs68K;
   ops.readMemory    = GDBReadMem68K;
   return &ops;
}

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
#include <string.h>
#include <stdlib.h>

#include <stdio.h>
#include "jaguar.h"
#include "blitter.h"
#include "blit_memo.h"
#include "log.h"  /* CDDA-DIAG */

#include "cdrom.h"
#include "perf_counters.h"
#include "jagcd_boot.h"
#include "jagcd_hle.h"
#include "dac.h"
#include "dsp.h"
#include "eeprom.h"
#include "event.h"
#include "gpu.h"
#include "jerry.h"
#include "joystick.h"
#include "m68000/m68kinterface.h"
#include "m68000/cpudefs.h"   /* regs.remainingCycles — 68K DRAM self-cost */
#include "bus_arbiter.h"
#include "memtrack.h"
#include "jaggd.h"
#include "nvmbios.h"
#include "settings.h"
#include "tom.h"
#include "vjtrace.h"

static bool frameDone;

/* Frame-pacing instrumentation (no-op unless built with BENCH_PROFILE).
 * Lets the acid runner / benchmark detect timing regressions like the
 * Doom 2x speed bug -- e.g. expected 524 halflines/field NTSC (JTRM Rev 10
 * "Video Timings", non-interlaced), 60.05 vblank IRQs/sec.  See test/acid/README.md and src/core/perf_counters.h.
 * Counters that fire from other TUs are declared at their use sites
 * (PERF_COUNTER backs each name with a file-scope static). */
PERF_COUNTER(timing_halfline_callbacks);
PERF_COUNTER(timing_vblank_irqs);
PERF_COUNTER(timing_jaguar_execute_calls);
PERF_COUNTER(timing_m68k_cycles);
PERF_COUNTER(timing_risc_cycles);

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

/* ----------------------------------------------------------------
 * HLE BIOS / Jaguar memory layout constants
 *
 * Constants shared by JaguarInit/JaguarReset/JaguarApplyHLEBIOSState.
 * Kept at file scope (rather than `#define`s inside JaguarReset()) so
 * the names don't leak past the function with no obvious owner; they
 * are also easier for IDE/grep tooling to follow at file scope.
 * ---------------------------------------------------------------- */

/* Main RAM */
#define JAGUAR_RAM_SIZE         0x00200000  /* 2 MB main RAM */
#define VECTOR_TABLE_BYTES      0x00000400  /* 256 * 4-byte exception vectors */

/* HLE supervisor stack pointer.
 * Cart-mode SSP=0x4000 matches what the real BIOS leaves behind on a
 * cart boot.  RAM-loaded executables (.abs/.cof/JagServer) park SSP at
 * the top of main RAM (0x200000) so the first push doesn't land inside
 * loaded code. */
#define HLE_SSP_CART            0x00004000
#define HLE_SSP_RAMLOAD         0x00200000

/* HLE BIOS workspace flag at $0804.
 *
 * Battle Sphere polls a long word at low-RAM offset $0804 and waits
 * for the high bit to be set before it considers the BIOS handshake
 * complete.  Without this, BS hangs at the cartridge banner.  The
 * real BIOS sets this byte during its boot sequence; the BS CRC is
 * matched against the GPU auth flow elsewhere.
 *
 * Other titles tested do not consult $0804, so this is currently a
 * single-game accommodation rather than a general BIOS-workspace
 * model.  If we ever observe more carts probing this region, the
 * fix is to widen the workspace block to cover the full
 * $0804-$0830 range that the BIOS actually populates.
 *
 * test/tools/test_bios_diff.c probes $0804 in its BIOS-vs-HLE
 * comparison, so any change here should be cross-checked there. */
#define HLE_BIOS_WORK_FLAG_ADDR 0x0804
#define HLE_BIOS_WORK_READY     0x00000001

/* HLE 68K exception handlers.
 * RAM[0x0400] = ADDQ.L #8,SP / RTE  (long-frame: bus/address error)
 * RAM[0x0404] =                 RTE  (short-frame: everything else) */
#define HLE_EXCEPT_HANDLER      0x0400
#define HLE_EXCEPT_HANDLER_RTE  0x0404
#define M68K_OP_ADDQ8_SP        0x508F
#define M68K_OP_RTE             0x4E73
/* Series K halt island at boot-ROM offset $5DC: ILLEGAL then BRA.S *
 * ($60FE).  Cart-BIOS mode parks unset exception vectors here so a
 * trap cannot execute PRNG-filled RAM.  Sticky: this BIOS image never
 * writes a vector table of its own. */
#define BIOS_ROM_PARK_PC        0x00E005DC

/* Cart header: byte 0 of the 4-byte CARTRIDGE block at $800400.
 * Bits 1-4 of this byte are the MEMCON1 ROM bus-width/speed bits the
 * BIOS reads to size the cart bus on power-on. */
#define CART_HEADER_BASE        0x800400
#define MEMCON1_BASE            0x1861   /* default minus the cart bits */
#define MEMCON1_CART_MASK       0x1E

/* JERRY clock dividers (chroma + video clock). */
#define JERRY_CLK3              0xF10014
#define JERRY_CLK2              0xF10012
#define CLK3_DEFAULT            0x0004
#define CLK2_NTSC               0x00B5
#define CLK2_PAL                0x00E2

/* GPU/DSP endianness registers.
 * Big-endian for both 16- and 32-bit accesses. */
#define GPU_G_END               0xF0210C
#define DSP_D_END_HI            0xF1A10C
#define DSP_D_END_LO            0xF1A10E
#define ENDIAN_BIG              0x0007
#define ENDIAN_BIG32            0x00070007

/* GPU auth-passed magic that real BIOS writes to $F03000 once it has
 * verified the cart's encryption header.  Cart code reads this to
 * decide whether the GPU has been trusted. */
#define GPU_AUTH_MAGIC          0x03D0DEAD

/* Object Processor STOP list.
 * Two long words at RAM offset $1000:
 *   .L 0x00000000        ; data
 *   .L 0x00000004        ; OP object type 4 = STOP
 * OLP is pointed at this list so the OP halts cleanly when the cart
 * has not yet installed its own object list. */
#define OP_STOP_LIST_ADDR       0x1000
#define OP_STOP_OBJECT          0x00000004

/* TOM register offsets within tomRam8 (relative to base $F00000). */
#define TOM_OLP_LO              0x20
#define TOM_OLP_HI              0x22
#define TOM_BORD1               0x2A
#define TOM_BORD2               0x2C
#define TOM_VDE                 0x48
#define TOM_VI                  0x4E
#define TOM_INT                 0xF000E0
#define TOM_INT_CLR_ALL         0x1F00

/* JERRY PIT base + I2S regs. */
#define JERRY_PIT0              0xF10000
#define JERRY_SMODE             0xF1A156
#define JERRY_SCLK              0xF1A152

/* Match what the real BIOS audio engine ends up writing.  Empirically
 * derived (2026-04-30) by snapshotting JERRY DAC regs at frame 30 with
 * BIOS vs HLE: HLE was writing SCLK=0x08 (~46 kHz I2S) / SMODE=0x01
 * (INTERNAL only); BIOS leaves SCLK=0x13 (~20 kHz) / SMODE=0x15
 * (INTERNAL + WSEN + FALLING). */
#define SCLK_DEFAULT            0x0013
#define SMODE_DEFAULT           0x0015

// Internal variables

/* Memory Track presence: an explicitly inserted MT cart dump (a cartridge
 * whose CRC we recognise) or CD content, where hardware would have the MT
 * plugged into the cartridge slot alongside the CD unit.
 *
 * There is deliberately NO MEMCON1/ROMWIDTH test here.  The old code gated on
 * ROMWIDTH == 2 on the theory that the width switch selected the MT; it does
 * not -- a plain cartridge sits at ROMWIDTH 2 as its normal width, and CD
 * content runs at ROMWIDTH 0, so the test only ever made the device
 * unreachable for discs.  The MiSTer core (the working reference, see
 * memtrack.c) gates purely on presence.
 *
 * Safety comes instead from MTClaimsRead/MTClaimsWrite, which restrict the
 * part to the $900000 NVRAM window plus a couple of override-only command
 * addresses -- everything else in cart space still reads the cartridge ROM or
 * the CD BIOS. */
#define MEMTRACK_PRESENT() \
   (jaguarMemTrackInserted || jaguarMainROMCRC32 == 0xFDF37F47)

uint32_t jaguarMainROMCRC32, jaguarROMSize, jaguarRunAddress;
uint32_t jaguarLoadedRAMStart, jaguarLoadedRAMEnd;

/* Clock-scale enhancement levers (issue #314), percent of stock rate.
 * Statically 100 so any path that runs the core before check_variables()
 * (or a harness that never sets the options) gets stock timing.  See the
 * block comment in jaguar.h for where these do and do not apply. */
uint32_t m68kClockScalePct = 100;
/* Error-diffusion remainder for the M68K scale (hundredths of a cycle,
 * same pattern as cdrom.c's fifoRefillAccum).  Without it, a sub-1x
 * scale rounds each small slice down independently -- a 1-cycle slice
 * at 0.5x scales to 0, and the UAE do/while then executes one full
 * instruction anyway, quietly defeating underclocking.  Carrying the
 * remainder makes the scale exact over time and lets zero-budget
 * slices genuinely skip.  Reset whenever the scale changes. */
static uint32_t m68kScaleAccum = 0;

void M68KClockScaleReset(void)
{
   m68kScaleAccum = 0;
}
uint32_t riscClockScalePct = 100;

bool jaguarCartInserted = false;
/* Memory Track cartridge presence.  On hardware the MT cart plugs into the
 * cartridge slot while the CD unit sits on top, so a disc and an MT cart are
 * present at the same time -- a combination the old CRC-only gate below could
 * never express.  Set for CD content; see JaguarReadWord(). */
bool jaguarMemTrackInserted = false;
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

/* Halfline-rate 68K PC sampler (BENCH_PROFILE only).  524 samples per
 * field is enough to tell WHICH wait a title's frame loop is parked in
 * without the cost of a per-instruction hook -- the question "is this
 * loop GPU-bound, DSP-handshake-bound, or field-synchronised?" is
 * answered by a PC histogram, and answering it by reasoning about the
 * source has produced two wrong root causes for #401 already.
 * Diagnostic only: the emulated machine never reads this. */
uint32_t m68kPCSample[0x2000];
uint32_t gpuPCSample[0x2000];
uint32_t m68kPCSampleIdx = 0;

// Breakpoint on memory access vars (exported)
bool bpmActive = false;
uint32_t bpmAddress1;


/* Callback function to detect illegal instructions */
static bool start = false;

void M68KInstructionHook(void)
{
   unsigned i;
   uint32_t m68kPC = m68k_get_reg(NULL, M68K_REG_PC);

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

   /* CD HLE jump-table dispatch.  When CD HLE BIOS is active, a small set
    * of magic PCs in cart ROM space resolve to BIOS routines emulated in
    * jagcd_hle.c instead of executing the cart bytes.  Returns true if the
    * hook handled the call (PC/registers updated). */
   if (JaguarCDHLEHook(m68kPC))
      return;

   /* Memory Track NVM BIOS dispatcher ($2404) — active for CD content in
    * BOTH boot modes; the module is RAM-resident on hardware, independent
    * of which CD BIOS variant booted the disc. */
   if (NVMBiosHook(m68kPC))
      return;

   /* CD boot strategy hook (cart strategy is a no-op for cart games;
    * HLE/BIOS strategies trap specific PCs to inject boot stubs, patch
    * auth checks, etc.). */
   if (bootConfig.strategy && bootConfig.strategy->instruction_hook
         && bootConfig.strategy->instruction_hook(m68kPC))
      return;
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

/* 68K DRAM self-cost (symmetric timing model): every 68K bus cycle
 * that leaves the CPU (shared DRAM, and GPU/DSP local RAM, which also
 * costs the 68K 2 system clocks) pays its DRAM/I-O access time out of the
 * 68K's own cycle budget.  naccesses = number of 16-bit bus cycles
 * (a longword = 2).  m68kBusNoCharge exempts disassembler reads so
 * debug output cannot perturb timing.
 *
 * Side effect, intended: remainingCycles now measures time including
 * wait-states, so m68k_cycles_run()-based coupling (GPUSyncToM68K)
 * sees the 68K's consumed bus time, not just retired-instruction
 * cycles. */
static int m68kBusNoCharge = 0;

#define M68K_BUS_CHARGE(addr, naccesses) \
   do { \
      if (busArbiter.enabled && !m68kBusNoCharge) \
         regs.remainingCycles -= (int32_t)bus_arbiter_m68k_access((addr), (naccesses), m68kClockScalePct); \
   } while (0)

/* A 68000 READ from GPU local RAM samples the state of a coprocessor that
 * is running concurrently, so the GPU has to be advanced to where the
 * 68000 already is inside this scheduler slice before the value is taken
 * -- the exact mirror of the write-side handshake (M68KGPURAMSync, below).
 *
 * Without it a mailbox handshake can be read stale.  Doom's GPU job
 * dispatch (issue #406): the GPU's idle loop re-writes a "done" flag at
 * $F0304C on every pass, and the 68000 posts a job by clearing $F0304C,
 * writing the routine address to $F03048, then polling $F0304C until the
 * GPU sets it again.  The write to $F03048 syncs the GPU up to the 68000's
 * position; the poll that follows a few 68000 cycles later does not, so the
 * GPU never gets the cycles in which it would have loaded the command and
 * cleared the flag.  The 68000 reads the idle loop's leftover 1, concludes
 * the job is finished before it has started, and consumes the output buffer
 * while the GPU is still filling it -- Doom then reads a level number of 0
 * out of a half-written demo header and stops in I_Error("W_GetNumForName:
 * MAP00 not found!").  On hardware the GPU runs ~2 instructions per 68000
 * cycle and clears the flag long before the first poll completes.
 *
 * Nothing about it is timing-model specific: the read simply samples the
 * GPU at the wrong point on the time line, so the outcome flips with any
 * change in 68000 cycle accounting (which is why the wedge moved between
 * VJ_DRAM_SCALE values instead of appearing past a threshold).
 *
 * m68kInLongRead suppresses the sync for the two halves of a 68000 long
 * read that decomposes into two 16-bit accesses: the GPU is advanced once,
 * before the pair, so a longword mailbox cannot be sampled with the GPU
 * having run in between and torn it. */
static int m68kInLongRead = 0;

static void M68KGPURAMSyncRead(unsigned int address, unsigned int length)
{
   if (m68kInLongRead || m68kBusNoCharge)
      return;
   if (address < GPU_WORK_RAM_BASE + 0x1000
       && address + length > GPU_WORK_RAM_BASE)
      GPUSyncToM68K();
   /* Same handshake on the DSP side (issue #456 / #408 H3).  Doom's
    * MiniLoop polls `DSPRead(&dspfinished)`; `_dspfinished` is a long
    * in dspbase.gas (`.org $f1b000`, DSP local RAM), not D_CTRL /
    * $F1A100.  The 68K read has to see the DSP that has already run
    * up to this point in the slice, not the leftover of the previous one. */
   if (address < DSP_WORK_RAM_BASE + 0x2000
       && address + length > DSP_WORK_RAM_BASE)
      DSPSyncToM68K();
}

unsigned int m68k_read_memory_8(unsigned int address)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;
   /* This bus fast path never routes through JaguarReadByte (see the
    * comment above M68K_BUS_CHARGE), so hook the watch check here.
    * m68kBusNoCharge is also true for disassembler reads (Task 7.5):
    * those aren't real bus traffic and must not appear as watch hits. */
   if (!m68kBusNoCharge)
      VJT_WATCH_RD(address, 0, M68K);
   M68K_BUS_CHARGE(address, 1);
   M68KGPURAMSyncRead(address, 1);

   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFF))
      return jaguarMainRAM[address];
   else if ((address >= 0x800000) && (address <= 0xDFFEFF))
   {
      if (MEMTRACK_PRESENT() && MTClaimsRead(address))
         return MTReadByte(address);
      if (JGD_BANKING())
         return JGDReadROM8(address - 0x800000);
      return jaguarMainROM[address - 0x800000];
   }
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
   /* Bus fast path, hooked directly -- see m68k_read_memory_8. */
   if (!m68kBusNoCharge)
      VJT_WATCH_RD(address, 0, M68K);
   M68K_BUS_CHARGE(address, 1);
   M68KGPURAMSyncRead(address, 2);

   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFE))
      return GET16(jaguarMainRAM, address);
   else if ((address >= 0x800000) && (address <= 0xDFFEFE))
   {
      /* Memory Track reading... */
      if (MEMTRACK_PRESENT() && MTClaimsRead(address))
         return MTReadWord(address);
      if (JGD_BANKING())
      {
         /* Byte-composed so a read straddling a 1 MB page boundary
          * pulls each half from its own bank. */
         uint32_t off = address - 0x800000;
         return (JGDReadROM8(off) << 8) | JGDReadROM8(off + 1);
      }
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

   if (address <= 0x1FFFFC)
   {
      /* Terminal branch (no decomposition) -- hook here.  The
       * CDROM/TOM/JERRY/unknown fallthrough below recurses into
       * m68k_read_memory_16() twice instead, which is already hooked;
       * hooking it again here would double-count that case. */
      if (!m68kBusNoCharge)
         VJT_WATCH_RD(address, 0, M68K);
      M68K_BUS_CHARGE(address, 2);
      return GET32(jaguarMainRAM, address);
   }
   else if ((address >= 0x800000) && (address <= 0xDFFEFE))
   {
      // Memory Track reading...
      /* Also terminal -- see the note above. */
      if (!m68kBusNoCharge)
         VJT_WATCH_RD(address, 0, M68K);
      M68K_BUS_CHARGE(address, 2);
      if (MEMTRACK_PRESENT() && MTClaimsRead(address))
         return MTReadLong(address);

      if (JGD_BANKING())
      {
         /* Byte-composed: a long can straddle a 1 MB page boundary. */
         uint32_t off = address - 0x800000;
         return ((uint32_t)JGDReadROM8(off) << 24)
            | ((uint32_t)JGDReadROM8(off + 1) << 16)
            | ((uint32_t)JGDReadROM8(off + 2) << 8)
            | JGDReadROM8(off + 3);
      }

      return GET32(jaguarMainROM, address - 0x800000);
   }

   /* Fallthrough recurses into _16 twice — charged there, not here.
    * The GPU catch-up runs once for the whole longword instead (see
    * M68KGPURAMSyncRead): the two halves are one 68000 access as far as
    * the mailbox handshakes that need it are concerned. */
   M68KGPURAMSyncRead(address, 4);
   {
      unsigned int v;
      m68kInLongRead++;
      v = (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
      m68kInLongRead--;
      return v;
   }
}


/* A 68000 write into GPU local RAM is a handshake with a concurrently running
 * coprocessor, so let the GPU catch up to where the 68000 already is inside
 * this scheduler slice before the 68000 goes any further.  Full explanation on
 * gpuSliceBudget in gpu.c (issue #138).
 *
 * m68kInLongWrite suppresses the sync for the two halves of a 68000 long
 * write, which reaches TOM as two word writes: the GPU must never observe a
 * half-written longword (Pitfall's mailbox poll loop read $00F00000 and jumped
 * into the TOM register file). */
static int m68kInLongWrite = 0;

/* length is the width of the access in bytes.  Testing the whole written
 * span rather than just its base address matters at the bottom edge of the
 * region: a long write starting at GPU_WORK_RAM_BASE - 2 puts its second
 * word inside GPU local RAM, and a base-address test would miss it and leave
 * the GPU un-synced for exactly the write that needs it. */
static void M68KGPURAMSync(unsigned int address, unsigned int length)
{
   if (m68kInLongWrite)
      return;
   if (address < GPU_WORK_RAM_BASE + 0x1000
       && address + length > GPU_WORK_RAM_BASE)
      GPUSyncToM68K();
   if (address < DSP_WORK_RAM_BASE + 0x2000
       && address + length > DSP_WORK_RAM_BASE)
      DSPSyncToM68K();
}

/* GPU and DSP local RAM are 32-bit memories, but an external bus master sees
 * them as 16-bit ports that must be written as ordered longword pairs:
 *
 *   "Addresses in DSP space are only available as 16-bit memory into which
 *    32-bit transfers must be performed in the order low address then high
 *    address."            -- JTRM Technical Reference v8, p.101 (p.44 for TOM)
 *
 * So the hardware latches the word written to the low address and commits the
 * full longword only when its partner at +2 arrives.  That latch is why the
 * 68000 errata singles out the two instructions that emit the halves in the
 * wrong order -- clr.l <ea> and move.l <ea>,-(An) "do not work correctly when
 * writing to Jaguar GPU & DSP hardware registers and internal RAM"
 * ("Hardware Bugs & Warnings", p.6).  Independent half-longword updates would
 * be order-insensitive and that erratum could not exist.
 *
 * Committing a lone low-address word directly into RAM is therefore more
 * permissive than the hardware, and it lets a concurrently running RISC read a
 * longword that was never written as one.  Power Drive Rally does exactly
 * that: its 68000 sound driver issues a bare `move.w #$14,$F1BE30` into a DSP
 * voice descriptor.  On hardware that word just sits in the latch; here it
 * used to land in DSP RAM, so the DSP's voice dispatcher read $00140000 as a
 * state code, indexed its jump table at $F1BB14 with it, fetched 0 and jumped
 * to PC 0.  The engine then ran garbage until it left mapped memory, at which
 * point DSPExec's escape guard parked it for good -- LTXD stopped updating and
 * the DAC froze on one sample, which is the "sound gets muted" in issue #355.
 *
 * A write that is part of a real 68000 long write (m68kInLongWrite) already
 * arrives as a correctly ordered pair, so it bypasses the latch untouched. */
#define M68K_RISC_LOCAL_RAM(a) \
   (((a) >= GPU_WORK_RAM_BASE && (a) < GPU_WORK_RAM_BASE + 0x1000) \
    || ((a) >= DSP_WORK_RAM_BASE && (a) < DSP_WORK_RAM_BASE + 0x2000))

/* Not serialised into save states, deliberately.  Note the reason is NOT
 * "a pending latch cannot outlive a frame boundary" -- it can: an unpaired
 * write (issue #355's own signature) leaves the latch held indefinitely.
 * The reason that holds is that dropping it degrades to "the lone write
 * never landed", which is what the hardware does with an unpaired half.
 * That rationale only works if the latch really is dropped when state is
 * replaced, so retro_unserialize() calls M68KResetRiscWordLatch() -- else
 * a pre-load pending low word could commit against a post-load partner
 * write.  Serialising the pair is the more faithful option if a savestate
 * version bump is being made anyway (see docs/savestate-compat.md for the
 * policy: one bump per release). */
static uint32_t m68kRiscLatchAddr = 0xFFFFFFFF;   /* low address, or ~0 */
static uint16_t m68kRiscLatchData = 0;

void M68KResetRiscWordLatch(void)
{
   m68kRiscLatchAddr = 0xFFFFFFFF;
   m68kRiscLatchData = 0;
}

/* Returns true when the caller should stop -- the word was latched and must
 * not reach RAM yet.  When the partner word arrives this commits the latched
 * half first, preserving low-then-high order, and lets the caller write the
 * second half normally. */
static bool M68KRiscWordLatch(unsigned int address, unsigned int value)
{
   if (m68kInLongWrite || !M68K_RISC_LOCAL_RAM(address))
      return false;

   if (!(address & 2))
   {
      m68kRiscLatchAddr = address;
      m68kRiscLatchData = (uint16_t)value;
      return true;
   }

   if (m68kRiscLatchAddr == address - 2)
   {
      uint16_t hi = m68kRiscLatchData;
      m68kRiscLatchAddr = 0xFFFFFFFF;
      if (address >= 0xF10000)
         JERRYWriteWord(address - 2, hi, M68K);
      else
         TOMWriteWord(address - 2, hi, M68K);
   }
   return false;
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
   /* Bus fast path, hooked directly -- never routes through
    * JaguarWriteByte (see the comment above M68K_BUS_CHARGE).  No
    * disassembler variant exists for writes, so unlike the read side
    * there is no m68kBusNoCharge guard to apply here.  UAE's `value`
    * argument is not masked to the access width (byte stores can
    * arrive sign-extended into the upper 24 bits), unlike
    * JaguarWriteByte's uint8_t parameter, so mask here to match its
    * record shape. */
   VJT_WATCH_WR(address, value & 0xFFu, M68K);
   M68K_BUS_CHARGE(address, 1);

   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFF))
   {
      if (blitMemoMode)
         BlitMemoWriteHook(address, 1, value);
      jaguarMainRAM[address] = value;
   }
   /* GameDrive: GD_ROMWriteEnable makes the SDRAM-backed "ROM" writable
    * (the GD menu loads through this; homebrew uses cart space as RAM). */
   else if (jgdActive && jgdWriteEnabled
            && (address >= 0x800000) && (address <= 0xDFFEFF))
      JGDWriteROM8(address - 0x800000, (uint8_t)value);
   /* Memory Track byte writes: cart space is otherwise read-only, so this
    * branch exists purely for the device (command window + NVRAM). */
   else if (((address >= 0x800000) && (address <= 0x87FFFF))
            || ((address >= MT_DATA_BASE) && (address < MT_DATA_END)))
   {
      if (MEMTRACK_PRESENT() && MTClaimsWrite(address))
         MTWriteByte(address, (uint8_t)value);
   }
   else if ((address >= 0xDFFF00) && (address <= 0xDFFFFF))
      CDROMWriteByte(address, value, M68K);
   else if ((address >= 0xF00000) && (address <= 0xF0FFFF))
      TOMWriteByte(address, value, M68K);
   else if ((address >= 0xF10000) && (address <= 0xF1FFFF))
      JERRYWriteByte(address, value, M68K);
   else
      jaguar_unknown_writebyte(address, value, M68K);

   M68KGPURAMSync(address, 1);
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
   /* Bus fast path, hooked directly -- terminal (never recurses into
    * another m68k_write_memory_* function), so one call here is exactly
    * one 68K bus write, including the half that only reaches the
    * GPU/DSP RISC-local latch below and not memory yet.  Masked to 16
    * bits to match JaguarWriteWord's record shape -- see the mask note
    * in m68k_write_memory_8. */
   VJT_WATCH_WR(address, value & 0xFFFFu, M68K);
   M68K_BUS_CHARGE(address, 1);

   /* GPU/DSP local RAM is a 16-bit port with a commit-on-partner latch --
    * see M68KRiscWordLatch. */
   if (M68KRiscWordLatch(address, value))
   {
      /* The half that only latches still occupies the bus, so the GPU
       * must be run up to this access exactly as it is for a committing
       * write.  Without this, GPU work that logically falls between the
       * two halves gets executed after the commit instead of before it,
       * and would observe the new longword a half-write early. */
      M68KGPURAMSync(address, 2);
      return;
   }

   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFE))
   {
      if (blitMemoMode)
         BlitMemoWriteHook(address, 2, value);
      SET16(jaguarMainRAM, address, value);
   }
   /* GameDrive write-enabled cart space (see the byte handler). */
   else if (jgdActive && jgdWriteEnabled
            && (address >= 0x800000) && (address <= 0xDFFEFE))
   {
      JGDWriteROM8(address - 0x800000, (uint8_t)(value >> 8));
      JGDWriteROM8(address - 0x800000 + 1, (uint8_t)(value & 0xFF));
   }
   /* Memory Track device writes: the flash command addresses live inside the
    * $8xxxxx ROM window, but the NVRAM itself is a separate window at
    * $900000 -- both have to be routed here or saves silently go nowhere. */
   else if (((address >= 0x800000) && (address <= 0x87FFFE))
            || ((address >= MT_DATA_BASE) && (address < MT_DATA_END)))
   {
      if (MEMTRACK_PRESENT() && MTClaimsWrite(address))
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

   M68KGPURAMSync(address, 2);
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

   if (address <= 0x1FFFFC)
   {
      /* Terminal branch (no decomposition) -- hook here.  Everything
       * else below recurses into m68k_write_memory_16() twice instead,
       * which is already hooked; hooking it again here would
       * double-count that case (three overlapping records for one
       * 32-bit access instead of the natural two).  No width mask
       * needed here (unlike the 8/16-bit sites) -- this is itself a
       * 32-bit access and `unsigned int` is exactly 32 bits, so
       * `value` already matches JaguarWriteLong's record shape. */
      VJT_WATCH_WR(address, value, M68K);
      M68K_BUS_CHARGE(address, 2);
      if (blitMemoMode)
         BlitMemoWriteHook(address, 4, value);
      SET32(jaguarMainRAM, address, value);
      return;
   }
   m68kInLongWrite++;
   m68k_write_memory_16(address, value >> 16);
   m68k_write_memory_16(address + 2, value & 0xFFFF);
   m68kInLongWrite--;

   M68KGPURAMSync(address, 4);
}

/* Disassemble M68K instructions at the given offset */

unsigned int m68k_read_disassembler_8(unsigned int address)
{
   unsigned int v;
   m68kBusNoCharge++;
   v = m68k_read_memory_8(address);
   m68kBusNoCharge--;
   return v;
}


unsigned int m68k_read_disassembler_16(unsigned int address)
{
   unsigned int v;
   m68kBusNoCharge++;
   v = m68k_read_memory_16(address);
   m68kBusNoCharge--;
   return v;
}


unsigned int m68k_read_disassembler_32(unsigned int address)
{
   unsigned int v;
   m68kBusNoCharge++;
   v = m68k_read_memory_32(address);
   m68kBusNoCharge--;
   return v;
}

uint8_t JaguarReadByte(uint32_t offset, uint32_t who)
{
   /* Mask BEFORE the watch check -- a caller passing an address with
    * upper bits set must still compare against the real 24-bit bus
    * address a watch range was defined against.  The memo hook sits
    * after the mask for the same reason. */
   offset &= 0xFFFFFF;
   VJT_WATCH_RD(offset, 0, who);
   if (blitMemoRecording)
      BlitMemoNoteRead(offset, 1);

   // First 2M is mirrored in the $0 - $7FFFFF range
   if (offset < 0x800000)
      return jaguarMainRAM[offset & 0x1FFFFF];
   else if ((offset >= 0x800000) && (offset < 0xDFFF00))
   {
      if (JGD_BANKING())
         return JGDReadROM8(offset - 0x800000);
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
   /* Mask before the watch check -- see JaguarReadByte. */
   offset &= 0xFFFFFF;
   VJT_WATCH_RD(offset, 0, who);
   if (blitMemoRecording)
      BlitMemoNoteRead(offset, 2);

   // First 2M is mirrored in the $0 - $7FFFFF range
   if (offset < 0x800000)
      return (jaguarMainRAM[(offset+0) & 0x1FFFFF] << 8) | jaguarMainRAM[(offset+1) & 0x1FFFFF];
   else if ((offset >= 0x800000) && (offset < 0xDFFF00))
   {
      offset -= 0x800000;
      if (JGD_BANKING())
         return (JGDReadROM8(offset) << 8) | JGDReadROM8(offset + 1);
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
   /* Mask before the watch check -- see JaguarReadByte. */
   offset &= 0xFFFFFF;
   VJT_WATCH_WR(offset, data, who);
   if (blitMemoMode)
      BlitMemoWriteHook(offset, 1, data);

   /* Only 2MB of DRAM is populated ($0-$1FFFFF; JTRM memory map and the
    * MiSTer core's address decode agree — $200000-$7FFFFF is unpopulated
    * expansion space).  Writes there fall on no device and vanish.
    * Mirroring them into the low 2MB (the old behaviour) let a game's
    * own out-of-range writes corrupt its code: Battle Morph's bottom
    * scroll-buffer row blits legitimately compute addresses past
    * $200000 (harmless on hardware) and the write-mirror folded them
    * onto the game's 68K code at $4400+, shredding it 8 bytes per 24
    * (the pitch-3 phrase stride) — black screen in both boot modes.
    * Reads keep the historical mirror for now: real unpopulated DRAM
    * reads float, and several recovery paths (wild-PC diagnostics)
    * depend on reads staying harmless. */
   if (offset < 0x200000)
   {
      jaguarMainRAM[offset] = data;
      return;
   }
   else if (offset < 0x800000)
      return;
   /* GameDrive write-enabled cart space (GPU/DSP/blitter writers). */
   else if (jgdActive && jgdWriteEnabled
            && (offset >= 0x800000) && (offset < 0xDFFF00))
   {
      JGDWriteROM8(offset - 0x800000, data);
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
   /* Mask before the watch check -- see JaguarReadByte. */
   offset &= 0xFFFFFF;
   VJT_WATCH_WR(offset, data, who);
   if (blitMemoMode)
      BlitMemoWriteHook(offset, 2, data);

   /* Unpopulated $200000-$7FFFFF: discard (see JaguarWriteByte). */
   if (offset <= 0x1FFFFE)
   {
      jaguarMainRAM[offset+0] = data >> 8;
      jaguarMainRAM[offset+1] = data & 0xFF;
      return;
   }
   else if (offset <= 0x7FFFFE)
      return;
   /* GameDrive write-enabled cart space (GPU/DSP/blitter writers). */
   else if (jgdActive && jgdWriteEnabled
            && offset >= 0x800000 && offset < 0xDFFF00)
   {
      JGDWriteROM8(offset - 0x800000, (uint8_t)(data >> 8));
      JGDWriteROM8(offset - 0x800000 + 1, (uint8_t)(data & 0xFF));
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


uint32_t JaguarReadLong(uint32_t offset, uint32_t who)
{
   uint32_t addr = offset & 0xFFFFFF;
   /* OP bus occupancy: every 32-bit read the object processor makes is
    * half a phrase; page-mode phrase cost is 2 system clocks (OP
    * streaming is sequential -> page hits), so charge 1 clock per long.
    * Row-change overhead is added per rendered object in op.c. */
   if (busArbiter.enabled && who == OP)
      bus_arbiter_op_charge(1);
   if (addr < 0x800000)
   {
      /* Fast path: bypasses JaguarReadWord, so the watch check there
       * never sees this access -- check it here instead.  Use addr
       * (already masked to 24 bits above), not offset, so a caller
       * passing upper bits set still compares against the real bus
       * address a watch range was defined against. */
      VJT_WATCH_RD(addr, 0, who);
      if (blitMemoRecording)
         BlitMemoNoteRead(addr, 4);
      return GET32(jaguarMainRAM, addr & 0x1FFFFF);
   }
   return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset+2, who);
}


void JaguarWriteLong(uint32_t offset, uint32_t data, uint32_t who)
{
   uint32_t addr = offset & 0xFFFFFF;
   /* OP bus occupancy: every 32-bit write-back the object processor
    * makes is half a phrase; page-mode phrase cost is 2 system clocks
    * (OP streaming is sequential -> page hits), so charge 1 clock per
    * long. Row-change overhead is added per rendered object in op.c. */
   if (busArbiter.enabled && who == OP)
      bus_arbiter_op_charge(1);
   /* CDDA-DIAG (Primal Rage): $F1B274 is the game's DSP command mailbox --
    * cmd 1 enables the DSP ISR's CD-audio mix (r20), cmd 2 disables.  The
    * missing "cmd 1" write is the open question in
    * docs/cd-diagnosis/primal-rage-cdda-diagnosis.md.  Address is game-
    * specific but the log line is harmless elsewhere (rare false hits at
    * worst).  Remove with the rest of the CDDA-DIAG layer when resolved. */
   if (addr == 0xF1B274 && data != 0)
      LOG_DBG("[CDDA] DSP mailbox $F1B274 = %08X who=%u 68kpc=$%06X\n",
              data, who, m68k_get_reg(NULL, M68K_REG_PC));
   if (addr < 0x200000)
   {
      /* Fast path: bypasses JaguarWriteWord, so the watch check there
       * never sees this access -- check it here instead.  Use addr
       * (already masked to 24 bits above) -- see JaguarReadLong. */
      VJT_WATCH_WR(addr, data, who);
      if (blitMemoMode)
         BlitMemoWriteHook(addr, 4, data);
      SET32(jaguarMainRAM, addr, data);
      return;
   }
   /* Unpopulated $200000-$7FFFFF: discard (see JaguarWriteByte). */
   else if (addr < 0x800000)
      return;
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
   for(i = 0; i < JAGUAR_RAM_SIZE; i += 4)
      SET32(jaguarMainRAM, i, JaguarRand());

   lowerField = false;							// Reset the lower field flag
   memset(jaguarMainRAM + 0x804, 0xFF, 4);

   m68k_pulse_reset();							// Need to do this so UAE disasm doesn't segfault on exit
   GPUInit();
   DSPInit();
   TOMInit();
   JERRYInit();
   CDROMInit();

   /* Fresh content boundary: drop any GameDrive image/state a previous
    * load in this process left behind (JGDLoadROM re-arms it for JST_ROM
    * content when the option + image size call for it). */
   JGDUnload();
}

/* New timer based code stuffola... */

// The thing to keep in mind is that the VC is advanced every HALF line, regardless
// of whether the display is interlaced or not. The only difference with an
// interlaced display is that the high bit of VC will be set when the lower
// field is being rendered. (NB: The high bit of VC is ALWAYS set on the lower field,
// regardless of whether it's in interlace mode or not.
// NB2: Seems it doens't always, not sure what the constraint is...)
//
/* Video field geometry -- the single source of truth behind the advertised
 * field rate (issue #392).  Definitions and the JTRM citations live in
 * jaguar.h; these are the three accessors every consumer uses so the
 * numbers cannot drift apart between libretro.c and dac.c. */
double JaguarGetHalflinePeriodUs(void)
{
   return vjs.hardwareTypeNTSC ? VJ_HALFLINE_US_NTSC : VJ_HALFLINE_US_PAL;
}

uint32_t JaguarGetDefaultFieldHalflines(void)
{
   return vjs.hardwareTypeNTSC ? VJ_HALFLINES_PER_FIELD_NTSC
                               : VJ_HALFLINES_PER_FIELD_PAL;
}

double JaguarGetFieldRateHz(void)
{
   return 1000000.0 / ((double)JaguarGetDefaultFieldHalflines()
                       * JaguarGetHalflinePeriodUs());
}

// Normally, TVs will render a full frame in 1/30s (NTSC) or 1/25s (PAL) by
// rendering two fields that are slighty vertically offset from each other.
// Each field is created in 1/60.05445s (NTSC) or 1/50.08013s (PAL) -- see the
// field-rate derivation below; 60/50 are the round numbers, not the rates --
// and every other line
// is rendered in this mode so that each field, when overlaid on each other,
// will yield the final picture at the full resolution for the full frame.
//
// We execute one field in each timeslice.  A field is VP+1 HALF lines, and
// per JTRM Rev 8 p.15 an odd half-line count selects interlace -- so a
// non-interlaced field is the even value from the Rev 10 "Video Timings"
// table: 524 for NTSC, 624 for PAL (525/625 are the interlaced variants).
// That makes a field 16651.56 us NTSC / 19968.0 us PAL, i.e. 60.05445 Hz
// and 50.08013 Hz -- not 60/50, and not NTSC's interlaced 59.94 Hz.
// See JaguarGetFieldRateHz() above and jaguar.h for the full citations.
//
// Scanline times are 63.5555... μs in NTSC and 64 μs in PAL
// Half line times are, naturally, half of this. :-P
void HalflineCallback(void)
{
   uint16_t vc           = (PERF_INC(timing_halfline_callbacks),
                            TOMReadWord(0xF00006, JAGUAR));
#ifdef BENCH_PROFILE
   m68kPCSample[m68kPCSampleIdx & 0x1FFF] =
      (uint32_t)m68k_get_reg(NULL, M68K_REG_PC);
   gpuPCSample[m68kPCSampleIdx & 0x1FFF] = GPUGetPC();
   m68kPCSampleIdx++;
#endif
   uint16_t vp           = TOMReadWord(0xF0003E, JAGUAR) + 1;
   uint16_t vi           = TOMReadWord(0xF0004E, JAGUAR);

   vc++;

   if ((vc & 0x7FF) >= vp)
   {
      lowerField = !lowerField;
      // If we're rendering the lower field, set the high bit (#11, counting
      // from 0) of VC
      vc         = (lowerField ? 0x0800 : 0x0000);
   }

   TOMWriteWord(0xF00006, vc, JAGUAR);

   // Time for Vertical Interrupt?
   if ((vc & 0x7FF) == vi && (vc & 0x7FF) > 0)
   {
      PERF_INC(timing_vblank_irqs);
      TOMSetPendingVideoInt();
   }

   TOMExecHalfline(vc, true);

   /* OP-fetch + DRAM-refresh occupancy: the 68K is the lowest-priority
    * bus master (JTRM: refresh pri 2, OP pri 6, CPU pri 11), so every
    * system clock the OP spent fetching this halfline's objects, and
    * every refresh cycle, is a clock the 68K could not use.  Deducted
    * from the 68K's next slice(s) by M68KExecuteWithStalls() — one
    * halfline of charge latency (the 68K ran halfline N unstalled and
    * pays during N+1), bounded and self-correcting. */
   if (busArbiter.enabled)
   {
      uint32_t halfclks;
      uint32_t charge;
      halfclks = (uint32_t)USEC_TO_RISC_CYCLES(
                    JaguarGetHalflinePeriodUs());
      charge = bus_arbiter_op_take() + bus_arbiter_refresh_clocks(halfclks);
      /* Bus occupancy within a halfline cannot exceed the halfline:
       * the emulated OP walks its whole list instantly, so a
       * pathological list could otherwise charge more clocks than
       * the window contains and grow the stall debt without bound.
       * When OP traffic alone saturates the window, the refresh share
       * is absorbed by the clamp (refresh scheduling itself, inside
       * bus_arbiter_refresh_clocks(), is unaffected). */
      if (charge > halfclks)
         charge = halfclks;
      busArbiter.m68k_pending_stall += charge;
   }

   /* Blitter bus-time window decays with real time: a blit finishes on
    * its own whether or not anyone is watching (blitter_mmio.c). */
   BlitterTimingTick(USEC_TO_RISC_CYCLES(JaguarGetHalflinePeriodUs()));

   //Change this to VBB???
   //Doesn't seem to matter (at least for Flip Out & I-War)
   if ((vc & 0x7FF) == 0)
   {
      JoystickExec();
      frameDone = true;
   }

   /* Tick BUTCH once per halfline when CD content is loaded.
    * BUTCHExec advances the seek/FIFO state machine and (when armed)
    * asserts GPU IRQ1 (the DSP/JERRY-sourced interrupt, vector $F03010
    * where the CD BIOS installs its CD-data ISR). Halfline cadence
    * (~32 us) is much coarser than real BUTCH I2S timing, but matches
    * our existing event-queue resolution.
    * JaguarCDHLEStreamTick advances any in-flight HLE CD_read transfer
    * at the real drive rate (no-op when idle / in BIOS mode). */
   if (bootConfig.isCDGame)
   {
      BUTCHExec(0);
      JaguarCDHLEStreamTick();
   }

   SetCallbackTime(HalflineCallback, JaguarGetHalflinePeriodUs(), EVENT_MAIN);
}

void JaguarReset(void)
{
   unsigned i;
   uint32_t clearStart = 8;            /* skip RAM[0..7] (SSP+PC) */
   uint32_t clearEnd = JAGUAR_RAM_SIZE;
   uint32_t preserveStart = jaguarLoadedRAMStart;
   uint32_t preserveEnd = jaguarLoadedRAMEnd;

   M68KResetRiscWordLatch();

   /* CD boot strategies (HLE/BIOS) hold per-run state (auth-bypass
    * installed flag, boot-stub-injected flag, HLE active flag, etc.)
    * that must be cleared on every reset.  Cart strategy reset is a no-op. */
   if (bootConfig.strategy && bootConfig.strategy->reset)
      bootConfig.strategy->reset();

   /* GameDrive: console reset returns the ASIC to identity pages,
    * write-protected, SPI idle; the game re-runs GD_Install itself. */
   JGDReset();

   // Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents.
   // Skip over any region where a RAM-loaded executable resides so we don't wipe it out.
   // In HLE (no-BIOS) mode, zero-fill instead: the real BIOS clears most of RAM
   // during its init, and many games assume zeroed working memory.
   if (vjs.useJaguarBIOS)
   {
      JaguarSeedPRNG(12345);
      /* Skip RAM[0..7] (SSP + initial PC vector); fill the rest unless
       * a RAM-loaded executable lives in this address range. */
      for(i = 8; i < JAGUAR_RAM_SIZE; i += 4)
      {
         uint32_t r = JaguarRand();
         if (jaguarLoadedRAMEnd > jaguarLoadedRAMStart
             && i >= jaguarLoadedRAMStart && i < jaguarLoadedRAMEnd)
            continue;
         SET32(jaguarMainRAM, i, r);
      }
   }
   else
   {
      if (preserveEnd > preserveStart)
      {
         if (preserveStart < clearStart)
            preserveStart = clearStart;
         if (preserveEnd > clearEnd)
            preserveEnd = clearEnd;

         if (preserveStart < preserveEnd)
         {
            memset(jaguarMainRAM + clearStart, 0, preserveStart - clearStart);
            memset(jaguarMainRAM + preserveEnd, 0, clearEnd - preserveEnd);
         }
         else
            memset(jaguarMainRAM + clearStart, 0, clearEnd - clearStart);
      }
      else
         memset(jaguarMainRAM + clearStart, 0, clearEnd - clearStart);
   }

   // New timer base code stuffola...
   InitializeEventList();
   //Need to change this so it uses the single RAM space and load the BIOS
   //into it somewhere...
   //Also, have to change this here and in JaguarReadXX() currently
   // Only use the system BIOS if it's available...! (it's always available now!)
   // AND only if a jaguar cartridge has been inserted.
   if (vjs.useJaguarBIOS && jaguarCartInserted)
   {
      memcpy(jaguarMainRAM, jagMemSpace + 0xE00000, 8);

      /* Cart BIOS only.  JaguarReset PRNG-fills RAM[8..] (vectors 2-255)
       * to mimic power-on DRAM.  Series K never installs a vector table;
       * it copies SSP+PC, copies workspace to $5000, then a 60-byte
       * trampoline to $400-$43B (LEA $400 / JMP $400) -- not the vectors.
       * A GPU-only jagcrypt cart (Fountain, #469) therefore leaves the
       * 68K with no program after that trampoline.  Illegal/F-line
       * fetches through PRNG vectors smash GPU RAM / G_PC; the core
       * then presents 1024-wide frames and RetroArch aborts (max_width
       * is 652).
       *
       * Park traps at the boot ROM BRA.S * ($E005DC), not a RAM stub:
       * the $400 trampoline would overwrite an HLE-style RTE there.
       * Skip CD BIOS: that path also sets jaguarCartInserted (the CD
       * BIOS image is mapped as a cart) and uses ILLEGAL as a deliberate
       * halt; parking vector 4 would turn that halt into a ROM spin. */
      if (!bootConfig.isCDGame)
      {
         unsigned v;

         for (v = 2; v <= 255; v++)
            SET32(jaguarMainRAM, v * 4, BIOS_ROM_PARK_PC);
      }
   }
   else
   {
      /* For RAM-loaded executables (.abs/.cof/JagServer), park SSP at the
         top of main RAM so the stack can't overlap loaded code/data.  For
         cartridge HLE, keep the historical 0x4000 SSP that matches what
         the real BIOS leaves behind. */
      uint32_t hleSSP = (jaguarLoadedRAMEnd > jaguarLoadedRAMStart)
         ? HLE_SSP_RAMLOAD : HLE_SSP_CART;
      SET32(jaguarMainRAM, 0, hleSSP);
      SET32(jaguarMainRAM, 4, jaguarRunAddress);
   }

   TOMReset();
   JERRYReset();
   GPUReset();
   DSPReset();
   CDROMReset();

   /* HLE BIOS: replicate post-boot hardware state that the real BIOS
    * leaves behind before jumping to the cartridge.  Without this,
    * games that rely on BIOS-initialized registers won't boot.  All
    * named constants used below are defined at file scope above this
    * function (look for the "HLE BIOS / Jaguar memory layout
    * constants" header comment). */
   if (!vjs.useJaguarBIOS && jaguarCartInserted)
   {
      uint8_t cartTypeByte;
      uint16_t newMemcon1;
      unsigned v;

      /* --- Exception vector stubs ---
       * Series K BIOS does not fill the vector table.  HLE does, with
       * RTE stubs, so a bus/address error or IRQ does not jump through
       * PRNG RAM.  Cart BIOS mode parks traps at $E005DC instead (see
       * JaguarReset above). */
      SET16(jaguarMainRAM, HLE_EXCEPT_HANDLER, M68K_OP_ADDQ8_SP);
      SET16(jaguarMainRAM, HLE_EXCEPT_HANDLER + 2, M68K_OP_RTE);
      SET16(jaguarMainRAM, HLE_EXCEPT_HANDLER_RTE, M68K_OP_RTE);

      /* Vectors 2-3: bus error, address error → long frame handler */
      SET32(jaguarMainRAM, 0x08, HLE_EXCEPT_HANDLER);
      SET32(jaguarMainRAM, 0x0C, HLE_EXCEPT_HANDLER);

      /* Vectors 4-255: all other exceptions → simple RTE
       * CRITICAL: vector 64 ($100) is the Jaguar interrupt vector —
       * irq_ack_handler() returns 64 for ALL hardware interrupts.
       * If $100 contains PRNG garbage, the first interrupt crashes. */
      for (v = 4; v <= 255; v++)
         SET32(jaguarMainRAM, v * 4, HLE_EXCEPT_HANDLER_RTE);

      JaguarApplyHLEBIOSState();

      /* --- MEMCON1 auto-detect from cart header ---
       * The BIOS reads bits 1-4 for ROM bus width/speed. */
      cartTypeByte = jagMemSpace[CART_HEADER_BASE];
      newMemcon1 = MEMCON1_BASE | (cartTypeByte & MEMCON1_CART_MASK);
      SET16(tomRam8, 0x00, newMemcon1);

      /* --- JERRY clock dividers --- */
      JERRYWriteWord(JERRY_CLK3, CLK3_DEFAULT, M68K);
      JERRYWriteWord(JERRY_CLK2,
            (vjs.hardwareTypeNTSC ? CLK2_NTSC : CLK2_PAL), M68K);

      /* --- GPU/DSP endianness registers ---
       * Big-endian for 32-bit and 16-bit accesses */
      GPUWriteLong(GPU_G_END, ENDIAN_BIG32, M68K);
      JERRYWriteWord(DSP_D_END_HI, ENDIAN_BIG, M68K);
      JERRYWriteWord(DSP_D_END_LO, ENDIAN_BIG, M68K);

      /* --- GPU encryption check magic ---
       * Games check this to verify the cart passed authentication. */
      GPUWriteLong(0xF03000, GPU_AUTH_MAGIC, M68K);

      /* --- Object Processor STOP list ---
       * The BIOS sets up a minimal OP list: STOP object (type 4). */
      SET32(jaguarMainRAM, OP_STOP_LIST_ADDR, 0x00000000);
      SET32(jaguarMainRAM, OP_STOP_LIST_ADDR + 4, OP_STOP_OBJECT);
      /* Point OLP to the STOP list (LO/HI word order). */
      SET16(tomRam8, TOM_OLP_LO, OP_STOP_LIST_ADDR);
      SET16(tomRam8, TOM_OLP_HI, 0x0000);

      /* --- Clear border color --- */
      SET16(tomRam8, TOM_BORD1, 0x0000);
      SET16(tomRam8, TOM_BORD2, 0x0000);

      /* --- Vertical interrupt line ---
       * The boot ROM programs VI to the first VBlank halfline (VDE+1,
       * measured $207 on the retail BIOS) before jumping to the cart.
       * Carts may enable the INT1 VI bit without ever writing VI
       * (Raiden does), relying on this value; with VI left 0 the
       * compare never fires and the game spins waiting for its VBlank
       * ISR. */
      SET16(tomRam8, TOM_VI, (uint16_t)(GET16(tomRam8, TOM_VDE) + 1));

      /* --- Interrupts: clear all pending, disable all enables --- */
      TOMWriteWord(TOM_INT, TOM_INT_CLR_ALL, M68K);

      /* --- Clear JERRY PIT timers --- */
      JERRYWriteWord(JERRY_PIT0 + 0, 0x0000, M68K);
      JERRYWriteWord(JERRY_PIT0 + 2, 0x0000, M68K);
      JERRYWriteWord(JERRY_PIT0 + 4, 0x0000, M68K);
      JERRYWriteWord(JERRY_PIT0 + 6, 0x0000, M68K);

      /* --- I2S (SCLK/SMODE) setup ---
       * The BIOS configures I2S with internal clock so JERRY fires
       * periodic SSI interrupts on the DSP.  Games that load their own
       * DSP programs often rely on these interrupts being active. */
      JERRYWriteWord(JERRY_SMODE, SMODE_DEFAULT, M68K);
      JERRYWriteWord(JERRY_SCLK, SCLK_DEFAULT, M68K);

      /* NB: The real BIOS would copy a 1992-byte DSP audio engine from
       * jaguarBootROM[0x214E..0x2916] into DSP RAM at offset 0 and
       * start the DSP, but this engine code alone does not work
       * without also replicating the DSP register-bank state that the
       * BIOS leaves behind.  Tried it (engine bytes + D_PC at engine
       * entry / mainloop / DSPGO=1) and the DSP escapes DSP RAM
       * within a few hundred frames (PC ends up at addresses like
       * 0x8A or 0x74 — main-RAM nonsense), because the engine reads
       * uninitialized DSP registers and uses them as jump targets.
       * Wolfenstein 3D and Skyhammer / IS2 audio remain broken on
       * HLE for this reason.  See docs/emulation-bug-hunt-todos.md
       * "Skyhammer / Iron Soldier 2 audio clipping" for next steps. */
   }

   m68k_pulse_reset();								// Reset the 68000

   lowerField = false;								// Reset the lower field flag
   SetCallbackTime(HalflineCallback, JaguarGetHalflinePeriodUs(), EVENT_MAIN);
}


void JaguarApplyHLEBIOSState(void)
{
   if (!vjs.useJaguarBIOS && jaguarCartInserted
         && GET32(jaguarMainRAM, HLE_BIOS_WORK_FLAG_ADDR) == 0)
      SET32(jaguarMainRAM, HLE_BIOS_WORK_FLAG_ADDR, HLE_BIOS_WORK_READY);
}


void JaguarDone(void)
{
   CDROMDone();
   DSPDone();
   TOMDone();
   JERRYDone();
   JGDDone();
   m68k_done();
}

uint8_t * GetRamPtr(void)
{
   return jaguarMainRAM;
}


/* Run a 68K slice minus any pending OP-fetch/refresh stall.  pending
 * is in system clocks; the 68K runs at system/2, so two pending clocks
 * consume one 68K cycle.  A slice can be fully consumed (the 68K runs
 * zero cycles) when occupancy exceeds it; the remainder carries into
 * the next slice, so no time is lost or invented. */
static void M68KExecuteWithStalls(uint32_t cycles)
{
   uint32_t stall;
   /* The drain is deliberately NOT gated on busArbiter.enabled: the
    * pending-stall channel is shared by the dram_timing model (whose
    * charge sites gate on the option) and blitter bus-time charges
    * (gated on vjs.blitterTiming at the charge site in
    * blitter_mmio.c).  With every charge site off the field stays
    * zero and this branch never runs, so pure-default behavior is
    * unchanged. */
   if (busArbiter.m68k_pending_stall >= 2)
   {
      /* Leave the 68K an eighth of every slice even under maximum
       * debt.  The blitter is the top-priority master but not a
       * perfect bus hog -- refresh slots and inter-op gaps still grant
       * the 68K occasional cycles (JTRM bus priority) -- and a full
       * freeze starves IRQ delivery: the VI handler that latches the
       * joypad stops running, a released button reads as held for the
       * whole debt, and one tap multiplies into many menu steps (the
       * exact symptom this model exists to fix, amplified).  Rounded
       * UP so even a sub-8-cycle slice keeps at least one cycle --
       * a floor of cycles>>3 would be 0 there and let a run of tiny
       * event-bounded slices fully freeze the 68K after all. */
      uint32_t keep = (cycles + 7) >> 3;
      stall = busArbiter.m68k_pending_stall >> 1;
      if (stall > cycles - keep)
         stall = cycles - keep;
      busArbiter.m68k_pending_stall -= stall << 1;
      cycles -= stall;
      /* A slice fully consumed by stall must not fall through to
       * m68k_execute(0): the UAE core's main loop is a do/while, so
       * even a zero-cycle budget executes exactly one instruction --
       * which would silently defeat the stall. Off-mode never enters
       * this branch, so develop's m68k_execute(0) edge-case behavior
       * (timeDelta rounding to 0 cycles) is unchanged. */
      if (cycles == 0)
         return;
   }
   /* The M68K clock scale applies to the cycles the CPU actually
    * executes, AFTER the stall deduction: the stall models real bus
    * occupancy (OP fetch, DRAM refresh) in wall time, which an
    * overclocked CPU on modified hardware would still sit out in
    * full.  At 100 the else branch is the exact pre-existing path.
    *
    * Non-100 uses error diffusion: the remainder in hundredths of a
    * cycle carries to the next slice, so 0.5x is exact over time and a
    * slice whose scaled budget is zero genuinely skips -- passing 0 to
    * m68k_execute() would run one instruction regardless (UAE
    * do/while), which is precisely the underclock-defeating edge the
    * review flagged. */
   if (m68kClockScalePct != 100u)
   {
      uint64_t budget = (uint64_t)cycles * m68kClockScalePct
                      + m68kScaleAccum;
      uint32_t scaled = (uint32_t)(budget / 100u);
      m68kScaleAccum  = (uint32_t)(budget % 100u);
      if (scaled == 0)
         return;
      m68k_execute(scaled);
      return;
   }
   m68k_execute(cycles);
}


/* New Jaguar execution stack
 * This executes 1 frame's worth of code.
 * Interleaves EVENT_MAIN (video/halfline) and EVENT_JERRY (DSP/I2S/timers)
 * so the DSP runs alongside the 68K and GPU, matching real hardware timing. */
void JaguarExecuteNew(void)
{
   PERF_INC(timing_jaguar_execute_calls);
   frameDone = false;

   do
   {
      double timeToMainEvent = GetTimeToNextEvent(EVENT_MAIN);
      double timeToJerryEvent = GetTimeToNextEvent(EVENT_JERRY);
      double timeDelta;
      uint32_t riscCycles;

      /* GPUBeginSlice/DSPBeginSlice + *SliceRemaining: part of each
       * RISC slice may already have been run from GPUSyncToM68K() /
       * DSPSyncToM68K() on a 68K access into local RAM, so the
       * end-of-slice call runs only what is left.  The total per slice
       * is unchanged -- see gpuSliceBudget in gpu.c and issue #456.
       *
       * Clock scales (issue #314) apply here, where the budgets are
       * handed out: the RISC scale widens the GPU+DSP compute budget per
       * slice, the M68K scale is applied inside M68KExecuteWithStalls()
       * after the (unscaled, wall-time) bus-occupancy stall.  Event
       * scheduling (timeDelta, EVENT_MAIN/EVENT_JERRY) stays on the real
       * sysclock, so video, PIT/UART timers and I2S sample pacing are
       * untouched -- more DSP cycles run between I2S interrupts, but the
       * interrupts (and thus audio pitch) keep their stock rate. */
      if (timeToJerryEvent < timeToMainEvent)
      {
         timeDelta = timeToJerryEvent;
         riscCycles = SCALE_RISC_CYCLES(USEC_TO_RISC_CYCLES(timeDelta));
         GPUBeginSlice(riscCycles);
         DSPBeginSlice(riscCycles);
         M68KExecuteWithStalls(USEC_TO_M68K_CYCLES(timeDelta));
         GPUExec(GPUSliceRemaining());
         DSPExec(DSPSliceRemaining());
         SubtractEventTimes(timeDelta, EVENT_MAIN);
         HandleNextEvent(EVENT_JERRY);
      }
      else
      {
         timeDelta = timeToMainEvent;
         riscCycles = SCALE_RISC_CYCLES(USEC_TO_RISC_CYCLES(timeDelta));
         GPUBeginSlice(riscCycles);
         DSPBeginSlice(riscCycles);
         M68KExecuteWithStalls(USEC_TO_M68K_CYCLES(timeDelta));
         GPUExec(GPUSliceRemaining());
         DSPExec(DSPSliceRemaining());
         SubtractEventTimes(timeDelta, EVENT_JERRY);
         HandleNextEvent(EVENT_MAIN);
      }
      PERF_ADD(timing_m68k_cycles, (unsigned long long)SCALE_M68K_CYCLES(USEC_TO_M68K_CYCLES(timeDelta)));
      PERF_ADD(timing_risc_cycles, (unsigned long long)SCALE_RISC_CYCLES(USEC_TO_RISC_CYCLES(timeDelta)));
   } while(!frameDone);
}

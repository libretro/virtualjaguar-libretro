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

#include "jaguar.h"

#include "cdrom.h"
#include "dac.h"
#include "dsp.h"
#include "eeprom.h"
#include "event.h"
#include "gpu.h"
#include "jerry.h"
#include "joystick.h"
#include "m68000/m68kinterface.h"
#include "memtrack.h"
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

uint32_t jaguarMainROMCRC32, jaguarROMSize, jaguarRunAddress;
uint32_t jaguarLoadedRAMStart, jaguarLoadedRAMEnd;

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

unsigned int m68k_read_memory_8(unsigned int address)
{
#ifdef ALPINE_FUNCTIONS
   // Check if breakpoint on memory is active, and deal with it
   if (bpmActive && address == bpmAddress1)
      M68KDebugHalt();
#endif

   // Musashi does this automagically for you, UAE core does not :-P
   address &= 0x00FFFFFF;

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
      return GET32(jaguarMainRAM, address);
   else if ((address >= 0x800000) && (address <= 0xDFFEFE))
   {
      // Memory Track reading...
      if (((TOMGetMEMCON1() & 0x0006) == (2 << 1)) && (jaguarMainROMCRC32 == 0xFDF37F47))
         return MTReadLong(address);

      return GET32(jaguarMainROM, address - 0x800000);
   }

   return (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
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
      SET32(jaguarMainRAM, address, value);
      return;
   }
   m68k_write_memory_16(address, value >> 16);
   m68k_write_memory_16(address + 2, value & 0xFFFF);
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
      return jaguarMainROM[offset - 0x800000];
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


uint32_t JaguarReadLong(uint32_t offset, uint32_t who)
{
   uint32_t addr = offset & 0xFFFFFF;
   if (addr < 0x800000)
      return GET32(jaguarMainRAM, addr & 0x1FFFFF);
   return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset+2, who);
}


void JaguarWriteLong(uint32_t offset, uint32_t data, uint32_t who)
{
   uint32_t addr = offset & 0xFFFFFF;
   if (addr < 0x800000)
   {
      SET32(jaguarMainRAM, addr & 0x1FFFFF, data);
      return;
   }
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
      TOMSetPendingVideoInt();

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
   uint32_t clearStart = 8;            /* skip RAM[0..7] (SSP+PC) */
   uint32_t clearEnd = JAGUAR_RAM_SIZE;
   uint32_t preserveStart = jaguarLoadedRAMStart;
   uint32_t preserveEnd = jaguarLoadedRAMEnd;

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
      memcpy(jaguarMainRAM, jagMemSpace + 0xE00000, 8);
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
       * The real BIOS populates the entire vector table with safe
       * handlers.  Without this, any exception (bus error, illegal
       * instruction, etc.) jumps to random PRNG garbage and the CPU
       * double-faults.  Place RTE stubs in low RAM and fill vectors. */
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
   SetCallbackTime(HalflineCallback, (vjs.hardwareTypeNTSC ? 31.777777777 : 32.0), EVENT_MAIN);
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
   m68k_done();
}

uint8_t * GetRamPtr(void)
{
   return jaguarMainRAM;
}


/* New Jaguar execution stack
 * This executes 1 frame's worth of code.
 * Interleaves EVENT_MAIN (video/halfline) and EVENT_JERRY (DSP/I2S/timers)
 * so the DSP runs alongside the 68K and GPU, matching real hardware timing. */
void JaguarExecuteNew(void)
{
   frameDone = false;

   do
   {
      double timeToMainEvent = GetTimeToNextEvent(EVENT_MAIN);
      double timeToJerryEvent = GetTimeToNextEvent(EVENT_JERRY);

      if (timeToJerryEvent < timeToMainEvent)
      {
         m68k_execute(USEC_TO_M68K_CYCLES(timeToJerryEvent));
         GPUExec(USEC_TO_RISC_CYCLES(timeToJerryEvent));
         DSPExec(USEC_TO_RISC_CYCLES(timeToJerryEvent));
         SubtractEventTimes(timeToJerryEvent, EVENT_MAIN);
         HandleNextEvent(EVENT_JERRY);
      }
      else
      {
         m68k_execute(USEC_TO_M68K_CYCLES(timeToMainEvent));
         GPUExec(USEC_TO_RISC_CYCLES(timeToMainEvent));
         DSPExec(USEC_TO_RISC_CYCLES(timeToMainEvent));
         SubtractEventTimes(timeToMainEvent, EVENT_JERRY);
         HandleNextEvent(EVENT_MAIN);
      }
   } while(!frameDone);
}

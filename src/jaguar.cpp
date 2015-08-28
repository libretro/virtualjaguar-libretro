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

#include <time.h>

#include "jaguar.h"

#include "cdrom.h"
#include "dsp.h"
#include "eeprom.h"
#include "event.h"
#include "gpu.h"
#include "jerry.h"
#include "joystick.h"
#include "log.h"
#include "m68000/m68kinterface.h"
#include "settings.h"
#include "tom.h"

bool frameDone;
uint32_t starCount;

//#define CPU_DEBUG
//#define LOG_UNMAPPED_MEMORY_ACCESSES
//#define ABORT_ON_UNMAPPED_MEMORY_ACCESS
//#define ABORT_ON_ILLEGAL_INSTRUCTIONS
//#define ABORT_ON_OFFICIAL_ILLEGAL_INSTRUCTION
//#define CPU_DEBUG_MEMORY
//#define LOG_CD_BIOS_CALLS
//#define CPU_DEBUG_TRACING
#define ALPINE_FUNCTIONS

// Private function prototypes

unsigned jaguar_unknown_readbyte(unsigned address, uint32_t who)
{
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
   WriteLog("Jaguar: Unknown byte read at %08X by %s (M68K PC=%06X)\n", address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
   finished = true;
   if (who == DSP)
      doDSPDis = true;
#endif
   return 0xFF;
}

unsigned jaguar_unknown_readword(unsigned address, uint32_t who)
{
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
   WriteLog("Jaguar: Unknown word read at %08X by %s (M68K PC=%06X)\n", address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
   finished = true;
   if (who == DSP)
      doDSPDis = true;
#endif
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
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
   WriteLog("Jaguar: Unknown byte %02X written at %08X by %s (M68K PC=%06X)\n", data, address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
   finished = true;
   if (who == DSP)
      doDSPDis = true;
#endif
}

void jaguar_unknown_writeword(unsigned address, unsigned data, uint32_t who)
{
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
   WriteLog("Jaguar: Unknown word %04X written at %08X by %s (M68K PC=%06X)\n", data, address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
   finished = true;
   if (who == DSP)
      doDSPDis = true;
#endif
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

void M68K_show_context(void)
{
   WriteLog("68K PC=%06X\n", m68k_get_reg(NULL, M68K_REG_PC));

   for(int i=M68K_REG_D0; i<=M68K_REG_D7; i++)
   {
      WriteLog("D%i = %08X ", i-M68K_REG_D0, m68k_get_reg(NULL, (m68k_register_t)i));

      if (i == M68K_REG_D3 || i == M68K_REG_D7)
         WriteLog("\n");
   }

   for(int i=M68K_REG_A0; i<=M68K_REG_A7; i++)
   {
      WriteLog("A%i = %08X ", i-M68K_REG_A0, m68k_get_reg(NULL, (m68k_register_t)i));

      if (i == M68K_REG_A3 || i == M68K_REG_A7)
         WriteLog("\n");
   }

   WriteLog("68K disasm\n");
   JaguarDasm(m68k_get_reg(NULL, M68K_REG_PC) - 0x80, 0x200);

   if (TOMIRQEnabled(IRQ_VIDEO))
   {
      WriteLog("video int: enabled\n");
      JaguarDasm(JaguarGetHandler(64), 0x200);
   }
   else
      WriteLog("video int: disabled\n");

   WriteLog("..................\n");

   for(int i=0; i<256; i++)
   {
      WriteLog("handler %03i at ", i);
      uint32_t address = (uint32_t)JaguarGetHandler(i);

      if (address == 0)
         WriteLog(".........\n");
      else
         WriteLog("$%08X\n", address);
   }
}

// External variables

#ifdef CPU_DEBUG_MEMORY
extern bool startMemLog;							// Set by "e" key
extern int effect_start;
extern int effect_start2, effect_start3, effect_start4, effect_start5, effect_start6;
#endif

// Really, need to include memory.h for this, but it might interfere with some stuff...
extern uint8_t jagMemSpace[];

// Internal variables

uint32_t jaguar_active_memory_dumps = 0;

#ifdef __cplusplus
extern "C" {
#endif
   uint32_t jaguarMainROMCRC32, jaguarROMSize, jaguarRunAddress;
#ifdef __cplusplus
}
#endif
bool jaguarCartInserted = false;
bool lowerField = false;

#ifdef CPU_DEBUG_MEMORY
uint8_t writeMemMax[0x400000], writeMemMin[0x400000];
uint8_t readMem[0x400000];
uint32_t returnAddr[4000], raPtr = 0xFFFFFFFF;
#endif

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
   uint32_t m68kPC = m68k_get_reg(NULL, M68K_REG_PC);

   /* For code tracing... */
#ifdef CPU_DEBUG_TRACING
   if (startM68KTracing)
   {
      static char buffer[2048];

      m68k_disassemble(buffer, m68kPC, 0);
      WriteLog("%06X: %s\n", m68kPC, buffer);
   }
#endif

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
   {
      WriteLog("M68K: Attempted to execute from an odd address!\n\nBacktrace:\n\n");

      static char buffer[2048];
      for(int i=0; i<0x400; i++)
      {
         //			WriteLog("[A2=%08X, D0=%08X]\n", a2Queue[(pcQPtr + i) & 0x3FF], d0Queue[(pcQPtr + i) & 0x3FF]);
         WriteLog("[A0=%08X, A1=%08X, A2=%08X, A3=%08X, A4=%08X, A5=%08X, A6=%08X, A7=%08X, D0=%08X, D1=%08X, D2=%08X, D3=%08X, D4=%08X, D5=%08X, D6=%08X, D7=%08X]\n", a0Queue[(pcQPtr + i) & 0x3FF], a1Queue[(pcQPtr + i) & 0x3FF], a2Queue[(pcQPtr + i) & 0x3FF], a3Queue[(pcQPtr + i) & 0x3FF], a4Queue[(pcQPtr + i) & 0x3FF], a5Queue[(pcQPtr + i) & 0x3FF], a6Queue[(pcQPtr + i) & 0x3FF], a7Queue[(pcQPtr + i) & 0x3FF], d0Queue[(pcQPtr + i) & 0x3FF], d1Queue[(pcQPtr + i) & 0x3FF], d2Queue[(pcQPtr + i) & 0x3FF], d3Queue[(pcQPtr + i) & 0x3FF], d4Queue[(pcQPtr + i) & 0x3FF], d5Queue[(pcQPtr + i) & 0x3FF], d6Queue[(pcQPtr + i) & 0x3FF], d7Queue[(pcQPtr + i) & 0x3FF]);
         m68k_disassemble(buffer, pcQueue[(pcQPtr + i) & 0x3FF], 0);//M68K_CPU_TYPE_68000);
         WriteLog("\t%08X: %s\n", pcQueue[(pcQPtr + i) & 0x3FF], buffer);
      }
      WriteLog("\n");

      M68K_show_context();
      LogDone();
      exit(0);
   }

#ifdef LOG_CD_BIOS_CALLS
   /*
      CD_init::	-> $3000
      BIOS_VER::	-> $3004
      CD_mode::	-> $3006
      CD_ack::	-> $300C
      CD_jeri::	-> $3012
      CD_spin::	-> $3018
      CD_stop::	-> $301E
      CD_mute::	-> $3024
      CD_umute::	-> $302A
      CD_paus::	-> $3030
      CD_upaus::	-> $3036
      CD_read::	-> $303C
      CD_uread::	-> $3042
      CD_setup::	-> $3048
      CD_ptr::	-> $304E
      CD_osamp::	-> $3054
      CD_getoc::	-> $305A
      CD_initm::	-> $3060
      CD_initf::	-> $3066
      CD_switch::	-> $306C
      */
   if (m68kPC == 0x3000)
      WriteLog("M68K: CD_init\n");
   else if (m68kPC == 0x3006 + (6 * 0))
      WriteLog("M68K: CD_mode\n");
   else if (m68kPC == 0x3006 + (6 * 1))
      WriteLog("M68K: CD_ack\n");
   else if (m68kPC == 0x3006 + (6 * 2))
      WriteLog("M68K: CD_jeri\n");
   else if (m68kPC == 0x3006 + (6 * 3))
      WriteLog("M68K: CD_spin\n");
   else if (m68kPC == 0x3006 + (6 * 4))
      WriteLog("M68K: CD_stop\n");
   else if (m68kPC == 0x3006 + (6 * 5))
      WriteLog("M68K: CD_mute\n");
   else if (m68kPC == 0x3006 + (6 * 6))
      WriteLog("M68K: CD_umute\n");
   else if (m68kPC == 0x3006 + (6 * 7))
      WriteLog("M68K: CD_paus\n");
   else if (m68kPC == 0x3006 + (6 * 8))
      WriteLog("M68K: CD_upaus\n");
   else if (m68kPC == 0x3006 + (6 * 9))
      WriteLog("M68K: CD_read\n");
   else if (m68kPC == 0x3006 + (6 * 10))
      WriteLog("M68K: CD_uread\n");
   else if (m68kPC == 0x3006 + (6 * 11))
      WriteLog("M68K: CD_setup\n");
   else if (m68kPC == 0x3006 + (6 * 12))
      WriteLog("M68K: CD_ptr\n");
   else if (m68kPC == 0x3006 + (6 * 13))
      WriteLog("M68K: CD_osamp\n");
   else if (m68kPC == 0x3006 + (6 * 14))
      WriteLog("M68K: CD_getoc\n");
   else if (m68kPC == 0x3006 + (6 * 15))
      WriteLog("M68K: CD_initm\n");
   else if (m68kPC == 0x3006 + (6 * 16))
      WriteLog("M68K: CD_initf\n");
   else if (m68kPC == 0x3006 + (6 * 17))
      WriteLog("M68K: CD_switch\n");

   if (m68kPC >= 0x3000 && m68kPC <= 0x306C)
      WriteLog("\t\tA0=%08X, A1=%08X, D0=%08X, D1=%08X, D2=%08X\n",
            m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
            m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2));
#endif

#ifdef ABORT_ON_ILLEGAL_INSTRUCTIONS
   if (!m68k_is_valid_instruction(m68k_read_memory_16(m68kPC), 0))//M68K_CPU_TYPE_68000))
   {
#ifndef ABORT_ON_OFFICIAL_ILLEGAL_INSTRUCTION
      if (m68k_read_memory_16(m68kPC) == 0x4AFC)
      {
         // This is a kludge to let homebrew programs work properly (i.e., let the other processors
         // keep going even when the 68K dumped back to the debugger or what have you).
         //dis no wok right!
         //			m68k_set_reg(M68K_REG_PC, m68kPC - 2);
         // Try setting the vector to the illegal instruction...
         //This doesn't work right either! Do something else! Quick!
         //			SET32(jaguar_mainRam, 0x10, m68kPC);

         return;
      }
#endif

      M68K_show_context();

      LogDone();
      exit(0);
   }
#endif
}

//#define EXPERIMENTAL_MEMORY_HANDLING
// Experimental memory mappage...
// Dunno if this is a good approach or not, but it seems to make better
// sense to have all this crap in one spot intstead of scattered all over
// the place the way it is now.
#ifdef EXPERIMENTAL_MEMORY_HANDLING
// Needed defines...
#define NEW_TIMER_SYSTEM

enum MemType { MM_NOP = 0, MM_RAM, MM_ROM, MM_IO };

// M68K Memory map/handlers
uint32_t 	{
   { 0x000000, 0x3FFFFF, MM_RAM, jaguarMainRAM },
      { 0x800000, 0xDFFEFF, MM_ROM, jaguarMainROM },
      // Note that this is really memory mapped I/O region...
      //	{ 0xDFFF00, 0xDFFFFF, MM_RAM, cdRAM },
      { 0xDFFF00, 0xDFFF03, MM_IO,  cdBUTCH }, // base of Butch == interrupt control register, R/W
      { 0xDFFF04, 0xDFFF07, MM_IO,  cdDSCNTRL }, // DSA control register, R/W
      { 0xDFFF0A, 0xDFFF0B, MM_IO,  cdDS_DATA }, // DSA TX/RX data, R/W
      { 0xDFFF10, 0xDFFF13, MM_IO,  cdI2CNTRL }, // i2s bus control register, R/W
      { 0xDFFF14, 0xDFFF17, MM_IO,  cdSBCNTRL }, // CD subcode control register, R/W
      { 0xDFFF18, 0xDFFF1B, MM_IO,  cdSUBDATA }, // Subcode data register A
      { 0xDFFF1C, 0xDFFF1F, MM_IO,  cdSUBDATB }, // Subcode data register B
      { 0xDFFF20, 0xDFFF23, MM_IO,  cdSB_TIME }, // Subcode time and compare enable (D24)
      { 0xDFFF24, 0xDFFF27, MM_IO,  cdFIFO_DATA }, // i2s FIFO data
      { 0xDFFF28, 0xDFFF2B, MM_IO,  cdI2SDAT2 }, // i2s FIFO data (old)
      { 0xDFFF2C, 0xDFFF2F, MM_IO,  cdUNKNOWN }, // Seems to be some sort of I2S interface

      { 0xE00000, 0xE3FFFF, MM_ROM, jaguarBootROM },

      //	{ 0xF00000, 0xF0FFFF, MM_IO,  TOM_REGS_RW },
      { 0xF00050, 0xF00051, MM_IO,  tomTimerPrescaler },
      { 0xF00052, 0xF00053, MM_IO,  tomTimerDivider },
      { 0xF00400, 0xF005FF, MM_RAM, tomRAM }, // CLUT A&B: How to link these? Write to one writes to the other...
      { 0xF00600, 0xF007FF, MM_RAM, tomRAM }, // Actually, this is a good approach--just make the reads the same as well
      //What about LBUF writes???
      { 0xF02100, 0xF0211F, MM_IO,  GPUWriteByte }, // GPU CONTROL
      { 0xF02200, 0xF0229F, MM_IO,  BlitterWriteByte }, // BLITTER
      { 0xF03000, 0xF03FFF, MM_RAM, GPUWriteByte }, // GPU RAM

      { 0xF10000, 0xF1FFFF, MM_IO,  JERRY_REGS_RW },

};

void WriteByte(uint32_t address, uint8_t byte, uint32_t who)
{
   // Not sure, but I think the system only has 24 address bits...
   address &= 0x00FFFFFF;

   // RAM			($000000 - $3FFFFF)		4M
   if (address <= 0x3FFFFF)
      jaguarMainRAM[address] = byte;
   // hole			($400000 - $7FFFFF)		4M
   else if (address <= 0x7FFFFF)
      ;	// Do nothing
   // GAME ROM		($800000 - $DFFEFF)		6M - 256 bytes
   else if (address <= 0xDFFEFF)
      ;	// Do nothing
   // CDROM		($DFFF00 - $DFFFFF)		256 bytes
   else if (address <= 0xDFFFFF)
   {
      cdRAM[address & 0xFF] = byte;
#ifdef CDROM_LOG
      if ((address & 0xFF) < 12 * 4)
         WriteLog("[%s] ", BReg[(address & 0xFF) / 4]);
      WriteLog("CDROM: %s writing byte $%02X at $%08X [68K PC=$%08X]\n", whoName[who], data, offset, m68k_get_reg(NULL, M68K_REG_PC));
#endif
   }
   // BIOS ROM		($E00000 - $E3FFFF)		256K
   else if (address <= 0xE3FFFF)
      ;	// Do nothing
   // hole			($E40000 - $EFFFFF)		768K
   else if (address <= 0xEFFFFF)
      ;	// Do nothing
   // TOM			($F00000 - $F0FFFF)		64K
   else if (address <= 0xF0FFFF)
      //		;	// Do nothing
   {
      if (address == 0xF00050)
      {
         tomTimerPrescaler = (tomTimerPrescaler & 0x00FF) | ((uint16_t)byte << 8);
         TOMResetPIT();
         return;
      }
      else if (address == 0xF00051)
      {
         tomTimerPrescaler = (tomTimerPrescaler & 0xFF00) | byte;
         TOMResetPIT();
         return;
      }
      else if (address == 0xF00052)
      {
         tomTimerDivider = (tomTimerDivider & 0x00FF) | ((uint16_t)byte << 8);
         TOMResetPIT();
         return;
      }
      else if (address == 0xF00053)
      {
         tomTimerDivider = (tomTimerDivider & 0xFF00) | byte;
         TOMResetPIT();
         return;
      }
      else if (address >= 0xF00400 && address <= 0xF007FF)	// CLUT (A & B)
      {
         // Writing to one CLUT writes to the other
         address &= 0x5FF;		// Mask out $F00600 (restrict to $F00400-5FF)
         tomRAM[address] = tomRAM[address + 0x200] = byte;
         return;
      }
      //What about LBUF writes???
      else if ((address >= 0xF02100) && (address <= 0xF0211F))	// GPU CONTROL
      {
         GPUWriteByte(address, byte, who);
         return;
      }
      else if ((address >= 0xF02200) && (address <= 0xF0229F))	// BLITTER
      {
         BlitterWriteByte(address, byte, who);
         return;
      }
      else if ((address >= 0xF03000) && (address <= 0xF03FFF))	// GPU RAM
      {
         GPUWriteByte(address, byte, who);
         return;
      }

      tomRAM[address & 0x3FFF] = byte;
   }
   // JERRY		($F10000 - $F1FFFF)		64K
   else if (address <= 0xF1FFFF)
      //		;	// Do nothing
   {
#ifdef JERRY_DEBUG
      WriteLog("jerry: writing byte %.2x at 0x%.6x\n", byte, address);
#endif
      if ((address >= DSP_CONTROL_RAM_BASE) && (address < DSP_CONTROL_RAM_BASE+0x20))
      {
         DSPWriteByte(address, byte, who);
         return;
      }
      else if ((address >= DSP_WORK_RAM_BASE) && (address < DSP_WORK_RAM_BASE+0x2000))
      {
         DSPWriteByte(address, byte, who);
         return;
      }
      // SCLK ($F1A150--8 bits wide)
      //NOTE: This should be taken care of in DAC...
      else if ((address >= 0xF1A152) && (address <= 0xF1A153))
      {
         //		WriteLog("JERRY: Writing %02X to SCLK...\n", data);
         if ((address & 0x03) == 2)
            JERRYI2SInterruptDivide = (JERRYI2SInterruptDivide & 0x00FF) | ((uint32_t)byte << 8);
         else
            JERRYI2SInterruptDivide = (JERRYI2SInterruptDivide & 0xFF00) | (uint32_t)byte;

         JERRYI2SInterruptTimer = -1;
#ifndef NEW_TIMER_SYSTEM
         jerry_i2s_exec(0);
#else
         RemoveCallback(JERRYI2SCallback);
         JERRYI2SCallback();
#endif
         //			return;
      }
      // LTXD/RTXD/SCLK/SMODE $F1A148/4C/50/54 (really 16-bit registers...)
      else if (address >= 0xF1A148 && address <= 0xF1A157)
      {
         DACWriteByte(address, byte, who);
         return;
      }
      else if (address >= 0xF10000 && address <= 0xF10007)
      {
#ifndef NEW_TIMER_SYSTEM
         switch (address & 0x07)
         {
            case 0:
               JERRYPIT1Prescaler = (JERRYPIT1Prescaler & 0x00FF) | (byte << 8);
               JERRYResetPIT1();
               break;
            case 1:
               JERRYPIT1Prescaler = (JERRYPIT1Prescaler & 0xFF00) | byte;
               JERRYResetPIT1();
               break;
            case 2:
               JERRYPIT1Divider = (JERRYPIT1Divider & 0x00FF) | (byte << 8);
               JERRYResetPIT1();
               break;
            case 3:
               JERRYPIT1Divider = (JERRYPIT1Divider & 0xFF00) | byte;
               JERRYResetPIT1();
               break;
            case 4:
               JERRYPIT2Prescaler = (JERRYPIT2Prescaler & 0x00FF) | (byte << 8);
               JERRYResetPIT2();
               break;
            case 5:
               JERRYPIT2Prescaler = (JERRYPIT2Prescaler & 0xFF00) | byte;
               JERRYResetPIT2();
               break;
            case 6:
               JERRYPIT2Divider = (JERRYPIT2Divider & 0x00FF) | (byte << 8);
               JERRYResetPIT2();
               break;
            case 7:
               JERRYPIT2Divider = (JERRYPIT2Divider & 0xFF00) | byte;
               JERRYResetPIT2();
         }
#else
         WriteLog("JERRY: Unhandled timer write (BYTE) at %08X...\n", address);
#endif
         return;
      }
      /*	else if ((offset >= 0xF10010) && (offset <= 0xF10015))
         {
         clock_byte_write(offset, byte);
         return;
         }//*/
      // JERRY -> 68K interrupt enables/latches (need to be handled!)
      else if (address >= 0xF10020 && address <= 0xF10023)
      {
         WriteLog("JERRY: (68K int en/lat - Unhandled!) Tried to write $%02X to $%08X!\n", byte, address);
      }
      /*	else if ((offset >= 0xF17C00) && (offset <= 0xF17C01))
         {
         anajoy_byte_write(offset, byte);
         return;
         }*/
      else if ((address >= 0xF14000) && (address <= 0xF14003))
      {
         JoystickWriteByte(address, byte);
         EepromWriteByte(address, byte);
         return;
      }
      else if ((address >= 0xF14004) && (address <= 0xF1A0FF))
      {
         EepromWriteByte(address, byte);
         return;
      }
      //Need to protect write attempts to Wavetable ROM (F1D000-FFF)
      else if (address >= 0xF1D000 && address <= 0xF1DFFF)
         return;

      jerryRAM[address & 0xFFFF] = byte;
   }
   // hole			($F20000 - $FFFFFF)		1M - 128K
   else
      ;	// Do nothing
}


void WriteWord(uint32_t adddress, uint16_t word)
{
}


void WriteDWord(uint32_t adddress, uint32_t dword)
{
}


uint8_t ReadByte(uint32_t adddress)
{
}


uint16_t ReadWord(uint32_t adddress)
{
}


uint32_t ReadDWord(uint32_t adddress)
{
}
#endif


void ShowM68KContext(void)
{
   printf("\t68K PC=%06X\n", m68k_get_reg(NULL, M68K_REG_PC));

   for(int i=M68K_REG_D0; i<=M68K_REG_D7; i++)
   {
      printf("D%i = %08X ", i-M68K_REG_D0, m68k_get_reg(NULL, (m68k_register_t)i));

      if (i == M68K_REG_D3 || i == M68K_REG_D7)
         printf("\n");
   }

   for(int i=M68K_REG_A0; i<=M68K_REG_A7; i++)
   {
      printf("A%i = %08X ", i-M68K_REG_A0, m68k_get_reg(NULL, (m68k_register_t)i));

      if (i == M68K_REG_A3 || i == M68K_REG_A7)
         printf("\n");
   }

   uint32_t currpc = m68k_get_reg(NULL, M68K_REG_PC);
   uint32_t disPC = currpc - 30;
   char buffer[128];

   do
   {
      uint32_t oldpc = disPC;
      disPC += m68k_disassemble(buffer, disPC, 0);
      printf("%s%08X: %s\n", (oldpc == currpc ? ">" : " "), oldpc, buffer);
   }
   while (disPC < (currpc + 10));
}


/* Custom UAE 68000 read/write/IRQ functions */

int irq_ack_handler(int level)
{
#ifdef CPU_DEBUG_TRACING
   if (startM68KTracing)
   {
      WriteLog("irq_ack_handler: M68K PC=%06X\n", m68k_get_reg(NULL, M68K_REG_PC));
   }
#endif

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
#ifdef CPU_DEBUG_MEMORY
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFF))
   {
      if (startMemLog)
         readMem[address] = 1;
   }
#endif
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
      return (jaguarMainROM[address - 0x800000] << 8) | jaguarMainROM[address - 0x800000 + 1];
   else if ((address >= 0xE00000) && (address <= 0xE3FFFE))
      return (jagMemSpace[address] << 8) | jagMemSpace[address + 1];
   else if ((address >= 0xDFFF00) && (address <= 0xDFFFFE))
      return CDROMReadWord(address, M68K);
   else if ((address >= 0xF00000) && (address <= 0xF0FFFE))
      return TOMReadWord(address, M68K);
   else if ((address >= 0xF10000) && (address <= 0xF1FFFE))
      return JERRYReadWord(address, M68K);
   else
      return jaguar_unknown_readword(address, M68K);

   return 0;
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
#ifdef CPU_DEBUG_MEMORY
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFF))
   {
      if (startMemLog)
      {
         if (value > writeMemMax[address])
            writeMemMax[address] = value;
         if (value < writeMemMin[address])
            writeMemMin[address] = value;
      }
   }
#endif

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
#ifdef CPU_DEBUG_MEMORY
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFE))
   {
      if (startMemLog)
      {
         uint8_t hi = value >> 8, lo = value & 0xFF;

         if (hi > writeMemMax[address])
            writeMemMax[address] = hi;
         if (hi < writeMemMin[address])
            writeMemMin[address] = hi;

         if (lo > writeMemMax[address+1])
            writeMemMax[address+1] = lo;
         if (lo < writeMemMin[address+1])
            writeMemMin[address+1] = lo;
      }
   }
#endif

#ifndef USE_NEW_MMU
   // Note that the Jaguar only has 2M of RAM, not 4!
   if ((address >= 0x000000) && (address <= 0x1FFFFE))
   {
      SET16(jaguarMainRAM, address, value);
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
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
      WriteLog("\tA0=%08X, A1=%08X, D0=%08X, D1=%08X\n",
            m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
            m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1));
#endif
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

void JaguarDasm(uint32_t offset, uint32_t qt)
{
#ifdef CPU_DEBUG
   static char buffer[2048];//, mem[64];
   int pc = offset, oldpc;

   for(uint32_t i=0; i<qt; i++)
   {
      oldpc = pc;
      pc += m68k_disassemble(buffer, pc, 0);//M68K_CPU_TYPE_68000);
      WriteLog("%08X: %s\n", oldpc, buffer);//*/
   }
#endif
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
   // For randomizing RAM
   srand(time(NULL));

   // Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents
   for(uint32_t i=0; i<0x200000; i+=4)
      *((uint32_t *)(&jaguarMainRAM[i])) = rand();

#ifdef CPU_DEBUG_MEMORY
   memset(readMem, 0x00, 0x400000);
   memset(writeMemMin, 0xFF, 0x400000);
   memset(writeMemMax, 0x00, 0x400000);
#endif
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
   uint16_t vc = TOMReadWord(0xF00006, JAGUAR);
   uint16_t vp = TOMReadWord(0xF0003E, JAGUAR) + 1;
   uint16_t vi = TOMReadWord(0xF0004E, JAGUAR);
   vc++;

   // Each # of lines is for a full frame == 1/30s (NTSC), 1/25s (PAL).
   // So we cut the number of half-lines in a frame in half. :-P
   uint16_t numHalfLines = ((vjs.hardwareTypeNTSC ? 525 : 625) * 2) / 2;

   if ((vc & 0x7FF) >= numHalfLines)
   {
      lowerField = !lowerField;
      // If we're rendering the lower field, set the high bit (#11, counting
      // from 0) of VC
      vc = (lowerField ? 0x0800 : 0x0000);
   }

   //WriteLog("HLC: Currently on line %u (VP=%u)...\n", vc, vp);
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
   // Only problem with this approach: It wipes out RAM loaded files...!
   // Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents
   for(uint32_t i=8; i<0x200000; i+=4)
      *((uint32_t *)(&jaguarMainRAM[i])) = rand();

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

   //	WriteLog("jaguar_reset():\n");
   TOMReset();
   JERRYReset();
   GPUReset();
   DSPReset();
   CDROMReset();
   m68k_pulse_reset();								// Reset the 68000
   WriteLog("Jaguar: 68K reset. PC=%06X SP=%08X\n", m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_A7));

   lowerField = false;								// Reset the lower field flag
   SetCallbackTime(HalflineCallback, (vjs.hardwareTypeNTSC ? 31.777777777 : 32.0), EVENT_MAIN);
}


void JaguarDone(void)
{
   WriteLog("Jaguar: Interrupt enable = $%02X\n", TOMReadByte(0xF000E1, JAGUAR) & 0x1F);
   WriteLog("Jaguar: Video interrupt is %s (line=%u)\n", ((TOMIRQEnabled(IRQ_VIDEO))
            && (JaguarInterruptHandlerIsValid(64))) ? "enabled" : "disabled", TOMReadWord(0xF0004E, JAGUAR));
   M68K_show_context();

   CDROMDone();
   GPUDone();
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

      if (vjs.GPUEnabled)
         GPUExec(USEC_TO_RISC_CYCLES(timeToNextEvent));

      HandleNextEvent(EVENT_MAIN);
   } while(!frameDone);
}

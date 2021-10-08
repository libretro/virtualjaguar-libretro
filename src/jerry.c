//
// JERRY Core
//
// Originally by David Raingeard
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Carwin Jones (BeOS)
// Cleanups/rewrites/fixes by James Hammons
//
// JLH = James Hammons <jlhamm@acm.org>
//
// WHO  WHEN        WHAT
// ---  ----------  -----------------------------------------------------------
// JLH  11/25/2009  Major rewrite of memory subsystem and handlers
//

// ------------------------------------------------------------
// JERRY REGISTERS (Mapped by Aaron Giles)
// ------------------------------------------------------------
// F10000-F13FFF   R/W   xxxxxxxx xxxxxxxx   Jerry
// F10000            W   xxxxxxxx xxxxxxxx   JPIT1 - timer 1 pre-scaler
// F10002            W   xxxxxxxx xxxxxxxx   JPIT2 - timer 1 divider
// F10004            W   xxxxxxxx xxxxxxxx   JPIT3 - timer 2 pre-scaler
// F10008            W   xxxxxxxx xxxxxxxx   JPIT4 - timer 2 divider
// F10010            W   ------xx xxxxxxxx   CLK1 - processor clock divider
// F10012            W   ------xx xxxxxxxx   CLK2 - video clock divider
// F10014            W   -------- --xxxxxx   CLK3 - chroma clock divider
// F10020          R/W   ---xxxxx ---xxxxx   JINTCTRL - interrupt control register
//                   W   ---x---- --------      (J_SYNCLR - clear synchronous serial intf ints)
//                   W   ----x--- --------      (J_ASYNCLR - clear asynchronous serial intf ints)
//                   W   -----x-- --------      (J_TIM2CLR - clear timer 2 [tempo] interrupts)
//                   W   ------x- --------      (J_TIM1CLR - clear timer 1 [sample] interrupts)
//                   W   -------x --------      (J_EXTCLR - clear external interrupts)
//                 R/W   -------- ---x----      (J_SYNENA - enable synchronous serial intf ints)
//                 R/W   -------- ----x---      (J_ASYNENA - enable asynchronous serial intf ints)
//                 R/W   -------- -----x--      (J_TIM2ENA - enable timer 2 [tempo] interrupts)
//                 R/W   -------- ------x-      (J_TIM1ENA - enable timer 1 [sample] interrupts)
//                 R/W   -------- -------x      (J_EXTENA - enable external interrupts)
// F10030          R/W   -------- xxxxxxxx   ASIDATA - asynchronous serial data
// F10032            W   -x------ -xxxxxxx   ASICTRL - asynchronous serial control
//                   W   -x------ --------      (TXBRK - transmit break)
//                   W   -------- -x------      (CLRERR - clear error)
//                   W   -------- --x-----      (RINTEN - enable receiver interrupts)
//                   W   -------- ---x----      (TINTEN - enable transmitter interrupts)
//                   W   -------- ----x---      (RXIPOL - receiver input polarity)
//                   W   -------- -----x--      (TXOPOL - transmitter output polarity)
//                   W   -------- ------x-      (PAREN - parity enable)
//                   W   -------- -------x      (ODD - odd parity select)
// F10032          R     xxx-xxxx x-xxxxxx   ASISTAT - asynchronous serial status
//                 R     x------- --------      (ERROR - OR of PE,FE,OE)
//                 R     -x------ --------      (TXBRK - transmit break)
//                 R     --x----- --------      (SERIN - serial input)
//                 R     ----x--- --------      (OE - overrun error)
//                 R     -----x-- --------      (FE - framing error)
//                 R     ------x- --------      (PE - parity error)
//                 R     -------x --------      (TBE - transmit buffer empty)
//                 R     -------- x-------      (RBF - receive buffer full)
//                 R     -------- ---x----      (TINTEN - enable transmitter interrupts)
//                 R     -------- ----x---      (RXIPOL - receiver input polarity)
//                 R     -------- -----x--      (TXOPOL - transmitter output polarity)
//                 R     -------- ------x-      (PAREN - parity enable)
//                 R     -------- -------x      (ODD - odd parity)
// F10034          R/W   xxxxxxxx xxxxxxxx   ASICLK - asynchronous serial interface clock
// F10036          R     xxxxxxxx xxxxxxxx   JPIT1 - timer 1 pre-scaler
// F10038          R     xxxxxxxx xxxxxxxx   JPIT2 - timer 1 divider
// F1003A          R     xxxxxxxx xxxxxxxx   JPIT3 - timer 2 pre-scaler
// F1003C          R     xxxxxxxx xxxxxxxx   JPIT4 - timer 2 divider
// ------------------------------------------------------------
// F14000-F17FFF   R/W   xxxxxxxx xxxxxxxx   Joysticks and GPIO0-5
// F14000          R     xxxxxxxx xxxxxxxx   JOYSTICK - read joystick state
// F14000            W   x------- xxxxxxxx   JOYSTICK - latch joystick output
//                   W   x------- --------      (enable joystick outputs)
//                   W   -------- xxxxxxxx      (joystick output data)
// F14002          R     xxxxxxxx xxxxxxxx   JOYBUTS - button register
// F14800-F14FFF   R/W   xxxxxxxx xxxxxxxx   GPI00 - reserved (CD-ROM? no.)
// F15000-F15FFF   R/W   xxxxxxxx xxxxxxxx   GPI01 - reserved
// F16000-F16FFF   R/W   xxxxxxxx xxxxxxxx   GPI02 - reserved
// F17000-F177FF   R/W   xxxxxxxx xxxxxxxx   GPI03 - reserved
// F17800-F17BFF   R/W   xxxxxxxx xxxxxxxx   GPI04 - reserved
// F17C00-F17FFF   R/W   xxxxxxxx xxxxxxxx   GPI05 - reserved
// ------------------------------------------------------------
// F18000-F1FFFF   R/W   xxxxxxxx xxxxxxxx   Jerry DSP
// F1A100          R/W   xxxxxxxx xxxxxxxx   D_FLAGS - DSP flags register
//                 R/W   x------- --------      (DMAEN - DMA enable)
//                 R/W   -x------ --------      (REGPAGE - register page)
//                   W   --x----- --------      (D_EXT0CLR - clear external interrupt 0)
//                   W   ---x---- --------      (D_TIM2CLR - clear timer 2 interrupt)
//                   W   ----x--- --------      (D_TIM1CLR - clear timer 1 interrupt)
//                   W   -----x-- --------      (D_I2SCLR - clear I2S interrupt)
//                   W   ------x- --------      (D_CPUCLR - clear CPU interrupt)
//                 R/W   -------x --------      (D_EXT0ENA - enable external interrupt 0)
//                 R/W   -------- x-------      (D_TIM2ENA - enable timer 2 interrupt)
//                 R/W   -------- -x------      (D_TIM1ENA - enable timer 1 interrupt)
//                 R/W   -------- --x-----      (D_I2SENA - enable I2S interrupt)
//                 R/W   -------- ---x----      (D_CPUENA - enable CPU interrupt)
//                 R/W   -------- ----x---      (IMASK - interrupt mask)
//                 R/W   -------- -----x--      (NEGA_FLAG - ALU negative)
//                 R/W   -------- ------x-      (CARRY_FLAG - ALU carry)
//                 R/W   -------- -------x      (ZERO_FLAG - ALU zero)
// F1A102          R/W   -------- ------xx   D_FLAGS - upper DSP flags
//                 R/W   -------- ------x-      (D_EXT1ENA - enable external interrupt 1)
//                 R/W   -------- -------x      (D_EXT1CLR - clear external interrupt 1)
// F1A104            W   -------- ----xxxx   D_MTXC - matrix control register
//                   W   -------- ----x---      (MATCOL - column/row major)
//                   W   -------- -----xxx      (MATRIX3-15 - matrix width)
// F1A108            W   ----xxxx xxxxxx--   D_MTXA - matrix address register
// F1A10C            W   -------- -----x-x   D_END - data organization register
//                   W   -------- -----x--      (BIG_INST - big endian instruction fetch)
//                   W   -------- -------x      (BIG_IO - big endian I/O)
// F1A110          R/W   xxxxxxxx xxxxxxxx   D_PC - DSP program counter
// F1A114          R/W   xxxxxxxx xx-xxxxx   D_CTRL - DSP control/status register
//                 R     xxxx---- --------      (VERSION - DSP version code)
//                 R/W   ----x--- --------      (BUS_HOG - hog the bus!)
//                 R/W   -----x-- --------      (D_EXT0LAT - external interrupt 0 latch)
//                 R/W   ------x- --------      (D_TIM2LAT - timer 2 interrupt latch)
//                 R/W   -------x --------      (D_TIM1LAT - timer 1 interrupt latch)
//                 R/W   -------- x-------      (D_I2SLAT - I2S interrupt latch)
//                 R/W   -------- -x------      (D_CPULAT - CPU interrupt latch)
//                 R/W   -------- ---x----      (SINGLE_GO - single step one instruction)
//                 R/W   -------- ----x---      (SINGLE_STEP - single step mode)
//                 R/W   -------- -----x--      (FORCEINT0 - cause interrupt 0 on GPU)
//                 R/W   -------- ------x-      (CPUINT - send GPU interrupt to CPU)
//                 R/W   -------- -------x      (DSPGO - enable DSP execution)
// F1A116          R/W   -------- -------x   D_CTRL - upper DSP control/status register
//                 R/W   -------- -------x      (D_EXT1LAT - external interrupt 1 latch)
// F1A118-F1A11B     W   xxxxxxxx xxxxxxxx   D_MOD - modulo instruction mask
// F1A11C-F1A11F   R     xxxxxxxx xxxxxxxx   D_REMAIN - divide unit remainder
// F1A11C            W   -------- -------x   D_DIVCTRL - divide unit control
//                   W   -------- -------x      (DIV_OFFSET - 1=16.16 divide, 0=32-bit divide)
// F1A120-F1A123   R     xxxxxxxx xxxxxxxx   D_MACHI - multiply & accumulate high bits
// F1A148            W   xxxxxxxx xxxxxxxx   R_DAC - right transmit data
// F1A14C            W   xxxxxxxx xxxxxxxx   L_DAC - left transmit data
// F1A150            W   -------- xxxxxxxx   SCLK - serial clock frequency
// F1A150          R     -------- ------xx   SSTAT
//                 R     -------- ------x-      (left - no description)
//                 R     -------- -------x      (WS - word strobe status)
// F1A154            W   -------- --xxxx-x   SMODE - serial mode
//                   W   -------- --x-----      (EVERYWORD - interrupt on MSB of every word)
//                   W   -------- ---x----      (FALLING - interrupt on falling edge)
//                   W   -------- ----x---      (RISING - interrupt of rising edge)
//                   W   -------- -----x--      (WSEN - enable word strobes)
//                   W   -------- -------x      (INTERNAL - enables serial clock)
// ------------------------------------------------------------
// F1B000-F1CFFF   R/W   xxxxxxxx xxxxxxxx   Local DSP RAM
// ------------------------------------------------------------
// F1D000          R     xxxxxxxx xxxxxxxx   ROM_TRI - triangle wave
// F1D200          R     xxxxxxxx xxxxxxxx   ROM_SINE - full sine wave
// F1D400          R     xxxxxxxx xxxxxxxx   ROM_AMSINE - amplitude modulated sine wave
// F1D600          R     xxxxxxxx xxxxxxxx   ROM_12W - sine wave and second order harmonic
// F1D800          R     xxxxxxxx xxxxxxxx   ROM_CHIRP16 - chirp
// F1DA00          R     xxxxxxxx xxxxxxxx   ROM_NTRI - traingle wave with noise
// F1DC00          R     xxxxxxxx xxxxxxxx   ROM_DELTA - spike
// F1DE00          R     xxxxxxxx xxxxxxxx   ROM_NOISE - white noise
// ------------------------------------------------------------

#include "jerry.h"

#include <string.h>								// For memcpy
#include "cdrom.h"
#include "dac.h"
#include "dsp.h"
#include "eeprom.h"
#include "event.h"
#include "jaguar.h"
#include "joystick.h"
#include "m68000/m68kinterface.h"
#include "memtrack.h"
#include "settings.h"
#include "tom.h"
#include "wavetable.h"

//Note that 44100 Hz requires samples every 22.675737 usec.

uint8_t jerry_ram_8[0x10000];

uint8_t analog_x, analog_y;

static uint32_t JERRYPIT1Prescaler;
static uint32_t JERRYPIT1Divider;
static uint32_t JERRYPIT2Prescaler;
static uint32_t JERRYPIT2Divider;
static int32_t jerry_timer_1_counter;
static int32_t jerry_timer_2_counter;

int32_t JERRYI2SInterruptTimer = -1;
uint32_t jerryI2SCycles;
uint32_t jerryIntPending;

static uint16_t jerryInterruptMask = 0;
static uint16_t jerryPendingInterrupt = 0;

// Private function prototypes

void JERRYResetPIT1(void);
void JERRYResetPIT2(void);
void JERRYResetI2S(void);

void JERRYPIT1Callback(void);
void JERRYPIT2Callback(void);
void JERRYI2SCallback(void);


void JERRYResetI2S(void)
{
   *sclk = 8;
   JERRYI2SInterruptTimer = -1;
}


void JERRYResetPIT1(void)
{
   RemoveCallback(JERRYPIT1Callback);

   if (JERRYPIT1Prescaler | JERRYPIT1Divider)
   {
      double usecs = (float)(JERRYPIT1Prescaler + 1) * (float)(JERRYPIT1Divider + 1) * RISC_CYCLE_IN_USEC;
      SetCallbackTime(JERRYPIT1Callback, usecs, EVENT_JERRY);
   }
}


void JERRYResetPIT2(void)
{
   RemoveCallback(JERRYPIT2Callback);

   if (JERRYPIT1Prescaler | JERRYPIT1Divider)
   {
      double usecs = (float)(JERRYPIT2Prescaler + 1) * (float)(JERRYPIT2Divider + 1) * RISC_CYCLE_IN_USEC;
      SetCallbackTime(JERRYPIT2Callback, usecs, EVENT_JERRY);
   }
}


// This is the cause of the regressions in Cybermorph and Missile Command 3D...
// Solution: Probably have to check the DSP enable bit before sending these thru.
void JERRYPIT1Callback(void)
{
   if (TOMIRQEnabled(IRQ_DSP))
   {
      if (jerryInterruptMask & IRQ2_TIMER1)		// CPU Timer 1 IRQ
      {
         // Not sure, but I think we don't generate another IRQ if one's already going...
         // But this seems to work... :-/
         jerryPendingInterrupt |= IRQ2_TIMER1;
         m68k_set_irq(2);						// Generate 68K IPL 2
      }
   }

   DSPSetIRQLine(DSPIRQ_TIMER0, ASSERT_LINE);	// This does the 'IRQ enabled' checking...
   JERRYResetPIT1();
}


void JERRYPIT2Callback(void)
{
   if (TOMIRQEnabled(IRQ_DSP))
   {
      if (jerryInterruptMask & IRQ2_TIMER2)		// CPU Timer 2 IRQ
      {
         jerryPendingInterrupt |= IRQ2_TIMER2;
         m68k_set_irq(2);						// Generate 68K IPL 2
      }
   }

   DSPSetIRQLine(DSPIRQ_TIMER1, ASSERT_LINE);	// This does the 'IRQ enabled' checking...
   JERRYResetPIT2();
}


void JERRYI2SCallback(void)
{
   // We don't have to divide the RISC clock rate by this--the reason is a bit
   // convoluted. Will put explanation here later...
   // What's needed here is to find the ratio of the frequency to the number of clock cycles
   // in one second. For example, if the sample rate is 44100, we divide the clock rate by
   // this: 26590906 / 44100 = 602 cycles.
   // Which means, every 602 cycles that go by we have to generate an interrupt.
   jerryI2SCycles = 32 * (2 * (*sclk + 1));

   // If INTERNAL flag is set, then JERRY's SCLK is master
   if (*smode & SMODE_INTERNAL)
   {
      double usecs;

      // This does the 'IRQ enabled' checking...
      DSPSetIRQLine(DSPIRQ_SSI, ASSERT_LINE);
      //		double usecs = (float)jerryI2SCycles * RISC_CYCLE_IN_USEC;
      //this fix is almost enough to fix timings in tripper, but not quite enough...
      usecs = (float)jerryI2SCycles * (vjs.hardwareTypeNTSC ? RISC_CYCLE_IN_USEC : RISC_CYCLE_PAL_IN_USEC);
      SetCallbackTime(JERRYI2SCallback, usecs, EVENT_JERRY);
   }
   else
   {
      // JERRY is slave to external word clock

      //Note that 44100 Hz requires samples every 22.675737 usec.
      //When JERRY is slave to the word clock, we need to do interrupts either at 44.1K
      //sample rate or at a 88.2K sample rate (11.332... usec).

      if (ButchIsReadyToSend())//Not sure this is right spot to check...
      {
         //	return GetWordFromButchSSI(offset, who);
         SetSSIWordsXmittedFromButch();
         DSPSetIRQLine(DSPIRQ_SSI, ASSERT_LINE);
      }

      SetCallbackTime(JERRYI2SCallback, 22.675737, EVENT_JERRY);
   }
}


void JERRYInit(void)
{
   JoystickInit();
   MTInit();
   memcpy(&jerry_ram_8[0xD000], waveTableROM, 0x1000);

   JERRYPIT1Prescaler = 0xFFFF;
   JERRYPIT2Prescaler = 0xFFFF;
   JERRYPIT1Divider = 0xFFFF;
   JERRYPIT2Divider = 0xFFFF;
   jerryInterruptMask = 0x0000;
   jerryPendingInterrupt = 0x0000;

   DACInit();
}


void JERRYReset(void)
{
   JoystickReset();
   EepromReset();
   MTReset();
   JERRYResetI2S();

   memset(jerry_ram_8, 0x00, 0xD000);		// Don't clear out the Wavetable ROM...!
   JERRYPIT1Prescaler = 0xFFFF;
   JERRYPIT2Prescaler = 0xFFFF;
   JERRYPIT1Divider = 0xFFFF;
   JERRYPIT2Divider = 0xFFFF;
   jerry_timer_1_counter = 0;
   jerry_timer_2_counter = 0;
   jerryInterruptMask = 0x0000;
   jerryPendingInterrupt = 0x0000;

   DACReset();
}


void JERRYDone(void)
{
   JoystickDone();
   DACDone();
   EepromDone();
   MTDone();
}


bool JERRYIRQEnabled(int irq)
{
   // Read the word @ $F10020
   return jerryInterruptMask & irq;
}


void JERRYSetPendingIRQ(int irq)
{
   // This is the shadow of INT (it's a split RO/WO register)
   jerryPendingInterrupt |= irq;
}


//
// JERRY byte access (read)
//
uint8_t JERRYReadByte(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
      return DSPReadByte(offset, who);
   else if ((offset >= DSP_WORK_RAM_BASE) && (offset < DSP_WORK_RAM_BASE+0x2000))
      return DSPReadByte(offset, who);
   // LRXD/RRXD/SSTAT $F1A148/4C/50 (really 16-bit registers...)
   else if (offset >= 0xF1A148 && offset <= 0xF1A153)
      return DACReadByte(offset, who);
   //	F10036          R     xxxxxxxx xxxxxxxx   JPIT1 - timer 1 pre-scaler
   //	F10038          R     xxxxxxxx xxxxxxxx   JPIT2 - timer 1 divider
   //	F1003A          R     xxxxxxxx xxxxxxxx   JPIT3 - timer 2 pre-scaler
   //	F1003C          R     xxxxxxxx xxxxxxxx   JPIT4 - timer 2 divider
   else if ((offset >= 0xF10036) && (offset <= 0xF1003D))
   {
      /* Unhandled timer read (BYTE) */
   }
   else if (offset >= 0xF14000 && offset <= 0xF14003)
   {
      uint16_t value = JoystickReadWord(offset & 0xFE);

      if (offset & 0x01)
         value &= 0xFF;
      else
         value >>= 8;

      // This is wrong, should only have the lowest bit from $F14001
      return value | EepromReadByte(offset);
   }
   else if (offset >= 0xF14000 && offset <= 0xF1A0FF)
      return EepromReadByte(offset);

   return jerry_ram_8[offset & 0xFFFF];
}


//
// JERRY word access (read)
//
uint16_t JERRYReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{

   if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
      return DSPReadWord(offset, who);
   else if (offset >= DSP_WORK_RAM_BASE && offset <= DSP_WORK_RAM_BASE + 0x1FFF)
      return DSPReadWord(offset, who);
   // LRXD/RRXD/SSTAT $F1A148/4C/50 (really 16-bit registers...)
   else if (offset >= 0xF1A148 && offset <= 0xF1A153)
      return DACReadWord(offset, who);
   //	F10036          R     xxxxxxxx xxxxxxxx   JPIT1 - timer 1 pre-scaler
   //	F10038          R     xxxxxxxx xxxxxxxx   JPIT2 - timer 1 divider
   //	F1003A          R     xxxxxxxx xxxxxxxx   JPIT3 - timer 2 pre-scaler
   //	F1003C          R     xxxxxxxx xxxxxxxx   JPIT4 - timer 2 divider
   else if ((offset >= 0xF10036) && (offset <= 0xF1003D))
   {
      /* Unhandled timer read (WORD) */
   }
   else if (offset == 0xF10020)
      return jerryPendingInterrupt;
   else if (offset == 0xF14000)
      return (JoystickReadWord(offset) & 0xFFFE) | EepromReadWord(offset);
   else if ((offset >= 0xF14002) && (offset < 0xF14003))
      return JoystickReadWord(offset);
   else if ((offset >= 0xF14000) && (offset <= 0xF1A0FF))
      return EepromReadWord(offset);

   offset &= 0xFFFF;				// Prevent crashing...!
   return ((uint16_t)jerry_ram_8[offset+0] << 8) | jerry_ram_8[offset+1];
}


//
// JERRY byte access (write)
//
void JERRYWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
   if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
   {
      DSPWriteByte(offset, data, who);
      return;
   }
   else if ((offset >= DSP_WORK_RAM_BASE) && (offset < DSP_WORK_RAM_BASE+0x2000))
   {
      DSPWriteByte(offset, data, who);
      return;
   }
   // SCLK ($F1A150--8 bits wide)
   //NOTE: This should be taken care of in DAC...
   // LTXD/RTXD/SCLK/SMODE $F1A148/4C/50/54 (really 16-bit registers...)
   else if (offset >= 0xF1A148 && offset <= 0xF1A157)
   {
      DACWriteByte(offset, data, who);
      return;
   }
   else if (offset >= 0xF10000 && offset <= 0xF10007)
   {
      /* Unhandled timer write (BYTE) */
      return;
   }
   // JERRY -> 68K interrupt enables/latches (need to be handled!)
   else if (offset >= 0xF10020 && offset <= 0xF10021)//WAS:23)
   {
      if (offset == 0xF10020)
      {
         // Clear pending interrupts...
         jerryPendingInterrupt &= ~data;
      }
      else if (offset == 0xF10021)
         jerryInterruptMask = data;
   }
   else if ((offset >= 0xF14000) && (offset <= 0xF14003))
   {
      JoystickWriteWord(offset & 0xFE, (uint16_t)data);
      EepromWriteByte(offset, data);
      return;
   }
   else if ((offset >= 0xF14000) && (offset <= 0xF1A0FF))
   {
      EepromWriteByte(offset, data);
      return;
   }

   //Need to protect write attempts to Wavetable ROM (F1D000-FFF)
   if (offset >= 0xF1D000 && offset <= 0xF1DFFF)
      return;

   jerry_ram_8[offset & 0xFFFF] = data;
}


//
// JERRY word access (write)
//
void JERRYWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
   if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
   {
      DSPWriteWord(offset, data, who);
      return;
   }
   else if ((offset >= DSP_WORK_RAM_BASE) && (offset < DSP_WORK_RAM_BASE+0x2000))
   {
      DSPWriteWord(offset, data, who);
      return;
   }
   //NOTE: This should be taken care of in DAC...
   // LTXD/RTXD/SCLK/SMODE $F1A148/4C/50/54 (really 16-bit registers...)
   else if (offset >= 0xF1A148 && offset <= 0xF1A156)
   {
      DACWriteWord(offset, data, who);
      return;
   }
   else if (offset >= 0xF10000 && offset <= 0xF10007)
   {
      switch(offset & 0x07)
      {
         case 0:
            JERRYPIT1Prescaler = data;
            JERRYResetPIT1();
            break;
         case 2:
            JERRYPIT1Divider = data;
            JERRYResetPIT1();
            break;
         case 4:
            JERRYPIT2Prescaler = data;
            JERRYResetPIT2();
            break;
         case 6:
            JERRYPIT2Divider = data;
            JERRYResetPIT2();
      }
      // Need to handle (unaligned) cases???

      return;
   }
   // JERRY -> 68K interrupt enables/latches (need to be handled!)
   else if (offset >= 0xF10020 && offset <= 0xF10022)
   {
      jerryInterruptMask = data & 0xFF;
      jerryPendingInterrupt &= ~(data >> 8);
      return;
   }
   else if (offset >= 0xF14000 && offset < 0xF14003)
   {
      JoystickWriteWord(offset, data);
      EepromWriteWord(offset, data);
      return;
   }
   else if (offset >= 0xF14000 && offset <= 0xF1A0FF)
   {
      EepromWriteWord(offset, data);
      return;
   }

   //Need to protect write attempts to Wavetable ROM (F1D000-FFF)
   if (offset >= 0xF1D000 && offset <= 0xF1DFFF)
      return;

   jerry_ram_8[(offset+0) & 0xFFFF] = (data >> 8) & 0xFF;
   jerry_ram_8[(offset+1) & 0xFFFF] = data & 0xFF;
}


int JERRYGetPIT1Frequency(void)
{
   int systemClockFrequency = (vjs.hardwareTypeNTSC ? RISC_CLOCK_RATE_NTSC : RISC_CLOCK_RATE_PAL);
   return systemClockFrequency / ((JERRYPIT1Prescaler + 1) * (JERRYPIT1Divider + 1));
}


int JERRYGetPIT2Frequency(void)
{
   int systemClockFrequency = (vjs.hardwareTypeNTSC ? RISC_CLOCK_RATE_NTSC : RISC_CLOCK_RATE_PAL);
   return systemClockFrequency / ((JERRYPIT2Prescaler + 1) * (JERRYPIT2Divider + 1));
}


//
// DSP core
//
// Originally by David Raingeard
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Extensive cleanups/rewrites by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
// JLH  11/26/2011  Added fixes for LOAD/STORE alignment issues
//

#include "dsp.h"

#include <stdlib.h>
#include "dac.h"
#include "gpu.h"
#include "jaguar.h"
#include "jerry.h"
#include "m68000/m68kinterface.h"

// Seems alignment in loads & stores was off...
#define DSP_CORRECT_ALIGNMENT
//#define DSP_CORRECT_ALIGNMENT_STORE

#define NEW_SCOREBOARD

// Pipeline structures

const bool affectsScoreboard[64] =
{
	 true,  true,  true,  true,
	 true,  true,  true,  true,
	 true,  true,  true,  true,
	 true, false,  true,  true,

	 true,  true, false,  true,
	false,  true,  true,  true,
	 true,  true,  true,  true,
	 true,  true, false, false,

	 true,  true,  true,  true,
	false,  true,  true,  true,
	 true,  true,  true,  true,
	 true, false, false, false,

	 true, false, false,  true,
	false, false,  true,  true,
	 true, false,  true,  true,
	false, false, false,  true
};

struct PipelineStage
{
	uint16_t instruction;
	uint8_t opcode, operand1, operand2;
	uint32_t reg1, reg2, areg1, areg2;
	uint32_t result;
	uint8_t writebackRegister;
	// General memory store...
	uint32_t address;
	uint32_t value;
	uint8_t type;
};

#define TYPE_BYTE			0
#define TYPE_WORD			1
#define TYPE_DWORD			2
#define PIPELINE_STALL		64						// Set to # of opcodes + 1
#ifndef NEW_SCOREBOARD
bool scoreboard[32];
#else
uint8_t scoreboard[32];
#endif
uint8_t plPtrFetch, plPtrRead, plPtrExec, plPtrWrite;
struct PipelineStage pipeline[4];
bool IMASKCleared = false;

// DSP flags (old--have to get rid of this crap)

#define CINT0FLAG			0x00200
#define CINT1FLAG			0x00400
#define CINT2FLAG			0x00800
#define CINT3FLAG			0x01000
#define CINT4FLAG			0x02000
#define CINT04FLAGS			(CINT0FLAG | CINT1FLAG | CINT2FLAG | CINT3FLAG | CINT4FLAG)
#define CINT5FLAG			0x20000		/* DSP only */

// DSP_FLAGS bits

#define ZERO_FLAG		0x00001
#define CARRY_FLAG		0x00002
#define NEGA_FLAG		0x00004
#define IMASK			0x00008
#define INT_ENA0		0x00010
#define INT_ENA1		0x00020
#define INT_ENA2		0x00040
#define INT_ENA3		0x00080
#define INT_ENA4		0x00100
#define INT_CLR0		0x00200
#define INT_CLR1		0x00400
#define INT_CLR2		0x00800
#define INT_CLR3		0x01000
#define INT_CLR4		0x02000
#define REGPAGE			0x04000
#define DMAEN			0x08000
#define INT_ENA5		0x10000
#define INT_CLR5		0x20000

// DSP_CTRL bits

#define DSPGO			0x00001
#define CPUINT			0x00002
#define DSPINT0			0x00004
#define SINGLE_STEP		0x00008
#define SINGLE_GO		0x00010
// Bit 5 is unused!
#define INT_LAT0		0x00040
#define INT_LAT1		0x00080
#define INT_LAT2		0x00100
#define INT_LAT3		0x00200
#define INT_LAT4		0x00400
#define BUS_HOG			0x00800
#define VERSION			0x0F000
#define INT_LAT5		0x10000

// Is opcode 62 *really* a NOP? Seems like it...
INLINE static void dsp_opcode_abs(void);
INLINE static void dsp_opcode_add(void);
INLINE static void dsp_opcode_addc(void);
INLINE static void dsp_opcode_addq(void);
INLINE static void dsp_opcode_addqmod(void);
INLINE static void dsp_opcode_addqt(void);
INLINE static void dsp_opcode_and(void);
INLINE static void dsp_opcode_bclr(void);
INLINE static void dsp_opcode_bset(void);
INLINE static void dsp_opcode_btst(void);
INLINE static void dsp_opcode_cmp(void);
INLINE static void dsp_opcode_cmpq(void);
INLINE static void dsp_opcode_div(void);
INLINE static void dsp_opcode_imacn(void);
INLINE static void dsp_opcode_imult(void);
INLINE static void dsp_opcode_imultn(void);
INLINE static void dsp_opcode_jr(void);
INLINE static void dsp_opcode_jump(void);
INLINE static void dsp_opcode_load(void);
INLINE static void dsp_opcode_loadb(void);
INLINE static void dsp_opcode_loadw(void);
INLINE static void dsp_opcode_load_r14_indexed(void);
INLINE static void dsp_opcode_load_r14_ri(void);
INLINE static void dsp_opcode_load_r15_indexed(void);
INLINE static void dsp_opcode_load_r15_ri(void);
INLINE static void dsp_opcode_mirror(void);
INLINE static void dsp_opcode_mmult(void);
INLINE static void dsp_opcode_move(void);
INLINE static void dsp_opcode_movei(void);
INLINE static void dsp_opcode_movefa(void);
INLINE static void dsp_opcode_move_pc(void);
INLINE static void dsp_opcode_moveq(void);
INLINE static void dsp_opcode_moveta(void);
INLINE static void dsp_opcode_mtoi(void);
INLINE static void dsp_opcode_mult(void);
INLINE static void dsp_opcode_neg(void);
INLINE static void dsp_opcode_nop(void);
INLINE static void dsp_opcode_normi(void);
INLINE static void dsp_opcode_not(void);
INLINE static void dsp_opcode_or(void);
INLINE static void dsp_opcode_resmac(void);
INLINE static void dsp_opcode_ror(void);
INLINE static void dsp_opcode_rorq(void);
INLINE static void dsp_opcode_xor(void);
INLINE static void dsp_opcode_sat16s(void);
INLINE static void dsp_opcode_sat32s(void);
INLINE static void dsp_opcode_sh(void);
INLINE static void dsp_opcode_sha(void);
INLINE static void dsp_opcode_sharq(void);
INLINE static void dsp_opcode_shlq(void);
INLINE static void dsp_opcode_shrq(void);
INLINE static void dsp_opcode_store(void);
INLINE static void dsp_opcode_storeb(void);
INLINE static void dsp_opcode_storew(void);
INLINE static void dsp_opcode_store_r14_indexed(void);
INLINE static void dsp_opcode_store_r14_ri(void);
INLINE static void dsp_opcode_store_r15_indexed(void);
INLINE static void dsp_opcode_store_r15_ri(void);
INLINE static void dsp_opcode_sub(void);
INLINE static void dsp_opcode_subc(void);
INLINE static void dsp_opcode_subq(void);
INLINE static void dsp_opcode_subqmod(void);
INLINE static void dsp_opcode_subqt(void);
INLINE static void dsp_opcode_illegal(void);

//Here's a QnD kludge...
//This is wrong, wrong, WRONG, but it seems to work for the time being...
//(That is, it fixes Flip Out which relies on GPU timing rather than semaphores. Bad developers! Bad!)
//What's needed here is a way to take pipeline effects into account (including pipeline stalls!)...
// Yup, without cheating like this, the sound in things like Rayman, FACTS, &
// Tripper Getem get starved for time and sounds like crap. So we have to figure
// out how to fix that. :-/
uint8_t dsp_opcode_cycles[64] =
{
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  9,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  2,
	2,  2,  2,  3,  3,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  4,  1,
	1,  1,  3,  3,  1,  1,  1,  1
};//*/

void (* dsp_opcode[64])() =
{
	dsp_opcode_add,					dsp_opcode_addc,				dsp_opcode_addq,				dsp_opcode_addqt,
	dsp_opcode_sub,					dsp_opcode_subc,				dsp_opcode_subq,				dsp_opcode_subqt,
	dsp_opcode_neg,					dsp_opcode_and,					dsp_opcode_or,					dsp_opcode_xor,
	dsp_opcode_not,					dsp_opcode_btst,				dsp_opcode_bset,				dsp_opcode_bclr,
	dsp_opcode_mult,				dsp_opcode_imult,				dsp_opcode_imultn,				dsp_opcode_resmac,
	dsp_opcode_imacn,				dsp_opcode_div,					dsp_opcode_abs,					dsp_opcode_sh,
	dsp_opcode_shlq,				dsp_opcode_shrq,				dsp_opcode_sha,					dsp_opcode_sharq,
	dsp_opcode_ror,					dsp_opcode_rorq,				dsp_opcode_cmp,					dsp_opcode_cmpq,
	dsp_opcode_subqmod,				dsp_opcode_sat16s,				dsp_opcode_move,				dsp_opcode_moveq,
	dsp_opcode_moveta,				dsp_opcode_movefa,				dsp_opcode_movei,				dsp_opcode_loadb,
	dsp_opcode_loadw,				dsp_opcode_load,				dsp_opcode_sat32s,				dsp_opcode_load_r14_indexed,
	dsp_opcode_load_r15_indexed,	dsp_opcode_storeb,				dsp_opcode_storew,				dsp_opcode_store,
	dsp_opcode_mirror,				dsp_opcode_store_r14_indexed,	dsp_opcode_store_r15_indexed,	dsp_opcode_move_pc,
	dsp_opcode_jump,				dsp_opcode_jr,					dsp_opcode_mmult,				dsp_opcode_mtoi,
	dsp_opcode_normi,				dsp_opcode_nop,					dsp_opcode_load_r14_ri,			dsp_opcode_load_r15_ri,
	dsp_opcode_store_r14_ri,		dsp_opcode_store_r15_ri,		dsp_opcode_illegal,				dsp_opcode_addqmod,
};

uint32_t dsp_opcode_use[65];

const char * dsp_opcode_str[65]=
{
	"add",				"addc",				"addq",				"addqt",
	"sub",				"subc",				"subq",				"subqt",
	"neg",				"and",				"or",				"xor",
	"not",				"btst",				"bset",				"bclr",
	"mult",				"imult",			"imultn",			"resmac",
	"imacn",			"div",				"abs",				"sh",
	"shlq",				"shrq",				"sha",				"sharq",
	"ror",				"rorq",				"cmp",				"cmpq",
	"subqmod",			"sat16s",			"move",				"moveq",
	"moveta",			"movefa",			"movei",			"loadb",
	"loadw",			"load",				"sat32s",			"load_r14_indexed",
	"load_r15_indexed",	"storeb",			"storew",			"store",
	"mirror",			"store_r14_indexed","store_r15_indexed","move_pc",
	"jump",				"jr",				"mmult",			"mtoi",
	"normi",			"nop",				"load_r14_ri",		"load_r15_ri",
	"store_r14_ri",		"store_r15_ri",		"illegal",			"addqmod",
	"STALL"
};

uint32_t dsp_pc;
static uint64_t dsp_acc;								// 40 bit register, NOT 32!
static uint32_t dsp_remain;
static uint32_t dsp_modulo;
static uint32_t dsp_flags;
static uint32_t dsp_matrix_control;
static uint32_t dsp_pointer_to_matrix;
static uint32_t dsp_data_organization;
uint32_t dsp_control;
static uint32_t dsp_div_control;
static uint8_t dsp_flag_z, dsp_flag_n, dsp_flag_c;
static uint32_t * dsp_reg = NULL, * dsp_alternate_reg = NULL;
uint32_t dsp_reg_bank_0[32], dsp_reg_bank_1[32];

static uint32_t dsp_opcode_first_parameter;
static uint32_t dsp_opcode_second_parameter;

#define DSP_RUNNING			(dsp_control & 0x01)

#define RM					dsp_reg[dsp_opcode_first_parameter]
#define RN					dsp_reg[dsp_opcode_second_parameter]
#define ALTERNATE_RM		dsp_alternate_reg[dsp_opcode_first_parameter]
#define ALTERNATE_RN		dsp_alternate_reg[dsp_opcode_second_parameter]
#define IMM_1				dsp_opcode_first_parameter
#define IMM_2				dsp_opcode_second_parameter

#define CLR_Z				(dsp_flag_z = 0)
#define CLR_ZN				(dsp_flag_z = dsp_flag_n = 0)
#define CLR_ZNC				(dsp_flag_z = dsp_flag_n = dsp_flag_c = 0)
#define SET_Z(r)			(dsp_flag_z = ((r) == 0))
#define SET_N(r)			(dsp_flag_n = (((uint32_t)(r) >> 31) & 0x01))
#define SET_C_ADD(a,b)		(dsp_flag_c = ((uint32_t)(b) > (uint32_t)(~(a))))
#define SET_C_SUB(a,b)		(dsp_flag_c = ((uint32_t)(b) > (uint32_t)(a)))
#define SET_ZN(r)			SET_N(r); SET_Z(r)
#define SET_ZNC_ADD(a,b,r)	SET_N(r); SET_Z(r); SET_C_ADD(a,b)
#define SET_ZNC_SUB(a,b,r)	SET_N(r); SET_Z(r); SET_C_SUB(a,b)

uint32_t dsp_convert_zero[32] = {
	32, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

uint8_t dsp_branch_condition_table[32 * 8];
static uint16_t mirror_table[65536];
static uint8_t dsp_ram_8[0x2000];

#define BRANCH_CONDITION(x)		dsp_branch_condition_table[(x) + ((jaguar_flags & 7) << 5)]

static uint32_t dsp_in_exec = 0;
static uint32_t dsp_releaseTimeSlice_flag = 0;

// Private function prototypes

void FlushDSPPipeline(void);


void dsp_reset_stats(void)
{
   unsigned i;
	for(i=0; i<65; i++)
		dsp_opcode_use[i] = 0;
}

void DSPReleaseTimeslice(void)
{
	dsp_releaseTimeSlice_flag = 1;
}

void dsp_build_branch_condition_table(void)
{
   unsigned i, j;

	/* Fill in the mirror table */

	for(i=0; i<65536; i++)
	{
		mirror_table[i] = ((i >> 15) & 0x0001) | ((i >> 13) & 0x0002)
			| ((i >> 11) & 0x0004) | ((i >> 9)  & 0x0008)
			| ((i >> 7)  & 0x0010) | ((i >> 5)  & 0x0020)
			| ((i >> 3)  & 0x0040) | ((i >> 1)  & 0x0080)
			| ((i << 1)  & 0x0100) | ((i << 3)  & 0x0200)
			| ((i << 5)  & 0x0400) | ((i << 7)  & 0x0800)
			| ((i << 9)  & 0x1000) | ((i << 11) & 0x2000)
			| ((i << 13) & 0x4000) | ((i << 15) & 0x8000);
	}

	// Fill in the condition table
	for(i=0; i<8; i++)
	{
		for(j=0; j<32; j++)
		{
			int result = 1;

			if ((j & 1) && (i & ZERO_FLAG))
				result = 0;

			if ((j & 2) && (!(i & ZERO_FLAG)))
				result = 0;

			if ((j & 4) && (i & (CARRY_FLAG << (j >> 4))))
				result = 0;

			if ((j & 8) && (!(i & (CARRY_FLAG << (j >> 4)))))
				result = 0;

			dsp_branch_condition_table[i * 32 + j] = result;
		}
	}
}

uint8_t DSPReadByte(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	if (offset >= DSP_WORK_RAM_BASE && offset <= (DSP_WORK_RAM_BASE + 0x1FFF))
		return dsp_ram_8[offset - DSP_WORK_RAM_BASE];

	if (offset >= DSP_CONTROL_RAM_BASE && offset <= (DSP_CONTROL_RAM_BASE + 0x1F))
	{
		uint32_t data = DSPReadLong(offset & 0xFFFFFFFC, who);

		if ((offset&0x03)==0)
			return(data>>24);
		else
		if ((offset&0x03)==1)
			return((data>>16)&0xff);
		else
		if ((offset&0x03)==2)
			return((data>>8)&0xff);
		else
		if ((offset&0x03)==3)
			return(data&0xff);
	}

	return JaguarReadByte(offset, who);
}

uint16_t DSPReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	offset &= 0xFFFFFFFE;

	if (offset >= DSP_WORK_RAM_BASE && offset <= DSP_WORK_RAM_BASE+0x1FFF)
	{
		offset -= DSP_WORK_RAM_BASE;
		return GET16(dsp_ram_8, offset);
	}
	else if ((offset>=DSP_CONTROL_RAM_BASE)&&(offset<DSP_CONTROL_RAM_BASE+0x20))
	{
		uint32_t data = DSPReadLong(offset & 0xFFFFFFFC, who);

		if (offset & 0x03)
			return data & 0xFFFF;
      return data >> 16;
	}

	return JaguarReadWord(offset, who);
}

uint32_t DSPReadLong(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFFFFFFFC;

   if (offset >= DSP_WORK_RAM_BASE && offset <= DSP_WORK_RAM_BASE + 0x1FFF)
   {
      offset -= DSP_WORK_RAM_BASE;
      return GET32(dsp_ram_8, offset);
   }
   if (offset >= DSP_CONTROL_RAM_BASE && offset <= DSP_CONTROL_RAM_BASE + 0x23)
   {
      offset &= 0x3F;
      switch (offset)
      {
         case 0x00:
            dsp_flags = (dsp_flags & 0xFFFFFFF8) | (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;
            return dsp_flags & 0xFFFFC1FF;
         case 0x04:
            return dsp_matrix_control;
         case 0x08:
            return dsp_pointer_to_matrix;
         case 0x0C:
            return dsp_data_organization;
         case 0x10:
            return dsp_pc;
         case 0x14:
            return dsp_control;
         case 0x18:
            return dsp_modulo;
         case 0x1C:
            return dsp_remain;
         case 0x20:
            return (int32_t)((int8_t)(dsp_acc >> 32));	// Top 8 bits of 40-bit accumulator, sign extended
      }
      // unaligned long read-- !!! FIX !!!
      return 0xFFFFFFFF;
   }

   return JaguarReadLong(offset, who);
}

void DSPWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
   if ((offset >= DSP_WORK_RAM_BASE) && (offset < DSP_WORK_RAM_BASE+0x2000))
   {
      offset -= DSP_WORK_RAM_BASE;
      dsp_ram_8[offset] = data;
      return;
   }
   if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
   {
      uint32_t reg = offset & 0x1C;
      int bytenum = offset & 0x03;

      if ((reg >= 0x1C) && (reg <= 0x1F))
         dsp_div_control = (dsp_div_control & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));
      else
      {
         //This looks funky. !!! FIX !!!
         uint32_t old_data = DSPReadLong(offset&0xFFFFFFC, who);
         bytenum = 3 - bytenum; // convention motorola !!!
         old_data = (old_data & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));
         DSPWriteLong(offset & 0xFFFFFFC, old_data, who);
      }
      return;
   }

   JaguarWriteByte(offset, data, who);
}

void DSPWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFFFFFFFE;

   if ((offset >= DSP_WORK_RAM_BASE) && (offset < DSP_WORK_RAM_BASE+0x2000))
   {
      offset -= DSP_WORK_RAM_BASE;
      dsp_ram_8[offset] = data >> 8;
      dsp_ram_8[offset+1] = data & 0xFF;
      //CC only!
      return;
   }
   else if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
   {
      if ((offset & 0x1C) == 0x1C)
      {
         if (offset & 0x03)
            dsp_div_control = (dsp_div_control & 0xFFFF0000) | (data & 0xFFFF);
         else
            dsp_div_control = (dsp_div_control & 0xFFFF) | ((data & 0xFFFF) << 16);
      }
      else
      {
         uint32_t old_data = DSPReadLong(offset & 0xFFFFFFC, who);

         if (offset & 0x03)
            old_data = (old_data & 0xFFFF0000) | (data & 0xFFFF);
         else
            old_data = (old_data & 0xFFFF) | ((data & 0xFFFF) << 16);

         DSPWriteLong(offset & 0xFFFFFFC, old_data, who);
      }

      return;
   }

   JaguarWriteWord(offset, data, who);
}

void DSPWriteLong(uint32_t offset, uint32_t data, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFFFFFFFC;

   if (offset >= DSP_WORK_RAM_BASE && offset <= DSP_WORK_RAM_BASE + 0x1FFF)
   {
      offset -= DSP_WORK_RAM_BASE;
      SET32(dsp_ram_8, offset, data);
      //CC only!
      return;
   }
   else if (offset >= DSP_CONTROL_RAM_BASE && offset <= (DSP_CONTROL_RAM_BASE + 0x1F))
   {
      offset &= 0x1F;
      switch (offset)
      {
         case 0x00:
            {
               IMASKCleared = (dsp_flags & IMASK) && !(data & IMASK);
               // NOTE: According to the JTRM, writing a 1 to IMASK has no effect; only the
               //       IRQ logic can set it. So we mask it out here to prevent problems...
               dsp_flags = data & (~IMASK);
               dsp_flag_z = dsp_flags & 0x01;
               dsp_flag_c = (dsp_flags >> 1) & 0x01;
               dsp_flag_n = (dsp_flags >> 2) & 0x01;
               DSPUpdateRegisterBanks();
               dsp_control &= ~((dsp_flags & CINT04FLAGS) >> 3);
               dsp_control &= ~((dsp_flags & CINT5FLAG) >> 1);
               break;
            }
         case 0x04:
            dsp_matrix_control = data;
            break;
         case 0x08:
            // According to JTRM, only lines 2-11 are addressable, the rest being
            // hardwired to $F1Bxxx.
            dsp_pointer_to_matrix = 0xF1B000 | (data & 0x000FFC);
            break;
         case 0x0C:
            dsp_data_organization = data;
            break;
         case 0x10:
            dsp_pc = data;
            //CC only!
            //!!!!!!!!
            break;
         case 0x14:
            {
               uint32_t mask;
               bool wasRunning = DSP_RUNNING;
               // Check for DSP -> CPU interrupt
               if (data & CPUINT)
               {
                  if (JERRYIRQEnabled(IRQ2_DSP))
                  {
                     JERRYSetPendingIRQ(IRQ2_DSP);
                     DSPReleaseTimeslice();
                     m68k_set_irq(2);			// Set 68000 IPL 2...
                  }
                  data &= ~CPUINT;
               }
               // Check for CPU -> DSP interrupt
               if (data & DSPINT0)
               {
                  m68k_end_timeslice();
                  DSPReleaseTimeslice();
                  DSPSetIRQLine(DSPIRQ_CPU, ASSERT_LINE);
                  data &= ~DSPINT0;
               }
               // Protect writes to VERSION and the interrupt latches...
               mask        = VERSION | INT_LAT0 | INT_LAT1 | INT_LAT2 | INT_LAT3 | INT_LAT4 | INT_LAT5;
               dsp_control = (dsp_control & mask) | (data & ~mask);
               //CC only!
               //!!!!!!!!

               //This isn't exactly right either--we don't know if it was the M68K or the DSP writing here...
               // !!! FIX !!! [DONE]
               if (DSP_RUNNING)
               {
                  if (who == M68K)
                     m68k_end_timeslice();
                  else if (who == DSP)
                     DSPReleaseTimeslice();

                  if (!wasRunning)
                     FlushDSPPipeline();
               }
               break;
            }
         case 0x18:
            dsp_modulo = data;
            break;
         case 0x1C:
            dsp_div_control = data;
            break;
      }
      return;
   }

   JaguarWriteLong(offset, data, who);
}

/* Update the DSP register file pointers depending on REGPAGE bit */
void DSPUpdateRegisterBanks(void)
{
	int bank = (dsp_flags & REGPAGE);

	if (dsp_flags & IMASK)
		bank = 0;							// IMASK forces main bank to be bank 0

	if (bank)
		dsp_reg = dsp_reg_bank_1, dsp_alternate_reg = dsp_reg_bank_0;
	else
		dsp_reg = dsp_reg_bank_0, dsp_alternate_reg = dsp_reg_bank_1;
}

/* Check for and handle any asserted DSP IRQs */
void DSPHandleIRQs(void)
{
   uint32_t bits, mask;
   int which = 0;									// Determine which interrupt
   if (dsp_flags & IMASK) 							// Bail if we're already inside an interrupt
      return;

   // Get the active interrupt bits (latches) & interrupt mask (enables)
   bits = ((dsp_control >> 10) & 0x20) | ((dsp_control >> 6) & 0x1F);
   mask = ((dsp_flags >> 11) & 0x20) | ((dsp_flags >> 4) & 0x1F);

   bits &= mask;

   if (!bits)										// Bail if nothing is enabled
      return;


   if (bits & 0x01)
      which = 0;
   if (bits & 0x02)
      which = 1;
   if (bits & 0x04)
      which = 2;
   if (bits & 0x08)
      which = 3;
   if (bits & 0x10)
      which = 4;
   if (bits & 0x20)
      which = 5;

   if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
   {
      if (pipeline[plPtrWrite].writebackRegister != 0xFF)
      {
         if (pipeline[plPtrWrite].writebackRegister != 0xFE)
            dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;
         else
         {
            if (pipeline[plPtrWrite].type == TYPE_BYTE)
               JaguarWriteByte(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
            else if (pipeline[plPtrWrite].type == TYPE_WORD)
               JaguarWriteWord(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
            else
               JaguarWriteLong(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
         }
      }

#ifndef NEW_SCOREBOARD
      if (affectsScoreboard[pipeline[plPtrWrite].opcode])
         scoreboard[pipeline[plPtrWrite].operand2] = false;
#else
      //Yup, sequential MOVEQ # problem fixing (I hope!)...
      if (affectsScoreboard[pipeline[plPtrWrite].opcode])
         if (scoreboard[pipeline[plPtrWrite].operand2])
            scoreboard[pipeline[plPtrWrite].operand2]--;
#endif
   }

   dsp_flags |= IMASK;
   DSPUpdateRegisterBanks();

   dsp_reg[31] -= 4;
   //CC only!
   //!!!!!!!!
   //This might not come back to the right place if the instruction was MOVEI #. !!! FIX !!!
   //But, then again, JTRM says that it adds two regardless of what the instruction was...
   //It missed the place that it was supposed to come back to, so this is WRONG!
   //
   // Look at the pipeline when an interrupt occurs (instructions of foo, bar, baz):
   //
   // R -> baz		(<- PC points here)
   // E -> bar		(when it should point here!)
   // W -> foo
   //
   // 'Foo' just completed executing as per above. PC is pointing to the instruction 'baz'
   // which means (assuming they're all 2 bytes long) that the code below will come back on
   // instruction 'baz' instead of 'bar' which is the next instruction to execute in the
   // instruction stream...

   DSPWriteLong(dsp_reg[31], dsp_pc - 2 - (pipeline[plPtrExec].opcode == 38 ? 6 : (pipeline[plPtrExec].opcode == PIPELINE_STALL ? 0 : 2)), DSP);

   dsp_pc = dsp_reg[30] = DSP_WORK_RAM_BASE + (which * 0x10);
   FlushDSPPipeline();
}

/* Non-pipelined version... */
void DSPHandleIRQsNP(void)
{
   uint32_t bits;
   uint32_t mask;
   int which = 0;									// Determine which interrupt
	if (dsp_flags & IMASK) 							// Bail if we're already inside an interrupt
		return;

	// Get the active interrupt bits (latches) & interrupt mask (enables)
	bits = ((dsp_control >> 10) & 0x20) | ((dsp_control >> 6) & 0x1F);
   mask = ((dsp_flags >> 11) & 0x20) | ((dsp_flags >> 4) & 0x1F);

	bits &= mask;

	if (!bits)										// Bail if nothing is enabled
		return;

	if (bits & 0x01)
		which = 0;
	if (bits & 0x02)
		which = 1;
	if (bits & 0x04)
		which = 2;
	if (bits & 0x08)
		which = 3;
	if (bits & 0x10)
		which = 4;
	if (bits & 0x20)
		which = 5;

	dsp_flags |= IMASK;		// Force Bank #0
	DSPUpdateRegisterBanks();


	dsp_reg[31] -= 4;
	dsp_reg[30] = dsp_pc - 2; // -2 because we've executed the instruction already

	DSPWriteLong(dsp_reg[31], dsp_reg[30], DSP);

	dsp_pc = dsp_reg[30] = DSP_WORK_RAM_BASE + (which * 0x10);
}

//
// Set the specified DSP IRQ line to a given state
//
void DSPSetIRQLine(int irqline, int state)
{
//NOTE: This doesn't take INT_LAT5 into account. !!! FIX !!!
	uint32_t mask = INT_LAT0 << irqline;
	dsp_control &= ~mask;							// Clear the latch bit

	if (state)
	{
		dsp_control |= mask;						// Set the latch bit
		DSPHandleIRQsNP();
	}
}

bool DSPIsRunning(void)
{
	return (DSP_RUNNING ? true : false);
}

void DSPInit(void)
{
	dsp_build_branch_condition_table();
	DSPReset();
}

void DSPReset(void)
{
   unsigned i;

	dsp_pc				  = 0x00F1B000;
	dsp_acc				  = 0x00000000;
	dsp_remain			  = 0x00000000;
	dsp_modulo			  = 0xFFFFFFFF;
	dsp_flags			  = 0x00040000;
	dsp_matrix_control    = 0x00000000;
	dsp_pointer_to_matrix = 0x00000000;
	dsp_data_organization = 0xFFFFFFFF;
	dsp_control			  = 0x00002000;				// Report DSP version 2
	dsp_div_control		  = 0x00000000;
	dsp_in_exec			  = 0;

	dsp_reg = dsp_reg_bank_0;
	dsp_alternate_reg = dsp_reg_bank_1;

	for(i=0; i<32; i++)
		dsp_reg[i] = dsp_alternate_reg[i] = 0x00000000;

	CLR_ZNC;
	IMASKCleared = false;
	FlushDSPPipeline();
	dsp_reset_stats();

	// Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents
	for(i=0; i<8192; i+=4)
		*((uint32_t *)(&dsp_ram_8[i])) = rand();
}

void DSPDone(void)
{
}

/* DSP execution core */

INLINE void DSPExec(int32_t cycles)
{
#ifdef DSP_SINGLE_STEPPING
	if (dsp_control & 0x18)
	{
		cycles = 1;
		dsp_control &= ~0x10;
	}
#endif
	dsp_releaseTimeSlice_flag = 0;
	dsp_in_exec++;

	while (cycles > 0 && DSP_RUNNING)
	{
      uint16_t opcode;
      uint32_t index;

		if (IMASKCleared)						// If IMASK was cleared,
		{
			DSPHandleIRQsNP();					// See if any other interrupts are pending!
			IMASKCleared = false;
		}

		opcode = DSPReadWord(dsp_pc, DSP);
		index = opcode >> 10;
		dsp_opcode_first_parameter = (opcode >> 5) & 0x1F;
		dsp_opcode_second_parameter = opcode & 0x1F;
		dsp_pc += 2;
		dsp_opcode[index]();
		dsp_opcode_use[index]++;
		cycles -= dsp_opcode_cycles[index];
	}

	dsp_in_exec--;
}

// DSP opcode handlers

// There is a problem here with interrupt handlers the JUMP and JR instructions that
// can cause trouble because an interrupt can occur *before* the instruction following the
// jump can execute... !!! FIX !!!
INLINE static void dsp_opcode_jump(void)
{
	// normalize flags
/*	dsp_flag_c=dsp_flag_c?1:0;
	dsp_flag_z=dsp_flag_z?1:0;
	dsp_flag_n=dsp_flag_n?1:0;*/
	// KLUDGE: Used by BRANCH_CONDITION
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
		uint32_t delayed_pc = RM;
		DSPExec(1);
		dsp_pc = delayed_pc;
	}
}


INLINE static void dsp_opcode_jr(void)
{
	// normalize flags
/*	dsp_flag_c=dsp_flag_c?1:0;
	dsp_flag_z=dsp_flag_z?1:0;
	dsp_flag_n=dsp_flag_n?1:0;*/
	// KLUDGE: Used by BRANCH_CONDITION
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
		int32_t offset = ((IMM_1 & 0x10) ? 0xFFFFFFF0 | IMM_1 : IMM_1);		// Sign extend IMM_1
		int32_t delayed_pc = dsp_pc + (offset * 2);
		DSPExec(1);
		dsp_pc = delayed_pc;
	}
}


INLINE static void dsp_opcode_add(void)
{
	uint32_t res = RN + RM;
	SET_ZNC_ADD(RN, RM, res);
	RN = res;
}


INLINE static void dsp_opcode_addc(void)
{
	uint32_t res = RN + RM + dsp_flag_c;
	uint32_t carry = dsp_flag_c;
	SET_ZNC_ADD(RN + carry, RM, res);
	RN = res;
}


INLINE static void dsp_opcode_addq(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t res = RN + r1;
	CLR_ZNC; SET_ZNC_ADD(RN, r1, res);
	RN = res;
}


INLINE static void dsp_opcode_sub(void)
{
	uint32_t res = RN - RM;
	SET_ZNC_SUB(RN, RM, res);
	RN = res;
}


INLINE static void dsp_opcode_subc(void)
{
	// This is how the DSP ALU does it--Two's complement with inverted carry
	uint64_t res = (uint64_t)RN + (uint64_t)(RM ^ 0xFFFFFFFF) + (dsp_flag_c ^ 1);
	// Carry out of the result is inverted too
	dsp_flag_c = ((res >> 32) & 0x01) ^ 1;
	RN = (res & 0xFFFFFFFF);
	SET_ZN(RN);
}


INLINE static void dsp_opcode_subq(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t res = RN - r1;
	SET_ZNC_SUB(RN, r1, res);
	RN = res;
}


INLINE static void dsp_opcode_cmp(void)
{
	uint32_t res = RN - RM;
	SET_ZNC_SUB(RN, RM, res);
}


INLINE static void dsp_opcode_cmpq(void)
{
	static int32_t sqtable[32] =
		{ 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,-16,-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1 };
	uint32_t r1 = sqtable[IMM_1 & 0x1F]; // I like this better -> (INT8)(jaguar.op >> 2) >> 3;
	uint32_t res = RN - r1;
	SET_ZNC_SUB(RN, r1, res);
}


INLINE static void dsp_opcode_and(void)
{
	RN = RN & RM;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_or(void)
{
	RN = RN | RM;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_xor(void)
{
	RN = RN ^ RM;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_not(void)
{
	RN = ~RN;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_move_pc(void)
{
	RN = dsp_pc - 2;
}


INLINE static void dsp_opcode_store_r14_indexed(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	DSPWriteLong((dsp_reg[14] & 0xFFFFFFFC) + (dsp_convert_zero[IMM_1] << 2), RN, DSP);
#else
	DSPWriteLong(dsp_reg[14] + (dsp_convert_zero[IMM_1] << 2), RN, DSP);
#endif
}


INLINE static void dsp_opcode_store_r15_indexed(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	DSPWriteLong((dsp_reg[15] & 0xFFFFFFFC) + (dsp_convert_zero[IMM_1] << 2), RN, DSP);
#else
	DSPWriteLong(dsp_reg[15] + (dsp_convert_zero[IMM_1] << 2), RN, DSP);
#endif
}


INLINE static void dsp_opcode_load_r14_ri(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	RN = DSPReadLong((dsp_reg[14] + RM) & 0xFFFFFFFC, DSP);
#else
	RN = DSPReadLong(dsp_reg[14] + RM, DSP);
#endif
}


INLINE static void dsp_opcode_load_r15_ri(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	RN = DSPReadLong((dsp_reg[15] + RM) & 0xFFFFFFFC, DSP);
#else
	RN = DSPReadLong(dsp_reg[15] + RM, DSP);
#endif
}


INLINE static void dsp_opcode_store_r14_ri(void)
{
	DSPWriteLong(dsp_reg[14] + RM, RN, DSP);
}


INLINE static void dsp_opcode_store_r15_ri(void)
{
	DSPWriteLong(dsp_reg[15] + RM, RN, DSP);
}


INLINE static void dsp_opcode_nop(void)
{
}


INLINE static void dsp_opcode_storeb(void)
{
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		DSPWriteLong(RM, RN & 0xFF, DSP);
	else
		JaguarWriteByte(RM, RN, DSP);
}


INLINE static void dsp_opcode_storew(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		DSPWriteLong(RM & 0xFFFFFFFE, RN & 0xFFFF, DSP);
	else
		JaguarWriteWord(RM & 0xFFFFFFFE, RN, DSP);
#else
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		DSPWriteLong(RM, RN & 0xFFFF, DSP);
	else
		JaguarWriteWord(RM, RN, DSP);
#endif
}


INLINE static void dsp_opcode_store(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	DSPWriteLong(RM & 0xFFFFFFFC, RN, DSP);
#else
	DSPWriteLong(RM, RN, DSP);
#endif
}


INLINE static void dsp_opcode_loadb(void)
{
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		RN = DSPReadLong(RM, DSP) & 0xFF;
	else
		RN = JaguarReadByte(RM, DSP);
}


INLINE static void dsp_opcode_loadw(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		RN = DSPReadLong(RM & 0xFFFFFFFE, DSP) & 0xFFFF;
	else
		RN = JaguarReadWord(RM & 0xFFFFFFFE, DSP);
#else
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		RN = DSPReadLong(RM, DSP) & 0xFFFF;
	else
		RN = JaguarReadWord(RM, DSP);
#endif
}


INLINE static void dsp_opcode_load(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	RN = DSPReadLong(RM & 0xFFFFFFFC, DSP);
#else
	RN = DSPReadLong(RM, DSP);
#endif
}


INLINE static void dsp_opcode_load_r14_indexed(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	RN = DSPReadLong((dsp_reg[14] & 0xFFFFFFFC) + (dsp_convert_zero[IMM_1] << 2), DSP);
#else
	RN = DSPReadLong(dsp_reg[14] + (dsp_convert_zero[IMM_1] << 2), DSP);
#endif
}


INLINE static void dsp_opcode_load_r15_indexed(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	RN = DSPReadLong((dsp_reg[15] & 0xFFFFFFFC) + (dsp_convert_zero[IMM_1] << 2), DSP);
#else
	RN = DSPReadLong(dsp_reg[15] + (dsp_convert_zero[IMM_1] << 2), DSP);
#endif
}


INLINE static void dsp_opcode_movei(void)
{
	// This instruction is followed by 32-bit value in LSW / MSW format...
	RN = (uint32_t)DSPReadWord(dsp_pc, DSP) | ((uint32_t)DSPReadWord(dsp_pc + 2, DSP) << 16);
	dsp_pc += 4;
}


INLINE static void dsp_opcode_moveta(void)
{
	ALTERNATE_RN = RM;
}


INLINE static void dsp_opcode_movefa(void)
{
	RN = ALTERNATE_RM;
}


INLINE static void dsp_opcode_move(void)
{
	RN = RM;
}


INLINE static void dsp_opcode_moveq(void)
{
	RN = IMM_1;
}


INLINE static void dsp_opcode_resmac(void)
{
	RN = (uint32_t)dsp_acc;
}


INLINE static void dsp_opcode_imult(void)
{
	RN = (int16_t)RN * (int16_t)RM;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_mult(void)
{
	RN = (uint16_t)RM * (uint16_t)RN;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_bclr(void)
{
	uint32_t res = RN & ~(1 << IMM_1);
	RN = res;
	SET_ZN(res);
}


INLINE static void dsp_opcode_btst(void)
{
	dsp_flag_z = (~RN >> IMM_1) & 1;
}


INLINE static void dsp_opcode_bset(void)
{
	uint32_t res = RN | (1 << IMM_1);
	RN = res;
	SET_ZN(res);
}


INLINE static void dsp_opcode_subqt(void)
{
	RN -= dsp_convert_zero[IMM_1];
}


INLINE static void dsp_opcode_addqt(void)
{
	RN += dsp_convert_zero[IMM_1];
}


INLINE static void dsp_opcode_imacn(void)
{
	int32_t res = (int16_t)RM * (int16_t)RN;
	dsp_acc += (int64_t)res;
//Should we AND the result to fit into 40 bits here???
}


INLINE static void dsp_opcode_mtoi(void)
{
	RN = (((int32_t)RM >> 8) & 0xFF800000) | (RM & 0x007FFFFF);
	SET_ZN(RN);
}


INLINE static void dsp_opcode_normi(void)
{
	uint32_t _Rm = RM;
	uint32_t res = 0;

	if (_Rm)
	{
		while ((_Rm & 0xffc00000) == 0)
		{
			_Rm <<= 1;
			res--;
		}
		while ((_Rm & 0xff800000) != 0)
		{
			_Rm >>= 1;
			res++;
		}
	}
	RN = res;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_mmult(void)
{
   uint32_t res;
   unsigned i;
   int count	= dsp_matrix_control&0x0f;
   uint32_t addr = dsp_pointer_to_matrix; // in the dsp ram
   int64_t accum = 0;

   if (!(dsp_matrix_control & 0x10))
   {
      for (i = 0; i < count; i++)
      {
         int16_t a;
         int16_t b;

         if (i&0x01)
            a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
         else
            a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
         b=((int16_t)DSPReadWord(addr + 2, DSP));
         accum += a*b;
         addr += 4;
      }
   }
   else
   {
      for (i = 0; i < count; i++)
      {
         int16_t a;
         int16_t b;

         if (i&0x01)
            a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
         else
            a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
         b=((int16_t)DSPReadWord(addr + 2, DSP));
         accum += a*b;
         addr += 4 * count;
      }
   }
   RN = res = (int32_t)accum;
   // carry flag to do
   //NOTE: The flags are set based upon the last add/multiply done...
   SET_ZN(RN);
}


INLINE static void dsp_opcode_abs(void)
{
	uint32_t _Rn = RN;

	if (_Rn == 0x80000000)
		dsp_flag_n = 1;
	else
	{
      uint32_t res;

		dsp_flag_c = ((_Rn & 0x80000000) >> 31);
		res = RN   = ((_Rn & 0x80000000) ? -_Rn : _Rn);
		CLR_ZN;
      SET_Z(res);
	}
}


INLINE static void dsp_opcode_div(void)
{
   unsigned i;
	// Real algorithm, courtesy of SCPCD: NYAN!
	uint32_t q = RN;
	uint32_t r = 0;

	// If 16.16 division, stuff top 16 bits of RN into remainder and put the
	// bottom 16 of RN in top 16 of quotient
	if (dsp_div_control & 0x01)
		q <<= 16, r = RN >> 16;

	for(i=0; i<32; i++)
	{
		uint32_t sign = r & 0x80000000;
		r = (r << 1) | ((q >> 31) & 0x01);
		r += (sign ? RM : -RM);
		q = (q << 1) | (((~r) >> 31) & 0x01);
	}

	RN = q;
	dsp_remain = r;
}


INLINE static void dsp_opcode_imultn(void)
{
	// This is OK, since this multiply won't overflow 32 bits...
	int32_t res = (int32_t)((int16_t)RN * (int16_t)RM);
	dsp_acc = (int64_t)res;
	SET_ZN(res);
}


INLINE static void dsp_opcode_neg(void)
{
	uint32_t res = -RN;
	SET_ZNC_SUB(0, RN, res);
	RN = res;
}


INLINE static void dsp_opcode_shlq(void)
{
	// NB: This instruction is the *only* one that does (32 - immediate data).
	int32_t r1 = 32 - IMM_1;
	uint32_t res = RN << r1;
	SET_ZN(res); dsp_flag_c = (RN >> 31) & 1;
	RN = res;
}


INLINE static void dsp_opcode_shrq(void)
{
	int32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t res = RN >> r1;
	SET_ZN(res); dsp_flag_c = RN & 1;
	RN = res;
}


INLINE static void dsp_opcode_ror(void)
{
	uint32_t r1 = RM & 0x1F;
	uint32_t res = (RN >> r1) | (RN << (32 - r1));
	SET_ZN(res); dsp_flag_c = (RN >> 31) & 1;
	RN = res;
}


INLINE static void dsp_opcode_rorq(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1 & 0x1F];
	uint32_t r2 = RN;
	uint32_t res = (r2 >> r1) | (r2 << (32 - r1));
	RN = res;
	SET_ZN(res); dsp_flag_c = (r2 >> 31) & 0x01;
}


INLINE static void dsp_opcode_sha(void)
{
	int32_t sRm=(int32_t)RM;
	uint32_t _Rn=RN;

	if (sRm<0)
	{
		uint32_t shift=-sRm;
		if (shift>=32) shift=32;
		dsp_flag_c=(_Rn&0x80000000)>>31;
		while (shift)
		{
			_Rn<<=1;
			shift--;
		}
	}
	else
	{
		uint32_t shift=sRm;
		if (shift>=32) shift=32;
		dsp_flag_c=_Rn&0x1;
		while (shift)
		{
			_Rn=((int32_t)_Rn)>>1;
			shift--;
		}
	}
	RN = _Rn;
	SET_ZN(RN);
}


INLINE static void dsp_opcode_sharq(void)
{
	uint32_t res = (int32_t)RN >> dsp_convert_zero[IMM_1];
	SET_ZN(res); dsp_flag_c = RN & 0x01;
	RN = res;
}


INLINE static void dsp_opcode_sh(void)
{
	int32_t sRm=(int32_t)RM;
	uint32_t _Rn=RN;

	if (sRm<0)
	{
		uint32_t shift=(-sRm);
		if (shift>=32) shift=32;
		dsp_flag_c=(_Rn&0x80000000)>>31;
		while (shift)
		{
			_Rn<<=1;
			shift--;
		}
	}
	else
	{
		uint32_t shift=sRm;
		if (shift>=32) shift=32;
		dsp_flag_c=_Rn&0x1;
		while (shift)
		{
			_Rn>>=1;
			shift--;
		}
	}
	RN = _Rn;
	SET_ZN(RN);
}

void dsp_opcode_addqmod(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t r2 = RN;
	uint32_t res = r2 + r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	RN = res;
	SET_ZNC_ADD(r2, r1, res);
}

void dsp_opcode_subqmod(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t r2 = RN;
	uint32_t res = r2 - r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	RN = res;

	SET_ZNC_SUB(r2, r1, res);
}

void dsp_opcode_mirror(void)
{
	uint32_t r1 = RN;
	RN = (mirror_table[r1 & 0xFFFF] << 16) | mirror_table[r1 >> 16];
	SET_ZN(RN);
}

void dsp_opcode_sat32s(void)
{
	int32_t r2 = (uint32_t)RN;
	int32_t temp = dsp_acc >> 32;
	uint32_t res = (temp < -1) ? (int32_t)0x80000000 : (temp > 0) ? (int32_t)0x7FFFFFFF : r2;
	RN = res;
	SET_ZN(res);
}

void dsp_opcode_sat16s(void)
{
	int32_t r2 = RN;
	uint32_t res = (r2 < -32768) ? -32768 : (r2 > 32767) ? 32767 : r2;
	RN = res;
	SET_ZN(res);
}

void dsp_opcode_illegal(void)
{
}

/* New pipelined DSP core */

INLINE static void DSP_abs(void);
INLINE static void DSP_add(void);
INLINE static void DSP_addc(void);
INLINE static void DSP_addq(void);
INLINE static void DSP_addqmod(void);
INLINE static void DSP_addqt(void);
INLINE static void DSP_and(void);
INLINE static void DSP_bclr(void);
INLINE static void DSP_bset(void);
INLINE static void DSP_btst(void);
INLINE static void DSP_cmp(void);
INLINE static void DSP_cmpq(void);
INLINE static void DSP_div(void);
INLINE static void DSP_imacn(void);
INLINE static void DSP_imult(void);
INLINE static void DSP_imultn(void);
INLINE static void DSP_illegal(void);
INLINE static void DSP_jr(void);
INLINE static void DSP_jump(void);
INLINE static void DSP_load(void);
INLINE static void DSP_loadb(void);
INLINE static void DSP_loadw(void);
INLINE static void DSP_load_r14_i(void);
INLINE static void DSP_load_r14_r(void);
INLINE static void DSP_load_r15_i(void);
INLINE static void DSP_load_r15_r(void);
INLINE static void DSP_mirror(void);
INLINE static void DSP_mmult(void);
INLINE static void DSP_move(void);
INLINE static void DSP_movefa(void);
INLINE static void DSP_movei(void);
INLINE static void DSP_movepc(void);
INLINE static void DSP_moveq(void);
INLINE static void DSP_moveta(void);
INLINE static void DSP_mtoi(void);
INLINE static void DSP_mult(void);
INLINE static void DSP_neg(void);
INLINE static void DSP_nop(void);
INLINE static void DSP_normi(void);
INLINE static void DSP_not(void);
INLINE static void DSP_or(void);
INLINE static void DSP_resmac(void);
INLINE static void DSP_ror(void);
INLINE static void DSP_rorq(void);
INLINE static void DSP_sat16s(void);
INLINE static void DSP_sat32s(void);
INLINE static void DSP_sh(void);
INLINE static void DSP_sha(void);
INLINE static void DSP_sharq(void);
INLINE static void DSP_shlq(void);
INLINE static void DSP_shrq(void);
INLINE static void DSP_store(void);
INLINE static void DSP_storeb(void);
INLINE static void DSP_storew(void);
INLINE static void DSP_store_r14_i(void);
INLINE static void DSP_store_r14_r(void);
INLINE static void DSP_store_r15_i(void);
INLINE static void DSP_store_r15_r(void);
INLINE static void DSP_sub(void);
INLINE static void DSP_subc(void);
INLINE static void DSP_subq(void);
INLINE static void DSP_subqmod(void);
INLINE static void DSP_subqt(void);
INLINE static void DSP_xor(void);

void (* DSPOpcode[64])() =
{
	DSP_add,			DSP_addc,			DSP_addq,			DSP_addqt,
	DSP_sub,			DSP_subc,			DSP_subq,			DSP_subqt,
	DSP_neg,			DSP_and,			DSP_or,				DSP_xor,
	DSP_not,			DSP_btst,			DSP_bset,			DSP_bclr,

	DSP_mult,			DSP_imult,			DSP_imultn,			DSP_resmac,
	DSP_imacn,			DSP_div,			DSP_abs,			DSP_sh,
	DSP_shlq,			DSP_shrq,			DSP_sha,			DSP_sharq,
	DSP_ror,			DSP_rorq,			DSP_cmp,			DSP_cmpq,

	DSP_subqmod,		DSP_sat16s,			DSP_move,			DSP_moveq,
	DSP_moveta,			DSP_movefa,			DSP_movei,			DSP_loadb,
	DSP_loadw,			DSP_load,			DSP_sat32s,			DSP_load_r14_i,
	DSP_load_r15_i,		DSP_storeb,			DSP_storew,			DSP_store,

	DSP_mirror,			DSP_store_r14_i,	DSP_store_r15_i,	DSP_movepc,
	DSP_jump,			DSP_jr,				DSP_mmult,			DSP_mtoi,
	DSP_normi,			DSP_nop,			DSP_load_r14_r,		DSP_load_r15_r,
	DSP_store_r14_r,	DSP_store_r15_r,	DSP_illegal,		DSP_addqmod
};

bool readAffected[64][2] =
{
	{ true,  true}, { true,  true}, {false,  true}, {false,  true},
	{ true,  true}, { true,  true}, {false,  true}, {false,  true},
	{false,  true}, { true,  true}, { true,  true}, { true,  true},
	{false,  true}, {false,  true}, {false,  true}, {false,  true},

	{ true,  true}, { true,  true}, { true,  true}, {false,  true},
	{ true,  true}, { true,  true}, {false,  true}, { true,  true},
	{false,  true}, {false,  true}, { true,  true}, {false,  true},
	{ true,  true}, {false,  true}, { true,  true}, {false,  true},

	{false,  true}, {false,  true}, { true, false}, {false, false},
	{ true, false}, {false, false}, {false, false}, { true, false},
	{ true, false}, { true, false}, {false,  true}, { true, false},
	{ true, false}, { true,  true}, { true,  true}, { true,  true},

	{false,  true}, { true,  true}, { true,  true}, {false,  true},
	{ true, false}, { true, false}, { true,  true}, { true, false},
	{ true, false}, {false, false}, { true, false}, { true, false},
	{ true,  true}, { true,  true}, {false, false}, {false,  true}
};

bool isLoadStore[65] =
{
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,

	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,

	false, false, false, false, false, false, false,  true,
	 true,  true, false,  true,  true,  true,  true,  true,

	false,  true,  true, false, false, false, false, false,
	false, false,  true,  true,  true,  true, false, false, false
};

void FlushDSPPipeline(void)
{
   unsigned i;

	plPtrFetch = 3, plPtrRead = 2, plPtrExec = 1, plPtrWrite = 0;

	for(i=0; i<4; i++)
		pipeline[i].opcode = PIPELINE_STALL;

	for(i=0; i<32; i++)
		scoreboard[i] = 0;
}

uint32_t pcQueue1[0x400];
uint32_t pcQPtr1 = 0;
static uint32_t prevR1;

/* DSP pipelined opcode handlers */

#define PRM				pipeline[plPtrExec].reg1
#define PRN				pipeline[plPtrExec].reg2
#define PIMM1			pipeline[plPtrExec].operand1
#define PIMM2			pipeline[plPtrExec].operand2
#define PRES			pipeline[plPtrExec].result
#define PWBR			pipeline[plPtrExec].writebackRegister
#define NO_WRITEBACK	pipeline[plPtrExec].writebackRegister = 0xFF
#define DSP_PPC			dsp_pc - (pipeline[plPtrRead].opcode == 38 ? 6 : (pipeline[plPtrRead].opcode == PIPELINE_STALL ? 0 : 2)) - (pipeline[plPtrExec].opcode == 38 ? 6 : (pipeline[plPtrExec].opcode == PIPELINE_STALL ? 0 : 2))
#define WRITEBACK_ADDR	pipeline[plPtrExec].writebackRegister = 0xFE

INLINE static void DSP_abs(void)
{
	uint32_t _Rn = PRN;

	if (_Rn == 0x80000000)
		dsp_flag_n = 1;
	else
	{
		dsp_flag_c = ((_Rn & 0x80000000) >> 31);
		PRES = ((_Rn & 0x80000000) ? -_Rn : _Rn);
		CLR_ZN; SET_Z(PRES);
	}
}

INLINE static void DSP_add(void)
{
	uint32_t res = PRN + PRM;
	SET_ZNC_ADD(PRN, PRM, res);
	PRES = res;
}

INLINE static void DSP_addc(void)
{
	uint32_t res = PRN + PRM + dsp_flag_c;
	uint32_t carry = dsp_flag_c;
	SET_ZNC_ADD(PRN + carry, PRM, res);
	PRES = res;
}

INLINE static void DSP_addq(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t res = PRN + r1;
	CLR_ZNC; SET_ZNC_ADD(PRN, r1, res);
	PRES = res;
}

INLINE static void DSP_addqmod(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t r2 = PRN;
	uint32_t res = r2 + r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	PRES = res;
	SET_ZNC_ADD(r2, r1, res);
}

INLINE static void DSP_addqt(void)
{
	PRES = PRN + dsp_convert_zero[PIMM1];
}

INLINE static void DSP_and(void)
{
	PRES = PRN & PRM;
	SET_ZN(PRES);
}

INLINE static void DSP_bclr(void)
{
	PRES = PRN & ~(1 << PIMM1);
	SET_ZN(PRES);
}

INLINE static void DSP_bset(void)
{
	PRES = PRN | (1 << PIMM1);
	SET_ZN(PRES);
}

INLINE static void DSP_btst(void)
{
	dsp_flag_z = (~PRN >> PIMM1) & 1;
	NO_WRITEBACK;
}

INLINE static void DSP_cmp(void)
{
	uint32_t res = PRN - PRM;
	SET_ZNC_SUB(PRN, PRM, res);
	NO_WRITEBACK;
}

INLINE static void DSP_cmpq(void)
{
	static int32_t sqtable[32] =
		{ 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,-16,-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1 };
	uint32_t r1 = sqtable[PIMM1 & 0x1F]; // I like this better -> (INT8)(jaguar.op >> 2) >> 3;
	uint32_t res = PRN - r1;
	SET_ZNC_SUB(PRN, r1, res);
	NO_WRITEBACK;
}

INLINE static void DSP_div(void)
{
	uint32_t _Rm = PRM, _Rn = PRN;

	if (_Rm)
	{
		if (dsp_div_control & 1)
		{
			dsp_remain = (((uint64_t)_Rn) << 16) % _Rm;
			if (dsp_remain & 0x80000000)
				dsp_remain -= _Rm;
			PRES = (((uint64_t)_Rn) << 16) / _Rm;
		}
		else
		{
			dsp_remain = _Rn % _Rm;
			if (dsp_remain & 0x80000000)
				dsp_remain -= _Rm;
			PRES = PRN / _Rm;
		}
	}
	else
		PRES = 0xFFFFFFFF;
}

INLINE static void DSP_imacn(void)
{
	int32_t res = (int16_t)PRM * (int16_t)PRN;
	dsp_acc += (int64_t)res;
//Should we AND the result to fit into 40 bits here???
	NO_WRITEBACK;
}

INLINE static void DSP_imult(void)
{
	PRES = (int16_t)PRN * (int16_t)PRM;
	SET_ZN(PRES);
}

INLINE static void DSP_imultn(void)
{
	// This is OK, since this multiply won't overflow 32 bits...
	int32_t res = (int32_t)((int16_t)PRN * (int16_t)PRM);
	dsp_acc = (int64_t)res;
	SET_ZN(res);
	NO_WRITEBACK;
}

INLINE static void DSP_illegal(void)
{
	NO_WRITEBACK;
}

// There is a problem here with interrupt handlers the JUMP and JR instructions that
// can cause trouble because an interrupt can occur *before* the instruction following the
// jump can execute... !!! FIX !!!
// This can probably be solved by judicious coding in the pipeline execution core...
// And should be fixed now...
INLINE static void DSP_jr(void)
{
   // KLUDGE: Used by BRANCH_CONDITION macro
   uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

   if (BRANCH_CONDITION(PIMM2))
   {
      int32_t offset = ((PIMM1 & 0x10) ? 0xFFFFFFF0 | PIMM1 : PIMM1);		// Sign extend PIMM1
      //Account for pipeline effects...
      uint32_t newPC = dsp_pc + (offset * 2) - (pipeline[plPtrRead].opcode == 38 ? 6 : (pipeline[plPtrRead].opcode == PIPELINE_STALL ? 0 : 2));

      // Now that we've branched, we have to make sure that the following instruction
      // is executed atomically with this one and then flush the pipeline before setting
      // the new PC.

      // Step 1: Handle writebacks at stage 3 of pipeline
      if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
      {
         if (pipeline[plPtrWrite].writebackRegister != 0xFF)
         {
            if (pipeline[plPtrWrite].writebackRegister != 0xFE)
               dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;
            else
            {
               if (pipeline[plPtrWrite].type == TYPE_BYTE)
                  JaguarWriteByte(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
               else if (pipeline[plPtrWrite].type == TYPE_WORD)
                  JaguarWriteWord(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
               else
                  JaguarWriteLong(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
            }
         }

#ifndef NEW_SCOREBOARD
         if (affectsScoreboard[pipeline[plPtrWrite].opcode])
            scoreboard[pipeline[plPtrWrite].operand2] = false;
#else
         //Yup, sequential MOVEQ # problem fixing (I hope!)...
         if (affectsScoreboard[pipeline[plPtrWrite].opcode])
            if (scoreboard[pipeline[plPtrWrite].operand2])
               scoreboard[pipeline[plPtrWrite].operand2]--;
#endif
      }

      // Step 2: Push instruction through pipeline & execute following instruction
      // NOTE: By putting our following instruction at stage 3 of the pipeline,
      //       we effectively handle the final push of the instruction through the
      //       pipeline when the new PC takes effect (since when we return, the
      //       pipeline code will be executing the writeback stage. If we reverse
      //       the execution order of the pipeline stages, this will no longer be
      //       the case!)...
      pipeline[plPtrExec] = pipeline[plPtrRead];
      //This is BAD. We need to get that next opcode and execute it!
      //NOTE: The problem is here because of a bad stall. Once those are fixed, we can probably
      //      remove this crap.
      if (pipeline[plPtrExec].opcode == PIPELINE_STALL)
      {
         uint16_t instruction = DSPReadWord(dsp_pc, DSP);
         pipeline[plPtrExec].opcode = instruction >> 10;
         pipeline[plPtrExec].operand1 = (instruction >> 5) & 0x1F;
         pipeline[plPtrExec].operand2 = instruction & 0x1F;
         pipeline[plPtrExec].reg1 = dsp_reg[pipeline[plPtrExec].operand1];
         pipeline[plPtrExec].reg2 = dsp_reg[pipeline[plPtrExec].operand2];
         pipeline[plPtrExec].writebackRegister = pipeline[plPtrExec].operand2;	// Set it to RN
      }//*/
      dsp_pc += 2;	// For DSP_DIS_* accuracy
      DSPOpcode[pipeline[plPtrExec].opcode]();
      dsp_opcode_use[pipeline[plPtrExec].opcode]++;
      pipeline[plPtrWrite] = pipeline[plPtrExec];

      // Step 3: Flush pipeline & set new PC
      pipeline[plPtrRead].opcode = pipeline[plPtrExec].opcode = PIPELINE_STALL;
      dsp_pc = newPC;
   }
   else
      NO_WRITEBACK;
}

INLINE static void DSP_jump(void)
{
	// KLUDGE: Used by BRANCH_CONDITION macro
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(PIMM2))
	{
		uint32_t PCSave = PRM;
		// Now that we've branched, we have to make sure that the following instruction
		// is executed atomically with this one and then flush the pipeline before setting
		// the new PC.

		// Step 1: Handle writebacks at stage 3 of pipeline
		if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
		{
			if (pipeline[plPtrWrite].writebackRegister != 0xFF)
			{
				if (pipeline[plPtrWrite].writebackRegister != 0xFE)
					dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;
				else
				{
					if (pipeline[plPtrWrite].type == TYPE_BYTE)
						JaguarWriteByte(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else if (pipeline[plPtrWrite].type == TYPE_WORD)
						JaguarWriteWord(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else
						JaguarWriteLong(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
				}
			}

#ifndef NEW_SCOREBOARD
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				scoreboard[pipeline[plPtrWrite].operand2] = false;
#else
//Yup, sequential MOVEQ # problem fixing (I hope!)...
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				if (scoreboard[pipeline[plPtrWrite].operand2])
					scoreboard[pipeline[plPtrWrite].operand2]--;
#endif
		}

		// Step 2: Push instruction through pipeline & execute following instruction
		// NOTE: By putting our following instruction at stage 3 of the pipeline,
		//       we effectively handle the final push of the instruction through the
		//       pipeline when the new PC takes effect (since when we return, the
		//       pipeline code will be executing the writeback stage. If we reverse
		//       the execution order of the pipeline stages, this will no longer be
		//       the case!)...
		pipeline[plPtrExec] = pipeline[plPtrRead];
//This is BAD. We need to get that next opcode and execute it!
//Also, same problem in JR!
//NOTE: The problem is here because of a bad stall. Once those are fixed, we can probably
//      remove this crap.
		if (pipeline[plPtrExec].opcode == PIPELINE_STALL)
		{
		uint16_t instruction = DSPReadWord(dsp_pc, DSP);
		pipeline[plPtrExec].opcode = instruction >> 10;
		pipeline[plPtrExec].operand1 = (instruction >> 5) & 0x1F;
		pipeline[plPtrExec].operand2 = instruction & 0x1F;
			pipeline[plPtrExec].reg1 = dsp_reg[pipeline[plPtrExec].operand1];
			pipeline[plPtrExec].reg2 = dsp_reg[pipeline[plPtrExec].operand2];
			pipeline[plPtrExec].writebackRegister = pipeline[plPtrExec].operand2;	// Set it to RN
		}
	dsp_pc += 2;	// For DSP_DIS_* accuracy
		DSPOpcode[pipeline[plPtrExec].opcode]();
		dsp_opcode_use[pipeline[plPtrExec].opcode]++;
		pipeline[plPtrWrite] = pipeline[plPtrExec];

		// Step 3: Flush pipeline & set new PC
		pipeline[plPtrRead].opcode = pipeline[plPtrExec].opcode = PIPELINE_STALL;
		dsp_pc = PCSave;
	}
	else
		NO_WRITEBACK;
}

INLINE static void DSP_load(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	PRES = DSPReadLong(PRM & 0xFFFFFFFC, DSP);
#else
	PRES = DSPReadLong(PRM, DSP);
#endif
}

INLINE static void DSP_loadb(void)
{
	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		PRES = DSPReadLong(PRM, DSP) & 0xFF;
	else
		PRES = JaguarReadByte(PRM, DSP);
}

INLINE static void DSP_loadw(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		PRES = DSPReadLong(PRM & 0xFFFFFFFE, DSP) & 0xFFFF;
	else
		PRES = JaguarReadWord(PRM & 0xFFFFFFFE, DSP);
#else
	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		PRES = DSPReadLong(PRM, DSP) & 0xFFFF;
	else
		PRES = JaguarReadWord(PRM, DSP);
#endif
}

INLINE static void DSP_load_r14_i(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	PRES = DSPReadLong((dsp_reg[14] & 0xFFFFFFFC) + (dsp_convert_zero[PIMM1] << 2), DSP);
#else
	PRES = DSPReadLong(dsp_reg[14] + (dsp_convert_zero[PIMM1] << 2), DSP);
#endif
}

INLINE static void DSP_load_r14_r(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	PRES = DSPReadLong((dsp_reg[14] + PRM) & 0xFFFFFFFC, DSP);
#else
	PRES = DSPReadLong(dsp_reg[14] + PRM, DSP);
#endif
}

INLINE static void DSP_load_r15_i(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	PRES = DSPReadLong((dsp_reg[15] &0xFFFFFFFC) + (dsp_convert_zero[PIMM1] << 2), DSP);
#else
	PRES = DSPReadLong(dsp_reg[15] + (dsp_convert_zero[PIMM1] << 2), DSP);
#endif
}

INLINE static void DSP_load_r15_r(void)
{
#ifdef DSP_CORRECT_ALIGNMENT
	PRES = DSPReadLong((dsp_reg[15] + PRM) & 0xFFFFFFFC, DSP);
#else
	PRES = DSPReadLong(dsp_reg[15] + PRM, DSP);
#endif
}

INLINE static void DSP_mirror(void)
{
	uint32_t r1 = PRN;
	PRES = (mirror_table[r1 & 0xFFFF] << 16) | mirror_table[r1 >> 16];
	SET_ZN(PRES);
}

INLINE static void DSP_mmult(void)
{
	uint32_t res;
   unsigned i;
	int count	= dsp_matrix_control&0x0f;
	uint32_t addr = dsp_pointer_to_matrix; // in the dsp ram
	int64_t accum = 0;

	if (!(dsp_matrix_control & 0x10))
	{
		for (i = 0; i < count; i++)
		{
			int16_t a;
         int16_t b;

			if (i&0x01)
				a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
			b=((int16_t)DSPReadWord(addr + 2, DSP));
			accum += a*b;
			addr += 4;
		}
	}
	else
	{
		for (i = 0; i < count; i++)
		{
			int16_t a;
         int16_t b;

			if (i&0x01)
				a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
			b=((int16_t)DSPReadWord(addr + 2, DSP));
			accum += a*b;
			addr += 4 * count;
		}
	}

	PRES = res = (int32_t)accum;
	// carry flag to do
//NOTE: The flags are set based upon the last add/multiply done...
	SET_ZN(PRES);
}

INLINE static void DSP_move(void)
{
	PRES = PRM;
}

INLINE static void DSP_movefa(void)
{
	PRES = dsp_alternate_reg[PIMM1];
}

INLINE static void DSP_movei(void)
{
//	// This instruction is followed by 32-bit value in LSW / MSW format...
}

INLINE static void DSP_movepc(void)
{
//Need to fix this to take into account pipelining effects... !!! FIX !!! [DONE]
//Account for pipeline effects...
	PRES = dsp_pc - 2 - (pipeline[plPtrRead].opcode == 38 ? 6 : (pipeline[plPtrRead].opcode == PIPELINE_STALL ? 0 : 2));
}

INLINE static void DSP_moveq(void)
{
	PRES = PIMM1;
}

INLINE static void DSP_moveta(void)
{
	dsp_alternate_reg[PIMM2] = PRM;
	NO_WRITEBACK;
}

INLINE static void DSP_mtoi(void)
{
	PRES = (((int32_t)PRM >> 8) & 0xFF800000) | (PRM & 0x007FFFFF);
	SET_ZN(PRES);
}

INLINE static void DSP_mult(void)
{
	PRES = (uint16_t)PRM * (uint16_t)PRN;
	SET_ZN(PRES);
}

INLINE static void DSP_neg(void)
{
	uint32_t res = -PRN;
	SET_ZNC_SUB(0, PRN, res);
	PRES = res;
}

INLINE static void DSP_nop(void)
{
	NO_WRITEBACK;
}

INLINE static void DSP_normi(void)
{
	uint32_t _Rm = PRM;
	uint32_t res = 0;

	if (_Rm)
	{
		while ((_Rm & 0xffc00000) == 0)
		{
			_Rm <<= 1;
			res--;
		}
		while ((_Rm & 0xff800000) != 0)
		{
			_Rm >>= 1;
			res++;
		}
	}
	PRES = res;
	SET_ZN(PRES);
}

INLINE static void DSP_not(void)
{
	PRES = ~PRN;
	SET_ZN(PRES);
}

INLINE static void DSP_or(void)
{
	PRES = PRN | PRM;
	SET_ZN(PRES);
}

INLINE static void DSP_resmac(void)
{
	PRES = (uint32_t)dsp_acc;
}

INLINE static void DSP_ror(void)
{
	uint32_t r1 = PRM & 0x1F;
	uint32_t res = (PRN >> r1) | (PRN << (32 - r1));
	SET_ZN(res); dsp_flag_c = (PRN >> 31) & 1;
	PRES = res;
}

INLINE static void DSP_rorq(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1 & 0x1F];
	uint32_t r2 = PRN;
	uint32_t res = (r2 >> r1) | (r2 << (32 - r1));
	PRES = res;
	SET_ZN(res); dsp_flag_c = (r2 >> 31) & 0x01;
}

INLINE static void DSP_sat16s(void)
{
	int32_t r2 = PRN;
	uint32_t res = (r2 < -32768) ? -32768 : (r2 > 32767) ? 32767 : r2;
	PRES = res;
	SET_ZN(res);
}

INLINE static void DSP_sat32s(void)
{
	int32_t r2 = (uint32_t)PRN;
	int32_t temp = dsp_acc >> 32;
	uint32_t res = (temp < -1) ? (int32_t)0x80000000 : (temp > 0) ? (int32_t)0x7FFFFFFF : r2;
	PRES = res;
	SET_ZN(res);
}

INLINE static void DSP_sh(void)
{
	int32_t sRm = (int32_t)PRM;
	uint32_t _Rn = PRN;

	if (sRm < 0)
	{
		uint32_t shift = -sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = (_Rn & 0x80000000) >> 31;

		while (shift)
		{
			_Rn <<= 1;
			shift--;
		}
	}
	else
	{
		uint32_t shift = sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = _Rn & 0x1;

		while (shift)
		{
			_Rn >>= 1;
			shift--;
		}
	}

	PRES = _Rn;
	SET_ZN(PRES);
}

INLINE static void DSP_sha(void)
{
	int32_t sRm = (int32_t)PRM;
	uint32_t _Rn = PRN;

	if (sRm < 0)
	{
		uint32_t shift = -sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = (_Rn & 0x80000000) >> 31;

		while (shift)
		{
			_Rn <<= 1;
			shift--;
		}
	}
	else
	{
		uint32_t shift = sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = _Rn & 0x1;

		while (shift)
		{
			_Rn = ((int32_t)_Rn) >> 1;
			shift--;
		}
	}

	PRES = _Rn;
	SET_ZN(PRES);
}

INLINE static void DSP_sharq(void)
{
	uint32_t res = (int32_t)PRN >> dsp_convert_zero[PIMM1];
	SET_ZN(res); dsp_flag_c = PRN & 0x01;
	PRES = res;
}

INLINE static void DSP_shlq(void)
{
	int32_t r1 = 32 - PIMM1;
	uint32_t res = PRN << r1;
	SET_ZN(res); dsp_flag_c = (PRN >> 31) & 1;
	PRES = res;
}

INLINE static void DSP_shrq(void)
{
	int32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t res = PRN >> r1;
	SET_ZN(res); dsp_flag_c = PRN & 1;
	PRES = res;
}

INLINE static void DSP_store(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	pipeline[plPtrExec].address = PRM & 0xFFFFFFFC;
#else
	pipeline[plPtrExec].address = PRM;
#endif
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

INLINE static void DSP_storeb(void)
{
	pipeline[plPtrExec].address = PRM;

	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
	{
		pipeline[plPtrExec].value = PRN & 0xFF;
		pipeline[plPtrExec].type = TYPE_DWORD;
	}
	else
	{
		pipeline[plPtrExec].value = PRN;
		pipeline[plPtrExec].type = TYPE_BYTE;
	}

	WRITEBACK_ADDR;
}

INLINE static void DSP_storew(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	pipeline[plPtrExec].address = PRM & 0xFFFFFFFE;
#else
	pipeline[plPtrExec].address = PRM;
#endif

	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
	{
		pipeline[plPtrExec].value = PRN & 0xFFFF;
		pipeline[plPtrExec].type = TYPE_DWORD;
	}
	else
	{
		pipeline[plPtrExec].value = PRN;
		pipeline[plPtrExec].type = TYPE_WORD;
	}
	WRITEBACK_ADDR;
}

INLINE static void DSP_store_r14_i(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	pipeline[plPtrExec].address = (dsp_reg[14] & 0xFFFFFFFC) + (dsp_convert_zero[PIMM1] << 2);
#else
	pipeline[plPtrExec].address = dsp_reg[14] + (dsp_convert_zero[PIMM1] << 2);
#endif
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

INLINE static void DSP_store_r14_r(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	pipeline[plPtrExec].address = (dsp_reg[14] + PRM) & 0xFFFFFFFC;
#else
	pipeline[plPtrExec].address = dsp_reg[14] + PRM;
#endif
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

INLINE static void DSP_store_r15_i(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	pipeline[plPtrExec].address = (dsp_reg[15] & 0xFFFFFFFC) + (dsp_convert_zero[PIMM1] << 2);
#else
	pipeline[plPtrExec].address = dsp_reg[15] + (dsp_convert_zero[PIMM1] << 2);
#endif
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

INLINE static void DSP_store_r15_r(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	pipeline[plPtrExec].address = (dsp_reg[15] + PRM) & 0xFFFFFFFC;
#else
	pipeline[plPtrExec].address = dsp_reg[15] + PRM;
#endif
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

INLINE static void DSP_sub(void)
{
	uint32_t res = PRN - PRM;
	SET_ZNC_SUB(PRN, PRM, res);
	PRES = res;
}

INLINE static void DSP_subc(void)
{
	uint32_t res = PRN - PRM - dsp_flag_c;
	uint32_t borrow = dsp_flag_c;
	SET_ZNC_SUB(PRN - borrow, PRM, res);
	PRES = res;
}

INLINE static void DSP_subq(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t res = PRN - r1;
	SET_ZNC_SUB(PRN, r1, res);
	PRES = res;
}

INLINE static void DSP_subqmod(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t r2 = PRN;
	uint32_t res = r2 - r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	PRES = res;
	SET_ZNC_SUB(r2, r1, res);
}

INLINE static void DSP_subqt(void)
{
	PRES = PRN - dsp_convert_zero[PIMM1];
}

INLINE static void DSP_xor(void)
{
	PRES = PRN ^ PRM;
	SET_ZN(PRES);
}

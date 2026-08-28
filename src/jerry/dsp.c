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

#include <compat/msvc.h>  /* snprintf shim for MSVC < 2015 (buildbot msvc05/10) */
#include "dsp.h"
#include "dsp_acc40.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dac.h"
#include "gpu.h"
#include "jaguar.h"
#include "jerry.h"
#include "log.h"
#include "m68000/m68kinterface.h"
#include "settings.h"
#include "tom.h"
#include "blit_memo.h"
#include "bus_arbiter.h"
#include "../core/vjtrace.h"
#include "perf_iface.h"
#include "gdbstub.h"

// Seems alignment in loads & stores was off...
#define DSP_CORRECT_ALIGNMENT
#define DSP_CORRECT_ALIGNMENT_STORE

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

// HLE auto-ack: BIOS sound engine command area in DSP RAM
// ($F1B9D0-$F1B9DF absolute; offsets relative to DSP_WORK_RAM_BASE)
#define DSP_SOUND_CMD_BASE	0x9D0
#define DSP_SOUND_CMD_END	0x9E0
// Tight-poll detection: auto-clear DSPGO after this many consecutive
// 68K reads without an intervening DSP_CTRL write.  A tight boot-time
// poll loop does ~44000 reads/frame; normal gameplay does ~1-10/frame.
#define DSPGO_POLL_THRESHOLD	8192
#define DSP_RAM_SIZE		8192

void DSPHandleIRQsNP(void);

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
INLINE static void dsp_executeOpcode(uint32_t index);

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

static uint32_t dspgo_poll_count;

#define BRANCH_CONDITION(x)		dsp_branch_condition_table[(x) + ((jaguar_flags & 7) << 5)]

static uint32_t dsp_in_exec = 0;
static int32_t dspSliceBudget;
static int32_t dspSliceSpent;
static uint32_t dsp_releaseTimeSlice_flag = 0;
/* DSP execution liveness counter for the crash watchdog's wedge
 * predicate -- see gpu.c gpu_exec_opcode_count for rationale and the
 * delay-slot caveat (PC sampling aliases on idle JR/wait loops, which
 * DSP audio engines sit in constantly). */
uint32_t dsp_exec_opcode_count = 0;

/* Instruction slots a DSP-issued D_FLAGS store waits out before it
 * retires: the slot holding the store itself, plus the one instruction
 * already past Read Operands behind it in the pipeline.  While the delay
 * is outstanding a cleared IMASK cannot yet let an interrupt in, and a
 * `jump` still reads its target register from dspPreStoreBank.  See the
 * D_FLAGS case in DSPWriteLong for the hardware citation. */
#define DSP_FLAGS_RETIRE_DELAY 2
static uint32_t dspFlagsRetireDelay = 0;
static uint32_t * dspPreStoreBank = NULL;



// Private function prototypes

void FlushDSPPipeline(void);


void dsp_reset_stats(void)
{
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
		uint16_t val;
		offset -= DSP_WORK_RAM_BASE;
		val = GET16(dsp_ram_8, offset);

		/* HLE sound-engine auto-ack (see DSPReadLong for details). */
		if (val != 0 && who == M68K && !DSP_RUNNING
				&& !vjs.useJaguarBIOS
				&& offset >= DSP_SOUND_CMD_BASE && offset < DSP_SOUND_CMD_END)
		{
			SET16(dsp_ram_8, offset, 0);
			return 0;
		}

		return val;
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
      uint32_t val;
      offset -= DSP_WORK_RAM_BASE;
      val = GET32(dsp_ram_8, offset);

      /* HLE sound-engine auto-ack:
       *
       * The real Jaguar BIOS loads a DSP sound engine into DSP RAM at
       * boot; that engine acknowledges command words by clearing flag
       * long words in the DSP_SOUND_CMD_BASE..DSP_SOUND_CMD_END region.
       * Cart-side audio code writes a non-zero command, then polls the
       * same word for it to go back to zero before issuing the next
       * one.  In HLE mode the BIOS engine isn't loaded and the DSP may
       * not even be running, so the cart spins forever polling
       * non-zero values.
       *
       * This is a workaround, NOT a real fix: it satisfies the polling
       * loop at the cost of dropping every command word the cart ever
       * writes (the engine never gets to mix anything because the
       * engine isn't there).  Audio is silent, but the game proceeds
       * past sound-init.
       *
       * Conditions:
       *   - val != 0    : only clear actual pending writes, not idle reads
       *   - who == M68K : only the cart's polling, not internal accesses
       *   - !DSP_RUNNING: the engine is absent / not consuming the cmd
       *   - !useJaguarBIOS: real BIOS path runs the engine, so don't
       *                     interfere there
       *   - offset in command range
       *
       * TODO(v2.3): remove this once the BIOS DSP audio engine is
       * properly replicated in HLE (Wolf3D / Skyhammer / IS2 family in
       * docs/emulation-bug-hunt-todos.md).  At that point the engine
       * will clear these words for real and we won't need the stub. */
      if (val != 0 && who == M68K && !DSP_RUNNING
            && !vjs.useJaguarBIOS
            && offset >= DSP_SOUND_CMD_BASE && offset < DSP_SOUND_CMD_END)
      {
         SET32(dsp_ram_8, offset, 0);
         return 0;
      }

      return val;
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
            /* HLE: When the 68K tight-polls DSPGO, auto-clear it.
             * The real BIOS+I2S infrastructure lets DSP programs
             * finish during SoundCallback; in HLE mode the DSP may
             * not terminate because it depends on BIOS-initialized
             * state.  A threshold of 8192 catches tight boot-time
             * poll loops (tens of thousands of reads/frame) while
             * ignoring normal gameplay status checks (~1-10/frame).
             *
             * Exception: if the DSP is actively producing audio
             * (non-zero LTXD/RTXD samples beyond the DACPrepareFrame
             * seed), it is running a legitimate audio mixer (e.g.
             * Doom) and must not be killed.  Issue #181 (Battle
             * Sphere): the silenced/escaped DSP still issues STORE
             * 0,RTXD per loop iteration, so we gate on non-zero
             * sample count, not raw write count. */
            if (who == M68K && DSP_RUNNING && !vjs.useJaguarBIOS)
            {
               /* "Real audio" gate: a DSP that's mixing for the user
                * writes non-zero LTXD/RTXD samples.  The non-zero
                * counter is reset to 0 at every DACPrepareFrame
                * (unlike i2sWriteCount which is seeded to 2 for the
                * resampler), so any non-zero sample this frame is
                * enough to declare the engine alive.  A DSP that
                * wrote only silence -- Battle Sphere with an escaped
                * DSP still issuing STORE 0,RTXD per loop iteration --
                * stays at 0 and the auto-clear correctly fires.
                * Counter ticks when either channel is non-zero. */
               if (DACGetI2SNonZeroCount() > 0)
                  dspgo_poll_count = 0;
               else
               {
                  dspgo_poll_count++;
                  if (dspgo_poll_count > DSPGO_POLL_THRESHOLD)
                  {
                     dsp_control &= ~0x01;
                     dspgo_poll_count = 0;
                  }
               }
            }
            else
               dspgo_poll_count = 0;
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
      /* CDDA-DIAG: see DSPWriteLong -- Primal Rage synth mailbox.
       * Rate-capped: other titles may use this RAM range as data. */
      if (offset >= 0xF1B270 && offset <= 0xF1B277)
      {
         static uint32_t mboxWrites = 0;
         mboxWrites++;
         if (mboxWrites <= 40 || (mboxWrites % 10000) == 0)
            LOG_DBG("[CDDA] DSP mailbox write.w $%06X = $%04X who=%u 68kpc=$%06X\n",
                    offset, data, who, m68k_get_reg(NULL, M68K_REG_PC));
         /* One-shot main-RAM snapshot at the mix-ON edge so the transient
          * music-player overlay around the writer PC can be disassembled.
          * Enabled only when VJ_CDDA_SNAPDIR is set (diagnostic builds). */
         if (offset == 0xF1B276 && data == 0x0001)
         {
            const char *dir = getenv("VJ_CDDA_SNAPDIR");
            static int snapped = 0;
            if (dir && !snapped)
            {
               char path[1024];
               FILE *f;
               snapped = 1;
               snprintf(path, sizeof(path), "%s/mixon_mainram.bin", dir);
               f = fopen(path, "wb");
               if (f)
               {
                  fwrite(jaguarMainRAM, 1, 0x200000, f);
                  fclose(f);
                  LOG_DBG("[CDDA] snapshot: %s\n", path);
               }
            }
         }
      }
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
      /* CDDA-DIAG: Primal Rage's synth-DSP command mailbox lives at
       * $F1B274 (cmd 1 = CD mix ON, 2 = OFF, 3 = reset, 4 = exit) --
       * log external writes so we can see who opens the mix gate.
       * Rate-capped: other titles may use this RAM range as data. */
      if (offset >= 0xF1B270 && offset <= 0xF1B277)
      {
         static uint32_t mboxWritesL = 0;
         mboxWritesL++;
         if (mboxWritesL <= 40 || (mboxWritesL % 10000) == 0)
            LOG_DBG("[CDDA] DSP mailbox write $%06X = $%08X who=%u 68kpc=$%06X\n",
                    offset, data, who, m68k_get_reg(NULL, M68K_REG_PC));
      }
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
               uint32_t * preWriteBank = dsp_reg;
               IMASKCleared = (dsp_flags & IMASK) && !(data & IMASK);
               dsp_flags = (data & ~IMASK) | ((data & IMASK) ? (dsp_flags & IMASK) : 0);
               dsp_flag_z = dsp_flags & 0x01;
               dsp_flag_c = (dsp_flags >> 1) & 0x01;
               dsp_flag_n = (dsp_flags >> 2) & 0x01;
               DSPUpdateRegisterBanks();
               /* A D_FLAGS store issued by the DSP itself does not retire
                * until the instruction behind it is already past Read
                * Operands.  JTRM (docs/jtrm-gpu-dsp.md, "Pipeline") gives
                * the RISC core four stages -- Decode, Read Operands,
                * Compute, Write-back -- so this store's write-back lands
                * a stage after the next instruction has latched its source
                * registers, and a whole stage after a branch has latched
                * the target register it needs to steer the fetch.  Two
                * consequences the register-bank pointer alone cannot
                * express, both recorded here and honoured by
                * dsp_opcode_jump and DSPExec:
                *
                *  - A `jump (Rn)` in the slot behind the store reads Rn
                *    from the PRE-store bank.  This is the ISR epilogue
                *    Wolfenstein 3D's I2S handler uses at $F1B24E:
                *        store  r0,(r1)     ; r1 = $F1A100, clears IMASK
                *        jump   (r3)        ; r3 = bank-0 return address
                *    with the main loop running in bank 1 (REGPAGE set).
                *    Swapping banks inside the store made `jump (r3)` read
                *    bank 1's uninitialised r3 == 0, so the DSP returned to
                *    PC $000000, left RAM and stopped feeding LTXD/RTXD --
                *    the game lost all audio from frame 48 on.
                *
                *  - IMASK is not really clear until the store retires
                *    either, so the instruction in that same slot still
                *    runs masked and cannot be preempted.  Wolf3D's outer
                *    epilogue at $F1B128-$F1B12A has the same store/jump
                *    shape; letting an I2S interrupt in between the two
                *    made the handler push the store's own address as the
                *    return address, so `jump (r4)` ran a slot later than
                *    it should have and read the wrong bank anyway.
                *
                * The bank pointer itself still swaps immediately, which is
                * what Doom's epilogue needs: it puts the D_FLAGS store in
                * the delay slot of `jump (r15)` at $F1B028, so the branch
                * target at $F1B6B2 -- a `movei` whose write-back follows
                * the store's -- must land in the NEW bank.
                *
                * CAVEAT on the derivation: the four-stage pipeline is
                * documented, but nothing in docs/jtrm-*.md describes the
                * taken-branch refill cost that lets a delay-slot store
                * retire before the branch target reads registers, and this
                * emulator's own dsp_opcode_cycles[] charges jump/jr only 1
                * cycle.  The rule above is inferred from behaviour: it is
                * the only composite that satisfies BOTH shipped epilogue
                * shapes -- Wolf3D's `store; jump (Rn)` needs the old bank
                * at the jump, Doom's `jump (Rn); store` needs the new bank
                * at the target -- with two independent code bases as the
                * evidence.  Revisit if primary JTRM pipeline timing ever
                * contradicts it. */
               if (who == DSP)
               {
                  dspPreStoreBank = preWriteBank;
                  dspFlagsRetireDelay = DSP_FLAGS_RETIRE_DELAY;
               }
               else
                  dspFlagsRetireDelay = 0;
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
               dspgo_poll_count = 0;
               // Check for DSP -> CPU interrupt
               if (data & CPUINT)
               {
                  if (JERRYIRQEnabled(IRQ2_DSP))
                  {
                     JERRYSetPendingIRQ(IRQ2_DSP);
                     DSPReleaseTimeslice();
                     /* JERRY has no wire of its own to the 68K: the DSP
                        interrupt merges with every other JERRY source
                        onto DINT, which enters TOM as INT1 bit 4
                        (C_JERENA).  The latch above stays outside this
                        gate, so a later C_JERENA write still delivers
                        via TOMWriteByte's newlyEnabled & TOMPendingMask()
                        path -- withheld, not lost.  Same gate as
                        JERRYPIT1Callback/JERRYPIT2Callback and
                        UARTRaiseIRQ. */
                     if (TOMIRQEnabled(IRQ_DSP))
                        m68k_set_irq(2);		// Set 68000 IPL 2...
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

/* Check for and handle any asserted DSP IRQs. */
void DSPHandleIRQsNP(void)
{
   uint32_t bits;
   uint32_t mask;
   int which = 0;									// Determine which interrupt
	if (dsp_flags & IMASK) 							// Bail if we're already inside an interrupt
		return;

	/* IMASK reads clear the moment the D_FLAGS store is decoded, but the
	 * store has not retired yet -- the instruction behind it is already
	 * past Read Operands and still runs masked (see DSPWriteLong).  This
	 * entry point is also reached asynchronously from DSPSetIRQLine, which
	 * cannot see DSPExec's own hold-off, so re-check here.  Leave the
	 * latch standing in dsp_control and IMASKCleared unconsumed: DSPExec
	 * dispatches it once the delay expires, at most two slots later.
	 *
	 * Without this, Wolfenstein 3D's I2S handler was re-entered between
	 * `store r0,(r1)` at $F1B24E and `jump (r3)` at $F1B250, so the nested
	 * handler returned to $F1B250 a slot late and `jump (r3)` read the
	 * post-store bank -- PC $000005, DSP off the rails at frame 2553. */
	if (dspFlagsRetireDelay && IMASKCleared)
		return;

	// Get the active interrupt bits (latches) & interrupt mask (enables)
	// INT_LAT5 is at dsp_control bit 16 (non-contiguous with LAT0-4 at bits 6-10)
	bits = ((dsp_control >> 11) & 0x20) | ((dsp_control >> 6) & 0x1F);
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
	/* Taking an interrupt flushes the pipeline, so an un-retired D_FLAGS
	 * store from before the vector fetch has no in-flight successor left
	 * to shadow -- retire it rather than let it reach into the handler. */
	dspFlagsRetireDelay = 0;
	DSPUpdateRegisterBanks();
	dspPreStoreBank = dsp_reg;


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
	uint32_t mask = (irqline < 5) ? (INT_LAT0 << irqline) : INT_LAT5;
	dsp_control &= ~mask;

	if (state)
	{
		dsp_control |= mask;
		DSPHandleIRQsNP();
	}
}

bool DSPIsRunning(void)
{
	return (DSP_RUNNING ? true : false);
}

uint8_t * DSPGetRAM(void)
{
	return dsp_ram_8;
}

uint32_t DSPGetFlags(void)
{
	return dsp_flags;
}

/* Write side of DSPGetFlags, added for the GDB stub (issue #652). A raw
 * poke of dsp_flags -- see GPUSetFlags in src/tom/gpu.c for why this
 * bypasses the D_FLAGS MMIO write path (interrupt-mask side effects a
 * debugger write should not trigger as a side effect of merely wanting
 * to see a different N/C/Z combination). */
void DSPSetFlags(uint32_t v)
{
	dsp_flags = v;
}

/* DSPGetPC/DSPSetPC, added for the GDB stub (issue #652): dsp_pc had no
 * accessor at all before this (GPU's equivalent, GPUGetPC, predates
 * vjtrace -- issue #406). Raw poke on the write side, like GPUSetPC. */
uint32_t DSPGetPC(void)
{
	return dsp_pc;
}

void DSPSetPC(uint32_t pc)
{
	dsp_pc = pc;
}

/* Diagnostic-only accessor, added for `monitor regs dsp` (the GDB stub,
 * issue #652): the raw D_CTRL value, read-only -- see GPUGetControl in
 * src/tom/gpu.c for why the write side is not exposed. */
uint32_t DSPGetControl(void)
{
	return dsp_control;
}

/* Originally vjtrace-only (#408) and gated behind VJ_TRACE, like GPU's
 * GPUGetReg was NOT (issue #406, pre-existing). The GDB stub (issue
 * #652) needs DSPGetReg in every build (shipped-by-default, off by
 * default -- see docs/gdb-stub-design.md), so it is unconditional now,
 * matching GPUGetReg. This does not change the shipped dylib's exported
 * symbol list -- production links still use exports.list, which never
 * listed either accessor. */
uint32_t DSPGetReg(int n)
{
	if (n < 0 || n > 31)
		return 0;
	return dsp_reg[n];
}

/* Write side of DSPGetReg, added for the GDB stub (issue #652). Pokes
 * the ACTIVE bank directly, mirroring GPUSetReg's reasoning in
 * src/tom/gpu.c: the $F1A000-range MMIO register window addresses both
 * banks unconditionally by register number, which is NOT what "the
 * current register file" means once REGPAGE has flipped. */
void DSPSetReg(int n, uint32_t v)
{
	if (n < 0 || n > 31)
		return;
	dsp_reg[n] = v;
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
	dspgo_poll_count	  = 0;
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
	dspSliceBudget		  = 0;
	dspSliceSpent		  = 0;

	dsp_reg = dsp_reg_bank_0;
	dsp_alternate_reg = dsp_reg_bank_1;

	for(i=0; i<32; i++)
		dsp_reg[i] = dsp_alternate_reg[i] = 0x00000000;

	CLR_ZNC;
	IMASKCleared = false;
	dspFlagsRetireDelay = 0;
	dspPreStoreBank = dsp_reg;
	FlushDSPPipeline();
	dsp_reset_stats();

	// Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents.
	// In HLE mode, zero-fill instead: the real BIOS loads a DSP sound engine that
	// initializes most of DSP RAM, and games may read locations they didn't write.
	if (vjs.useJaguarBIOS)
	{
		for(i=0; i<DSP_RAM_SIZE; i+=4)
		{
			uint32_t r = JaguarRand();
			memcpy(&dsp_ram_8[i], &r, sizeof(r));
		}
	}
	else
	{
		memset(dsp_ram_8, 0, DSP_RAM_SIZE);
	}
}

void DSPDone(void)
{
}

void DSPBeginSlice(uint32_t riscCycles)
{
	dspSliceBudget = (int32_t)riscCycles;
	dspSliceSpent  = 0;
}

int32_t DSPSliceRemaining(void)
{
	int32_t left = dspSliceBudget - dspSliceSpent;
	return (left > 0 ? left : 0);
}

/* Advance the DSP to the 68000's position inside the current slice.
 * Mirror of GPUSyncToM68K (issue #406 / PR #445): without this a 68K
 * poll of a DSP mailbox samples stale state from the previous slice,
 * which is #408 H3 and issue #456.  Same 2:1 RISC:68K clock ratio. */
void DSPSyncToM68K(void)
{
	int32_t target, run;

	if (!DSP_RUNNING || dsp_in_exec)
		return;

	target = (int32_t)(((int64_t)m68k_cycles_run() * 2 * riscClockScalePct)
	                   / m68kClockScalePct);
	if (target > dspSliceBudget)
		target = dspSliceBudget;

	run = target - dspSliceSpent;
	if (run <= 0)
		return;

	/* Mirror of GPUSyncToM68K's probe -- see perf_iface.h. */
	dspSliceSpent += run;
	VJP_ENTER(VJP_DSP_SYNC);
	DSPExec(run);
	VJP_LEAVE(VJP_DSP_SYNC);
}

/* ==================================================================
 * Cycle-exact idle-loop fast-forward   (issue #569, perf audit P1)
 * ==================================================================
 *
 * Commercial titles park the DSP in a 3-5 instruction wait loop for
 * 74-99.7% of every frame while it still burns its whole 26.6 MHz
 * budget interpreting that loop (docs/perf-audit-2026-08.md, P1).  This
 * skips the provably redundant iterations.  It is NOT an approximation:
 * the registers, the flags, the PC, the cycles charged and
 * dsp_exec_opcode_count all end up exactly where the interpreter would
 * have left them.  Run-ahead, netplay and savestates depend on that.
 *
 * ---- SAFETY THEOREM (verified against this tree, not assumed) -------
 *
 * (Line numbers below are pre-patch, i.e. as of the parent commit, so
 * they line up with the file this block was written against.)
 *
 * (1) Nothing else in the machine executes inside one DSPExec() call.
 *     The slice budget is "time until the next scheduled event":
 *     JaguarExecuteNew() (src/core/jaguar.c:1683-1752) computes
 *     GetTimeToNextEvent(), runs the 68K and the GPU, then calls
 *     DSPExec(DSPSliceRemaining()); SubtractEventTimes() and
 *     HandleNextEvent() only run AFTER DSPExec returns.  The OP runs
 *     from a halfline event, i.e. also outside.  DSPSyncToM68K()
 *     (dsp.c:1045-1066) is a different, EARLIER DSPExec call with its
 *     own budget, not a re-entry -- it bails on dsp_in_exec.
 *
 * (2) No DSP interrupt can be dispatched mid-call for an admitted body.
 *     NOTE: unlike GPUExec, DSPExec has no slice-entry DSPHandleIRQs
 *     call at all.  The only in-loop dispatch is the
 *     `IMASKCleared && dspFlagsRetireDelay == 0` re-check at the top of
 *     DSPExec's loop below, and IMASKCleared is set only by a
 *     store to D_FLAGS (dsp.c:698) -- which the admission rule excludes.
 *     The other entry point, DSPSetIRQLine -> DSPHandleIRQsNP
 *     (dsp.c:925-935), is only ever reached from event callbacks, i.e.
 *     between slices, per (1).  The probe additionally requires
 *     IMASKCleared == false and dspFlagsRetireDelay == 0, so neither
 *     the pending-IRQ path nor the D_FLAGS retire countdown is live.
 *
 * (3) The register banks cannot move.  dsp_reg / dsp_alternate_reg are
 *     repointed only by DSPUpdateRegisterBanks(), reachable from a
 *     D_FLAGS write (dsp.c:756) or from taking an interrupt
 *     (dsp.c:911) -- both excluded by (2).  The probe records the bank
 *     pointer at the first snapshot and re-checks it at the third.
 *
 * (4) DSP-side reads are pure.  The HLE sound-engine auto-ack in
 *     DSPReadWord/DSPReadLong (dsp.c:423/484) are gated `who == M68K`.
 *     The DSPGO poll auto-clear (dsp.c:526) tests `who == M68K` on the
 *     clearing branch but resets dspgo_poll_count on its `else`, so a
 *     DSP-side read of $F1A114 would touch that counter -- unreachable
 *     here, because the load-EA rule admits DSP local SRAM and DRAM
 *     only, never DSP_CONTROL_RAM_BASE.  Reads of
 *     main DRAM take JaguarReadLong's `addr < 0x800000` fast path
 *     (src/core/jaguar.c:1131-1141) whose only side effects are
 *     VJT_WATCH_RD (compiled out unless VJ_TRACE) and BlitMemoNoteRead
 *     (gated on blitMemoRecording); both are hard gates below.
 *     busArbiter is charged there only for `who == OP`.
 *
 * Therefore: if a loop body performs no store and reads only plain RAM,
 * every value it reads is constant for the remainder of the call, and
 * one iteration is a pure function of (register file, flags).
 *
 * NOTE the premise is about the instructions the machine ACTUALLY
 * executed between two arrivals at the loop head, not about the ones the
 * decoder walked.  Those are not the same thing for free: the probe is
 * only hooked on a taken backward jr, so a not-taken jr, a `jump` or
 * plain fall-through can carry execution back to `head` without the
 * probe ever seeing it.  The opcode-count identity in the proof below
 * (opcost == idleBodyCount + 1) is what closes that gap; without it a
 * compound period containing an undecoded store could satisfy every
 * other condition here.
 *
 * ---- ADMISSION RULE ------------------------------------------------
 *
 * Candidate: a TAKEN backward (or self-targeting) `jr` whose target is
 * at most 8 words behind, with the whole body inside DSP local RAM.
 * Body = target .. jr inclusive, plus the inlined delay-slot
 * instruction.  Static decode (register-independent) requires:
 *   - every opcode on the whitelist below (no store of any kind, no
 *     accumulator/latency opcode, no second branch);
 *   - the decode walk lands exactly on the jr, so the body really is
 *     straight-line (movei carries a 32-bit immediate, i.e. 3 words);
 *   - no movei in the delay slot (its immediate would be fetched from
 *     past the branch).
 * Register-dependent checks are deliberately deferred to the third
 * snapshot: control can enter a loop body part-way through, so at the
 * first snapshot a register written only in the early part of the body
 * may still hold pre-loop garbage, and a load EA computed from it would
 * be meaningless.  After two complete iterations the head state is
 * steady-state and the checks are authoritative.
 *
 * ---- FIXED-POINT PROOF ---------------------------------------------
 *
 * Snapshots S0/S1/S2 are taken at the loop head (immediately after the
 * taken jr's delay slot) on three consecutive arrivals, driven from
 * DSPExec's own loop -- the probe iterations are ordinary interpreted
 * execution, so no semantics are duplicated anywhere.  Extrapolate only
 * when all of:
 *   - the branch was taken all three times and landed on the same head
 *     from the same jr (a different loop appearing mid-probe aborts it);
 *   - elementwise S2-S1 == S1-S0 across BOTH register banks;
 *   - dsp_flag_z/n/c identical in S0, S1 and S2;
 *   - the measured cycle cost of iteration 1 equals iteration 2 (taken
 *     as the delta of `cycles`, never recomputed from
 *     dsp_opcode_cycles[] -- the inlined delay slot is not charged);
 *   - likewise the measured dsp_exec_opcode_count delta;
 *   - that measured opcode delta equals idleBodyCount + 1 EXACTLY, which
 *     is the count only a straight-line traversal of the decoded body
 *     can produce -- this is the executed-path check, see the comment at
 *     the test itself;
 *   - every register with a NONZERO per-iteration delta is read and
 *     written only by an addqt/subqt whose destination is that same
 *     register.
 *
 * That last rule is what makes the branch unable to flip.  It is
 * strictly stronger than "the last flag-setting instruction reads only
 * zero-delta registers": addqt/subqt are the only admitted opcodes that
 * write no flags (dsp_opcode_addqt/subqt above), so any flag-setting
 * instruction is forced to read zero-delta operands, AND so is every
 * load base and every shift/rotate count.  Proof that the loop is then
 * affine forever: memory is constant by the theorem, so with all
 * zero-delta registers holding their S1 values and the flags holding
 * their S1 values, every instruction that does not touch a counter
 * register recomputes exactly the iteration-1 result -- hence flags and
 * zero-delta registers stay put -- while each counter register is only
 * ever incremented by a constant.  So iteration k's state is
 * S1 + (k-1)*delta for all k, and the branch condition never changes.
 *
 * Then n = (cycles_remaining / cost) - 1 (always leave one full
 * iteration to execute normally), reg += n*delta, cycles -= n*cost,
 * dsp_exec_opcode_count += n*opcodes (crash_detect's wedge predicate
 * reads that counter -- src/core/crash_detect.c:470-482 -- so a skipped
 * loop must still look like progress).  Flags and PC are already
 * correct by construction.  Nothing else is touched.
 *
 * Exit is automatic: the I2S/timer callback that eventually changes the
 * polled location runs between slices, the next probe fails, and normal
 * interpretation resumes.  No savestate impact -- every byte of probe
 * state is re-derived from scratch inside each DSPExec call.
 */

/* Longest body accepted, in 16-bit words (jr offset range [-8,-1]).
 * Belt-and-braces only: the caller's `pcThis - dsp_pc <= 14` filter
 * already caps [head, jr) at 7 words, so this bound never binds. */
#define DSP_IDLE_MAX_BODY	8
/* Body instruction slots: <= 7 before the jr, plus the delay slot. */
#define DSP_IDLE_MAX_INSN	10
/* Per-slice reject memo: without it a loop that fails the fixed-point
 * test would be re-probed every three iterations for the whole slice. */
#define DSP_IDLE_MEMO		16

/* Effective addresses we will admit for a load: DSP local SRAM (the
 * range DSPReadLong itself serves out of dsp_ram_8 -- $F1B000-$F1CFFF,
 * note this is the full 8K, not the $F1BFFF the task brief quoted) or
 * main DRAM below the 2 MB aperture, where JaguarReadLong is a plain
 * GET32 of jaguarMainRAM. */
#define DSP_IDLE_EA_OK(ea) \
	(((ea) >= DSP_WORK_RAM_BASE && (ea) <= DSP_WORK_RAM_BASE + 0x1FFF) \
	 || ((ea) < 0x200000))

#define DSP_IDLE_FETCH(a) \
	((uint16_t)(((uint16_t)dsp_ram_8[(a) - DSP_WORK_RAM_BASE] << 8) \
	            | (uint16_t)dsp_ram_8[(a) - DSP_WORK_RAM_BASE + 1]))

/* Option gate (libretro `virtualjaguar_risc_idle_skip`) lives in vjs. */
/* Diagnostics: counted only on the cold probe path, never per opcode. */
uint32_t dsp_idle_skip_fires   = 0;		/* successful extrapolations */
uint32_t dsp_idle_skip_rejects = 0;		/* candidate loops turned down */
uint32_t dsp_idle_skip_iters   = 0;		/* iterations actually skipped */
uint32_t dsp_idle_skip_opcodes = 0;		/* opcodes NOT interpreted -- the
										 * honest, host-independent measure:
										 * dsp_exec_opcode_count is advanced
										 * over a skip on purpose, so it does
										 * NOT show the saving. */

static int       idleProbeStage;		/* 0 = none, 1 = have S0, 2 = have S1 */
static uint32_t  idleProbeHead;
static uint32_t  idleProbeJr;
static uint32_t *idleProbeBank;			/* dsp_reg at S0 -- see theorem (3) */
static int32_t   idleProbeCyc0, idleProbeCyc1;
static uint32_t  idleProbeOpc0, idleProbeOpc1;
static uint32_t  idleProbeS0[64], idleProbeS1[64];
static uint8_t   idleProbeFz0, idleProbeFn0, idleProbeFc0;
static uint8_t   idleProbeFz1, idleProbeFn1, idleProbeFc1;

static uint8_t   idleBodyIdx[DSP_IDLE_MAX_INSN];
static uint8_t   idleBodyP1[DSP_IDLE_MAX_INSN];
static uint8_t   idleBodyP2[DSP_IDLE_MAX_INSN];
static uint32_t  idleBodyImm[DSP_IDLE_MAX_INSN];
static int       idleBodyCount;

static uint32_t  idleMemoHead[DSP_IDLE_MEMO];
static uint32_t  idleMemoJr[DSP_IDLE_MEMO];
static int       idleMemoCount;
static int       idleMemoNext;

/* Opcode whitelist.  Admit only what has been reasoned about; anything
 * absent is rejected, including every store (45/46/47/49/50/60/61), the
 * accumulator and long-latency opcodes (16-21, 23, 26, 54-56, 63, 32)
 * and both branches (52/53 -- the loop's own jr is handled separately).
 * sat32s (42) is rejected because it reads dsp_acc, which the snapshot
 * does not model; sat16s (33) is a pure RN->RN saturate and is admitted.
 * mirror (48) and move_pc (51) are pure too but are left out to keep the
 * whitelist to opcodes the audit actually observed in wait loops. */
static int dsp_idle_op_admitted(uint32_t idx)
{
	switch (idx)
	{
	case  0: case  1: case  2: case  3:		/* add addc addq addqt */
	case  4: case  5: case  6: case  7:		/* sub subc subq subqt */
	case  8: case  9: case 10: case 11:		/* neg and or xor */
	case 12: case 13: case 14: case 15:		/* not btst bset bclr */
	case 22:								/* abs */
	case 24: case 25: case 27:				/* shlq shrq sharq */
	case 28: case 29:						/* ror rorq */
	case 30: case 31:						/* cmp cmpq */
	case 33:								/* sat16s */
	case 34: case 35: case 36: case 37:		/* move moveq moveta movefa */
	case 38:								/* movei */
	case 39: case 40: case 41:				/* loadb loadw load */
	case 43: case 44:						/* load_r14/15_indexed */
	case 57:								/* nop */
	case 58: case 59:						/* load_r14/15_ri */
		return 1;
	/* 52 (jump) and 53 (jr) are deliberately absent: excluding every
	 * PC-modifying opcode but the loop-closing jr itself is load-bearing
	 * for the executed-path check below (idleBodyCount forces exactly
	 * one route from head to jrAddr), not only for the store-freedom
	 * this whitelist was originally written to guarantee. */
	default:
		return 0;
	}
}

/* Register operands of an admitted opcode, as indices into the 64-entry
 * snapshot (0..31 = dsp_reg_bank_0, 32..63 = dsp_reg_bank_1).  `cur` is
 * the base of the bank dsp_reg points at, `alt` the other one -- the
 * bank split matters: moveta writes ALTERNATE_RN and movefa reads
 * ALTERNATE_RM, which is exactly what Iron Soldier's wait loop polls.
 * *dst is -1 when the instruction writes no register. */
static int dsp_idle_operands(uint32_t idx, uint32_t p1, uint32_t p2,
                             int cur, int alt, int *src, int *nsrc, int *dst)
{
	*nsrc = 0;
	*dst  = -1;

	switch (idx)
	{
	case  0: case  1: case  4: case  5:		/* add addc sub subc */
	case  9: case 10: case 11:				/* and or xor */
	case 28:								/* ror  (RM = count) */
		src[(*nsrc)++] = cur + (int)p1;
		src[(*nsrc)++] = cur + (int)p2;
		*dst = cur + (int)p2;
		return 1;
	case 30:								/* cmp -- flags only */
		src[(*nsrc)++] = cur + (int)p1;
		src[(*nsrc)++] = cur + (int)p2;
		return 1;
	case  2: case  3: case  6: case  7:		/* addq addqt subq subqt */
	case  8: case 12: case 14: case 15:		/* neg not bset bclr */
	case 22: case 24: case 25: case 27:		/* abs shlq shrq sharq */
	case 29: case 33:						/* rorq sat16s */
		src[(*nsrc)++] = cur + (int)p2;
		*dst = cur + (int)p2;
		return 1;
	case 13: case 31:						/* btst cmpq -- flags only */
		src[(*nsrc)++] = cur + (int)p2;
		return 1;
	case 34:								/* move   RN = RM */
	case 39: case 40: case 41:				/* loadb loadw load: RM = base */
		src[(*nsrc)++] = cur + (int)p1;
		*dst = cur + (int)p2;
		return 1;
	case 35: case 38:						/* moveq / movei -- immediate */
		*dst = cur + (int)p2;
		return 1;
	case 36:								/* moveta  ALT_RN = RM */
		src[(*nsrc)++] = cur + (int)p1;
		*dst = alt + (int)p2;
		return 1;
	case 37:								/* movefa  RN = ALT_RM */
		src[(*nsrc)++] = alt + (int)p1;
		*dst = cur + (int)p2;
		return 1;
	case 43:								/* load_r14_indexed */
		src[(*nsrc)++] = cur + 14;
		*dst = cur + (int)p2;
		return 1;
	case 44:								/* load_r15_indexed */
		src[(*nsrc)++] = cur + 15;
		*dst = cur + (int)p2;
		return 1;
	case 58:								/* load_r14_ri */
		src[(*nsrc)++] = cur + 14;
		src[(*nsrc)++] = cur + (int)p1;
		*dst = cur + (int)p2;
		return 1;
	case 59:								/* load_r15_ri */
		src[(*nsrc)++] = cur + 15;
		src[(*nsrc)++] = cur + (int)p1;
		*dst = cur + (int)p2;
		return 1;
	case 57:								/* nop */
		return 1;
	default:
		return 0;
	}
}

static int dsp_idle_memo_hit(uint32_t head, uint32_t jr)
{
	int i;

	for (i = 0; i < idleMemoCount; i++)
		if (idleMemoHead[i] == head && idleMemoJr[i] == jr)
			return 1;
	return 0;
}

static void dsp_idle_memo_reject(uint32_t head, uint32_t jr)
{
	if (dsp_idle_memo_hit(head, jr))
		return;
	idleMemoHead[idleMemoNext] = head;
	idleMemoJr[idleMemoNext]   = jr;
	idleMemoNext = (idleMemoNext + 1) % DSP_IDLE_MEMO;
	if (idleMemoCount < DSP_IDLE_MEMO)
		idleMemoCount++;
	dsp_idle_skip_rejects++;
}

/* Register-independent decode of target..jr plus the delay slot. */
static int dsp_idle_decode(uint32_t head, uint32_t jrAddr)
{
	uint32_t pc = head;
	uint16_t op;
	uint32_t idx;
	int n = 0;

	idleBodyCount = 0;

	while (pc < jrAddr)
	{
		if (n >= DSP_IDLE_MAX_BODY)
			return 0;
		op  = DSP_IDLE_FETCH(pc);
		idx = (uint32_t)(op >> 10);
		if (!dsp_idle_op_admitted(idx))
			return 0;
		idleBodyIdx[n] = (uint8_t)idx;
		idleBodyP1[n]  = (uint8_t)((op >> 5) & 0x1F);
		idleBodyP2[n]  = (uint8_t)(op & 0x1F);
		idleBodyImm[n] = 0;
		if (idx == 38)						/* movei: opcode + LSW + MSW */
		{
			if (pc + 6 > jrAddr)			/* immediate would overrun the jr */
				return 0;
			idleBodyImm[n] = (uint32_t)DSP_IDLE_FETCH(pc + 2)
			               | ((uint32_t)DSP_IDLE_FETCH(pc + 4) << 16);
			pc += 6;
		}
		else
			pc += 2;
		n++;
	}

	if (pc != jrAddr)						/* decode fell out of step */
		return 0;

	/* The inlined delay slot at jr+2 runs on every iteration too.  A
	 * movei there would fetch its immediate from past the branch, so
	 * reject it rather than model it. */
	op  = DSP_IDLE_FETCH(jrAddr + 2);
	idx = (uint32_t)(op >> 10);
	if (idx == 38 || !dsp_idle_op_admitted(idx))
		return 0;
	idleBodyIdx[n] = (uint8_t)idx;
	idleBodyP1[n]  = (uint8_t)((op >> 5) & 0x1F);
	idleBodyP2[n]  = (uint8_t)(op & 0x1F);
	idleBodyImm[n] = 0;
	n++;

	idleBodyCount = n;
	return 1;
}

/* Register-dependent half of the admission rule, run at S2 where the
 * head state is provably steady.  Walks the body tracking each
 * register's known value so a load base written earlier in the same body
 * (Doom's `movei #addr,r2 ; load (r2),r1`) resolves to the address the
 * load will really use, not to whatever the head snapshot held. */
static int dsp_idle_check_body(const uint32_t *delta, int cur, int alt)
{
	uint32_t kval[64];
	uint8_t  kok[64];
	int src[3];
	int nsrc, dst, s, j, i;
	uint32_t idx, p1, p2, ea;

	for (i = 0; i < 32; i++)
	{
		kval[i]      = dsp_reg_bank_0[i];
		kval[32 + i] = dsp_reg_bank_1[i];
		kok[i]       = 1;
		kok[32 + i]  = 1;
	}

	for (j = 0; j < idleBodyCount; j++)
	{
		idx = idleBodyIdx[j];
		p1  = idleBodyP1[j];
		p2  = idleBodyP2[j];

		if (!dsp_idle_operands(idx, p1, p2, cur, alt, src, &nsrc, &dst))
			return 0;

		/* A register whose per-iteration delta is nonzero is a pure
		 * counter: it may only be read, and only be written, by an
		 * addqt/subqt targeting itself.  Those two are the only
		 * admitted opcodes that touch no flag, so this forbids a
		 * growing value from reaching a compare, a load address, a
		 * shift count or anything else. */
		for (s = 0; s < nsrc; s++)
			if (delta[src[s]] != 0
			    && !((idx == 3 || idx == 7) && src[s] == dst))
				return 0;
		if (dst >= 0 && delta[dst] != 0 && !(idx == 3 || idx == 7))
			return 0;

		/* Loads: the effective address must be provably plain RAM. */
		switch (idx)
		{
		case 39:							/* loadb */
		case 40:							/* loadw */
			/* Outside local RAM these divert to JaguarReadByte /
			 * JaguarReadWord, whose full TOM/JERRY decode is not
			 * audited here; inside it they degenerate to a pure
			 * DSPReadLong of the containing long. */
			if (!kok[cur + (int)p1])
				return 0;
			ea = kval[cur + (int)p1];
			if (!(ea >= DSP_WORK_RAM_BASE && ea <= DSP_WORK_RAM_BASE + 0x1FFF))
				return 0;
			break;
		case 41:							/* load */
			if (!kok[cur + (int)p1])
				return 0;
			ea = kval[cur + (int)p1] & 0xFFFFFFFC;
			if (!DSP_IDLE_EA_OK(ea))
				return 0;
			break;
		case 43:							/* load_r14_indexed */
			if (!kok[cur + 14])
				return 0;
			ea = (kval[cur + 14] & 0xFFFFFFFC)
			   + (dsp_convert_zero[p1] << 2);
			if (!DSP_IDLE_EA_OK(ea))
				return 0;
			break;
		case 44:							/* load_r15_indexed */
			if (!kok[cur + 15])
				return 0;
			ea = (kval[cur + 15] & 0xFFFFFFFC)
			   + (dsp_convert_zero[p1] << 2);
			if (!DSP_IDLE_EA_OK(ea))
				return 0;
			break;
		case 58:							/* load_r14_ri */
			if (!kok[cur + 14] || !kok[cur + (int)p1])
				return 0;
			ea = (kval[cur + 14] + kval[cur + (int)p1]) & 0xFFFFFFFC;
			if (!DSP_IDLE_EA_OK(ea))
				return 0;
			break;
		case 59:							/* load_r15_ri */
			if (!kok[cur + 15] || !kok[cur + (int)p1])
				return 0;
			ea = (kval[cur + 15] + kval[cur + (int)p1]) & 0xFFFFFFFC;
			if (!DSP_IDLE_EA_OK(ea))
				return 0;
			break;
		default:
			break;
		}

		/* Propagate what we can still prove about the destination. */
		if (dst >= 0)
		{
			switch (idx)
			{
			case 38:						/* movei -- 32-bit immediate */
				kval[dst] = idleBodyImm[j];
				kok[dst]  = 1;
				break;
			case 35:						/* moveq -- RN = IMM_1 */
				kval[dst] = p1;
				kok[dst]  = 1;
				break;
			case 34: case 36: case 37:		/* move / moveta / movefa */
				kval[dst] = kval[src[0]];
				kok[dst]  = kok[src[0]];
				break;
			default:
				kok[dst] = 0;
				break;
			}
		}
	}

	return 1;
}

static void dsp_idle_snapshot(uint32_t *s)
{
	memcpy(s,      dsp_reg_bank_0, 32 * sizeof(uint32_t));
	memcpy(s + 32, dsp_reg_bank_1, 32 * sizeof(uint32_t));
}

/* Give up on an in-flight probe.  When the loop that displaced it is a
 * different one, memo the abandoned loop: otherwise two interleaved
 * loops could restart each other forever without either reaching S2. */
static void dsp_idle_probe_abandon(uint32_t head, uint32_t jr)
{
	if (idleProbeStage != 0
	    && (idleProbeHead != head || idleProbeJr != jr))
		dsp_idle_memo_reject(idleProbeHead, idleProbeJr);
	idleProbeStage = 0;
}

/* Called from DSPExec immediately after a taken backward/self `jr`, with
 * dsp_pc already at the loop head and the delay slot already executed.
 * Returns the (possibly advanced) cycle budget. */
static int32_t DSPIdleLoopProbe(int32_t cycles, uint32_t head, uint32_t jrAddr)
{
	uint32_t delta[64];
	int32_t  cost, n;
	uint32_t opcost;
	int      cur, alt, i;

	/* Whole body -- including the delay slot and its trailing byte --
	 * must sit in DSP local SRAM, so every fetch is a pure dsp_ram_8
	 * read and the PC-escape check in DSPExec is a no-op for it. */
	if (head < DSP_WORK_RAM_BASE
	    || jrAddr + 3 > DSP_WORK_RAM_BASE + 0x1FFF)
	{
		dsp_idle_probe_abandon(head, jrAddr);
		return cycles;
	}

	/* Theorem (2): a pending IMASK clear or an un-retired D_FLAGS store
	 * means interrupt state is in flight; do not extrapolate over it.
	 * Both are transient (DSPExec dispatches the pending IRQ on its very
	 * next iteration), so this is a reset, not a permanent reject. */
	if (IMASKCleared || dspFlagsRetireDelay)
	{
		dsp_idle_probe_abandon(head, jrAddr);
		return cycles;
	}

	/* A different loop showed up mid-probe.  Only then -- calling this
	 * unconditionally would reset the probe of the loop we are actually
	 * in the middle of measuring, and nothing would ever reach S2. */
	if (idleProbeStage != 0
	    && (idleProbeHead != head || idleProbeJr != jrAddr))
		dsp_idle_probe_abandon(head, jrAddr);

	if (dsp_idle_memo_hit(head, jrAddr))
		return cycles;

	if (idleProbeStage == 0)
	{
		if (!dsp_idle_decode(head, jrAddr))
		{
			dsp_idle_memo_reject(head, jrAddr);
			return cycles;
		}
		dsp_idle_snapshot(idleProbeS0);
		idleProbeFz0   = dsp_flag_z;
		idleProbeFn0   = dsp_flag_n;
		idleProbeFc0   = dsp_flag_c;
		idleProbeCyc0  = cycles;
		idleProbeOpc0  = dsp_exec_opcode_count;
		idleProbeHead  = head;
		idleProbeJr    = jrAddr;
		idleProbeBank  = dsp_reg;
		idleProbeStage = 1;
		return cycles;
	}

	if (idleProbeStage == 1)
	{
		dsp_idle_snapshot(idleProbeS1);
		idleProbeFz1   = dsp_flag_z;
		idleProbeFn1   = dsp_flag_n;
		idleProbeFc1   = dsp_flag_c;
		idleProbeCyc1  = cycles;
		idleProbeOpc1  = dsp_exec_opcode_count;
		idleProbeStage = 2;
		return cycles;
	}

	/* Stage 2: the live machine state is S2. */
	idleProbeStage = 0;

	/* Flags must have been identical at all THREE loop heads.  Comparing
	 * only S0 against S2 would admit a two-iteration oscillation whose
	 * third iteration behaves like neither probe. */
	if (idleProbeFz1 != idleProbeFz0 || idleProbeFn1 != idleProbeFn0
	    || idleProbeFc1 != idleProbeFc0
	    || dsp_flag_z != idleProbeFz0 || dsp_flag_n != idleProbeFn0
	    || dsp_flag_c != idleProbeFc0)
	{
		dsp_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Theorem (3): the bank pointers must not have moved. */
	if (dsp_reg != idleProbeBank)
	{
		dsp_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Cost and opcode count measured, never recomputed: the inlined
	 * delay slot is not charged by DSPExec's own `cycles -=`. */
	cost = idleProbeCyc0 - idleProbeCyc1;
	if (cost <= 0 || (idleProbeCyc1 - cycles) != cost)
	{
		dsp_idle_memo_reject(head, jrAddr);
		return cycles;
	}
	opcost = idleProbeOpc1 - idleProbeOpc0;
	if (opcost == 0 || (dsp_exec_opcode_count - idleProbeOpc1) != opcost)
	{
		dsp_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* EXECUTED-PATH CHECK.  Everything above pins what the *decoded* body
	 * would do; this is what pins that the machine actually walked it.
	 *
	 * The probe only fires on a TAKEN backward jr, so an arrival at the
	 * loop head says nothing about how the previous arrival got here: a
	 * NOT-taken jr, a `jump`, or fall-through all reach `head` without
	 * ever entering this function.  A compound period that happens to
	 * have the same net register/flag effect -- e.g. a counter that
	 * cycles 2 -> 1 (taken) -> 0 (not taken) -> a fall-through path that
	 * resets it, containing an undecoded `store` -- would otherwise pass
	 * every check above, because the decoded portion really is pure and
	 * the deltas really are constant.  Eliding `n` copies of that store
	 * (I2S LTXD/RTXD, a blitter command, a latch clear) is exactly the
	 * silent divergence this whole design exists to prevent.
	 *
	 * dsp_exec_opcode_count is incremented in three reachable places --
	 * DSPExec's main loop, and the inlined delay slots in
	 * dsp_opcode_jump and dsp_opcode_jr (a fourth site exists in the
	 * pipelined DSP_jr, but that executor is dead code -- DSPExecP/
	 * DSPExecP2 are declared and never defined) -- so one traversal of
	 * a branch-free admitted body charges, deterministically:
	 *
	 *     (idleBodyCount - 1)   body instructions before the jr
	 *   + 1                     the loop-closing jr itself
	 *   + 1                     its inlined delay slot
	 *   = idleBodyCount + 1
	 *
	 * (idleBodyCount counts the decoded [head, jr) instructions -- movei
	 * once, not three times -- plus the delay slot; the jr is not in the
	 * array.)  Any other route from one arrival to the next must execute
	 * the whole body AND at least one further instruction to get back,
	 * so it costs strictly more.  Requiring exact equality therefore
	 * admits the straight-line traversal and nothing else. */
	if (opcost != (uint32_t)idleBodyCount + 1)
	{
		dsp_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Per-iteration register delta must be constant across both probes. */
	for (i = 0; i < 32; i++)
	{
		delta[i]      = idleProbeS1[i] - idleProbeS0[i];
		delta[32 + i] = idleProbeS1[32 + i] - idleProbeS0[32 + i];
		if (dsp_reg_bank_0[i] - idleProbeS1[i] != delta[i])
		{
			dsp_idle_memo_reject(head, jrAddr);
			return cycles;
		}
		if (dsp_reg_bank_1[i] - idleProbeS1[32 + i] != delta[32 + i])
		{
			dsp_idle_memo_reject(head, jrAddr);
			return cycles;
		}
	}

	cur = (dsp_reg == dsp_reg_bank_0) ? 0 : 32;
	alt = 32 - cur;

	if (!dsp_idle_check_body(delta, cur, alt))
	{
		dsp_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Always leave one full iteration to execute normally. */
	n = (cycles / cost) - 1;
	if (n <= 0)
		return cycles;

	for (i = 0; i < 32; i++)
	{
		if (delta[i])
			dsp_reg_bank_0[i] += (uint32_t)n * delta[i];
		if (delta[32 + i])
			dsp_reg_bank_1[i] += (uint32_t)n * delta[32 + i];
	}
	cycles -= n * cost;
	dsp_exec_opcode_count += (uint32_t)n * opcost;

	dsp_idle_skip_fires++;
	dsp_idle_skip_iters   += (uint32_t)n;
	dsp_idle_skip_opcodes += (uint32_t)n * opcost;

	return cycles;
}

/* DSP execution core */

void DSPExec(int32_t cycles)
{
	int idleSkipActive;
	int gdbArmedSlice;

#ifdef DSP_SINGLE_STEPPING
	if (dsp_control & 0x18)
	{
		cycles = 1;
		dsp_control &= ~0x10;
	}
#endif
	/* No return between here and VJP_LEAVE (perf_iface.h). */
	VJP_ENTER(VJP_DSP);

	dsp_releaseTimeSlice_flag = 0;

	/* Hot global read once per emulated instruction, with an opaque
	 * opcode call downstream, so without a local the compiler reloads it
	 * GOT-indirect every opcode -- the same shape issue #532 fixed for
	 * GPUExec's pipeTiming/riscScale. Measured at ~4.5% (issue #652).
	 * Only GDBHalt() can change the arming inside a slice, so the
	 * refresh below it is the only one needed. */
	gdbArmedSlice = gdbArmedDSP;
	dsp_in_exec++;

	/* Idle-loop fast-forward gates (issue #569).  Every one of these adds
	 * per-instruction state the affine extrapolation does not model, so
	 * any of them turns the whole thing off:
	 *   - vjs.riscIdleSkip: the user-facing core option itself;
	 *   - blitMemoMode / blitMemoRecording: the JaguarRead and JaguarWrite
	 *     families note reads and writes for the memo (blit_memo.h:64-65,
	 *     src/core/jaguar.c:1139), and skipping loads would change what
	 *     the memo records;
	 *   - riscClockScalePct != 100: strictly speaking this one is
	 *     conservative rather than necessary -- SCALE_RISC_CYCLES is
	 *     applied where the budget is handed out (jaguar.c), not inside
	 *     DSPExec, so `cycles` here is already in the same currency the
	 *     measured per-iteration cost was charged in.  It stays because
	 *     the overclock presets (#378) are themselves an accuracy
	 *     trade-off already under investigation, and stacking a second
	 *     non-stock execution path underneath them is not something this
	 *     change should do unasked;
	 *   - busArbiter.enabled (the dram_timing option): bus occupancy is
	 *     accounted per access.  Belt-and-braces here -- dsp.c never
	 *     touches busArbiter and JaguarReadLong charges it only for
	 *     `who == OP` -- but a DSP-side arbiter charge added later must
	 *     not silently invalidate this;
	 *   - vjs.gpuPipelineTiming: the DSP has no pipeline-timing mode of
	 *     its own (DSPExecP/DSPExecP2 are declared in dsp.h but never
	 *     called), and this is the only pipeline-timing switch the
	 *     frontend exposes -- a user who turns it on has asked for
	 *     accuracy over speed, so honour it for the DSP too;
	 *   - DSP_CTRL single-step / single-go: the slice is one instruction;
	 *   - vjtrace armed or a memory watch installed (VJ_TRACE builds
	 *     only): VJT_PCHIST_DSP in this loop and VJT_WATCH_RD on the
	 *     DRAM read path are per-instruction / per-read side effects.
	 *     This has to be a RUNTIME check, not `#ifdef VJ_TRACE`: the
	 *     whole test ABI is built with -DVJ_TRACE (Makefile:158-161),
	 *     so a compile-time gate would make every headless harness --
	 *     including the A/B that proves this exact -- silently measure
	 *     a disabled feature.  Both flags default off, so the cost is
	 *     two loads per slice on trace builds and nothing at all on
	 *     release builds. */
	idleSkipActive = vjs.riscIdleSkip
	              && !blitMemoMode && !blitMemoRecording
	              && riscClockScalePct == 100
	              && !busArbiter.enabled
	              && !vjs.gpuPipelineTiming
	              && !(dsp_control & 0x18);
#ifdef VJ_TRACE
	if (vjtrace_armed || vjtrace_nwatch)
		idleSkipActive = 0;
#endif
#if !defined(VJ_GDB_STUB_DISABLE_HOOKS) && !defined(VJ_GDB_STUB_DISABLE_IDLE_GATE)
	/* GDB stub (issue #652): DSPIdleLoopProbe() extrapolates the PC
	 * forward over many idle-loop iterations without visiting each one,
	 * exactly the class of per-instruction side effect the #569 idle-skip
	 * gate list above already disables for (blit memo, busArbiter,
	 * vjtrace watch). A DSP breakpoint or pending step would be stepped
	 * clean over. This is a RUNTIME check, same reasoning as the VJ_TRACE
	 * block above: gdbArmedDSP defaults to 0 and costs one load + branch
	 * when nothing is armed. VJ_GDB_STUB_DISABLE_HOOKS exists only for
	 * the Phase 2 A/B perf measurement. */
	if (gdbArmedDSP)
		idleSkipActive = 0;
#endif
	/* Probe + reject memo are re-derived from scratch every call, so
	 * nothing here reaches a savestate and nothing survives a slice. */
	idleProbeStage = 0;
	idleMemoCount  = 0;
	idleMemoNext   = 0;

#ifdef VJ_GDB_STUB_DISABLE_HOOKS
	while (cycles > 0 && DSP_RUNNING)
	{
		uint16_t opcode;
		uint32_t index;
		uint32_t pcThis;
#include "dsp_exec_body.h"
	}
#else
	if (!gdbArmedSlice)
	{
		while (cycles > 0 && DSP_RUNNING)
		{
		uint16_t opcode;
		uint32_t index;
		uint32_t pcThis;
#include "dsp_exec_body.h"
		}
		/* No re-arm check here, deliberately: it would be a global load per
		 * emulated instruction, the exact cost this specialisation removes.
		 * Nothing can arm mid-slice in this loop -- the GDB service loop runs
		 * between retro_run() calls, and GDBHalt() cannot fire from a loop
		 * that carries no check. */
	}

	if (gdbArmedSlice)
	{
		while (cycles > 0 && DSP_RUNNING)
		{
		uint16_t opcode;
		uint32_t index;
		uint32_t pcThis;

			if (GDBCheckPC(GDB_TGT_DSP, dsp_pc))
			{
				GDBHalt(GDB_TGT_DSP, GDB_STOP_BREAKPOINT, dsp_pc);
				gdbArmedSlice = gdbArmedDSP;
			}
#include "dsp_exec_body.h"
		}
	}
#endif

	dsp_in_exec--;

	VJP_LEAVE(VJP_DSP);
}

INLINE static void dsp_executeOpcode(uint32_t index)
{
#ifdef __GNUC__
	/* Computed-goto dispatch table -- one label per RISC opcode.
	 * GCC/Clang extension: &&label yields the address of a label;
	 * goto *ptr jumps through an arbitrary code address.  This
	 * eliminates the single indirect-branch bottleneck of switch
	 * dispatch and lets the branch predictor track each opcode
	 * independently. */
	static const void *dsp_dispatch[64] = {
		&&dsp_op_add,             &&dsp_op_addc,
		&&dsp_op_addq,            &&dsp_op_addqt,
		&&dsp_op_sub,             &&dsp_op_subc,
		&&dsp_op_subq,            &&dsp_op_subqt,
		&&dsp_op_neg,             &&dsp_op_and,
		&&dsp_op_or,              &&dsp_op_xor,
		&&dsp_op_not,             &&dsp_op_btst,
		&&dsp_op_bset,            &&dsp_op_bclr,
		&&dsp_op_mult,            &&dsp_op_imult,
		&&dsp_op_imultn,          &&dsp_op_resmac,
		&&dsp_op_imacn,           &&dsp_op_div,
		&&dsp_op_abs,             &&dsp_op_sh,
		&&dsp_op_shlq,            &&dsp_op_shrq,
		&&dsp_op_sha,             &&dsp_op_sharq,
		&&dsp_op_ror,             &&dsp_op_rorq,
		&&dsp_op_cmp,             &&dsp_op_cmpq,
		&&dsp_op_subqmod,         &&dsp_op_sat16s,
		&&dsp_op_move,            &&dsp_op_moveq,
		&&dsp_op_moveta,          &&dsp_op_movefa,
		&&dsp_op_movei,           &&dsp_op_loadb,
		&&dsp_op_loadw,           &&dsp_op_load,
		&&dsp_op_sat32s,          &&dsp_op_load_r14_indexed,
		&&dsp_op_load_r15_indexed,&&dsp_op_storeb,
		&&dsp_op_storew,          &&dsp_op_store,
		&&dsp_op_mirror,          &&dsp_op_store_r14_indexed,
		&&dsp_op_store_r15_indexed,&&dsp_op_move_pc,
		&&dsp_op_jump,            &&dsp_op_jr,
		&&dsp_op_mmult,           &&dsp_op_mtoi,
		&&dsp_op_normi,           &&dsp_op_nop,
		&&dsp_op_load_r14_ri,     &&dsp_op_load_r15_ri,
		&&dsp_op_store_r14_ri,    &&dsp_op_store_r15_ri,
		&&dsp_op_illegal,         &&dsp_op_addqmod
	};

	goto *dsp_dispatch[index];

	dsp_op_add:             dsp_opcode_add();             return;
	dsp_op_addc:            dsp_opcode_addc();            return;
	dsp_op_addq:            dsp_opcode_addq();            return;
	dsp_op_addqt:           dsp_opcode_addqt();           return;
	dsp_op_sub:             dsp_opcode_sub();             return;
	dsp_op_subc:            dsp_opcode_subc();            return;
	dsp_op_subq:            dsp_opcode_subq();            return;
	dsp_op_subqt:           dsp_opcode_subqt();           return;
	dsp_op_neg:             dsp_opcode_neg();             return;
	dsp_op_and:             dsp_opcode_and();             return;
	dsp_op_or:              dsp_opcode_or();              return;
	dsp_op_xor:             dsp_opcode_xor();             return;
	dsp_op_not:             dsp_opcode_not();             return;
	dsp_op_btst:            dsp_opcode_btst();            return;
	dsp_op_bset:            dsp_opcode_bset();            return;
	dsp_op_bclr:            dsp_opcode_bclr();            return;
	dsp_op_mult:            dsp_opcode_mult();            return;
	dsp_op_imult:           dsp_opcode_imult();           return;
	dsp_op_imultn:          dsp_opcode_imultn();          return;
	dsp_op_resmac:          dsp_opcode_resmac();          return;
	dsp_op_imacn:           dsp_opcode_imacn();           return;
	dsp_op_div:             dsp_opcode_div();             return;
	dsp_op_abs:             dsp_opcode_abs();             return;
	dsp_op_sh:              dsp_opcode_sh();              return;
	dsp_op_shlq:            dsp_opcode_shlq();            return;
	dsp_op_shrq:            dsp_opcode_shrq();            return;
	dsp_op_sha:             dsp_opcode_sha();             return;
	dsp_op_sharq:           dsp_opcode_sharq();           return;
	dsp_op_ror:             dsp_opcode_ror();             return;
	dsp_op_rorq:            dsp_opcode_rorq();            return;
	dsp_op_cmp:             dsp_opcode_cmp();             return;
	dsp_op_cmpq:            dsp_opcode_cmpq();            return;
	dsp_op_subqmod:         dsp_opcode_subqmod();         return;
	dsp_op_sat16s:          dsp_opcode_sat16s();          return;
	dsp_op_move:            dsp_opcode_move();            return;
	dsp_op_moveq:           dsp_opcode_moveq();           return;
	dsp_op_moveta:          dsp_opcode_moveta();          return;
	dsp_op_movefa:          dsp_opcode_movefa();          return;
	dsp_op_movei:           dsp_opcode_movei();           return;
	dsp_op_loadb:           dsp_opcode_loadb();           return;
	dsp_op_loadw:           dsp_opcode_loadw();           return;
	dsp_op_load:            dsp_opcode_load();            return;
	dsp_op_sat32s:          dsp_opcode_sat32s();          return;
	dsp_op_load_r14_indexed:dsp_opcode_load_r14_indexed();return;
	dsp_op_load_r15_indexed:dsp_opcode_load_r15_indexed();return;
	dsp_op_storeb:          dsp_opcode_storeb();          return;
	dsp_op_storew:          dsp_opcode_storew();          return;
	dsp_op_store:           dsp_opcode_store();           return;
	dsp_op_mirror:          dsp_opcode_mirror();          return;
	dsp_op_store_r14_indexed:dsp_opcode_store_r14_indexed();return;
	dsp_op_store_r15_indexed:dsp_opcode_store_r15_indexed();return;
	dsp_op_move_pc:         dsp_opcode_move_pc();         return;
	dsp_op_jump:            dsp_opcode_jump();            return;
	dsp_op_jr:              dsp_opcode_jr();              return;
	dsp_op_mmult:           dsp_opcode_mmult();           return;
	dsp_op_mtoi:            dsp_opcode_mtoi();            return;
	dsp_op_normi:           dsp_opcode_normi();           return;
	dsp_op_nop:             dsp_opcode_nop();             return;
	dsp_op_load_r14_ri:     dsp_opcode_load_r14_ri();     return;
	dsp_op_load_r15_ri:     dsp_opcode_load_r15_ri();     return;
	dsp_op_store_r14_ri:    dsp_opcode_store_r14_ri();    return;
	dsp_op_store_r15_ri:    dsp_opcode_store_r15_ri();    return;
	dsp_op_illegal:         dsp_opcode_illegal();         return;
	dsp_op_addqmod:         dsp_opcode_addqmod();         return; /* NOLINT(readability-redundant-control-flow) -- goto target */
#else
	/* Switch fallback for MSVC and other non-GNU compilers */
	switch (index)
	{
	case 0:  dsp_opcode_add(); break;
	case 1:  dsp_opcode_addc(); break;
	case 2:  dsp_opcode_addq(); break;
	case 3:  dsp_opcode_addqt(); break;
	case 4:  dsp_opcode_sub(); break;
	case 5:  dsp_opcode_subc(); break;
	case 6:  dsp_opcode_subq(); break;
	case 7:  dsp_opcode_subqt(); break;
	case 8:  dsp_opcode_neg(); break;
	case 9:  dsp_opcode_and(); break;
	case 10: dsp_opcode_or(); break;
	case 11: dsp_opcode_xor(); break;
	case 12: dsp_opcode_not(); break;
	case 13: dsp_opcode_btst(); break;
	case 14: dsp_opcode_bset(); break;
	case 15: dsp_opcode_bclr(); break;
	case 16: dsp_opcode_mult(); break;
	case 17: dsp_opcode_imult(); break;
	case 18: dsp_opcode_imultn(); break;
	case 19: dsp_opcode_resmac(); break;
	case 20: dsp_opcode_imacn(); break;
	case 21: dsp_opcode_div(); break;
	case 22: dsp_opcode_abs(); break;
	case 23: dsp_opcode_sh(); break;
	case 24: dsp_opcode_shlq(); break;
	case 25: dsp_opcode_shrq(); break;
	case 26: dsp_opcode_sha(); break;
	case 27: dsp_opcode_sharq(); break;
	case 28: dsp_opcode_ror(); break;
	case 29: dsp_opcode_rorq(); break;
	case 30: dsp_opcode_cmp(); break;
	case 31: dsp_opcode_cmpq(); break;
	case 32: dsp_opcode_subqmod(); break;
	case 33: dsp_opcode_sat16s(); break;
	case 34: dsp_opcode_move(); break;
	case 35: dsp_opcode_moveq(); break;
	case 36: dsp_opcode_moveta(); break;
	case 37: dsp_opcode_movefa(); break;
	case 38: dsp_opcode_movei(); break;
	case 39: dsp_opcode_loadb(); break;
	case 40: dsp_opcode_loadw(); break;
	case 41: dsp_opcode_load(); break;
	case 42: dsp_opcode_sat32s(); break;
	case 43: dsp_opcode_load_r14_indexed(); break;
	case 44: dsp_opcode_load_r15_indexed(); break;
	case 45: dsp_opcode_storeb(); break;
	case 46: dsp_opcode_storew(); break;
	case 47: dsp_opcode_store(); break;
	case 48: dsp_opcode_mirror(); break;
	case 49: dsp_opcode_store_r14_indexed(); break;
	case 50: dsp_opcode_store_r15_indexed(); break;
	case 51: dsp_opcode_move_pc(); break;
	case 52: dsp_opcode_jump(); break;
	case 53: dsp_opcode_jr(); break;
	case 54: dsp_opcode_mmult(); break;
	case 55: dsp_opcode_mtoi(); break;
	case 56: dsp_opcode_normi(); break;
	case 57: dsp_opcode_nop(); break;
	case 58: dsp_opcode_load_r14_ri(); break;
	case 59: dsp_opcode_load_r15_ri(); break;
	case 60: dsp_opcode_store_r14_ri(); break;
	case 61: dsp_opcode_store_r15_ri(); break;
	case 62: dsp_opcode_illegal(); break;
	case 63: dsp_opcode_addqmod(); break;
	default: break;
	}
#endif
}

// DSP opcode handlers

/* There is a problem here with interrupt handlers the JUMP and JR instructions that
 * can cause trouble because an interrupt can occur *before* the instruction following the
 * jump can execute... !!! FIX !!! */
INLINE static void dsp_opcode_jump(void)
{
	/* KLUDGE: Used by BRANCH_CONDITION */
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
		/* The target register is latched a pipeline stage before a
		 * D_FLAGS store in the slot ahead of us can retire, so when one
		 * is still outstanding read it from the bank that was live when
		 * that store issued -- not from the bank it selected.  See the
		 * D_FLAGS case in DSPWriteLong. */
		uint32_t delayed_pc = dspFlagsRetireDelay
		                      ? dspPreStoreBank[dsp_opcode_first_parameter]
		                      : RM;
		uint16_t ds_opcode;
		uint32_t ds_index;
		/* Inline delay-slot: fetch-decode-execute one instruction at current
		 * PC before applying the branch target.  This replaces the old
		 * recursive DSPExec(1) call, avoiding full function-call overhead,
		 * redundant IRQ checks, and pipeline-state save/restore. */
		if (dsp_pc >= DSP_WORK_RAM_BASE && dsp_pc < DSP_WORK_RAM_BASE + 0x2000)
		{
			uint32_t off = dsp_pc - DSP_WORK_RAM_BASE;
			ds_opcode = ((uint16_t)dsp_ram_8[off] << 8) | (uint16_t)dsp_ram_8[off + 1];
		}
		else
			ds_opcode = DSPReadWord(dsp_pc, DSP);
		ds_index = ds_opcode >> 10;
		dsp_opcode_first_parameter  = (ds_opcode >> 5) & 0x1F;
		dsp_opcode_second_parameter = ds_opcode & 0x1F;
		dsp_pc += 2;
		dsp_exec_opcode_count++;
		dsp_executeOpcode(ds_index);
		dsp_pc = delayed_pc;
		/* Refilling the pipeline from the branch target costs enough
		 * cycles that an outstanding D_FLAGS store -- including one the
		 * delay slot just issued -- reaches Write-back first, so the
		 * target instruction sees the new bank.  See DSPWriteLong. */
		dspFlagsRetireDelay = 0;
	}
}


INLINE static void dsp_opcode_jr(void)
{
	/* KLUDGE: Used by BRANCH_CONDITION */
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
		int32_t offset = ((IMM_1 & 0x10) ? 0xFFFFFFF0 | IMM_1 : IMM_1);		/* Sign extend IMM_1 */
		int32_t delayed_pc = dsp_pc + (offset * 2);
		uint16_t ds_opcode;
		uint32_t ds_index;
		/* Inline delay-slot: fetch-decode-execute one instruction at current
		 * PC before applying the branch target.  Same rationale as in
		 * dsp_opcode_jump above. */
		if (dsp_pc >= DSP_WORK_RAM_BASE && dsp_pc < DSP_WORK_RAM_BASE + 0x2000)
		{
			uint32_t off = dsp_pc - DSP_WORK_RAM_BASE;
			ds_opcode = ((uint16_t)dsp_ram_8[off] << 8) | (uint16_t)dsp_ram_8[off + 1];
		}
		else
			ds_opcode = DSPReadWord(dsp_pc, DSP);
		ds_index = ds_opcode >> 10;
		dsp_opcode_first_parameter  = (ds_opcode >> 5) & 0x1F;
		dsp_opcode_second_parameter = ds_opcode & 0x1F;
		dsp_pc += 2;
		dsp_exec_opcode_count++;
		dsp_executeOpcode(ds_index);
		dsp_pc = delayed_pc;
		/* Same branch-target pipeline refill as dsp_opcode_jump. */
		dspFlagsRetireDelay = 0;
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
	uint64_t res = (uint64_t)RN + (uint64_t)RM + (uint64_t)dsp_flag_c;
	dsp_flag_c = (uint8_t)((res >> 32) & 0x01);
	RN = (uint32_t)(res & 0xFFFFFFFF);
	SET_ZN(RN);
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
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	DSPWriteLong((dsp_reg[14] + RM) & 0xFFFFFFFC, RN, DSP);
#else
	DSPWriteLong(dsp_reg[14] + RM, RN, DSP);
#endif
}


INLINE static void dsp_opcode_store_r15_ri(void)
{
#ifdef DSP_CORRECT_ALIGNMENT_STORE
	DSPWriteLong((dsp_reg[15] + RM) & 0xFFFFFFFC, RN, DSP);
#else
	DSPWriteLong(dsp_reg[15] + RM, RN, DSP);
#endif
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
	{
		/* JTRM (Technical Reference v8, "Load Byte"): byte extraction
		 * "applies to external memory only, internal memory will perform
		 * a 32-bit read."  A byte load from DSP local RAM returns the
		 * ENTIRE long containing the address -- same rule as the GPU
		 * (see gpu_opcode_loadb). */
		RN = DSPReadLong(RM & 0xFFFFFFFC, DSP);
	}
	else
		RN = JaguarReadByte(RM, DSP);
}


INLINE static void dsp_opcode_loadw(void)
{
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
	{
		/* Same JTRM rule as LOADB: word loads from internal RAM perform
		 * a full 32-bit read of the long containing the address. */
		RN = DSPReadLong(RM & 0xFFFFFFFC, DSP);
	}
#ifdef DSP_CORRECT_ALIGNMENT
	else
		RN = JaguarReadWord(RM & 0xFFFFFFFE, DSP);
#else
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

	dsp_acc_mac_apply(&dsp_acc, res);
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

   /* Per JTRM ("Systolic Matrix Multiplies"), the packed vector operand
    * lives in the SECONDARY register bank (bank 1) — an absolute bank
    * reference, not "the bank not currently selected".  With IMASK set
    * (interrupt service) the current bank is forced to 0, so the old
    * dsp_alternate_reg happened to be bank 1 and looked correct; but a
    * mixer running mainline with REGPAGE=1 (e.g. Baldies) loads its
    * sample vector into bank 1 as its CURRENT bank, and reading the
    * "alternate" bank 0 multiplies the matrix by the interrupt
    * handler's pointers instead — rail-to-rail clipped audio. */
   if (!(dsp_matrix_control & 0x10))
   {
      for (i = 0; i < count; i++)
      {
         int16_t a;
         int16_t b;

         if (i&0x01)
            a=(int16_t)((dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
         else
            a=(int16_t)(dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]&0xffff);
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
            a=(int16_t)((dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
         else
            a=(int16_t)(dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]&0xffff);
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
	uint32_t res;

	if (_Rn == 0x80000000)
	{
		/* ABS(0x80000000) overflows back to 0x80000000; set flags accordingly:
		 * C=1 (input was negative), N=1 (result is negative), Z=0. */
		dsp_flag_n = 1;
		dsp_flag_c = 1;
		dsp_flag_z = 0;
	}
	else
	{
		dsp_flag_c = ((_Rn & 0x80000000) >> 31);
		res = RN   = ((_Rn & 0x80000000) ? -_Rn : _Rn);
		CLR_ZN;
		SET_Z(res);
	}
}


INLINE static void dsp_opcode_div(void)
{
	unsigned i;
	uint32_t q;
	uint32_t r;

	/* Guard against divide-by-zero: JTRM says result is undefined but no
	 * trap occurs.  Match the pipelined version's behavior. */
	if (RM == 0)
	{
		RN = 0xFFFFFFFF;
		dsp_remain = 0;
		return;
	}

	/* Real algorithm, courtesy of SCPCD: NYAN! */
	q = RN;
	r = 0;

	/* If 16.16 division, stuff top 16 bits of RN into remainder and put the
	 * bottom 16 of RN in top 16 of quotient */
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

	dsp_acc_set_from_i32(&dsp_acc, res);
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
	uint32_t res = (RN >> r1) | (RN << ((-r1) & 31));
	SET_ZN(res); dsp_flag_c = (RN >> 31) & 1;
	RN = res;
}


INLINE static void dsp_opcode_rorq(void)
{
	/* dsp_convert_zero[0] returns 32 (rotate-by-0 means rotate-by-full-word,
	 * a no-op).  Masking to 0x1F maps 32 -> 0, preserving that semantic and
	 * avoiding `RN >> 32` UB in the rotate idiom below. */
	uint32_t r1 = dsp_convert_zero[IMM_1 & 0x1F] & 0x1F;
	uint32_t r2 = RN;
	uint32_t res = (r2 >> r1) | (r2 << ((-r1) & 31));
	RN = res;
	SET_ZN(res); dsp_flag_c = (r2 >> 31) & 0x01;
}


INLINE static void dsp_opcode_sha(void)
{
	uint32_t res;

	if ((int32_t)RM < 0)
	{
		res = ((int32_t)RM <= -32) ? 0 : (RN << -(int32_t)RM);
		dsp_flag_c = RN >> 31;
	}
	else
	{
		res = ((int32_t)RM >= 32) ? ((int32_t)RN >> 31) : ((int32_t)RN >> (int32_t)RM);
		dsp_flag_c = RN & 0x01;
	}
	RN = res;
	SET_ZN(res);
}


INLINE static void dsp_opcode_sharq(void)
{
	uint32_t res = (int32_t)RN >> dsp_convert_zero[IMM_1];
	SET_ZN(res); dsp_flag_c = RN & 0x01;
	RN = res;
}


INLINE static void dsp_opcode_sh(void)
{
	if (RM & 0x80000000)		/* Shift left */
	{
		dsp_flag_c = RN >> 31;
		RN = ((int32_t)RM <= -32 ? 0 : RN << -(int32_t)RM);
	}
	else						/* Shift right */
	{
		dsp_flag_c = RN & 0x01;
		RN = (RM >= 32 ? 0 : RN >> RM);
	}
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
	int32_t temp = (int32_t)(dsp_acc_i40_signed(dsp_acc) >> 32);
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
	{
		/* ABS(0x80000000) overflows back to 0x80000000; set flags accordingly:
		 * C=1 (input was negative), N=1 (result is negative), Z=0.
		 * Must set PRES so the writeback stage stores the correct value. */
		PRES = 0x80000000;
		dsp_flag_n = 1;
		dsp_flag_c = 1;
		dsp_flag_z = 0;
	}
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
	{
		PRES = 0xFFFFFFFF;
		dsp_remain = 0;
	}
}

INLINE static void DSP_imacn(void)
{
	int32_t res = (int16_t)PRM * (int16_t)PRN;

	dsp_acc_mac_apply(&dsp_acc, res);
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

	dsp_acc_set_from_i32(&dsp_acc, res);
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
      dsp_exec_opcode_count++;
      DSPOpcode[pipeline[plPtrExec].opcode]();
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
	{
		/* JTRM: internal-RAM byte loads perform a full 32-bit read
		 * (see dsp_opcode_loadb). */
		PRES = DSPReadLong(PRM & 0xFFFFFFFC, DSP);
	}
	else
		PRES = JaguarReadByte(PRM, DSP);
}

INLINE static void DSP_loadw(void)
{
	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
	{
		/* JTRM: internal-RAM word loads perform a full 32-bit read
		 * (see dsp_opcode_loadw). */
		PRES = DSPReadLong(PRM & 0xFFFFFFFC, DSP);
	}
#ifdef DSP_CORRECT_ALIGNMENT
	else
		PRES = JaguarReadWord(PRM & 0xFFFFFFFE, DSP);
#else
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

	/* Vector operand comes from the SECONDARY bank (bank 1) per JTRM —
	 * see dsp_opcode_mmult above. */
	if (!(dsp_matrix_control & 0x10))
	{
		for (i = 0; i < count; i++)
		{
			int16_t a;
         int16_t b;

			if (i&0x01)
				a=(int16_t)((dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]&0xffff);
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
				a=(int16_t)((dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_reg_bank_1[dsp_opcode_first_parameter + (i>>1)]&0xffff);
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
	uint32_t res = (PRN >> r1) | (PRN << ((-r1) & 31));
	SET_ZN(res); dsp_flag_c = (PRN >> 31) & 1;
	PRES = res;
}

INLINE static void DSP_rorq(void)
{
	/* See dsp_opcode_rorq above for why we mask to 0x1F. */
	uint32_t r1 = dsp_convert_zero[PIMM1 & 0x1F] & 0x1F;
	uint32_t r2 = PRN;
	uint32_t res = (r2 >> r1) | (r2 << ((-r1) & 31));
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
	int32_t temp = (int32_t)(dsp_acc_i40_signed(dsp_acc) >> 32);
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


/* Save state serialization for DSP */

#include "state.h"

size_t DSPStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   uint8_t active_bank;

   STATE_SAVE_BUF(buf, dsp_ram_8, sizeof(dsp_ram_8));
   STATE_SAVE_VAR(buf, dsp_pc);
   STATE_SAVE_VAR(buf, dsp_acc);
   STATE_SAVE_VAR(buf, dsp_remain);
   STATE_SAVE_VAR(buf, dsp_modulo);
   STATE_SAVE_VAR(buf, dsp_flags);
   STATE_SAVE_VAR(buf, dsp_matrix_control);
   STATE_SAVE_VAR(buf, dsp_pointer_to_matrix);
   STATE_SAVE_VAR(buf, dsp_data_organization);
   STATE_SAVE_VAR(buf, dsp_control);
   STATE_SAVE_VAR(buf, dsp_div_control);
   STATE_SAVE_VAR(buf, dsp_flag_z);
   STATE_SAVE_VAR(buf, dsp_flag_n);
   STATE_SAVE_VAR(buf, dsp_flag_c);
   STATE_SAVE_BUF(buf, dsp_reg_bank_0, sizeof(dsp_reg_bank_0));
   STATE_SAVE_BUF(buf, dsp_reg_bank_1, sizeof(dsp_reg_bank_1));

   active_bank = (dsp_reg == dsp_reg_bank_0) ? 0 : 1;
   STATE_SAVE_VAR(buf, active_bank);

   STATE_SAVE_VAR(buf, dsp_opcode_first_parameter);
   STATE_SAVE_VAR(buf, dsp_opcode_second_parameter);
   STATE_SAVE_VAR(buf, dsp_in_exec);
   STATE_SAVE_VAR(buf, dsp_releaseTimeSlice_flag);

   /* Pipeline state */
   STATE_SAVE_BUF(buf, pipeline, sizeof(pipeline));
   STATE_SAVE_VAR(buf, plPtrFetch);
   STATE_SAVE_VAR(buf, plPtrRead);
   STATE_SAVE_VAR(buf, plPtrExec);
   STATE_SAVE_VAR(buf, plPtrWrite);
   STATE_SAVE_BUF(buf, scoreboard, sizeof(scoreboard));
   STATE_SAVE_VAR(buf, IMASKCleared);

   STATE_SAVE_VAR(buf, prevR1);

   return (size_t)(buf - start);
}


size_t DSPStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   uint8_t active_bank;

   STATE_LOAD_BUF(buf, dsp_ram_8, sizeof(dsp_ram_8));
   STATE_LOAD_VAR(buf, dsp_pc);
   STATE_LOAD_VAR(buf, dsp_acc);
   STATE_LOAD_VAR(buf, dsp_remain);
   STATE_LOAD_VAR(buf, dsp_modulo);
   STATE_LOAD_VAR(buf, dsp_flags);
   STATE_LOAD_VAR(buf, dsp_matrix_control);
   STATE_LOAD_VAR(buf, dsp_pointer_to_matrix);
   STATE_LOAD_VAR(buf, dsp_data_organization);
   STATE_LOAD_VAR(buf, dsp_control);
   STATE_LOAD_VAR(buf, dsp_div_control);
   STATE_LOAD_VAR(buf, dsp_flag_z);
   STATE_LOAD_VAR(buf, dsp_flag_n);
   STATE_LOAD_VAR(buf, dsp_flag_c);
   STATE_LOAD_BUF(buf, dsp_reg_bank_0, sizeof(dsp_reg_bank_0));
   STATE_LOAD_BUF(buf, dsp_reg_bank_1, sizeof(dsp_reg_bank_1));

   STATE_LOAD_VAR(buf, active_bank);
   if (active_bank == 0)
   {
      dsp_reg = dsp_reg_bank_0;
      dsp_alternate_reg = dsp_reg_bank_1;
   }
   else
   {
      dsp_reg = dsp_reg_bank_1;
      dsp_alternate_reg = dsp_reg_bank_0;
   }
   /* The state format carries no slot for an un-retired D_FLAGS store
    * (see DSP_FLAGS_RETIRE_DELAY); retire it on load.  The window is two
    * instruction slots wide, and the register banks themselves are saved
    * in full either way. */
   dspFlagsRetireDelay = 0;
   dspPreStoreBank = dsp_reg;

   STATE_LOAD_VAR(buf, dsp_opcode_first_parameter);
   STATE_LOAD_VAR(buf, dsp_opcode_second_parameter);
   STATE_LOAD_VAR(buf, dsp_in_exec);
   STATE_LOAD_VAR(buf, dsp_releaseTimeSlice_flag);

   STATE_LOAD_BUF(buf, pipeline, sizeof(pipeline));
   STATE_LOAD_VAR(buf, plPtrFetch);
   STATE_LOAD_VAR(buf, plPtrRead);
   STATE_LOAD_VAR(buf, plPtrExec);
   STATE_LOAD_VAR(buf, plPtrWrite);
   STATE_LOAD_BUF(buf, scoreboard, sizeof(scoreboard));
   STATE_LOAD_VAR(buf, IMASKCleared);

   STATE_LOAD_VAR(buf, prevR1);

   return (size_t)(buf - start);
}


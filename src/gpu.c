//
// GPU Core
//
// Originally by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups, endian wrongness, and bad ASM amelioration by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
// JLH  11/26/2011  Added fixes for LOAD/STORE alignment issues

//
// Note: Endian wrongness probably stems from the MAME origins of this emu and
//       the braindead way in which MAME handles memory. :-)
//
// Problem with not booting the BIOS was the incorrect way that the
// SUBC instruction set the carry when the carry was set going in...
// Same problem with ADDC...
//

#include "gpu.h"

#include <stdlib.h>
#include <string.h>								// For memset
#include "dsp.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"
#include "tom.h"


// Seems alignment in loads & stores was off...
#define GPU_CORRECT_ALIGNMENT

// For GPU dissasembly...

// Various bits

#define CINT0FLAG			0x0200
#define CINT1FLAG			0x0400
#define CINT2FLAG			0x0800
#define CINT3FLAG			0x1000
#define CINT4FLAG			0x2000
#define CINT04FLAGS			(CINT0FLAG | CINT1FLAG | CINT2FLAG | CINT3FLAG | CINT4FLAG)

// GPU_FLAGS bits

#define ZERO_FLAG		0x0001
#define CARRY_FLAG		0x0002
#define NEGA_FLAG		0x0004
#define IMASK			0x0008
#define INT_ENA0		0x0010
#define INT_ENA1		0x0020
#define INT_ENA2		0x0040
#define INT_ENA3		0x0080
#define INT_ENA4		0x0100
#define INT_CLR0		0x0200
#define INT_CLR1		0x0400
#define INT_CLR2		0x0800
#define INT_CLR3		0x1000
#define INT_CLR4		0x2000
#define REGPAGE			0x4000
#define DMAEN			0x8000

// Private function prototypes

void GPUUpdateRegisterBanks(void);

INLINE static void gpu_opcode_add(void);
INLINE static void gpu_opcode_addc(void);
INLINE static void gpu_opcode_addq(void);
INLINE static void gpu_opcode_addqt(void);
INLINE static void gpu_opcode_sub(void);
INLINE static void gpu_opcode_subc(void);
INLINE static void gpu_opcode_subq(void);
INLINE static void gpu_opcode_subqt(void);
INLINE static void gpu_opcode_neg(void);
INLINE static void gpu_opcode_and(void);
INLINE static void gpu_opcode_or(void);
INLINE static void gpu_opcode_xor(void);
INLINE static void gpu_opcode_not(void);
INLINE static void gpu_opcode_btst(void);
INLINE static void gpu_opcode_bset(void);
INLINE static void gpu_opcode_bclr(void);
INLINE static void gpu_opcode_mult(void);
INLINE static void gpu_opcode_imult(void);
INLINE static void gpu_opcode_imultn(void);
INLINE static void gpu_opcode_resmac(void);
INLINE static void gpu_opcode_imacn(void);
INLINE static void gpu_opcode_div(void);
INLINE static void gpu_opcode_abs(void);
INLINE static void gpu_opcode_sh(void);
INLINE static void gpu_opcode_shlq(void);
INLINE static void gpu_opcode_shrq(void);
INLINE static void gpu_opcode_sha(void);
INLINE static void gpu_opcode_sharq(void);
INLINE static void gpu_opcode_ror(void);
INLINE static void gpu_opcode_rorq(void);
INLINE static void gpu_opcode_cmp(void);
INLINE static void gpu_opcode_cmpq(void);
INLINE static void gpu_opcode_sat8(void);
INLINE static void gpu_opcode_sat16(void);
INLINE static void gpu_opcode_move(void);
INLINE static void gpu_opcode_moveq(void);
INLINE static void gpu_opcode_moveta(void);
INLINE static void gpu_opcode_movefa(void);
INLINE static void gpu_opcode_movei(void);
INLINE static void gpu_opcode_loadb(void);
INLINE static void gpu_opcode_loadw(void);
INLINE static void gpu_opcode_load(void);
INLINE static void gpu_opcode_loadp(void);
INLINE static void gpu_opcode_load_r14_indexed(void);
INLINE static void gpu_opcode_load_r15_indexed(void);
INLINE static void gpu_opcode_storeb(void);
INLINE static void gpu_opcode_storew(void);
INLINE static void gpu_opcode_store(void);
INLINE static void gpu_opcode_storep(void);
INLINE static void gpu_opcode_store_r14_indexed(void);
INLINE static void gpu_opcode_store_r15_indexed(void);
INLINE static void gpu_opcode_move_pc(void);
INLINE static void gpu_opcode_jump(void);
INLINE static void gpu_opcode_jr(void);
INLINE static void gpu_opcode_mmult(void);
INLINE static void gpu_opcode_mtoi(void);
INLINE static void gpu_opcode_normi(void);
INLINE static void gpu_opcode_nop(void);
INLINE static void gpu_opcode_load_r14_ri(void);
INLINE static void gpu_opcode_load_r15_ri(void);
INLINE static void gpu_opcode_store_r14_ri(void);
INLINE static void gpu_opcode_store_r15_ri(void);
INLINE static void gpu_opcode_sat24(void);
INLINE static void gpu_opcode_pack(void);

INLINE static void executeOpcode(uint32_t index);

uint8_t gpu_opcode_cycles[64] =
{
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1
};

void (*gpu_opcode[64])()=
{
	gpu_opcode_add,					gpu_opcode_addc,				gpu_opcode_addq,				gpu_opcode_addqt,
	gpu_opcode_sub,					gpu_opcode_subc,				gpu_opcode_subq,				gpu_opcode_subqt,
	gpu_opcode_neg,					gpu_opcode_and,					gpu_opcode_or,					gpu_opcode_xor,
	gpu_opcode_not,					gpu_opcode_btst,				gpu_opcode_bset,				gpu_opcode_bclr,
	gpu_opcode_mult,				gpu_opcode_imult,				gpu_opcode_imultn,				gpu_opcode_resmac,
	gpu_opcode_imacn,				gpu_opcode_div,					gpu_opcode_abs,					gpu_opcode_sh,
	gpu_opcode_shlq,				gpu_opcode_shrq,				gpu_opcode_sha,					gpu_opcode_sharq,
	gpu_opcode_ror,					gpu_opcode_rorq,				gpu_opcode_cmp,					gpu_opcode_cmpq,
	gpu_opcode_sat8,				gpu_opcode_sat16,				gpu_opcode_move,				gpu_opcode_moveq,
	gpu_opcode_moveta,				gpu_opcode_movefa,				gpu_opcode_movei,				gpu_opcode_loadb,
	gpu_opcode_loadw,				gpu_opcode_load,				gpu_opcode_loadp,				gpu_opcode_load_r14_indexed,
	gpu_opcode_load_r15_indexed,	gpu_opcode_storeb,				gpu_opcode_storew,				gpu_opcode_store,
	gpu_opcode_storep,				gpu_opcode_store_r14_indexed,	gpu_opcode_store_r15_indexed,	gpu_opcode_move_pc,
	gpu_opcode_jump,				gpu_opcode_jr,					gpu_opcode_mmult,				gpu_opcode_mtoi,
	gpu_opcode_normi,				gpu_opcode_nop,					gpu_opcode_load_r14_ri,			gpu_opcode_load_r15_ri,
	gpu_opcode_store_r14_ri,		gpu_opcode_store_r15_ri,		gpu_opcode_sat24,				gpu_opcode_pack,
};

static uint8_t gpu_ram_8[0x1000];
uint32_t gpu_pc;
static uint32_t gpu_acc;
static uint32_t gpu_remain;
static uint32_t gpu_hidata;
static uint32_t gpu_flags;
static uint32_t gpu_matrix_control;
static uint32_t gpu_pointer_to_matrix;
static uint32_t gpu_data_organization;
static uint32_t gpu_control;
static uint32_t gpu_div_control;
// There is a distinct advantage to having these separated out--there's no need to clear
// a bit before writing a result. I.e., if the result of an operation leaves a zero in
// the carry flag, you don't have to zero gpu_flag_c before you can write that zero!
static uint8_t gpu_flag_z, gpu_flag_n, gpu_flag_c;
uint32_t gpu_reg_bank_0[32];
uint32_t gpu_reg_bank_1[32];
static uint32_t * gpu_reg;
static uint32_t * gpu_alternate_reg;

static uint32_t gpu_instruction;
static uint32_t gpu_opcode_first_parameter;
static uint32_t gpu_opcode_second_parameter;

#define GPU_RUNNING	(gpu_control & 0x01)

#define RM		gpu_reg[gpu_opcode_first_parameter]
#define RN		gpu_reg[gpu_opcode_second_parameter]
#define ALTERNATE_RM	gpu_alternate_reg[gpu_opcode_first_parameter]
#define ALTERNATE_RN	gpu_alternate_reg[gpu_opcode_second_parameter]
#define IMM_1		gpu_opcode_first_parameter
#define IMM_2		gpu_opcode_second_parameter

#define SET_FLAG_Z(r)	(gpu_flag_z = ((r) == 0));
#define SET_FLAG_N(r)	(gpu_flag_n = (((uint32_t)(r) >> 31) & 0x01));

#define RESET_FLAG_Z()	gpu_flag_z = 0;
#define RESET_FLAG_N()	gpu_flag_n = 0;
#define RESET_FLAG_C()	gpu_flag_c = 0;

#define CLR_Z			(gpu_flag_z = 0)
#define CLR_ZN			(gpu_flag_z = gpu_flag_n = 0)
#define CLR_ZNC			(gpu_flag_z = gpu_flag_n = gpu_flag_c = 0)
#define SET_Z(r)		(gpu_flag_z = ((r) == 0))
#define SET_N(r)		(gpu_flag_n = (((uint32_t)(r) >> 31) & 0x01))
#define SET_C_ADD(a,b)		(gpu_flag_c = ((uint32_t)(b) > (uint32_t)(~(a))))
#define SET_C_SUB(a,b)		(gpu_flag_c = ((uint32_t)(b) > (uint32_t)(a)))
#define SET_ZN(r)		SET_N(r); SET_Z(r)
#define SET_ZNC_ADD(a,b,r)	SET_N(r); SET_Z(r); SET_C_ADD(a,b)
#define SET_ZNC_SUB(a,b,r)	SET_N(r); SET_Z(r); SET_C_SUB(a,b)

uint32_t gpu_convert_zero[32] =
	{ 32,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31 };

uint8_t * branch_condition_table = 0;
#define BRANCH_CONDITION(x)	branch_condition_table[(x) + ((jaguar_flags & 7) << 5)]

static uint32_t gpu_in_exec = 0;
static uint32_t gpu_releaseTimeSlice_flag = 0;

void GPUReleaseTimeslice(void)
{
	gpu_releaseTimeSlice_flag = 1;
}

uint32_t GPUGetPC(void)
{
	return gpu_pc;
}

void build_branch_condition_table(void)
{
   unsigned i, j;

   if (branch_condition_table)
      return;

   branch_condition_table = (uint8_t *)malloc(32 * 8 * sizeof(branch_condition_table[0]));

   if (!branch_condition_table)
      return;

   for(i=0; i<8; i++)
   {
      for(j=0; j<32; j++)
      {
         int result = 1;
         if (j & 1)
            if (i & ZERO_FLAG)
               result = 0;
         if (j & 2)
            if (!(i & ZERO_FLAG))
               result = 0;
         if (j & 4)
            if (i & (CARRY_FLAG << (j >> 4)))
               result = 0;
         if (j & 8)
            if (!(i & (CARRY_FLAG << (j >> 4))))
               result = 0;
         branch_condition_table[i * 32 + j] = result;
      }
   }
}

// GPU byte access (read)
uint8_t GPUReadByte(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
		return gpu_ram_8[offset & 0xFFF];
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	{
		uint32_t data = GPUReadLong(offset & 0xFFFFFFFC, who);

		if ((offset & 0x03) == 0)
			return data >> 24;
		else if ((offset & 0x03) == 1)
			return (data >> 16) & 0xFF;
		else if ((offset & 0x03) == 2)
			return (data >> 8) & 0xFF;
		else if ((offset & 0x03) == 3)
			return data & 0xFF;
	}

	return JaguarReadByte(offset, who);
}

// GPU word access (read)
uint16_t GPUReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
	{
		uint16_t data;
		offset &= 0xFFF;
		data    = ((uint16_t)gpu_ram_8[offset] << 8) | (uint16_t)gpu_ram_8[offset+1];
		return data;
	}
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	{
		uint32_t data;

		// This looks and smells wrong...
		// But it *might* be OK...
		if (offset & 0x01)			// Catch cases 1 & 3... (unaligned read)
			return (GPUReadByte(offset, who) << 8) | GPUReadByte(offset+1, who);

		data = GPUReadLong(offset & 0xFFFFFFFC, who);

		if (offset & 0x02)			// Cases 0 & 2...
			return data & 0xFFFF;
		return data >> 16;
	}

	return JaguarReadWord(offset, who);
}

// GPU dword access (read)
uint32_t GPUReadLong(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	if (offset >= 0xF02000 && offset <= 0xF020FF)
	{
		uint32_t reg = (offset & 0xFC) >> 2;
		return (reg < 32 ? gpu_reg_bank_0[reg] : gpu_reg_bank_1[reg - 32]); 
	}

	if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFC))
	{
		offset &= 0xFFF;
		return ((uint32_t)gpu_ram_8[offset] << 24) | ((uint32_t)gpu_ram_8[offset+1] << 16)
			| ((uint32_t)gpu_ram_8[offset+2] << 8) | (uint32_t)gpu_ram_8[offset+3];//*/
	}
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1C))
	{
		offset &= 0x1F;
		switch (offset)
		{
			case 0x00:
				gpu_flag_c = (gpu_flag_c ? 1 : 0);
				gpu_flag_z = (gpu_flag_z ? 1 : 0);
				gpu_flag_n = (gpu_flag_n ? 1 : 0);

				gpu_flags = (gpu_flags & 0xFFFFFFF8) | (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

				return gpu_flags & 0xFFFFC1FF;
			case 0x04:
				return gpu_matrix_control;
			case 0x08:
				return gpu_pointer_to_matrix;
			case 0x0C:
				return gpu_data_organization;
			case 0x10:
				return gpu_pc;
			case 0x14:
				return gpu_control;
			case 0x18:
				return gpu_hidata;
			case 0x1C:
				return gpu_remain;
			default:								// unaligned long read
				break;
		}

		return 0;
	}

	return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset + 2, who);
}

// GPU byte access (write)
void GPUWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
   if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFF))
   {
      gpu_ram_8[offset & 0xFFF] = data;

      return;
   }
   else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1F))
   {
      uint32_t reg = offset & 0x1C;
      int bytenum = offset & 0x03;

      //This is definitely wrong!
      if ((reg >= 0x1C) && (reg <= 0x1F))
         gpu_div_control = (gpu_div_control & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));
      else
      {
         uint32_t old_data = GPUReadLong(offset & 0xFFFFFFC, who);
         bytenum = 3 - bytenum; // convention motorola !!!
         old_data = (old_data & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));
         GPUWriteLong(offset & 0xFFFFFFC, old_data, who);
      }
      return;
   }
   JaguarWriteByte(offset, data, who);
}

// GPU word access (write)
void GPUWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
   if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFE))
   {
      gpu_ram_8[offset & 0xFFF] = (data>>8) & 0xFF;
      gpu_ram_8[(offset+1) & 0xFFF] = data & 0xFF;//*/

      return;
   }
   else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1E))
   {
      if (offset & 0x01)		// This is supposed to weed out unaligned writes, but does nothing...
         return;
      //Dual locations in this range: $1C Divide unit remainder/Divide unit control (R/W)
      //This just literally sucks.
      if ((offset & 0x1C) == 0x1C)
      {
         //This doesn't look right either--handles cases 1, 2, & 3 all the same!
         if (offset & 0x02)
            gpu_div_control = (gpu_div_control & 0xFFFF0000) | (data & 0xFFFF);
         else
            gpu_div_control = (gpu_div_control & 0x0000FFFF) | ((data & 0xFFFF) << 16);
      }
      else
      {
         uint32_t old_data = GPUReadLong(offset & 0xFFFFFFC, who);

         if (offset & 0x02)
            old_data = (old_data & 0xFFFF0000) | (data & 0xFFFF);
         else
            old_data = (old_data & 0x0000FFFF) | ((data & 0xFFFF) << 16);

         GPUWriteLong(offset & 0xFFFFFFC, old_data, who);
      }

      return;
   }
   else if ((offset == GPU_WORK_RAM_BASE + 0x0FFF) || (GPU_CONTROL_RAM_BASE + 0x1F))
      return;

   // Have to be careful here--this can cause an infinite loop!
   JaguarWriteWord(offset, data, who);
}

// GPU dword access (write)
void GPUWriteLong(uint32_t offset, uint32_t data, uint32_t who/*=UNKNOWN*/)
{
   if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFC))
   {
      offset &= 0xFFF;
      SET32(gpu_ram_8, offset, data);
      return;
   }
   else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1C))
   {
      offset &= 0x1F;
      switch (offset)
      {
         case 0x00:
            {
               bool IMASKCleared = (gpu_flags & IMASK) && !(data & IMASK);
               // NOTE: According to the JTRM, writing a 1 to IMASK has no effect; only the
               //       IRQ logic can set it. So we mask it out here to prevent problems...
               gpu_flags = data & (~IMASK);
               gpu_flag_z = gpu_flags & ZERO_FLAG;
               gpu_flag_c = (gpu_flags & CARRY_FLAG) >> 1;
               gpu_flag_n = (gpu_flags & NEGA_FLAG) >> 2;
               GPUUpdateRegisterBanks();
               gpu_control &= ~((gpu_flags & CINT04FLAGS) >> 3);	// Interrupt latch clear bits
               //Writing here is only an interrupt enable--this approach is just plain wrong!
               //			GPUHandleIRQs();
               //This, however, is A-OK! ;-)
               if (IMASKCleared)						// If IMASK was cleared,
                  GPUHandleIRQs();					// see if any other interrupts need servicing!
               break;
            }
         case 0x04:
            gpu_matrix_control = data;
            break;
         case 0x08:
            // This can only point to long aligned addresses
            gpu_pointer_to_matrix = data & 0xFFFFFFFC;
            break;
         case 0x0C:
            gpu_data_organization = data;
            break;
         case 0x10:
            gpu_pc = data;
            break;
         case 0x14:
            {
               data &= ~0xF7C0;		// Disable writes to INT_LAT0-4 & TOM version number

               // check for GPU -> CPU interrupt
               if (data & 0x02)
               {
                  if (TOMIRQEnabled(IRQ_GPU))
                  {
                     //This is the programmer's responsibility, to make sure the handler is valid, not ours!
                     //					if ((TOMIRQEnabled(IRQ_GPU))// && (JaguarInterruptHandlerIsValid(64)))
                     {
                        TOMSetPendingGPUInt();
                        m68k_set_irq(2);			// Set 68000 IPL 2
                        GPUReleaseTimeslice();
                     }
                  }
                  data &= ~0x02;
               }

               // check for CPU -> GPU interrupt #0
               if (data & 0x04)
               {
                  GPUSetIRQLine(0, ASSERT_LINE);
                  m68k_end_timeslice();
                  DSPReleaseTimeslice();
                  data &= ~0x04;
               }

               gpu_control = (gpu_control & 0xF7C0) | (data & (~0xF7C0));

               // if gpu wasn't running but is now running, execute a few cycles
#ifdef GPU_SINGLE_STEPPING
               if (gpu_control & 0x18)
                  GPUExec(1);
#endif
               // (?) If we're set running by the M68K (or DSP?) then end its timeslice to
               // allow the GPU a chance to run...
               // Yes! This partially fixed Trevor McFur...
               if (GPU_RUNNING)
                  m68k_end_timeslice();
               break;
            }
         case 0x18:
            gpu_hidata = data;
            break;
         case 0x1C:
            gpu_div_control = data;
            break;
            //		default:   // unaligned long write
            //exit(0);
            //__asm int 3
      }
      return;
   }

   //	JaguarWriteWord(offset, (data >> 16) & 0xFFFF, who);
   //	JaguarWriteWord(offset+2, data & 0xFFFF, who);
   // We're a 32-bit processor, we can do a long write...!
   JaguarWriteLong(offset, data, who);
}

// Change register banks if necessary
void GPUUpdateRegisterBanks(void)
{
   int bank = (gpu_flags & REGPAGE);		// REGPAGE bit

   if (gpu_flags & IMASK)					// IMASK bit
      bank = 0;							// IMASK forces main bank to be bank 0

   if (bank)
      gpu_reg = gpu_reg_bank_1, gpu_alternate_reg = gpu_reg_bank_0;
   else
      gpu_reg = gpu_reg_bank_0, gpu_alternate_reg = gpu_reg_bank_1;
}

void GPUHandleIRQs(void)
{
   uint32_t bits, mask;
   uint32_t which = 0; //Isn't there a #pragma to disable this warning???
   // Bail out if we're already in an interrupt!
   if (gpu_flags & IMASK)
      return;

   // Get the interrupt latch & enable bits
   bits = (gpu_control >> 6) & 0x1F;
   mask = (gpu_flags >> 4) & 0x1F;

   // Bail out if latched interrupts aren't enabled
   bits &= mask;
   if (!bits)
      return;

   // Determine which interrupt to service
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

   // set the interrupt flag
   gpu_flags |= IMASK;
   GPUUpdateRegisterBanks();

   // subqt  #4,r31		; pre-decrement stack pointer
   // move  pc,r30			; address of interrupted code
   // store  r30,(r31)     ; store return address
   gpu_reg[31] -= 4;
   GPUWriteLong(gpu_reg[31], gpu_pc - 2, GPU);

   // movei  #service_address,r30  ; pointer to ISR entry
   // jump  (r30)					; jump to ISR
   // nop
   gpu_pc = gpu_reg[30] = GPU_WORK_RAM_BASE + (which * 0x10);
}

void GPUSetIRQLine(int irqline, int state)
{
   uint32_t mask = 0x0040 << irqline;
   gpu_control &= ~mask;				// Clear the interrupt latch

   if (state)
   {
      gpu_control |= mask;			// Assert the interrupt latch
      GPUHandleIRQs();				// And handle the interrupt...
   }
}

void GPUInit(void)
{
   build_branch_condition_table();

   GPUReset();
}

void GPUReset(void)
{
   unsigned i;

   // GPU registers (directly visible)
   gpu_flags			  = 0x00000000;
   gpu_matrix_control    = 0x00000000;
   gpu_pointer_to_matrix = 0x00000000;
   gpu_data_organization = 0xFFFFFFFF;
   gpu_pc				  = 0x00F03000;
   gpu_control			  = 0x00002800;			// Correctly sets this as TOM Rev. 2
   gpu_hidata			  = 0x00000000;
   gpu_remain			  = 0x00000000;			// These two registers are RO/WO
   gpu_div_control		  = 0x00000000;

   // GPU internal register
   gpu_acc				  = 0x00000000;

   gpu_reg = gpu_reg_bank_0;
   gpu_alternate_reg = gpu_reg_bank_1;

   for(i=0; i<32; i++)
      gpu_reg[i] = gpu_alternate_reg[i] = 0x00000000;

   CLR_ZNC;
   memset(gpu_ram_8, 0xFF, 0x1000);
   gpu_in_exec = 0;
   //not needed	GPUInterruptPending = false;
   GPUResetStats();

   // Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents
   for(i=0; i<4096; i+=4)
      *((uint32_t *)(&gpu_ram_8[i])) = rand();
}

uint32_t GPUReadPC(void)
{
   return gpu_pc;
}

void GPUResetStats(void)
{
}

// Main GPU execution core

void GPUExec(int32_t cycles)
{
   if (!GPU_RUNNING)
      return;

#ifdef GPU_SINGLE_STEPPING
   if (gpu_control & 0x18)
   {
      cycles = 1;
      gpu_control &= ~0x10;
   }
#endif
   GPUHandleIRQs();
   gpu_releaseTimeSlice_flag = 0;
   gpu_in_exec++;

   while (cycles > 0 && GPU_RUNNING)
   {
      uint16_t opcode = GPUReadWord(gpu_pc, GPU);
      uint32_t index  = opcode >> 10;
      gpu_instruction = opcode;	// Added for GPU #3...
      gpu_opcode_first_parameter  = (opcode >> 5) & 0x1F;
      gpu_opcode_second_parameter = opcode & 0x1F;

      //$E400 -> 1110 01 -> $39 -> 57
      //GPU #1
      gpu_pc += 2;
#if 0
      gpu_opcode[index]();
#else
       executeOpcode(index);
#endif
      // BIOS hacking
      //GPU: [00F03548] jr      nz,00F03560 (0xd561) (RM=00F03114, RN=00000004) ->     --> JR: Branch taken.
      //GPU: [00F0354C] jump    nz,(r29) (0xd3a1) (RM=00F03314, RN=00000004) -> (RM=00F03314, RN=00000004)

      cycles -= gpu_opcode_cycles[index];
   }

   gpu_in_exec--;
}

INLINE static void executeOpcode(uint32_t index) {
    switch (index) {
        case 0:
            gpu_opcode_add();
            break;
        case 1:
            gpu_opcode_addc();
            break;
        case 2:
            gpu_opcode_addq();
            break;
        case 3:
            gpu_opcode_addqt();
            break;
        case 4:
            gpu_opcode_sub();
            break;
        case 5:
            gpu_opcode_subc();
            break;
        case 6:
            gpu_opcode_subq();
            break;
        case 7:
            gpu_opcode_subqt();
            break;
        case 8:
            gpu_opcode_neg();
            break;
        case 9:
            gpu_opcode_and();
            break;
        case 10:
            gpu_opcode_or();
            break;
        case 11:
            gpu_opcode_xor();
            break;
        case 12:
            gpu_opcode_not();
            break;
        case 13:
            gpu_opcode_btst();
            break;
        case 14:
            gpu_opcode_bset();
            break;
        case 15:
            gpu_opcode_bclr();
            break;
        case 16:
            gpu_opcode_mult();
            break;
        case 17:
            gpu_opcode_imult();
            break;
        case 18:
            gpu_opcode_imultn();
            break;
        case 19:
            gpu_opcode_resmac();
            break;
        case 20:
            gpu_opcode_imacn();
            break;
        case 21:
            gpu_opcode_div();
            break;
        case 22:
            gpu_opcode_abs();
            break;
        case 23:
            gpu_opcode_sh();
            break;
        case 24:
            gpu_opcode_shlq();
            break;
        case 25:
            gpu_opcode_shrq();
            break;
        case 26:
            gpu_opcode_sha();
            break;
        case 27:
            gpu_opcode_sharq();
            break;
        case 28:
            gpu_opcode_ror();
            break;
        case 29:
            gpu_opcode_rorq();
            break;
        case 30:
            gpu_opcode_cmp();
            break;
        case 31:
            gpu_opcode_cmpq();
            break;
        case 32:
            gpu_opcode_sat8();
            break;
        case 33:
            gpu_opcode_sat16();
            break;
        case 34:
            gpu_opcode_move();
            break;
        case 35:
            gpu_opcode_moveq();
            break;
        case 36:
            gpu_opcode_moveta();
            break;
        case 37:
            gpu_opcode_movefa();
            break;
        case 38:
            gpu_opcode_movei();
            break;
        case 39:
            gpu_opcode_loadb();
            break;
        case 40:
            gpu_opcode_loadw();
            break;
        case 41:
            gpu_opcode_load();
            break;
        case 42:
            gpu_opcode_loadp();
            break;
        case 43:
            gpu_opcode_load_r14_indexed();
            break;
        case 44:
            gpu_opcode_load_r15_indexed();
            break;
        case 45:
            gpu_opcode_storeb();
            break;
        case 46:
            gpu_opcode_storew();
            break;
        case 47:
            gpu_opcode_store();
            break;
        case 48:
            gpu_opcode_storep();
            break;
        case 49:
            gpu_opcode_store_r14_indexed();
            break;
        case 50:
            gpu_opcode_store_r15_indexed();
            break;
        case 51:
            gpu_opcode_move_pc();
            break;
        case 52:
            gpu_opcode_jump();
            break;
        case 53:
            gpu_opcode_jr();
            break;
        case 54:
            gpu_opcode_mmult();
            break;
        case 55:
            gpu_opcode_mtoi();
            break;
        case 56:
            gpu_opcode_normi();
            break;
        case 57:
            gpu_opcode_nop();
            break;
        case 58:
            gpu_opcode_load_r14_ri();
            break;
        case 59:
            gpu_opcode_load_r15_ri();
            break;
        case 60:
            gpu_opcode_store_r14_ri();
            break;
        case 61:
            gpu_opcode_store_r15_ri();
            break;
        case 62:
            gpu_opcode_sat24();
            break;
        case 63:
            gpu_opcode_pack();
            break;
        default:
            // WriteLog("\nUnknown opcode %i\n", index);
            break;
    }
}

// GPU opcodes

/*
   GPU opcodes use (offset punch--vertically below bad guy):
   add 18686
   addq 32621
   sub 7483
   subq 10252
   and 21229
   or 15003
   btst 1822
   bset 2072
   mult 141
   div 2392
   shlq 13449
   shrq 10297
   sharq 11104
   cmp 6775
   cmpq 5944
   move 31259
   moveq 4473
   movei 23277
   loadb 46
   loadw 4201
   load 28580
   load_r14_indexed 1183
   load_r15_indexed 1125
   storew 178
   store 10144
   store_r14_indexed 320
   store_r15_indexed 1
   move_pc 1742
   jump 24467
   jr 18090
   nop 41362
   */


INLINE static void gpu_opcode_jump(void)
{
   // normalize flags
   /*	gpu_flag_c = (gpu_flag_c ? 1 : 0);
      gpu_flag_z = (gpu_flag_z ? 1 : 0);
      gpu_flag_n = (gpu_flag_n ? 1 : 0);*/
   // KLUDGE: Used by BRANCH_CONDITION
   uint32_t jaguar_flags = (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

   if (BRANCH_CONDITION(IMM_2))
   {
      uint32_t delayed_pc = RM;
      GPUExec(1);
      gpu_pc = delayed_pc;
   }
}


INLINE static void gpu_opcode_jr(void)
{
   uint32_t jaguar_flags = (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

   if (BRANCH_CONDITION(IMM_2))
   {
      int32_t offset     = ((IMM_1 & 0x10) ? 0xFFFFFFF0 | IMM_1 : IMM_1);		// Sign extend IMM_1
      int32_t delayed_pc = gpu_pc + (offset * 2);
      GPUExec(1);
      gpu_pc = delayed_pc;
   }
}


INLINE static void gpu_opcode_add(void)
{
   uint32_t res = RN + RM;
   CLR_ZNC; SET_ZNC_ADD(RN, RM, res);
   RN = res;
}


INLINE static void gpu_opcode_addc(void)
{
   uint32_t res = RN + RM + gpu_flag_c;
   uint32_t carry = gpu_flag_c;
   SET_ZNC_ADD(RN + carry, RM, res);
   RN = res;
}


INLINE static void gpu_opcode_addq(void)
{
   uint32_t r1 = gpu_convert_zero[IMM_1];
   uint32_t res = RN + r1;
   CLR_ZNC; SET_ZNC_ADD(RN, r1, res);
   RN = res;
}


INLINE static void gpu_opcode_addqt(void)
{
   RN += gpu_convert_zero[IMM_1];
}


INLINE static void gpu_opcode_sub(void)
{
   uint32_t res = RN - RM;
   SET_ZNC_SUB(RN, RM, res);
   RN = res;
}


INLINE static void gpu_opcode_subc(void)
{
   // This is how the GPU ALU does it--Two's complement with inverted carry
   uint64_t res = (uint64_t)RN + (uint64_t)(RM ^ 0xFFFFFFFF) + (gpu_flag_c ^ 1);
   // Carry out of the result is inverted too
   gpu_flag_c = ((res >> 32) & 0x01) ^ 1;
   RN = (res & 0xFFFFFFFF);
   SET_ZN(RN);
}


INLINE static void gpu_opcode_subq(void)
{
   uint32_t r1 = gpu_convert_zero[IMM_1];
   uint32_t res = RN - r1;
   SET_ZNC_SUB(RN, r1, res);
   RN = res;
}


INLINE static void gpu_opcode_subqt(void)
{
   RN -= gpu_convert_zero[IMM_1];
}


INLINE static void gpu_opcode_cmp(void)
{
   uint32_t res = RN - RM;
   SET_ZNC_SUB(RN, RM, res);
}


INLINE static void gpu_opcode_cmpq(void)
{
   static int32_t sqtable[32] =
   { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,-16,-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1 };
   uint32_t r1 = sqtable[IMM_1 & 0x1F]; // I like this better -> (INT8)(jaguar.op >> 2) >> 3;
   uint32_t res = RN - r1;
   SET_ZNC_SUB(RN, r1, res);
}


INLINE static void gpu_opcode_and(void)
{
   RN = RN & RM;
   SET_ZN(RN);
}


INLINE static void gpu_opcode_or(void)
{
   RN = RN | RM;
   SET_ZN(RN);
}


INLINE static void gpu_opcode_xor(void)
{
   RN = RN ^ RM;
   SET_ZN(RN);
}


INLINE static void gpu_opcode_not(void)
{
   RN = ~RN;
   SET_ZN(RN);
}


INLINE static void gpu_opcode_move_pc(void)
{
   // Should be previous PC--this might not always be previous instruction!
   // Then again, this will point right at the *current* instruction, i.e., MOVE PC,R!
   RN = gpu_pc - 2;
}


INLINE static void gpu_opcode_sat8(void)
{
   RN = ((int32_t)RN < 0 ? 0 : (RN > 0xFF ? 0xFF : RN));
   SET_ZN(RN);
}


INLINE static void gpu_opcode_sat16(void)
{
   RN = ((int32_t)RN < 0 ? 0 : (RN > 0xFFFF ? 0xFFFF : RN));
   SET_ZN(RN);
}

INLINE static void gpu_opcode_sat24(void)
{
   RN = ((int32_t)RN < 0 ? 0 : (RN > 0xFFFFFF ? 0xFFFFFF : RN));
   SET_ZN(RN);
}


INLINE static void gpu_opcode_store_r14_indexed(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2);

   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   GPUWriteLong(gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2), RN, GPU);
#endif
}


INLINE static void gpu_opcode_store_r15_indexed(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);

   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   GPUWriteLong(gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2), RN, GPU);
#endif
}


INLINE static void gpu_opcode_load_r14_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[14] + RM;

   if (address >= 0xF03000 && address <= 0xF03FFF)
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   RN = GPUReadLong(gpu_reg[14] + RM, GPU);
#endif
}


INLINE static void gpu_opcode_load_r15_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[15] + RM;

   if (address >= 0xF03000 && address <= 0xF03FFF)
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   RN = GPUReadLong(gpu_reg[15] + RM, GPU);
#endif
}


INLINE static void gpu_opcode_store_r14_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[14] + RM;

   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   GPUWriteLong(gpu_reg[14] + RM, RN, GPU);
#endif
}


INLINE static void gpu_opcode_store_r15_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT_STORE
   uint32_t address = gpu_reg[15] + RM;

   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   GPUWriteLong(gpu_reg[15] + RM, RN, GPU);
#endif
}


INLINE static void gpu_opcode_nop(void)
{
}


INLINE static void gpu_opcode_pack(void)
{
   uint32_t val = RN;

   if (IMM_1 == 0)				// Pack
      RN = ((val >> 10) & 0x0000F000) | ((val >> 5) & 0x00000F00) | (val & 0x000000FF);
   else						// Unpack
      RN = ((val & 0x0000F000) << 10) | ((val & 0x00000F00) << 5) | (val & 0x000000FF);
}


INLINE static void gpu_opcode_storeb(void)
{
   //Is this right???
   // Would appear to be so...!
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM, RN & 0xFF, GPU);
   else
      JaguarWriteByte(RM, RN, GPU);
}


INLINE static void gpu_opcode_storew(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM & 0xFFFFFFFE, RN & 0xFFFF, GPU);
   else
      JaguarWriteWord(RM, RN, GPU);
#else
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM, RN & 0xFFFF, GPU);
   else
      JaguarWriteWord(RM, RN, GPU);
#endif
}


INLINE static void gpu_opcode_store(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(RM, RN, GPU);
#else
   GPUWriteLong(RM, RN, GPU);
#endif
}


INLINE static void gpu_opcode_storep(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
   {
      GPUWriteLong((RM & 0xFFFFFFF8) + 0, gpu_hidata, GPU);
      GPUWriteLong((RM & 0xFFFFFFF8) + 4, RN, GPU);
   }
   else
   {
      GPUWriteLong(RM + 0, gpu_hidata, GPU);
      GPUWriteLong(RM + 4, RN, GPU);
   }
#else
   GPUWriteLong(RM + 0, gpu_hidata, GPU);
   GPUWriteLong(RM + 4, RN, GPU);
#endif
}

INLINE static void gpu_opcode_loadb(void)
{
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      RN = GPUReadLong(RM, GPU) & 0xFF;
   else
      RN = JaguarReadByte(RM, GPU);
}


INLINE static void gpu_opcode_loadw(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      RN = GPUReadLong(RM & 0xFFFFFFFE, GPU) & 0xFFFF;
   else
      RN = JaguarReadWord(RM, GPU);
#else
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      RN = GPUReadLong(RM, GPU) & 0xFFFF;
   else
      RN = JaguarReadWord(RM, GPU);
#endif
}


// According to the docs, & "Do The Same", this address is long aligned...
// So let's try it:
// And it works!!! Need to fix all instances...
// Also, Power Drive Rally seems to contradict the idea that only LOADs in
// the $F03000-$F03FFF range are aligned...
// #warning "!!! Alignment issues, need to find definitive final word on this !!!"
/*
   Preliminary testing on real hardware seems to confirm that something strange goes on
   with unaligned reads in main memory. When the address is off by 1, the result is the
   same as the long address with the top byte replaced by something. So if the read is
   from $401, and $400 has 12 34 56 78, the value read will be $nn345678, where nn is a currently unknown vlaue.
   When the address is off by 2, the result would be $nnnn5678, where nnnn is unknown.
   When the address is off by 3, the result would be $nnnnnn78, where nnnnnn is unknown.
   It may be that the "unknown" values come from the prefetch queue, but not sure how
   to test that. They seem to be stable, though, which would indicate such a mechanism.
   Sometimes, however, the off by 2 case returns $12345678!
   */
INLINE static void gpu_opcode_load(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   RN = GPUReadLong(RM & 0xFFFFFFFC, GPU);
#else
   RN = GPUReadLong(RM, GPU);
#endif
}


INLINE static void gpu_opcode_loadp(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
   {
      gpu_hidata = GPUReadLong((RM & 0xFFFFFFF8) + 0, GPU);
      RN		   = GPUReadLong((RM & 0xFFFFFFF8) + 4, GPU);
   }
   else
   {
      gpu_hidata = GPUReadLong(RM + 0, GPU);
      RN		   = GPUReadLong(RM + 4, GPU);
   }
#else
   gpu_hidata = GPUReadLong(RM + 0, GPU);
   RN		   = GPUReadLong(RM + 4, GPU);
#endif
}


INLINE static void gpu_opcode_load_r14_indexed(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2);

   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   RN = GPUReadLong(gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2), GPU);
#endif
}


INLINE static void gpu_opcode_load_r15_indexed(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);

   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   RN = GPUReadLong(gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2), GPU);
#endif
}


INLINE static void gpu_opcode_movei(void)
{
   // This instruction is followed by 32-bit value in LSW / MSW format...
   RN = (uint32_t)GPUReadWord(gpu_pc, GPU) | ((uint32_t)GPUReadWord(gpu_pc + 2, GPU) << 16);
   gpu_pc += 4;
}


INLINE static void gpu_opcode_moveta(void)
{
   ALTERNATE_RN = RM;
}


INLINE static void gpu_opcode_movefa(void)
{
   RN = ALTERNATE_RM;
}


INLINE static void gpu_opcode_move(void)
{
   RN = RM;
}


INLINE static void gpu_opcode_moveq(void)
{
   RN = IMM_1;
}


INLINE static void gpu_opcode_resmac(void)
{
   RN = gpu_acc;
}


INLINE static void gpu_opcode_imult(void)
{
   RN = (int16_t)RN * (int16_t)RM;
   SET_ZN(RN);
}


INLINE static void gpu_opcode_mult(void)
{
   RN = (uint16_t)RM * (uint16_t)RN;
   SET_ZN(RN);
}


INLINE static void gpu_opcode_bclr(void)
{
   uint32_t res = RN & ~(1 << IMM_1);
   RN = res;
   SET_ZN(res);
}


INLINE static void gpu_opcode_btst(void)
{
   gpu_flag_z = (~RN >> IMM_1) & 1;
}


INLINE static void gpu_opcode_bset(void)
{
   uint32_t res = RN | (1 << IMM_1);
   RN = res;
   SET_ZN(res);
}


INLINE static void gpu_opcode_imacn(void)
{
   uint32_t res = (int16_t)RM * (int16_t)(RN);
   gpu_acc += res;
}


INLINE static void gpu_opcode_mtoi(void)
{
   uint32_t _RM = RM;
   uint32_t res = RN = (((int32_t)_RM >> 8) & 0xFF800000) | (_RM & 0x007FFFFF);
   SET_ZN(res);
}


INLINE static void gpu_opcode_normi(void)
{
   uint32_t _RM = RM;
   uint32_t res = 0;

   if (_RM)
   {
      while ((_RM & 0xFFC00000) == 0)
      {
         _RM <<= 1;
         res--;
      }
      while ((_RM & 0xFF800000) != 0)
      {
         _RM >>= 1;
         res++;
      }
   }
   RN = res;
   SET_ZN(res);
}

INLINE static void gpu_opcode_mmult(void)
{
   unsigned i;
   int count	= gpu_matrix_control & 0x0F;	// Matrix width
   uint32_t addr = gpu_pointer_to_matrix;		// In the GPU's RAM
   int64_t accum = 0;
   uint32_t res;

   if (gpu_matrix_control & 0x10)				// Column stepping
   {
      for(i=0; i<count; i++)
      {
         int16_t a;
         int16_t b;
         if (i & 0x01)
            a = (int16_t)((gpu_alternate_reg[IMM_1 + (i >> 1)] >> 16) & 0xFFFF);
         else
            a = (int16_t)(gpu_alternate_reg[IMM_1 + (i >> 1)] & 0xFFFF);

         b = ((int16_t)GPUReadWord(addr + 2, GPU));
         accum += a * b;
         addr += 4 * count;
      }
   }
   else										// Row stepping
   {
      for(i=0; i<count; i++)
      {
         int16_t a;
         int16_t b;
         if (i & 0x01)
            a = (int16_t)((gpu_alternate_reg[IMM_1 + (i >> 1)] >> 16) & 0xFFFF);
         else
            a = (int16_t)(gpu_alternate_reg[IMM_1 + (i >> 1)] & 0xFFFF);

         b = ((int16_t)GPUReadWord(addr + 2, GPU));
         accum += a * b;
         addr += 4;
      }
   }
   RN = res = (int32_t)accum;
   // carry flag to do (out of the last add)
   SET_ZN(res);
}


INLINE static void gpu_opcode_abs(void)
{
   gpu_flag_c = RN >> 31;
   if (RN == 0x80000000)
      //Is 0x80000000 a positive number? If so, then we need to set C to 0 as well!
      gpu_flag_n = 1, gpu_flag_z = 0;
   else
   {
      if (gpu_flag_c)
         RN = -RN;
      gpu_flag_n = 0; SET_FLAG_Z(RN);
   }
}


INLINE static void gpu_opcode_div(void)	// RN / RM
{
   unsigned i;
   // Real algorithm, courtesy of SCPCD: NYAN!
   uint32_t q = RN;
   uint32_t r = 0;

   // If 16.16 division, stuff top 16 bits of RN into remainder and put the
   // bottom 16 of RN in top 16 of quotient
   if (gpu_div_control & 0x01)
      q <<= 16, r = RN >> 16;

   for(i=0; i<32; i++)
   {
      uint32_t sign = r & 0x80000000;
      r = (r << 1) | ((q >> 31) & 0x01);
      r += (sign ? RM : -RM);
      q = (q << 1) | (((~r) >> 31) & 0x01);
   }

   RN = q;
   gpu_remain = r;

}


INLINE static void gpu_opcode_imultn(void)
{
   uint32_t res = (int32_t)((int16_t)RN * (int16_t)RM);
   gpu_acc = (int32_t)res;
   SET_FLAG_Z(res);
   SET_FLAG_N(res);
}


INLINE static void gpu_opcode_neg(void)
{
   uint32_t res = -RN;
   SET_ZNC_SUB(0, RN, res);
   RN = res;
}


INLINE static void gpu_opcode_shlq(void)
{
   int32_t r1 = 32 - IMM_1;
   uint32_t res = RN << r1;
   SET_ZN(res); gpu_flag_c = (RN >> 31) & 1;
   RN = res;
}


INLINE static void gpu_opcode_shrq(void)
{
   int32_t r1 = gpu_convert_zero[IMM_1];
   uint32_t res = RN >> r1;
   SET_ZN(res); gpu_flag_c = RN & 1;
   RN = res;
}


INLINE static void gpu_opcode_ror(void)
{
   uint32_t r1 = RM & 0x1F;
   uint32_t res = (RN >> r1) | (RN << (32 - r1));
   SET_ZN(res); gpu_flag_c = (RN >> 31) & 1;
   RN = res;
}


INLINE static void gpu_opcode_rorq(void)
{
   uint32_t r1 = gpu_convert_zero[IMM_1 & 0x1F];
   uint32_t r2 = RN;
   uint32_t res = (r2 >> r1) | (r2 << (32 - r1));
   RN = res;
   SET_ZN(res); gpu_flag_c = (r2 >> 31) & 0x01;
}


INLINE static void gpu_opcode_sha(void)
{
   uint32_t res;

   if ((int32_t)RM < 0)
   {
      res = ((int32_t)RM <= -32) ? 0 : (RN << -(int32_t)RM);
      gpu_flag_c = RN >> 31;
   }
   else
   {
      res = ((int32_t)RM >= 32) ? ((int32_t)RN >> 31) : ((int32_t)RN >> (int32_t)RM);
      gpu_flag_c = RN & 0x01;
   }
   RN = res;
   SET_ZN(res);
}


INLINE static void gpu_opcode_sharq(void)
{
   uint32_t res = (int32_t)RN >> gpu_convert_zero[IMM_1];
   SET_ZN(res); gpu_flag_c = RN & 0x01;
   RN = res;
}


INLINE static void gpu_opcode_sh(void)
{
   if (RM & 0x80000000)		// Shift left
   {
      gpu_flag_c = RN >> 31;
      RN = ((int32_t)RM <= -32 ? 0 : RN << -(int32_t)RM);
   }
   else						// Shift right
   {
      gpu_flag_c = RN & 0x01;
      RN = (RM >= 32 ? 0 : RN >> RM);
   }
   SET_ZN(RN);
}

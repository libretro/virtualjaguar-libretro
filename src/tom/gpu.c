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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>								// For memset
#include "bus_arbiter.h"
#include "log.h"
#include "dsp.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"
#include "tom.h"
#include "event.h"
#include "settings.h"
#include "blit_memo.h"
#include "../core/vjtrace.h"
#include "../core/crash_detect.h"
#include "perf_iface.h"
#include "gdbstub.h"


// Seems alignment in loads & stores was off...
#define GPU_CORRECT_ALIGNMENT

/* Bus contention: accumulates stall cycles for GPU external memory
 * accesses within a single instruction.  Reset before each opcode
 * in GPUExec(), then added to cycle cost after the opcode completes.
 * Units: system clocks (same as bus_arbiter charges). */
static uint32_t gpu_bus_stall;

/* Sub-cycle remainder from converting wall-sysclk bus stalls into the
 * GPU's scaled cycle domain (cycle-domain contract, bus_arbiter.h).
 * Units: sysclk-hundredths, remainder modulo 100.  Always 0 at stock
 * scale.  Not serialized — same precedent as m68kScaleAccum (a bounded
 * sub-cycle epsilon); reset alongside the scale in GPUClockScaleReset(). */
static uint32_t gpu_stall_scale_accum;

/* Charge an external memory access and accumulate the stall.
 * addr is the target address — GPU local RAM (0xF03000-0xF03FFF)
 * costs nothing; external addresses cost DRAM access time. */
#define GPU_EXT_ACCESS(addr) \
   do { \
      if (busArbiter.enabled) \
         gpu_bus_stall += bus_arbiter_charge_access(BM_GPU, (addr)); \
   } while (0)

void GPUChargeBusStall(uint32_t sysclks)
{
   gpu_bus_stall += sysclks;
}

/* ------------------------------------------------------------------ *
 * GPU pipeline / external-gateway timing model (issue #401 / #313,
 * core option virtualjaguar_gpu_pipeline_timing, default off).
 *
 * The emulated GPU historically executed most instructions in one
 * cycle with memory free, finishing real render kernels 2-4x faster
 * than silicon.  Titles that pace an UNGATED loop on render
 * completion (Jaguar Doom's menu and demo MiniLoop, Hover Strike)
 * therefore run measurably fast (#401).  The machine timing itself
 * (VBL rate, field length) is correct -- the missing time is
 * instruction-level.  This model implements the three mechanisms the
 * JTRM (rev 8, "Register Score-Boarding" / "Memory Interface" /
 * "Load and Store Operations") actually describes:
 *
 * 1. SINGLE EXTERNAL GATEWAY.  "The gateway between the GPU local bus
 *    and the external co-processor bus contains a control block for
 *    generating external memory transfers. ... If there is another
 *    load or store instruction in the program before the gateway has
 *    completed its transfer, then it will be held up until the
 *    gateway is idle."  One outstanding external transfer; the
 *    transfer itself runs in the BACKGROUND (the issuing load costs
 *    its normal tick), and the NEXT load/store stalls for whatever
 *    remains.  Back-to-back external ops therefore serialize at bus
 *    speed -- the dominant cost of a texel loop.
 *
 * 2. LOAD SCORE-BOARD.  "For load operations, the data is not loaded
 *    into the target register until the external transfer has taken
 *    place.  The score-board mechanism prevents use of this data
 *    before it has been loaded, but other computation may take
 *    place."  Reading a register with a pending load stalls to the
 *    transfer's completion.
 *
 * 3. ALU RAW INTERLOCK.  Reading a register "still in the process of
 *    being computed by the ALU" inserts a wait state; the JTRM's own
 *    advice is to interleave two calculation streams so consecutive
 *    instructions don't share registers.  Modeled as one wait state
 *    when an instruction reads the register written by the
 *    immediately-preceding instruction.
 *
 * Time base: gpu_pipe_clock counts executed GPU cycles (== sysclks at
 * stock clock scale).  Stall cycles feed the existing gpu_bus_stall
 * per-instruction accumulator, so slice accounting and the
 * risc_clock_scale conversion path are unchanged.  All state here is
 * transient micro-state (bounded by one external transfer, tens of
 * cycles): never serialized, zeroed on GPUReset and IRQ dispatch
 * (bank switch), so a savestate load diverges by at most one
 * in-flight transfer's worth of stall.
 *
 * Rule constants below are from the Flare design sources shipped in
 * jag_sim/netlists (the ORIGINAL commented netlists: SBOARD.NET,
 * INS_EXEC.NET, EXECON.NET, ARB.NET, MEM.NET) -- see
 * GPU-TIMING-SPEC.md from the #401 calibration session.  Pinned
 * there: ALU 1/tick; +1 result-use interlock (one-deep window); +1
 * flags interlock (CMP;JRcc and ADDC/SUBC after a flag-setter);
 * local LOAD 3-tick non-blocking latency, engine takes a new op
 * every 2 ticks; external loads are split-transaction with TWO
 * pending slots, best-case ~7-9 sysclks end-to-end; a store drains
 * pending loads first; DIV runs 16 background ticks (2 bits/tick);
 * MOVEI 3 ticks; indexed load +2 / indexed store +1; taken JUMP +2 /
 * JR +3 dead ticks (delay slot then executes).  Bus grant latency
 * under load (blitter/OP running) is the remaining sim-pinned item. */
/* External LOAD end-to-end latency, pinned by Verilator against the
 * jag_sim netlist GPU: L = 7 + D sysclks, where D is the memory
 * controller's occupancy (9 with a DRAM page hit, 12 at the page-miss
 * average this model's bus_arbiter uses).  The three constants below
 * decompose the fixed 7: issue 2 + grant 2 + return 3. */
#define GPU_PIPE_GRANT_CLKS   2u   /* request->grant, idle bus (ARB.NET) */
#define GPU_PIPE_EXT_ISSUE    2u   /* gateway address/issue phase */
#define GPU_PIPE_EXT_RETURN   3u   /* load data return + scoreboard write */
#define GPU_PIPE_IO_CLKS      4u   /* externals bus_arbiter prices at 0 (I/O) */
#define GPU_PIPE_LOCAL_LOAD   3u   /* local-RAM load latency (SBOARD.NET) */
#define GPU_PIPE_LOCAL_REPEAT 2u   /* local load engine: one op per 2 ticks */
#define GPU_PIPE_DIV_TICKS    16u  /* 32-bit quotient at 2 bits/tick */

/* CYCLE DOMAIN (bus_arbiter.h contract).  Two accumulators, because
 * the model produces costs in BOTH domains and they must not be
 * converted alike under the risc_clock_scale enhancement:
 *
 *   gpu_bus_stall       WALL time (sysclks).  External memory latency:
 *                       gateway waits, DRAM/ROM access.  Silicon does
 *                       not speed up when the RISC is overclocked, so
 *                       GPUExec rescales these into the scaled cycle
 *                       domain.
 *   gpu_pipe_core_stall CORE time (GPU cycles).  Everything internal
 *                       to the RISC: scoreboard/RAW/flags interlocks,
 *                       MOVEI and indexed-address extra ticks, jump
 *                       refill, the delay-slot instruction's own
 *                       cycles, local-RAM load latency (local RAM is
 *                       clocked at the GPU clock -- JTRM: "It can be
 *                       cycled at the graphics processor clock rate").
 *                       These scale WITH the processor, i.e. they are
 *                       already in the right domain and must be
 *                       deducted from the slice budget unscaled.
 *
 * Charging a pipeline interlock through gpu_bus_stall would make one
 * interlock cycle cost two at 2x clock, which is backwards. */
static uint32_t gpu_pipe_core_stall;

static uint64_t gpu_pipe_clock;        /* cycles executed since reset */
static uint64_t gpu_ext_done[2];       /* split-transaction load/store slots */
static uint64_t gpu_gate_issue_free;   /* gateway issue-phase serialization */
static uint64_t gpu_local_busy_until;  /* local load engine */
static uint64_t gpu_reg_ready[32];     /* pending load/div result per reg */
/* Domain of each pending result: nonzero = the wait is external
 * (wall time), zero = internal (core time). */
static uint8_t  gpu_reg_ready_ext[32];
static uint8_t  gpu_pipe_prev_dest = 0xFF; /* prev instr's ALU dest, 0xFF none */
static uint8_t  gpu_pipe_prev_flags = 0;   /* prev instr set the flags */

/* Diagnostics (test ABI): stall cycles charged and external transfers
 * issued by the pipeline model.  MONOTONIC since the last GPUReset --
 * deliberately NOT cleared by GPUPipeTimingReset(), which also runs on
 * every interrupt bank switch and would blank these many times a frame,
 * leaving probes to difference against a moving zero.  Same convention
 * as the shadowHiresResolve* counters.  Not serialized. */
uint64_t gpu_pipe_stall_total = 0;
uint64_t gpu_pipe_ext_total   = 0;

/* Defined further down with the rest of the decode state; needed here
 * because the pipeline helpers read the current opcode's operand
 * fields. */
static uint32_t gpu_opcode_first_parameter;
static uint32_t gpu_opcode_second_parameter;

/* Per-opcode operand classification, indexed by opcode (gpu_dispatch
 * order).  Bit 0: field 1 (IMM_1) names a register this op READS.
 * Bit 1: field 2 (IMM_2) names a register this op READS.  Bit 2: this
 * op WRITES the field-2 register.  Ops whose field 1 is an immediate
 * (addq, btst, moveq...) do not set bit 0.  moveta/movefa touch the
 * alternate bank, which this model does not track. */
static const uint8_t gpu_pipe_flags[64] = {
   7, 7, 6, 6, 7, 7, 6, 6,   /* add addc addq addqt sub subc subq subqt */
   6, 7, 7, 7, 6, 2, 6, 6,   /* neg and or xor not btst bset bclr */
   7, 7, 3, 4, 3, 7, 6, 7,   /* mult imult imultn resmac imacn div abs sh */
   6, 6, 7, 6, 7, 6, 3, 2,   /* shlq shrq sha sharq ror rorq cmp cmpq */
   6, 6, 5, 4, 1, 4, 4, 5,   /* sat8 sat16 move moveq moveta movefa movei loadb */
   5, 5, 5, 4, 4, 3, 3, 3,   /* loadw load loadp load_r14_ix load_r15_ix storeb storew store */
   3, 2, 2, 4, 1, 0, 6, 5,   /* storep store_r14_ix store_r15_ix move_pc jump jr mmult mtoi */
   5, 0, 5, 5, 3, 3, 6, 6    /* normi nop load_r14_ri load_r15_ri store_r14_ri store_r15_ri sat24 pack */
};

void GPUPipeTimingReset(void)
{
   unsigned i;
   gpu_pipe_clock       = 0;
   gpu_pipe_core_stall  = 0;
   gpu_ext_done[0]      = 0;
   gpu_ext_done[1]      = 0;
   gpu_gate_issue_free  = 0;
   gpu_local_busy_until = 0;
   gpu_pipe_prev_dest   = 0xFF;
   gpu_pipe_prev_flags  = 0;
   for (i = 0; i < 32; i++)
   {
      gpu_reg_ready[i]     = 0;
      gpu_reg_ready_ext[i] = 0;
   }
}

/* Flag-setting opcodes (JTRM ISA): the transparent variants (addqt,
 * subqt) and the pure moves/loads/stores/jumps do not touch flags. */
static const uint8_t gpu_pipe_sets_flags[64] = {
   1,1,1,0,1,1,1,0, 1,1,1,1,1,1,1,1,       /* add..bclr (addqt/subqt no) */
   1,1,0,0,0,1,1,1, 1,1,1,1,1,1,1,1,       /* mult..cmpq (imultn/imacn/resmac no) */
   1,1,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,       /* sat8,sat16; moves/loads no */
   0,0,0,0,0,0,0,0, 0,0,0,0,0,0,1,0       /* ...sat24 yes, pack no */
};

/* Score-board check before dispatch.  Wait sources overlap on
 * hardware, so the charge is the MAX of: pending load/div result on a
 * register this opcode reads (R5/R15), the one-deep ALU result-use
 * interlock (R2), and the flags interlock (R3: conditional JR/JUMP or
 * ADDC/SUBC immediately after a flag-setter).  Fixed per-opcode extras
 * (MOVEI +2, indexed load +2, indexed store +1) are added on top --
 * they are issue cost, not a wait. */
static void GPUPipeCheckUse(uint32_t index)
{
   uint8_t f = gpu_pipe_flags[index];
   uint64_t ready = gpu_pipe_clock;
   uint32_t wait, extra;
   int wait_is_ext = 0;
   if (f & 1)
   {
      uint64_t r = gpu_reg_ready[gpu_opcode_first_parameter];
      if (r > ready)
      {
         ready = r;
         wait_is_ext = gpu_reg_ready_ext[gpu_opcode_first_parameter];
      }
   }
   if (f & 2)
   {
      uint64_t r = gpu_reg_ready[gpu_opcode_second_parameter];
      if (r > ready)
      {
         ready = r;
         wait_is_ext = gpu_reg_ready_ext[gpu_opcode_second_parameter];
      }
   }
   wait = (uint32_t)(ready - gpu_pipe_clock);
   if (wait < 1 && gpu_pipe_prev_dest != 0xFF)
   {
      if (((f & 1) && gpu_opcode_first_parameter == gpu_pipe_prev_dest)
       || ((f & 2) && gpu_opcode_second_parameter == gpu_pipe_prev_dest))
         wait = 1;                          /* ALU RAW interlock */
      else if ((f & 1) && (f & 2))
         /* Write-back port conflict (JTRM "Register Write-Back"): the
          * register bank is dual-port, so the previous instruction's
          * write-back can only be concealed if this instruction reads
          * its target or reads fewer than two registers.  Two reads,
          * neither the write-back target => one wait state.  Verilator
          * (jag_sim netlist GPU): every register-writing op in a
          * disjoint-register stream retires at 2.00 ticks, NOP streams
          * at 1.00 -- so this fires on essentially every back-to-back
          * ALU pair in compiled code and roughly halves real IPC. */
         wait = 1;
   }
   if (wait < 1 && gpu_pipe_prev_flags)
   {
      /* addc(1)/subc(5) always read carry; jump(52)/jr(53) read the
       * flags only for a real condition (cc field != 0 == "T"). */
      if (index == 1 || index == 5
         || ((index == 52 || index == 53) && gpu_opcode_second_parameter != 0))
         wait = 1;                          /* flags interlock */
   }
   extra = 0;
   if (index == 38)                          extra = 2;  /* movei: 3 ticks */
   else if (index == 43 || index == 44
         || index == 58 || index == 59)      extra = 2;  /* indexed load */
   else if (index == 49 || index == 50
         || index == 60 || index == 61)      extra = 1;  /* indexed store */
   /* A wait on a pending EXTERNAL load is wall time (the transfer is
    * running in DRAM); every other cost here is internal RISC time. */
   if (wait)
   {
      if (wait_is_ext)
         gpu_bus_stall += wait;
      else
         gpu_pipe_core_stall += wait;
      gpu_pipe_stall_total += wait;
   }
   if (extra)
   {
      gpu_pipe_core_stall += extra;
      gpu_pipe_stall_total += extra;
   }
}

/* A load/store opcode touched `addr`.  Local space (GPU regs, blitter
 * regs, local RAM: $F02000-$F03FFF) never uses the gateway: a local
 * load arms the 3-tick use-block and the local engine accepts one op
 * per 2 ticks.  Anything else is a split-transaction external
 * transfer: TWO pending slots, a store first drains pending loads
 * (EXECON.NET), the issue phase serializes back-to-back memops, and
 * only USE of the result (or slot exhaustion) blocks. */
static void GPUPipeMemAccess(uint32_t addr, int isLoad)
{
   uint64_t t, t0, done;
   uint32_t cost;
   int slot;
   addr &= 0xFFFFFF;
   t0 = gpu_pipe_clock + gpu_bus_stall + gpu_pipe_core_stall;
   t  = t0;
   if (addr >= 0xF02000 && addr <= 0xF03FFF)
   {
      if (gpu_local_busy_until > t)
         t = gpu_local_busy_until;
      gpu_local_busy_until = t + GPU_PIPE_LOCAL_REPEAT;
      if (isLoad)
      {
         gpu_reg_ready[gpu_opcode_second_parameter] = t + GPU_PIPE_LOCAL_LOAD;
         gpu_reg_ready_ext[gpu_opcode_second_parameter] = 0;
      }
      /* Local RAM is clocked at the GPU clock: core time. */
      if (t > t0)
      {
         gpu_pipe_core_stall += (uint32_t)(t - t0);
         gpu_pipe_stall_total += t - t0;
      }
      return;
   }
   gpu_pipe_ext_total++;
   if (!isLoad)
   {
      /* Store: wait out every pending load first. */
      if (gpu_ext_done[0] > t) t = gpu_ext_done[0];
      if (gpu_ext_done[1] > t) t = gpu_ext_done[1];
   }
   else if (gpu_ext_done[0] > t && gpu_ext_done[1] > t)
   {
      /* Both slots pending: third load waits for the earlier one. */
      t = (gpu_ext_done[0] < gpu_ext_done[1]) ? gpu_ext_done[0]
                                              : gpu_ext_done[1];
   }
   if (gpu_gate_issue_free > t)
      t = gpu_gate_issue_free;
   if (t > t0)
   {
      gpu_bus_stall += (uint32_t)(t - t0);
      gpu_pipe_stall_total += t - t0;
   }
   cost = bus_arbiter_charge_access(BM_GPU, addr);
   if (cost == 0)
      cost = GPU_PIPE_IO_CLKS;
   done = t + GPU_PIPE_EXT_ISSUE + GPU_PIPE_GRANT_CLKS + cost
        + (isLoad ? GPU_PIPE_EXT_RETURN : 0);
   gpu_gate_issue_free = t + GPU_PIPE_EXT_ISSUE;
   slot = (gpu_ext_done[0] <= gpu_ext_done[1]) ? 0 : 1;
   gpu_ext_done[slot] = done;
   if (isLoad)
   {
      gpu_reg_ready[gpu_opcode_second_parameter] = done;
      gpu_reg_ready_ext[gpu_opcode_second_parameter] = 1;
   }
}

/* Hook for the load/store opcode bodies.  Pipeline model on: gateway +
 * scoreboard semantics above (the access itself is then NOT charged as
 * an immediate stall -- it runs in the background).  Off: the legacy
 * dram_timing immediate self-cost, exactly as before. */
#define GPU_PIPE_LOAD(addr) \
   do { \
      if (vjs.gpuPipelineTiming) GPUPipeMemAccess((addr), 1); \
      else GPU_EXT_ACCESS(addr); \
   } while (0)
#define GPU_PIPE_STORE(addr) \
   do { \
      if (vjs.gpuPipelineTiming) GPUPipeMemAccess((addr), 0); \
      else GPU_EXT_ACCESS(addr); \
   } while (0)

#define GPU_TRACE_DEBUG 0
#if GPU_TRACE_DEBUG
#define GPU_TRACE(...) LOG_DBG("[GPU-TRACE] " __VA_ARGS__)
#else
#define GPU_TRACE(...) do {} while(0)
#endif

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

static const uint8_t gpu_opcode_cycles[64] =
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

static uint8_t gpu_ram_8[0x1000];
uint32_t gpu_pc;


/* Diagnostic IRQ counters (see gpu.h). Pure observability — incremented on
 * GPUSetIRQLine(line, ASSERT_LINE), reset in GPUReset. */
uint32_t gpu_irq0_count = 0;
uint32_t gpu_irq3_count = 0;
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

/* Branch delay-slot IRQ hazard state.  gpu_opcode_jump/jr execute their
 * delay-slot instruction inline and then apply the branch target to gpu_pc.
 * If the delay-slot instruction's side effects dispatch a GPU interrupt
 * synchronously -- the canonical case is the CD BIOS / game streaming-ISR
 * epilogue `JUMP T,(Rret)` with `STORE Rflags,(G_FLAGS)` in the delay slot,
 * where the store clears IMASK while a FIFO interrupt is already latched --
 * then GPUHandleIRQs must push the BRANCH TARGET as the return address
 * (the delay slot has completed; the next instruction is the target), and
 * the jump must NOT overwrite gpu_pc afterwards, or the vector jump is
 * clobbered: IMASK stays set forever, no interrupt is ever delivered again,
 * and CD transfers wedge (video_stall / cd_seek_wedge with G_FLAGS IMASK
 * stuck).  Transient within a single opcode's execution -- never live at a
 * savestate boundary, so deliberately not serialized. */
static uint32_t gpu_ds_branch_target;
static uint8_t  gpu_in_delay_slot;
static uint8_t  gpu_ds_irq_dispatched;

/* 68K -> GPU-local-RAM communication sync.
 *
 * JaguarExecuteNew() runs the 68000 for a whole scheduler slice and only then
 * gives the GPU the matching number of RISC cycles, so the 68000 can advance
 * a couple of hundred cycles -- easily a whole interrupt entry -- before the
 * GPU observes anything the 68000 wrote.  On silicon the two run
 * concurrently: the GPU is clocked at the full system clock and the 68000 at
 * system_clock/2 (JTRM clock hierarchy: 26.590906 MHz vs 13.295453 MHz NTSC,
 * exactly 2:1), and the 68000 is the *lowest* priority bus master (JTRM bus
 * priority table: CPU is 11 of 11, below GPU normal at 9), so a GPU spinning
 * on a location in its own local RAM samples a 68000 write within a handful
 * of RISC cycles.
 *
 * Pitfall: The Mayan Adventure depends on that.  Its GPU parks in a 3-word
 * poll loop on a mailbox at $F03E30, and the 68000 feeds it a parameter block
 * at $F03E00 followed by the routine address in the mailbox.  With the
 * coarse-grained interleaving the 68000 got as far as taking an interrupt and
 * rewriting a parameter before the GPU sampled the mailbox, so the object
 * list builder ran with the *next* caller's element count (800 instead of
 * 213) and wrote 587 longwords of colour data past the end of its buffer,
 * over the game's own data at $43FBC-$448E8 (issue #138: it corrupts the
 * palette-fade parameter block, whose GPU ISR then loops for ~65000
 * iterations, never reaches its epilogue, and leaks 4 bytes of GPU stack per
 * interrupt until r31 walks out of local RAM; it also leaves the odd pointer
 * that makes the 68000 take the address error fixed in 3ba2f56).
 *
 * GPUSyncToM68K() closes the gap without changing anybody's cycle budget: at
 * a 68000 write into GPU local RAM the GPU is advanced to the position the
 * 68000 has already reached inside the current slice (68K cycles run x 2),
 * clamped to the slice budget, and the scheduler's end-of-slice call then
 * runs only the remainder.  Total RISC cycles per slice are unchanged; only
 * *when* within the slice they are spent moves. */
static int32_t gpuSliceBudget;
static int32_t gpuSliceSpent;

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

static const uint32_t gpu_convert_zero[32] =
	{ 32,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31 };

uint8_t * branch_condition_table = 0;
#define BRANCH_CONDITION(x)	branch_condition_table[(x) + ((jaguar_flags & 7) << 5)]

static uint32_t gpu_in_exec = 0;
static uint32_t gpu_releaseTimeSlice_flag = 0;
/* GPU execution LIVENESS counter -- not an exact executed-opcode count:
 * delay-slot instructions run inline inside jump/jr without incrementing
 * it.  That cannot produce a frozen counter on a live core (the jump/jr
 * that owns the delay slot is itself counted), which is all the consumer
 * needs: the crash watchdog (src/core/crash_detect.c) asks "is the GPU
 * actually executing?" -- the sampled PC alone aliases on wait/spin loops
 * under deterministic slice budgets (the Super Burnout false positive,
 * issue #378 pilot).  Wraparound is fine: consumers compare deltas. */
uint32_t gpu_exec_opcode_count = 0;

/* Timeslice bookkeeping for GPU-raised 68K interrupts (CPUINT).
 *
 * The main loop runs the 68K's timeslice to completion BEFORE the GPU's
 * (JaguarExecuteNew), so the GPU observes 68K writes "from the future": a
 * mailbox command written near the END of a 68K slice is visible to the GPU
 * at the START of its own slice covering the same emulated interval.  If the
 * GPU responds with a CPUINT and we deliver it synchronously, the 68K can
 * receive the interrupt BEFORE it has executed the instructions that follow
 * the mailbox write -- on silicon that ordering is physically impossible,
 * because the GPU's decode path from mailbox poll to the G_CTRL store takes
 * thousands of GPU cycles while `move.l -> stop` is ~6 CPU cycles.  Games
 * built on the standard coprocessor handshake (command; stop; wait for the
 * one-shot CPUINT) then deadlock on a lost wakeup (BrainDead 13 FMV engine).
 *
 * Fix: deliver GPU-raised CPUINT through the event scheduler at
 *   (slice budget + GPU cycles actually consumed to reach the store) usec,
 * i.e. the GPU's own measured decode latency replayed into the 68K's next
 * slice.  That is provably never EARLIER than silicon (write time <= slice
 * end, decode consumed >= true decode path) and late by at most one slice
 * (~32 usec), well inside real-world interrupt-latency slack.  No tuned
 * constants: the offset is whatever the emulated GPU actually executed. */
static int32_t gpuExecSliceBudget = 0;
static int32_t gpuExecSliceRemaining = 0;

void GPUReleaseTimeslice(void)
{
	gpu_releaseTimeSlice_flag = 1;
}

/* Event-scheduler callback: deliver a GPU-raised CPUINT to the 68K.
 * TOMSetPendingGPUInt() latches the pending bit and asserts the 68K IRQ
 * if the GPU interrupt is still enabled in INT1 (TOMAssertEnabledIRQs). */
void GPUCPUINTCallback(void)
{
	TOMSetPendingGPUInt();
}

uint32_t GPUGetPC(void)
{
	return gpu_pc;
}

/* Diagnostic-only accessor, added for `monitor regs gpu` (the GDB stub,
 * issue #652): the raw G_CTRL value (GPUGO, SINGLE_STEP, IMASK and the
 * REGPAGE bit), read-only -- writing this is not exposed, since it is
 * exactly the register real hardware uses to start/stop the RISC core
 * and a debugger accidentally toggling GPUGO would be far more
 * surprising than useful. */
uint32_t GPUGetControl(void)
{
	return gpu_control;
}

/* Diagnostic-only accessor (issue #406 investigation): expose the active
 * register bank so test harnesses can inspect GPU register state without
 * a full savestate. Not part of the shipped ABI (production link uses
 * exports.list, which does not have the _GPU* wildcard). Also used
 * unconditionally by the GDB stub (src/debug/gdbtarget.c, issue #652)
 * to serve GDB thread 2's register file -- see GPUSetReg below for the
 * write side. */
uint32_t GPUGetReg(int n)
{
	if (n < 0 || n > 31)
		return 0;
	return gpu_reg[n];
}

/* Write side of GPUGetReg, added for the GDB stub (issue #652): "G"
 * (write all registers) pokes the ACTIVE bank directly, exactly
 * mirroring what GPUGetReg reads -- not the $F02000-$F020FF MMIO window,
 * which addresses both banks unconditionally by register number and
 * would silently write the wrong one whenever REGPAGE has bank_1
 * active. A raw poke, like m68k_set_reg(): no side effects, same as any
 * other debugger register write. */
void GPUSetReg(int n, uint32_t v)
{
	if (n < 0 || n > 31)
		return;
	gpu_reg[n] = v;
}

/* GPUGetPC's write counterpart, added for the GDB stub (issue #652). A
 * raw poke of gpu_pc, not a simulated G_CTRL PC-latch write -- GDB
 * setting PC must not also toggle GPUGO or any other control-register
 * side effect. */
void GPUSetPC(uint32_t pc)
{
	gpu_pc = pc;
}

/* Diagnostic-only accessor: raw GPU_FLAGS/control register value. Like
 * dsp_flags read by DSPGetFlags() in dsp.c, the live N/C/Z bits (kept
 * separately in gpu_flag_n/c/z for cheap RMW) are merged into gpu_flags
 * only on the $F02100 register-read path (see the case 0x00 block
 * above), so this can read one flag update stale mid-instruction --
 * snapshot-quality only, not for cycle-exact NCZ probes.
 *
 * Originally vjtrace-only (#408) and gated behind VJ_TRACE; the GDB stub
 * (issue #652) needs it in every build (shipped-by-default, off by
 * default -- see docs/gdb-stub-design.md), so it is unconditional now.
 * This does not change the shipped dylib's exported symbol list --
 * production links still use exports.list, which never listed this. */
uint32_t GPUGetFlags(void)
{
	return gpu_flags;
}

/* Write side of GPUGetFlags, added for the GDB stub (issue #652). A raw
 * poke of gpu_flags -- see the GPUSetPC comment above for why this
 * bypasses the G_FLAGS MMIO write path (interrupt-mask side effects a
 * debugger write should not trigger as a side effect of merely wanting
 * to see a different N/C/Z combination). */
void GPUSetFlags(uint32_t v)
{
	gpu_flags = v;
}

#ifdef VJ_TRACE
/* Diagnostic-only accessor (vjtrace #408 snapshot export): expose GPU
 * local work RAM (the REGSGPU/GPURAM sections of vjtrace_snapshot()).
 * Same not-in-shipped-ABI caveat as GPUGetReg above. */
uint8_t * GPUGetRAM(void)
{
	return gpu_ram_8;
}
#endif /* VJ_TRACE */

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

	return JaguarReadLong(offset, who);
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
   else if ((offset == GPU_WORK_RAM_BASE + 0x0FFF) || (offset == GPU_CONTROL_RAM_BASE + 0x1F))
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
               bool wasIMASK = (gpu_flags & IMASK) ? 1 : 0;
               bool IMASKCleared = wasIMASK && !(data & IMASK);
               gpu_flags = (data & ~IMASK) | ((data & IMASK) ? (gpu_flags & IMASK) : 0);
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
            /* Immediate retarget.  A write to G_PC while the GPU is
             * already running is a jump; record it so gpu_runaway does
             * not treat the new page as a data-buffer escape.  A write
             * while stopped is the start address for the next GO --
             * recording it here matches the GO-edge call below. */
            gpu_pc = data;
            CrashDetectNoteGPUGo(gpu_pc);
            break;
         case 0x14:
            {
               int wasRunning = GPU_RUNNING;

               data &= ~0xF7C0;		// Disable writes to INT_LAT0-4 & TOM version number

               // check for GPU -> CPU interrupt
               if (data & 0x02)
               {
                  if (TOMIRQEnabled(IRQ_GPU))
                  {
                     //This is the programmer's responsibility, to make sure the handler is valid, not ours!
                     //					if ((TOMIRQEnabled(IRQ_GPU))// && (JaguarInterruptHandlerIsValid(64)))
                     if (who == GPU && !m68k_is_stopped())
                     {
                        /* GPU-raised CPUINT while the 68K is RUNNING: never
                         * deliver synchronously -- the 68K may still be on
                         * its way to the `stop` of a command/stop/wait
                         * handshake, and consuming the one-shot wakeup
                         * before the halt deadlocks it.  See the
                         * gpuExecSliceBudget comment block.  (If the 68K is
                         * already stopped, immediate delivery just wakes it
                         * -- no race is possible -- so we keep the cheap
                         * path and avoid fragmenting the timeslice.) */
                        double riscUSec = (vjs.hardwareTypeNTSC
                           ? RISC_CYCLE_IN_USEC : RISC_CYCLE_PAL_IN_USEC);
                        double consumed = (double)(gpuExecSliceBudget - gpuExecSliceRemaining);
                        SetCallbackTime(GPUCPUINTCallback,
                           ((double)gpuExecSliceBudget + consumed) * riscUSec, EVENT_MAIN);
                        GPUReleaseTimeslice();
                     }
                     else
                     {
                        TOMSetPendingGPUInt();
                        m68k_set_irq(2);			// Set 68000 IPL 2
                        GPUReleaseTimeslice();
                     }
                  }
                  data &= ~0x02;
               }

               /* Apply GO and the other persistent control bits BEFORE
                * processing the CPU->GPU interrupt request below: a single
                * write may set GPUGO and INT0 together, and the interrupt
                * is only captured by a RUNNING GPU (see GPUSetIRQLine) —
                * so the GO bit must land first.  $06 (the two transient
                * interrupt-request bits) never persists in gpu_control. */
               gpu_control = (gpu_control & 0xF7C0) | (data & ~(0xF7C0 | 0x06));

               // GO/STOP transition trace: covers both host-issued G_CTRL
               // writes and GPU self-writes (STORE opcodes route through
               // this same GPUWriteLong path with who == GPU).
               if (!wasRunning && GPU_RUNNING)
               {
                  VJT_EMIT(VJT_EV_GPU_GO, GPU, 0, gpu_pc);
                  CrashDetectNoteGPUGo(gpu_pc);
               }
               else if (wasRunning && !GPU_RUNNING)
                  VJT_EMIT(VJT_EV_GPU_STOP, GPU, 0, gpu_pc);

               // check for CPU -> GPU interrupt #0
               if (data & 0x04)
               {
                  GPUSetIRQLine(0, ASSERT_LINE);
                  m68k_end_timeslice();
                  DSPReleaseTimeslice();
                  data &= ~0x04;
               }

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

   /* A halted GPU (G_CTRL GPUGO=0) cannot service interrupts: the dispatch
    * sequence (push return address via r31, jump to vector) is executed by
    * the RISC core itself, and a stopped core executes nothing.  Dispatching
    * here while stopped pushes a return address through the STOPPED
    * context's r31 — if the 68K halted the GPU mid-handler (as Hover
    * Strike's CD driver does around seeks), that push lands inside GPU
    * code/variables and corrupts them.  A latch that was captured while the
    * GPU was still running is serviced by GPUExec() when it next runs.
    *
    * Note this is only about *servicing*.  A halted GPU does not capture new
    * interrupts at all — GPUSetIRQLine drops the assert rather than latching
    * it, so nothing accumulates across a stopped window.  The two rules are
    * pinned by tests/op/op_gpu_int_object{,_halted}.s in the acid suite.
    *
    * A single-step-paused GPU (G_CTRL SINGLE_STEP, bit 3) is the same case:
    * GPUGO is still set but the RISC core is not advancing (it is parked in a
    * coprocessor barrier — see GPUExec), so it likewise cannot run a dispatch
    * sequence.  Dispatching would retarget gpu_pc to a vector mid-barrier and
    * corrupt the handshake; defer until SINGLE_STEP clears and the core runs. */
   if (!GPU_RUNNING || (gpu_control & 0x08))
      return;

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

   /* Bank switch: the pipeline model's pending-load tracking is
    * per-register-number in the CURRENT bank; dispatching into the
    * other bank invalidates it.  Dropping the (tens of cycles of)
    * outstanding state is bounded and safe -- see the model header. */
   if (vjs.gpuPipelineTiming)
      GPUPipeTimingReset();

   // subqt  #4,r31		; pre-decrement stack pointer
   // move  pc,r30			; address of interrupted code
   // store  r30,(r31)     ; store return address
   /* That "store r30,(r31)" is an ordinary RISC LONG store, so hardware
    * ignores address bits 1-0 (see the alignment note on gpu_opcode_storep).
    * A game that takes a GPU interrupt while r31 still holds an unaligned
    * value pushes the return address into the longword containing r31 -- it
    * cannot straddle two longwords and corrupt the neighbour, which is what
    * an unmasked push here would do. */
   gpu_reg[31] -= 4;
   if (gpu_in_delay_slot)
   {
      /* Dispatch triggered by the delay-slot instruction itself (e.g. an
       * IMASK-clearing G_FLAGS store in an ISR epilogue's delay slot).
       * The delay slot has already executed, so the interrupted-code
       * address is the pending BRANCH TARGET, not gpu_pc (which still
       * points just past the delay slot).  Flag the in-flight jump opcode
       * so it does not overwrite gpu_pc (the vector) afterwards. */
      GPUWriteLong(gpu_reg[31] & 0xFFFFFFFC, gpu_ds_branch_target - 2, GPU);
      gpu_ds_irq_dispatched = 1;
   }
   else
      GPUWriteLong(gpu_reg[31] & 0xFFFFFFFC, gpu_pc - 2, GPU);

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
      /* A halted GPU (GPUGO=0) does not capture interrupts: the RISC
       * core's clock is stopped, so nothing samples the interrupt lines
       * and no latch accumulates.  Dropping the assert here is what real
       * silicon does — the source's LEVEL either still holds when the
       * GPU is restarted (and re-edges once software services it) or the
       * condition was consumed by the 68K in the meantime (e.g. a polled
       * DSA response) and no interrupt should be seen at all.  Latching
       * here instead meant every 68K stop/reprogram/restart of the GPU
       * could dispatch a stale interrupt into the new program before its
       * r31 stack init, pushing a return address into GPU code — Hover
       * Strike's B-skip lockup.  A single-step-paused GPU (SINGLE_STEP, bit 3)
       * is likewise not sampling — GPUGO is set but the core is parked in a
       * coprocessor barrier — so drop the assert the same way; the source
       * re-edges once the 68K clears SINGLE_STEP and the core resumes. */
      if (!GPU_RUNNING || (gpu_control & 0x08))
         return;
      /* Diagnostic counters — see gpu.h */
      if (irqline == 0) gpu_irq0_count++;
      else if (irqline == 3) gpu_irq3_count++;

      gpu_control |= mask;			// Assert the interrupt latch
      GPUHandleIRQs();				// And handle the interrupt...
   }
}

void GPUInit(void)
{
   build_branch_condition_table();

   GPUReset();
}

void GPUDone(void)
{
   /* Release the branch-condition LUT so process-lifetime ASAN runs
    * don't report it as a leak.  Unconditional: free(NULL) is a
    * no-op, and a subsequent GPUInit() re-allocates cleanly because
    * build_branch_condition_table() early-outs on non-NULL pointer. */
   free(branch_condition_table);
   branch_condition_table = NULL;
}

/* Drop the sub-cycle bus-stall remainder when the RISC clock scale
 * (possibly) changed, so a new scale starts from a clean accumulator.
 * Mirrors M68KClockScaleReset() for m68kScaleAccum. */
void GPUClockScaleReset(void)
{
   gpu_stall_scale_accum = 0;
}


void GPUReset(void)
{
   unsigned i;

   GPUPipeTimingReset();
   /* Machine reset is the diagnostics' epoch (see their declaration). */
   gpu_pipe_stall_total = 0;
   gpu_pipe_ext_total   = 0;

   // GPU registers (directly visible)
   gpu_flags			  = 0x00000000;
   gpu_matrix_control    = 0x00000000;
   gpu_pointer_to_matrix = 0x00000000;
   gpu_data_organization = 0xFFFFFFFF;
   gpu_pc				  = 0x00F03000;
   gpu_irq0_count       = 0;
   gpu_irq3_count       = 0;
   gpuExecSliceBudget   = 0;
   gpuExecSliceRemaining = 0;
   gpu_control			  = 0x00002800;			// Correctly sets this as TOM Rev. 2
   gpu_hidata			  = 0x00000000;
   gpu_remain			  = 0x00000000;			// These two registers are RO/WO
   gpu_div_control		  = 0x00000000;

   // GPU internal register
   gpu_acc				  = 0x00000000;

   // Delay-slot IRQ hazard state (transient; reset for iOS static-state hygiene)
   gpu_ds_branch_target  = 0x00000000;
   gpu_in_delay_slot     = 0;
   gpu_ds_irq_dispatched = 0;

   /* Scheduler-slice sync state.  Like the delay-slot hazard flags above this
    * is transient inside one JaguarExecuteNew() slice and is never live at a
    * savestate boundary, so it is deliberately not serialized. */
   gpuSliceBudget        = 0;
   gpuSliceSpent         = 0;

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
      *((uint32_t *)(&gpu_ram_8[i])) = JaguarRand();
}

uint32_t GPUReadPC(void)
{
   return gpu_pc;
}

void GPUResetStats(void)
{
}

int GPUIsRunning(void)
{
   return GPU_RUNNING ? 1 : 0;
}

/* See gpu.h: matches the assert-drop condition in GPUSetIRQLine (a
 * stopped or single-step-parked core samples no interrupt lines). */
int GPUCanCaptureIRQ(void)
{
   return (GPU_RUNNING && !(gpu_control & 0x08)) ? 1 : 0;
}

/* Is the Object Processor interrupt (IRQ3) enabled in G_FLAGS?  Used by
 * OPProcessList to decide whether a GPU object's inline release-wait can
 * ever be serviced — if the game never enabled IRQ3, waiting is futile. */
int GPUOPInterruptEnabled(void)
{
   return (gpu_flags & 0x80) ? 1 : 0;
}

void GPUDumpState(const char *tag)
{
   LOG_INF("[GPU %s] PC=%08X ctrl=%08X flags=%08X running=%d\n",
      tag ? tag : "", gpu_pc, gpu_control, gpu_flags, GPU_RUNNING ? 1 : 0);
}

/* Called by JaguarExecuteNew() before the 68000 runs, with the RISC cycle
 * count the GPU will be given for this slice.  See the comment on
 * gpuSliceBudget. */
void GPUBeginSlice(uint32_t riscCycles)
{
   gpuSliceBudget = (int32_t)riscCycles;
   gpuSliceSpent  = 0;
}

/* RISC cycles of the current slice that the scheduler still owes the GPU. */
int32_t GPUSliceRemaining(void)
{
   int32_t left = gpuSliceBudget - gpuSliceSpent;
   return (left > 0 ? left : 0);
}

/* Advance the GPU to the 68000's position inside the current slice.  Called
 * once per completed 68000 write access into GPU local RAM (see
 * m68k_write_memory_* in jaguar.c).
 *
 * It has to be the *whole* 68000 access, not each bus half: a 68000 MOVE.L
 * reaches TOM as two word writes, and running the GPU between them let it
 * sample a half-written mailbox -- Pitfall's poll loop read $00F00000 and
 * jumped into the TOM register file during boot.  Our 68000 core executes an
 * instruction atomically, so the closest available approximation to real
 * concurrency is to advance the GPU only once the access is complete. */
void GPUSyncToM68K(void)
{
   int32_t target, run;

   /* A halted or single-step-paused GPU is not executing; nothing to sync.
    * gpu_in_exec guards against re-entering the exec loop (the OP runs the
    * GPU inline from inside a halfline callback -- see op.c). */
   if (!GPU_RUNNING || (gpu_control & 0x08) || gpu_in_exec)
      return;

   /* GPU is clocked at twice the 68000 (JTRM clock hierarchy).  With
    * clock scales (issue #314) both sides run in their own scaled cycle
    * domain, so map through wall time: 68K cycles run are divided by the
    * M68K scale to get wall position, then multiplied by 2 and the RISC
    * scale to get the GPU's scaled position.  At 100/100 this is
    * (run * 200) / 100 == run * 2, an exact identity. */
   target = (int32_t)(((int64_t)m68k_cycles_run() * 2 * riscClockScalePct)
                      / m68kClockScalePct);
   if (target > gpuSliceBudget)
      target = gpuSliceBudget;

   run = target - gpuSliceSpent;
   if (run <= 0)
      return;

   /* Bracketed here, not around GPUExec's body: this measures the GPU time
    * a 68K bus access pulled in, which is the term that makes vj_m68k_slice
    * interpretable (perf_iface.h).  It NESTS inside vj_gpu_exec on purpose. */
   gpuSliceSpent += run;
   VJP_ENTER(VJP_GPU_SYNC);
   GPUExec(run);
   VJP_LEAVE(VJP_GPU_SYNC);
}

/* ==================================================================
 * Cycle-exact idle-loop fast-forward   (issue #569, GPU port)
 * ==================================================================
 *
 * Port of the DSP idle-loop fast-forward (src/jerry/dsp.c -- read the
 * theorem/proof block there first; this comment only records what is
 * DIFFERENT on the GPU).  Motivation and loop shapes:
 * docs/perf-audit/mc3d-stall-attribution.md -- Missile Command 3D
 * parks the GPU in a 3-instruction semaphore poll at $F03134 for 66.8%
 * of all interpreted GPU instructions, jagniccc's $F03042 mailbox poll
 * and yarc's $F03192 self-jr are ~100% of their GPU samples.  Same
 * contract as the DSP: NOT an approximation -- registers, flags, PC,
 * cycles charged and gpu_exec_opcode_count all end up exactly where
 * the interpreter would have left them.
 *
 * ---- SAFETY THEOREM, GPU DELTAS (verified against this tree) --------
 *
 * (1) Nothing else in the machine executes inside one GPUExec() call.
 *     Same scheduler argument as the DSP: JaguarExecuteNew() runs the
 *     68K, then GPUExec, then DSPExec; events (OP halfline, timers,
 *     I2S) are dispatched by HandleNextEvent AFTER the exec calls
 *     return.  GPUSyncToM68K() is a different, EARLIER GPUExec call
 *     with its own budget (it runs from 68K writes into GPU local RAM,
 *     i.e. during the 68K's slice, never nested inside GPUExec).
 *     MC3D's poll on main-RAM $9704 exits via a 68K write that can
 *     therefore only land BETWEEN GPUExec calls: within one call the
 *     polled word is constant, the loop is a fixed point for the rest
 *     of the call's budget, and skipping to one-iteration-before-
 *     budget-end is exact.  The probe state is reset at every GPUExec
 *     entry, so a probe never spans two calls.
 *
 * (2) No GPU interrupt can be dispatched mid-call for an admitted
 *     body.  STRONGER than the DSP case: GPUExec's only dispatch is
 *     the GPUHandleIRQs() call at slice ENTRY, before the exec loop --
 *     there is no in-loop IMASKCleared re-check because the GPU has no
 *     D_FLAGS retire-delay analog; a G_FLAGS write that clears IMASK
 *     dispatches synchronously INSIDE GPUWriteLong (case 0x00), which
 *     only a store can reach, and the whitelist admits no store.
 *     GPUSetIRQLine's other callers (tom.c timers, op.c, cdrom.c) all
 *     run from event callbacks, i.e. between slices per (1).  So the
 *     probe needs no IMASKCleared/retire-delay reset check at all.
 *
 * (3) The register banks cannot move.  gpu_reg / gpu_alternate_reg are
 *     repointed only by GPUUpdateRegisterBanks(), reachable from a
 *     G_FLAGS write (a store -- excluded) or from GPUHandleIRQs
 *     (excluded by (2)).  The probe still records and re-checks the
 *     bank pointer, same belt-and-braces as the DSP.
 *
 * (4) GPU-side reads of the admitted EA classes are pure.  GPU local
 *     RAM ($F03000-$F03FFF, 4K -- not the DSP's 8K) is served straight
 *     out of gpu_ram_8 by GPUReadLong with no side effect.  Main DRAM
 *     below 2 MB takes JaguarReadLong's `addr < 0x800000` fast path
 *     (src/core/jaguar.c), whose only side effects are VJT_WATCH_RD
 *     (a hard gate below) and BlitMemoNoteRead (gated on
 *     blitMemoRecording, a hard gate below); busArbiter is charged
 *     there only for `who == OP`.  Register space ($F02000-$F020FF,
 *     the MMIO view of the banks) and G_CTRL space are NOT admitted:
 *     bank values change per iteration and the control decode has
 *     side effects.
 *
 * ---- ADMISSION / PROOF DELTAS ---------------------------------------
 *
 * Identical to the DSP (three head snapshots S0/S1/S2 driven by
 * ordinary interpreted iterations, elementwise-equal deltas across
 * both banks, identical flags at all three heads, measured -- never
 * recomputed -- cycle and opcode costs, nonzero-delta registers
 * touched only by addqt/subqt onto themselves, EA checks deferred to
 * S2) except for:
 *
 *   - THE EXECUTED-PATH IDENTITY IS opcost == idleBodyCount, NOT
 *     idleBodyCount + 1.  gpu_exec_opcode_count is a liveness counter
 *     incremented ONLY in GPUExec's main loop (see its declaration):
 *     the delay slot inlined by gpu_opcode_jump/jr does NOT increment
 *     it, unlike dsp_opcode_jump/jr.  One straight-line traversal of
 *     the decoded body therefore charges (idleBodyCount - 1) body
 *     instructions plus the loop-closing jr = idleBodyCount, with the
 *     delay slot uncounted.  The soundness argument survives: any
 *     OTHER route from one head arrival to the next executes the whole
 *     body and the (not-taken) jr, then at least one further main-loop
 *     -counted instruction to get back -- inlined delay slots are the
 *     only uncounted instructions and they cannot BE the route (the
 *     branch owning them is itself counted) -- so any compound period
 *     costs strictly more than idleBodyCount and exact equality admits
 *     the straight-line traversal and nothing else.  This is pinned by
 *     test/tools/gpu_idle_probe_falsify.
 *
 *   - Load EA rules follow the GPU's own opcode semantics
 *     (gpu_opcode_load* above), which differ from the DSP's in the
 *     masking: loadb/loadw take the pure containing-long local read
 *     only when the RAW address is inside local RAM (outside, they
 *     divert to JaguarReadByte/Word, unaudited -- rejected); load
 *     masks ~3 unconditionally; the indexed and ri forms do NOT mask
 *     the base -- they test the RAW effective address for the local
 *     range and mask only in that branch, so the admission check here
 *     classifies the raw EA exactly as the opcode will.
 *
 *   - Delay-slot IRQ hazard (gpu_ds_irq_dispatched): an admitted body
 *     contains no store, so its delay slot cannot dispatch an
 *     interrupt and the probe can never be reached with gpu_pc
 *     pointing at a vector from its own loop's delay slot.
 *
 * Extrapolation, exit and savestate behavior are the DSP's: n =
 * (cycles / cost) - 1 iterations are applied as reg += n*delta,
 * cycles -= n*cost, gpu_exec_opcode_count += n*opcost (crash_detect's
 * gpu wedge predicate reads that counter), flags/PC already correct by
 * construction; the write that ends the wait lands between slices, the
 * next probe fails, interpretation resumes; no probe state survives a
 * slice, so savestates are untouched.
 */

/* Longest body accepted, in 16-bit words (jr offset range [-8,-1]).
 * Belt-and-braces only: the caller's `pcThis - gpu_pc <= 14` filter
 * already caps [head, jr) at 7 words, so this bound never binds. */
#define GPU_IDLE_MAX_BODY	8
/* Body instruction slots: <= 7 before the jr, plus the delay slot. */
#define GPU_IDLE_MAX_INSN	10
/* Per-slice reject memo: without it a loop that fails the fixed-point
 * test would be re-probed every three iterations for the whole slice. */
#define GPU_IDLE_MEMO		16

/* Effective addresses we will admit for a load: GPU local SRAM (the 4K
 * range GPUReadLong itself serves out of gpu_ram_8) or main DRAM below
 * the 2 MB aperture, where JaguarReadLong is a plain GET32 of
 * jaguarMainRAM.  Register space $F02000-$F020FF stays excluded. */
#define GPU_IDLE_EA_OK(ea) \
	(((ea) >= GPU_WORK_RAM_BASE && (ea) <= GPU_WORK_RAM_BASE + 0xFFF) \
	 || ((ea) < 0x200000))

#define GPU_IDLE_FETCH(a) \
	((uint16_t)(((uint16_t)gpu_ram_8[(a) - GPU_WORK_RAM_BASE] << 8) \
	            | (uint16_t)gpu_ram_8[(a) - GPU_WORK_RAM_BASE + 1]))

/* Option gate (libretro `virtualjaguar_risc_idle_skip`) lives in vjs. */
/* Diagnostics: counted only on the cold probe path, never per opcode. */
uint32_t gpu_idle_skip_fires   = 0;		/* successful extrapolations */
uint32_t gpu_idle_skip_rejects = 0;		/* candidate loops turned down */
uint32_t gpu_idle_skip_iters   = 0;		/* iterations actually skipped */
uint32_t gpu_idle_skip_opcodes = 0;		/* opcodes NOT interpreted -- the
										 * honest, host-independent measure:
										 * gpu_exec_opcode_count is advanced
										 * over a skip on purpose, so it does
										 * NOT show the saving. */

static int       idleProbeStage;		/* 0 = none, 1 = have S0, 2 = have S1 */
static uint32_t  idleProbeHead;
static uint32_t  idleProbeJr;
static uint32_t *idleProbeBank;			/* gpu_reg at S0 -- see theorem (3) */
static int32_t   idleProbeCyc0, idleProbeCyc1;
static uint32_t  idleProbeOpc0, idleProbeOpc1;
static uint32_t  idleProbeS0[64], idleProbeS1[64];
static uint8_t   idleProbeFz0, idleProbeFn0, idleProbeFc0;
static uint8_t   idleProbeFz1, idleProbeFn1, idleProbeFc1;

static uint8_t   idleBodyIdx[GPU_IDLE_MAX_INSN];
static uint8_t   idleBodyP1[GPU_IDLE_MAX_INSN];
static uint8_t   idleBodyP2[GPU_IDLE_MAX_INSN];
static uint32_t  idleBodyImm[GPU_IDLE_MAX_INSN];
static int       idleBodyCount;

static uint32_t  idleMemoHead[GPU_IDLE_MEMO];
static uint32_t  idleMemoJr[GPU_IDLE_MEMO];
static int       idleMemoCount;
static int       idleMemoNext;

/* Opcode whitelist -- the DSP's list transposed to the GPU dispatch
 * table.  Indices 0-31 and 34-44/57-59 name the same operations on
 * both processors; the divergent slots are 32/33 (GPU sat8/sat16 vs
 * DSP subqmod/sat16s -- 33 is a pure RN->RN saturate on both and stays
 * admitted, 32 differs and stays out), 42 (GPU loadp reads AND writes
 * gpu_hidata, which the snapshot does not model -- rejected, where the
 * DSP's 42 was sat32s rejected for reading the accumulator), 48 (GPU
 * storep -- a store, out like every store) and 62/63 (GPU sat24/pack
 * -- pure, but left out to keep the whitelist to opcodes wait loops
 * actually use, same policy as the DSP's mirror/move_pc).  Both
 * branches (52/53) are deliberately absent: excluding every
 * PC-modifying opcode but the loop-closing jr itself is load-bearing
 * for the executed-path check. */
static int gpu_idle_op_admitted(uint32_t idx)
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
	case 33:								/* sat16 */
	case 34: case 35: case 36: case 37:		/* move moveq moveta movefa */
	case 38:								/* movei */
	case 39: case 40: case 41:				/* loadb loadw load */
	case 43: case 44:						/* load_r14/15_indexed */
	case 57:								/* nop */
	case 58: case 59:						/* load_r14/15_ri */
		return 1;
	default:
		return 0;
	}
}

/* Register operands of an admitted opcode, as indices into the 64-entry
 * snapshot (0..31 = gpu_reg_bank_0, 32..63 = gpu_reg_bank_1).  `cur` is
 * the base of the bank gpu_reg points at, `alt` the other one.  *dst is
 * -1 when the instruction writes no register. */
static int gpu_idle_operands(uint32_t idx, uint32_t p1, uint32_t p2,
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
	case 29: case 33:						/* rorq sat16 */
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

static int gpu_idle_memo_hit(uint32_t head, uint32_t jr)
{
	int i;

	for (i = 0; i < idleMemoCount; i++)
		if (idleMemoHead[i] == head && idleMemoJr[i] == jr)
			return 1;
	return 0;
}

static void gpu_idle_memo_reject(uint32_t head, uint32_t jr)
{
	if (gpu_idle_memo_hit(head, jr))
		return;
	idleMemoHead[idleMemoNext] = head;
	idleMemoJr[idleMemoNext]   = jr;
	idleMemoNext = (idleMemoNext + 1) % GPU_IDLE_MEMO;
	if (idleMemoCount < GPU_IDLE_MEMO)
		idleMemoCount++;
	gpu_idle_skip_rejects++;
}

/* Register-independent decode of target..jr plus the delay slot. */
static int gpu_idle_decode(uint32_t head, uint32_t jrAddr)
{
	uint32_t pc = head;
	uint16_t op;
	uint32_t idx;
	int n = 0;

	idleBodyCount = 0;

	while (pc < jrAddr)
	{
		if (n >= GPU_IDLE_MAX_BODY)
			return 0;
		op  = GPU_IDLE_FETCH(pc);
		idx = (uint32_t)(op >> 10);
		if (!gpu_idle_op_admitted(idx))
			return 0;
		idleBodyIdx[n] = (uint8_t)idx;
		idleBodyP1[n]  = (uint8_t)((op >> 5) & 0x1F);
		idleBodyP2[n]  = (uint8_t)(op & 0x1F);
		idleBodyImm[n] = 0;
		if (idx == 38)						/* movei: opcode + LSW + MSW */
		{
			if (pc + 6 > jrAddr)			/* immediate would overrun the jr */
				return 0;
			idleBodyImm[n] = (uint32_t)GPU_IDLE_FETCH(pc + 2)
			               | ((uint32_t)GPU_IDLE_FETCH(pc + 4) << 16);
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
	op  = GPU_IDLE_FETCH(jrAddr + 2);
	idx = (uint32_t)(op >> 10);
	if (idx == 38 || !gpu_idle_op_admitted(idx))
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
 * register's known value so a load base written earlier in the same
 * body (`movei #addr,r2 ; load (r2),r1`) resolves to the address the
 * load will really use, not to whatever the head snapshot held. */
static int gpu_idle_check_body(const uint32_t *delta, int cur, int alt)
{
	uint32_t kval[64];
	uint8_t  kok[64];
	int src[3];
	int nsrc, dst, s, j, i;
	uint32_t idx, p1, p2, ea;

	for (i = 0; i < 32; i++)
	{
		kval[i]      = gpu_reg_bank_0[i];
		kval[32 + i] = gpu_reg_bank_1[i];
		kok[i]       = 1;
		kok[32 + i]  = 1;
	}

	for (j = 0; j < idleBodyCount; j++)
	{
		idx = idleBodyIdx[j];
		p1  = idleBodyP1[j];
		p2  = idleBodyP2[j];

		if (!gpu_idle_operands(idx, p1, p2, cur, alt, src, &nsrc, &dst))
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

		/* Loads: the effective address must be provably plain RAM.
		 * The EA classification mirrors each gpu_opcode_* body above
		 * exactly -- see the admission-delta comment in the header
		 * block for why the masking differs from the DSP's. */
		switch (idx)
		{
		case 39:							/* loadb */
		case 40:							/* loadw */
			/* Pure containing-long local read only when the RAW
			 * address is inside GPU local RAM; anywhere else these
			 * divert to JaguarReadByte / JaguarReadWord, whose full
			 * TOM/JERRY decode is not audited here. */
			if (!kok[cur + (int)p1])
				return 0;
			ea = kval[cur + (int)p1];
			if (!(ea >= GPU_WORK_RAM_BASE && ea <= GPU_WORK_RAM_BASE + 0xFFF))
				return 0;
			break;
		case 41:							/* load -- masks ~3 always */
			if (!kok[cur + (int)p1])
				return 0;
			ea = kval[cur + (int)p1] & 0xFFFFFFFC;
			if (!GPU_IDLE_EA_OK(ea))
				return 0;
			break;
		case 43:							/* load_r14_indexed -- raw base */
			if (!kok[cur + 14])
				return 0;
			ea = kval[cur + 14] + (gpu_convert_zero[p1] << 2);
			if (!GPU_IDLE_EA_OK(ea))
				return 0;
			break;
		case 44:							/* load_r15_indexed */
			if (!kok[cur + 15])
				return 0;
			ea = kval[cur + 15] + (gpu_convert_zero[p1] << 2);
			if (!GPU_IDLE_EA_OK(ea))
				return 0;
			break;
		case 58:							/* load_r14_ri -- raw sum */
			if (!kok[cur + 14] || !kok[cur + (int)p1])
				return 0;
			ea = kval[cur + 14] + kval[cur + (int)p1];
			if (!GPU_IDLE_EA_OK(ea))
				return 0;
			break;
		case 59:							/* load_r15_ri */
			if (!kok[cur + 15] || !kok[cur + (int)p1])
				return 0;
			ea = kval[cur + 15] + kval[cur + (int)p1];
			if (!GPU_IDLE_EA_OK(ea))
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

static void gpu_idle_snapshot(uint32_t *s)
{
	memcpy(s,      gpu_reg_bank_0, 32 * sizeof(uint32_t));
	memcpy(s + 32, gpu_reg_bank_1, 32 * sizeof(uint32_t));
}

/* Give up on an in-flight probe.  When the loop that displaced it is a
 * different one, memo the abandoned loop: otherwise two interleaved
 * loops could restart each other forever without either reaching S2. */
static void gpu_idle_probe_abandon(uint32_t head, uint32_t jr)
{
	if (idleProbeStage != 0
	    && (idleProbeHead != head || idleProbeJr != jr))
		gpu_idle_memo_reject(idleProbeHead, idleProbeJr);
	idleProbeStage = 0;
}

/* Called from GPUExec immediately after a taken backward/self `jr`, with
 * gpu_pc already at the loop head and the delay slot already executed.
 * Returns the (possibly advanced) cycle budget. */
static int32_t GPUIdleLoopProbe(int32_t cycles, uint32_t head, uint32_t jrAddr)
{
	uint32_t delta[64];
	int32_t  cost, n;
	uint32_t opcost;
	int      cur, alt, i;

	/* Whole body -- including the delay slot and its trailing byte --
	 * must sit in GPU local SRAM, so every fetch is a pure gpu_ram_8
	 * read (GPUExec's own fast path for this range). */
	if (head < GPU_WORK_RAM_BASE
	    || jrAddr + 3 > GPU_WORK_RAM_BASE + 0xFFF)
	{
		gpu_idle_probe_abandon(head, jrAddr);
		return cycles;
	}

	/* No IMASKCleared / retire-delay reset here, deliberately: the GPU
	 * dispatches an IMASK-clearing G_FLAGS write synchronously inside
	 * the store itself, and no store is admitted -- see theorem (2). */

	/* A different loop showed up mid-probe.  Only then -- calling this
	 * unconditionally would reset the probe of the loop we are actually
	 * in the middle of measuring, and nothing would ever reach S2. */
	if (idleProbeStage != 0
	    && (idleProbeHead != head || idleProbeJr != jrAddr))
		gpu_idle_probe_abandon(head, jrAddr);

	if (gpu_idle_memo_hit(head, jrAddr))
		return cycles;

	if (idleProbeStage == 0)
	{
		if (!gpu_idle_decode(head, jrAddr))
		{
			gpu_idle_memo_reject(head, jrAddr);
			return cycles;
		}
		gpu_idle_snapshot(idleProbeS0);
		idleProbeFz0   = gpu_flag_z;
		idleProbeFn0   = gpu_flag_n;
		idleProbeFc0   = gpu_flag_c;
		idleProbeCyc0  = cycles;
		idleProbeOpc0  = gpu_exec_opcode_count;
		idleProbeHead  = head;
		idleProbeJr    = jrAddr;
		idleProbeBank  = gpu_reg;
		idleProbeStage = 1;
		return cycles;
	}

	if (idleProbeStage == 1)
	{
		gpu_idle_snapshot(idleProbeS1);
		idleProbeFz1   = gpu_flag_z;
		idleProbeFn1   = gpu_flag_n;
		idleProbeFc1   = gpu_flag_c;
		idleProbeCyc1  = cycles;
		idleProbeOpc1  = gpu_exec_opcode_count;
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
	    || gpu_flag_z != idleProbeFz0 || gpu_flag_n != idleProbeFn0
	    || gpu_flag_c != idleProbeFc0)
	{
		gpu_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Theorem (3): the bank pointers must not have moved. */
	if (gpu_reg != idleProbeBank)
	{
		gpu_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Cost and opcode count measured, never recomputed: the inlined
	 * delay slot is not charged by GPUExec's own `cycles -=`. */
	cost = idleProbeCyc0 - idleProbeCyc1;
	if (cost <= 0 || (idleProbeCyc1 - cycles) != cost)
	{
		gpu_idle_memo_reject(head, jrAddr);
		return cycles;
	}
	opcost = idleProbeOpc1 - idleProbeOpc0;
	if (opcost == 0 || (gpu_exec_opcode_count - idleProbeOpc1) != opcost)
	{
		gpu_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* EXECUTED-PATH CHECK -- see the header block for the full argument
	 * and for why the GPU identity is idleBodyCount, NOT the DSP's
	 * idleBodyCount + 1: gpu_exec_opcode_count is incremented only in
	 * GPUExec's main loop, never for the delay slot gpu_opcode_jr/jump
	 * inline.  One straight-line traversal charges the (idleBodyCount-1)
	 * decoded body instructions plus the loop-closing jr; any other
	 * route from head back to head must additionally execute at least
	 * one more counted instruction, so exact equality admits the
	 * straight-line traversal and nothing else.  Without this, a
	 * compound period hiding an undecoded store behind a not-taken jr
	 * would pass every other check -- pinned by
	 * test/tools/gpu_idle_probe_falsify. */
	if (opcost != (uint32_t)idleBodyCount)
	{
		gpu_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Per-iteration register delta must be constant across both probes. */
	for (i = 0; i < 32; i++)
	{
		delta[i]      = idleProbeS1[i] - idleProbeS0[i];
		delta[32 + i] = idleProbeS1[32 + i] - idleProbeS0[32 + i];
		if (gpu_reg_bank_0[i] - idleProbeS1[i] != delta[i])
		{
			gpu_idle_memo_reject(head, jrAddr);
			return cycles;
		}
		if (gpu_reg_bank_1[i] - idleProbeS1[32 + i] != delta[32 + i])
		{
			gpu_idle_memo_reject(head, jrAddr);
			return cycles;
		}
	}

	cur = (gpu_reg == gpu_reg_bank_0) ? 0 : 32;
	alt = 32 - cur;

	if (!gpu_idle_check_body(delta, cur, alt))
	{
		gpu_idle_memo_reject(head, jrAddr);
		return cycles;
	}

	/* Always leave one full iteration to execute normally. */
	n = (cycles / cost) - 1;
	if (n <= 0)
		return cycles;

	for (i = 0; i < 32; i++)
	{
		if (delta[i])
			gpu_reg_bank_0[i] += (uint32_t)n * delta[i];
		if (delta[32 + i])
			gpu_reg_bank_1[i] += (uint32_t)n * delta[32 + i];
	}
	cycles -= n * cost;
	gpu_exec_opcode_count += (uint32_t)n * opcost;

	gpu_idle_skip_fires++;
	gpu_idle_skip_iters   += (uint32_t)n;
	gpu_idle_skip_opcodes += (uint32_t)n * opcost;

	return cycles;
}

// Main GPU execution core

void GPUExec(int32_t cycles)
{
   /* Slice-invariant settings, cached in locals for the exec loop (issue
    * #532).  Both are written ONLY by check_variables() in libretro.c --
    * never by emulation code -- so they cannot change while this loop runs.
    * executeOpcode() is an opaque call, so without these locals the compiler
    * must assume the call clobbers both globals and reloads them on every
    * emulated instruction (two `ldrb` of vjs.gpuPipelineTiming plus a
    * GOT-indirect reload of riscClockScalePct per opcode on arm64).
    *
    * Cached at slice ENTRY, not once at init: a mid-run option change goes
    * through check_variables() between retro_run() calls, so the next slice
    * picks the new value up.  Re-entrant runs (the OP executes the GPU
    * inline from a halfline callback, see op.c) re-read on each entry for
    * the same reason. */
   int      pipeTiming;
   uint32_t riscScale;
   int      idleSkipActive;

   if (!GPU_RUNNING)
      return;

   /* Paused in single-step mode: a running GPU that has set G_CTRL
    * SINGLE_STEP (bit 3) does not free-run and does not service interrupts
    * until software clears SINGLE_STEP (or steps it via SINGLE_GO).  See the
    * barrier note in the exec loop below — this is the resting state of Iron
    * Soldier 2's match-load coprocessor handshake between 68K acknowledgements. */
   if (gpu_control & 0x08)
      return;

#ifdef GPU_SINGLE_STEPPING
   if (gpu_control & 0x18)
   {
      cycles = 1;
      gpu_control &= ~0x10;
   }
#endif
   /* After the two guards above, deliberately: a halted or single-stepped
    * GPU does no work, and counting those calls would dilute the average
    * with no-ops.  No return between here and VJP_LEAVE (perf_iface.h). */
   VJP_ENTER(VJP_GPU);

   GPUHandleIRQs();
   gpu_releaseTimeSlice_flag = 0;
   gpu_in_exec++;
   gpuExecSliceBudget = cycles;

   pipeTiming = vjs.gpuPipelineTiming ? 1 : 0;
   riscScale  = riscClockScalePct;

   /* Idle-loop fast-forward gates (issue #569, GPU port).  Same
    * suppression list as DSPExec's -- every entry adds per-instruction
    * state the affine extrapolation does not model, so any of them
    * turns the whole thing off; see the rationale on each item there.
    * The vjtrace/gdb checks are RUNTIME checks for the same reason as
    * the DSP's: the test ABI is built with -DVJ_TRACE, and a compile-
    * time gate would make every headless harness silently measure a
    * disabled feature. */
   idleSkipActive = vjs.riscIdleSkip
                 && !blitMemoMode && !blitMemoRecording
                 && riscClockScalePct == 100
                 && !busArbiter.enabled
                 && !vjs.gpuPipelineTiming
                 && !(gpu_control & 0x18);
#ifdef VJ_TRACE
   if (vjtrace_armed || vjtrace_nwatch)
      idleSkipActive = 0;
#endif
#ifndef VJ_GDB_STUB_DISABLE_HOOKS
   /* GDB stub (issue #652): a GPU breakpoint or pending step inside the
    * loop would be stepped clean over -- same reasoning as DSPExec. */
   if (gdbArmedGPU)
      idleSkipActive = 0;
#endif
   /* Probe + reject memo are re-derived from scratch every call, so
    * nothing here reaches a savestate and nothing survives a slice. */
   idleProbeStage = 0;
   idleMemoCount  = 0;
   idleMemoNext   = 0;

   while (cycles > 0 && GPU_RUNNING)
   {
      uint16_t opcode;
      uint32_t index;
      uint32_t pcThis;
      gpuExecSliceRemaining = cycles;
#ifdef VJ_TRACE
      VJT_PCHIST_GPU(gpu_pc);
#endif
#ifndef VJ_GDB_STUB_DISABLE_HOOKS
      /* GDB stub (issue #652): one load of a hot global, one never-taken
       * branch when no GPU breakpoint/step is armed -- see
       * docs/gdb-stub-design.md "Breakpoint detection". GDBHalt() blocks
       * in place (does not unwind this loop) until the client continues,
       * steps, or disconnects. VJ_GDB_STUB_DISABLE_HOOKS exists only for
       * the Phase 2 A/B perf measurement -- see the comment in
       * src/core/jaguar.c's M68KInstructionHook(). */
      if (gdbArmedGPU && GDBCheckPC(GDB_TGT_GPU, gpu_pc))
         GDBHalt(GDB_TGT_GPU, GDB_STOP_BREAKPOINT, gpu_pc);
#endif

      pcThis = gpu_pc;

      if (gpu_pc >= GPU_WORK_RAM_BASE && gpu_pc < GPU_WORK_RAM_BASE + 0x1000)
      {
         uint32_t off = gpu_pc - GPU_WORK_RAM_BASE;
         opcode = ((uint16_t)gpu_ram_8[off] << 8) | (uint16_t)gpu_ram_8[off + 1];
      }
      else
         opcode = GPUReadWord(gpu_pc, GPU);
      index = opcode >> 10;
      gpu_instruction = opcode;	// Added for GPU #3...
      gpu_opcode_first_parameter  = (opcode >> 5) & 0x1F;
      gpu_opcode_second_parameter = opcode & 0x1F;

      //$E400 -> 1110 01 -> $39 -> 57
      gpu_pc += 2;
      gpu_bus_stall = 0;
      gpu_pipe_core_stall = 0;
      gpu_exec_opcode_count++;
      /* Pipeline model: stall for pending load results / RAW interlock
       * BEFORE the opcode runs (results are unchanged; only time is
       * charged, via the same gpu_bus_stall channel as DRAM costs). */
      if (pipeTiming)
         GPUPipeCheckUse(index);
      executeOpcode(index);
      if (pipeTiming)
      {
         gpu_pipe_clock += (uint64_t)gpu_opcode_cycles[index] + gpu_bus_stall
                         + gpu_pipe_core_stall;
         gpu_pipe_prev_dest =
            (gpu_pipe_flags[index] & 4) ? (uint8_t)gpu_opcode_second_parameter
                                        : (uint8_t)0xFF;
         gpu_pipe_prev_flags = gpu_pipe_sets_flags[index];
         /* DIV runs 16 background ticks (2 bits/tick); only USE of the
          * quotient blocks (R15). */
         if (index == 21)
         {
            gpu_reg_ready[gpu_opcode_second_parameter] =
               gpu_pipe_clock + GPU_PIPE_DIV_TICKS;
            gpu_reg_ready_ext[gpu_opcode_second_parameter] = 0;
         }
      }

      /* Bus stalls are wall time (sysclks); the slice budget is in the
       * GPU's scaled cycle domain.  Convert so a DRAM wait costs the
       * same wall time at any RISC clock scale (bus_arbiter.h contract):
       * at 2x the budget holds twice the cycles per wall second, so the
       * same stall must consume twice the cycles.  Stock scale takes
       * the identity branch — bit-exact pre-#318 behavior. */
      /* gpu_pipe_core_stall is already in the GPU's own cycle domain
       * (see the domain note at its declaration): deduct it unscaled
       * in both branches.  Only gpu_bus_stall -- wall-time memory
       * latency -- goes through the scale conversion. */
      if (riscScale != 100u && gpu_bus_stall != 0)
      {
         uint32_t stall_scaled;
         gpu_stall_scale_accum += gpu_bus_stall * riscScale;
         stall_scaled = gpu_stall_scale_accum / 100u;
         gpu_stall_scale_accum %= 100u;
         cycles -= gpu_opcode_cycles[index] + (int32_t)stall_scaled
                 + (int32_t)gpu_pipe_core_stall;
      }
      else
         cycles -= gpu_opcode_cycles[index] + (int32_t)gpu_bus_stall
                 + (int32_t)gpu_pipe_core_stall;

      /* Idle-loop fast-forward (issue #569, GPU port).  A taken `jr`
       * that landed on or behind its own address, at most 8 words back,
       * is the only candidate; the unsigned difference rejects a
       * not-taken jr (pc = pcThis + 2) and every forward branch in one
       * compare. */
      if (idleSkipActive && index == 53
          && (uint32_t)(pcThis - gpu_pc) <= 14)
         cycles = GPUIdleLoopProbe(cycles, gpu_pc, pcThis);

      /* Single-step barrier (G_CTRL SINGLE_STEP, bit 3): a running RISC core
       * that has just set SINGLE_STEP has entered single-step mode and stops
       * free-running — it now advances only when software clears SINGLE_STEP
       * or writes SINGLE_GO.  Iron Soldier 2's match-load geometry coprocessor
       * job (GPU $F03024) uses this as a producer/consumer barrier: after
       * posting each object's result to a main-RAM mailbox it writes G_CTRL=9
       * (GPUGO|SINGLE_STEP) to pause, and the 68K consumer resumes it by
       * writing G_CTRL=$11 (GPUGO|SINGLE_GO, SINGLE_STEP clear) once it has
       * drained the result.  Without honoring SINGLE_STEP the GPU free-runs the
       * whole job to its self-halt before the 68K reads a single result, then
       * the 68K re-kicks the parked GPU and both wedge (JTRM: bit 3 enables
       * single-step mode). */
      if (gpu_control & 0x08)
         break;
   }

   gpu_in_exec--;

   VJP_LEAVE(VJP_GPU);
}

INLINE static void executeOpcode(uint32_t index) {
#ifdef __GNUC__
	/* Computed-goto dispatch table -- one label per RISC opcode.
	 * GCC/Clang extension: &&label yields the address of a label;
	 * goto *ptr jumps through an arbitrary code address.  This
	 * eliminates the single indirect-branch bottleneck of switch
	 * dispatch and lets the branch predictor track each opcode
	 * independently. */
	static const void *gpu_dispatch[64] = {
		&&gpu_op_add,             &&gpu_op_addc,
		&&gpu_op_addq,            &&gpu_op_addqt,
		&&gpu_op_sub,             &&gpu_op_subc,
		&&gpu_op_subq,            &&gpu_op_subqt,
		&&gpu_op_neg,             &&gpu_op_and,
		&&gpu_op_or,              &&gpu_op_xor,
		&&gpu_op_not,             &&gpu_op_btst,
		&&gpu_op_bset,            &&gpu_op_bclr,
		&&gpu_op_mult,            &&gpu_op_imult,
		&&gpu_op_imultn,          &&gpu_op_resmac,
		&&gpu_op_imacn,           &&gpu_op_div,
		&&gpu_op_abs,             &&gpu_op_sh,
		&&gpu_op_shlq,            &&gpu_op_shrq,
		&&gpu_op_sha,             &&gpu_op_sharq,
		&&gpu_op_ror,             &&gpu_op_rorq,
		&&gpu_op_cmp,             &&gpu_op_cmpq,
		&&gpu_op_sat8,            &&gpu_op_sat16,
		&&gpu_op_move,            &&gpu_op_moveq,
		&&gpu_op_moveta,          &&gpu_op_movefa,
		&&gpu_op_movei,           &&gpu_op_loadb,
		&&gpu_op_loadw,           &&gpu_op_load,
		&&gpu_op_loadp,           &&gpu_op_load_r14_indexed,
		&&gpu_op_load_r15_indexed,&&gpu_op_storeb,
		&&gpu_op_storew,          &&gpu_op_store,
		&&gpu_op_storep,          &&gpu_op_store_r14_indexed,
		&&gpu_op_store_r15_indexed,&&gpu_op_move_pc,
		&&gpu_op_jump,            &&gpu_op_jr,
		&&gpu_op_mmult,           &&gpu_op_mtoi,
		&&gpu_op_normi,           &&gpu_op_nop,
		&&gpu_op_load_r14_ri,     &&gpu_op_load_r15_ri,
		&&gpu_op_store_r14_ri,    &&gpu_op_store_r15_ri,
		&&gpu_op_sat24,           &&gpu_op_pack
	};

	goto *gpu_dispatch[index];

	gpu_op_add:             gpu_opcode_add();             return;
	gpu_op_addc:            gpu_opcode_addc();            return;
	gpu_op_addq:            gpu_opcode_addq();            return;
	gpu_op_addqt:           gpu_opcode_addqt();           return;
	gpu_op_sub:             gpu_opcode_sub();             return;
	gpu_op_subc:            gpu_opcode_subc();            return;
	gpu_op_subq:            gpu_opcode_subq();            return;
	gpu_op_subqt:           gpu_opcode_subqt();           return;
	gpu_op_neg:             gpu_opcode_neg();             return;
	gpu_op_and:             gpu_opcode_and();             return;
	gpu_op_or:              gpu_opcode_or();              return;
	gpu_op_xor:             gpu_opcode_xor();             return;
	gpu_op_not:             gpu_opcode_not();             return;
	gpu_op_btst:            gpu_opcode_btst();            return;
	gpu_op_bset:            gpu_opcode_bset();            return;
	gpu_op_bclr:            gpu_opcode_bclr();            return;
	gpu_op_mult:            gpu_opcode_mult();            return;
	gpu_op_imult:           gpu_opcode_imult();           return;
	gpu_op_imultn:          gpu_opcode_imultn();          return;
	gpu_op_resmac:          gpu_opcode_resmac();          return;
	gpu_op_imacn:           gpu_opcode_imacn();           return;
	gpu_op_div:             gpu_opcode_div();             return;
	gpu_op_abs:             gpu_opcode_abs();             return;
	gpu_op_sh:              gpu_opcode_sh();              return;
	gpu_op_shlq:            gpu_opcode_shlq();            return;
	gpu_op_shrq:            gpu_opcode_shrq();            return;
	gpu_op_sha:             gpu_opcode_sha();             return;
	gpu_op_sharq:           gpu_opcode_sharq();           return;
	gpu_op_ror:             gpu_opcode_ror();             return;
	gpu_op_rorq:            gpu_opcode_rorq();            return;
	gpu_op_cmp:             gpu_opcode_cmp();             return;
	gpu_op_cmpq:            gpu_opcode_cmpq();            return;
	gpu_op_sat8:            gpu_opcode_sat8();            return;
	gpu_op_sat16:           gpu_opcode_sat16();           return;
	gpu_op_move:            gpu_opcode_move();            return;
	gpu_op_moveq:           gpu_opcode_moveq();           return;
	gpu_op_moveta:          gpu_opcode_moveta();          return;
	gpu_op_movefa:          gpu_opcode_movefa();          return;
	gpu_op_movei:           gpu_opcode_movei();           return;
	gpu_op_loadb:           gpu_opcode_loadb();           return;
	gpu_op_loadw:           gpu_opcode_loadw();           return;
	gpu_op_load:            gpu_opcode_load();            return;
	gpu_op_loadp:           gpu_opcode_loadp();           return;
	gpu_op_load_r14_indexed:gpu_opcode_load_r14_indexed();return;
	gpu_op_load_r15_indexed:gpu_opcode_load_r15_indexed();return;
	gpu_op_storeb:          gpu_opcode_storeb();          return;
	gpu_op_storew:          gpu_opcode_storew();          return;
	gpu_op_store:           gpu_opcode_store();           return;
	gpu_op_storep:          gpu_opcode_storep();          return;
	gpu_op_store_r14_indexed:gpu_opcode_store_r14_indexed();return;
	gpu_op_store_r15_indexed:gpu_opcode_store_r15_indexed();return;
	gpu_op_move_pc:         gpu_opcode_move_pc();         return;
	gpu_op_jump:            gpu_opcode_jump();            return;
	gpu_op_jr:              gpu_opcode_jr();              return;
	gpu_op_mmult:           gpu_opcode_mmult();           return;
	gpu_op_mtoi:            gpu_opcode_mtoi();            return;
	gpu_op_normi:           gpu_opcode_normi();           return;
	gpu_op_nop:             gpu_opcode_nop();             return;
	gpu_op_load_r14_ri:     gpu_opcode_load_r14_ri();     return;
	gpu_op_load_r15_ri:     gpu_opcode_load_r15_ri();     return;
	gpu_op_store_r14_ri:    gpu_opcode_store_r14_ri();    return;
	gpu_op_store_r15_ri:    gpu_opcode_store_r15_ri();    return;
	gpu_op_sat24:           gpu_opcode_sat24();           return;
	gpu_op_pack:            gpu_opcode_pack();            return; /* NOLINT(readability-redundant-control-flow) -- goto target */
#else
	/* Switch fallback for MSVC and other non-GNU compilers */
	switch (index) {
	case 0:  gpu_opcode_add();             break;
	case 1:  gpu_opcode_addc();            break;
	case 2:  gpu_opcode_addq();            break;
	case 3:  gpu_opcode_addqt();           break;
	case 4:  gpu_opcode_sub();             break;
	case 5:  gpu_opcode_subc();            break;
	case 6:  gpu_opcode_subq();            break;
	case 7:  gpu_opcode_subqt();           break;
	case 8:  gpu_opcode_neg();             break;
	case 9:  gpu_opcode_and();             break;
	case 10: gpu_opcode_or();              break;
	case 11: gpu_opcode_xor();             break;
	case 12: gpu_opcode_not();             break;
	case 13: gpu_opcode_btst();            break;
	case 14: gpu_opcode_bset();            break;
	case 15: gpu_opcode_bclr();            break;
	case 16: gpu_opcode_mult();            break;
	case 17: gpu_opcode_imult();           break;
	case 18: gpu_opcode_imultn();          break;
	case 19: gpu_opcode_resmac();          break;
	case 20: gpu_opcode_imacn();           break;
	case 21: gpu_opcode_div();             break;
	case 22: gpu_opcode_abs();             break;
	case 23: gpu_opcode_sh();              break;
	case 24: gpu_opcode_shlq();            break;
	case 25: gpu_opcode_shrq();            break;
	case 26: gpu_opcode_sha();             break;
	case 27: gpu_opcode_sharq();           break;
	case 28: gpu_opcode_ror();             break;
	case 29: gpu_opcode_rorq();            break;
	case 30: gpu_opcode_cmp();             break;
	case 31: gpu_opcode_cmpq();            break;
	case 32: gpu_opcode_sat8();            break;
	case 33: gpu_opcode_sat16();           break;
	case 34: gpu_opcode_move();            break;
	case 35: gpu_opcode_moveq();           break;
	case 36: gpu_opcode_moveta();          break;
	case 37: gpu_opcode_movefa();          break;
	case 38: gpu_opcode_movei();           break;
	case 39: gpu_opcode_loadb();           break;
	case 40: gpu_opcode_loadw();           break;
	case 41: gpu_opcode_load();            break;
	case 42: gpu_opcode_loadp();           break;
	case 43: gpu_opcode_load_r14_indexed(); break;
	case 44: gpu_opcode_load_r15_indexed(); break;
	case 45: gpu_opcode_storeb();          break;
	case 46: gpu_opcode_storew();          break;
	case 47: gpu_opcode_store();           break;
	case 48: gpu_opcode_storep();          break;
	case 49: gpu_opcode_store_r14_indexed(); break;
	case 50: gpu_opcode_store_r15_indexed(); break;
	case 51: gpu_opcode_move_pc();         break;
	case 52: gpu_opcode_jump();            break;
	case 53: gpu_opcode_jr();              break;
	case 54: gpu_opcode_mmult();           break;
	case 55: gpu_opcode_mtoi();            break;
	case 56: gpu_opcode_normi();           break;
	case 57: gpu_opcode_nop();             break;
	case 58: gpu_opcode_load_r14_ri();     break;
	case 59: gpu_opcode_load_r15_ri();     break;
	case 60: gpu_opcode_store_r14_ri();    break;
	case 61: gpu_opcode_store_r15_ri();    break;
	case 62: gpu_opcode_sat24();           break;
	case 63: gpu_opcode_pack();            break;
	default: break;
	}
#endif
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
   /* normalize flags */
   /* KLUDGE: Used by BRANCH_CONDITION */
   uint32_t jaguar_flags = (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

   if (BRANCH_CONDITION(IMM_2))
   {
      uint32_t delayed_pc = RM;
      uint16_t ds_opcode;
      uint32_t ds_index;
      /* Inline delay-slot: fetch-decode-execute one instruction at current
       * PC before applying the branch target.  This replaces the old
       * recursive GPUExec(1) call, avoiding full function-call overhead,
       * redundant IRQ checks, and pipeline-state save/restore. */
      if (gpu_pc >= GPU_WORK_RAM_BASE && gpu_pc < GPU_WORK_RAM_BASE + 0x1000)
      {
         uint32_t off = gpu_pc - GPU_WORK_RAM_BASE;
         ds_opcode = ((uint16_t)gpu_ram_8[off] << 8) | (uint16_t)gpu_ram_8[off + 1];
      }
      else
         ds_opcode = GPUReadWord(gpu_pc, GPU);
      ds_index = ds_opcode >> 10;
      gpu_opcode_first_parameter  = (ds_opcode >> 5) & 0x1F;
      gpu_opcode_second_parameter = ds_opcode & 0x1F;
      gpu_pc += 2;
      gpu_in_delay_slot = 1;
      gpu_ds_branch_target = delayed_pc;
      gpu_ds_irq_dispatched = 0;
      if (vjs.gpuPipelineTiming)
         GPUPipeCheckUse(ds_index);
      executeOpcode(ds_index);
      if (vjs.gpuPipelineTiming)
      {
         /* The inlined delay slot is never charged by the outer exec
          * loop, and a taken JUMP costs ~2 dead refill ticks on top
          * (INS_EXEC.NET, R12).  Both only under the option so the
          * default path stays byte-identical. */
         gpu_pipe_core_stall += (uint32_t)gpu_opcode_cycles[ds_index] + 2u;
         gpu_pipe_stall_total += (uint64_t)gpu_opcode_cycles[ds_index] + 2u;
      }
      gpu_in_delay_slot = 0;
      /* If the delay-slot instruction dispatched an interrupt, gpu_pc is
       * the ISR vector and the branch target is on the ISR stack as the
       * return address -- do not clobber the vector. */
      if (!gpu_ds_irq_dispatched)
         gpu_pc = delayed_pc;
   }
}


INLINE static void gpu_opcode_jr(void)
{
   uint32_t jaguar_flags = (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

   if (BRANCH_CONDITION(IMM_2))
   {
      int32_t offset     = ((IMM_1 & 0x10) ? 0xFFFFFFF0 | IMM_1 : IMM_1);		/* Sign extend IMM_1 */
      int32_t delayed_pc = gpu_pc + (offset * 2);
      uint16_t ds_opcode;
      uint32_t ds_index;
      /* Inline delay-slot: fetch-decode-execute one instruction at current
       * PC before applying the branch target.  Same rationale as in
       * gpu_opcode_jump above. */
      if (gpu_pc >= GPU_WORK_RAM_BASE && gpu_pc < GPU_WORK_RAM_BASE + 0x1000)
      {
         uint32_t off = gpu_pc - GPU_WORK_RAM_BASE;
         ds_opcode = ((uint16_t)gpu_ram_8[off] << 8) | (uint16_t)gpu_ram_8[off + 1];
      }
      else
         ds_opcode = GPUReadWord(gpu_pc, GPU);
      ds_index = ds_opcode >> 10;
      gpu_opcode_first_parameter  = (ds_opcode >> 5) & 0x1F;
      gpu_opcode_second_parameter = ds_opcode & 0x1F;
      gpu_pc += 2;
      gpu_in_delay_slot = 1;
      gpu_ds_branch_target = (uint32_t)delayed_pc;
      gpu_ds_irq_dispatched = 0;
      if (vjs.gpuPipelineTiming)
         GPUPipeCheckUse(ds_index);
      executeOpcode(ds_index);
      if (vjs.gpuPipelineTiming)
      {
         /* Delay-slot charge + taken-JR refill (~3 dead ticks, R13):
          * the JR target is computed a tick later than JUMP's. */
         gpu_pipe_core_stall += (uint32_t)gpu_opcode_cycles[ds_index] + 3u;
         gpu_pipe_stall_total += (uint64_t)gpu_opcode_cycles[ds_index] + 3u;
      }
      gpu_in_delay_slot = 0;
      /* See gpu_opcode_jump: don't clobber a vector jump dispatched by
       * the delay-slot instruction. */
      if (!gpu_ds_irq_dispatched)
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
   uint64_t res = (uint64_t)RN + (uint64_t)RM + (uint64_t)gpu_flag_c;
   gpu_flag_c = (uint8_t)((res >> 32) & 0x01);
   RN = (uint32_t)(res & 0xFFFFFFFF);
   SET_ZN(RN);
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

   GPU_PIPE_STORE(address);
   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   {
      uint32_t address = gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2);
      GPU_PIPE_STORE(address);
      GPUWriteLong(address, RN, GPU);
   }
#endif
}


INLINE static void gpu_opcode_store_r15_indexed(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);

   GPU_PIPE_STORE(address);
   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   {
      uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);
      GPU_PIPE_STORE(address);
      GPUWriteLong(address, RN, GPU);
   }
#endif
}


INLINE static void gpu_opcode_load_r14_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[14] + RM;

   GPU_PIPE_LOAD(address);
   if (address >= 0xF03000 && address <= 0xF03FFF)
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   {
      uint32_t address = gpu_reg[14] + RM;
      GPU_PIPE_LOAD(address);
      RN = GPUReadLong(address, GPU);
   }
#endif
}


INLINE static void gpu_opcode_load_r15_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[15] + RM;

   GPU_PIPE_LOAD(address);
   if (address >= 0xF03000 && address <= 0xF03FFF)
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   {
      uint32_t address = gpu_reg[15] + RM;
      GPU_PIPE_LOAD(address);
      RN = GPUReadLong(address, GPU);
   }
#endif
}


INLINE static void gpu_opcode_store_r14_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[14] + RM;

   GPU_PIPE_STORE(address);
   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   {
      uint32_t address = gpu_reg[14] + RM;
      GPU_PIPE_STORE(address);
      GPUWriteLong(address, RN, GPU);
   }
#endif
}


INLINE static void gpu_opcode_store_r15_ri(void)
{
#ifdef GPU_CORRECT_ALIGNMENT_STORE
   uint32_t address = gpu_reg[15] + RM;

   GPU_PIPE_STORE(address);
   if (address >= 0xF03000 && address <= 0xF03FFF)
      GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
   else
      GPUWriteLong(address, RN, GPU);
#else
   {
      uint32_t address = gpu_reg[15] + RM;
      GPU_PIPE_STORE(address);
      GPUWriteLong(address, RN, GPU);
   }
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
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM, RN & 0xFF, GPU);
   else
   {
      GPU_PIPE_STORE(RM);
      JaguarWriteByte(RM, RN, GPU);
   }
}


INLINE static void gpu_opcode_storew(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM & 0xFFFFFFFE, RN & 0xFFFF, GPU);
   else
   {
      GPU_PIPE_STORE(RM);
      JaguarWriteWord(RM, RN, GPU);
   }
#else
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM, RN & 0xFFFF, GPU);
   else
   {
      GPU_PIPE_STORE(RM);
      JaguarWriteWord(RM, RN, GPU);
   }
#endif
}


INLINE static void gpu_opcode_store(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
      GPUWriteLong(RM & 0xFFFFFFFC, RN, GPU);
   else
   {
      GPU_PIPE_STORE(RM);
      GPUWriteLong(RM, RN, GPU);
   }
#else
   GPU_PIPE_STORE(RM);
   GPUWriteLong(RM, RN, GPU);
#endif
}


/* STOREP moves a whole phrase, so the memory controller sees a phrase
 * address: bits 2-0 carry no meaning for a 64-bit transfer and hardware
 * rounds them away.  The Jaguar bus has no cycle that writes eight bytes
 * starting at an arbitrary byte offset, so an unaligned STOREP can never
 * smear the transfer forward into the following phrase.  The address was
 * already masked for GPU local RAM; external addresses were passed through
 * raw, which is the bug.
 *
 * Pitfall: The Mayan Adventure depends on this.  Its GPU code keeps a
 * two-longword mailbox at main RAM $0-$7 and reaches it with STOREP through a
 * register that sometimes holds $7 rather than $0 (the low bits are junk the
 * hardware discards).  Unmasked, that wrote longwords at $7 and $B; the
 * second landed on $C-$E and replaced the 68K address-error vector.  The game
 * installs a deliberate "ADDQ.L #8,A7 ; RTE" address-error recovery stub at
 * $400 and takes real address errors during play, so a few frames later it
 * vectored into garbage and the 68K never came back (issue #138).
 *
 * The same argument applies to the plain LONG stores (bits 1-0), which still
 * only mask GPU local RAM.  Masking those too is left alone deliberately: it
 * is not needed for #138 and it does perturb Ruiner Pinball and Super Burnout,
 * which needs its own before/after evidence rather than riding along here. */
INLINE static void gpu_opcode_storep(void)
{
   /* One 64-bit phrase = one bus transaction (JTRM: "the memory
    * controller makes it all look 64 bits wide"), not two 32-bit ones. */
   GPU_PIPE_STORE(RM);
   GPUWriteLong((RM & 0xFFFFFFF8) + 0, gpu_hidata, GPU);
   GPUWriteLong((RM & 0xFFFFFFF8) + 4, RN, GPU);
}

INLINE static void gpu_opcode_loadb(void)
{
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
   {
      /* JTRM (Technical Reference v8, "Load Byte"): "The destination
       * register will have the byte loaded into bits 0-7 ... This applies
       * to external memory only, internal memory will perform a 32-bit
       * read."  Internal RAM is one long per row; a byte load returns the
       * ENTIRE long containing the address -- no byte extraction at all.
       * Atari's own Cinepak decompressor (JTRM vol. 12) depends on this:
       * its CRY/RGB clamp tables in GPU local RAM store one entry per
       * 32-bit long (value in the low byte, upper bytes zero) and index
       * them with LOADB at arbitrary byte offsets.  The old code returned
       * `GPUReadLong(RM) & 0xFF` (= the byte at RM+3): every decoded FMV
       * frame came out as structured garbage (BrainDead 13 / Dragon's
       * Lair / Space Ace). */
      RN = GPUReadLong(RM & 0xFFFFFFFC, GPU);
   }
   else
   {
      GPU_PIPE_LOAD(RM);
      RN = JaguarReadByte(RM, GPU);
   }
}


INLINE static void gpu_opcode_loadw(void)
{
   if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
   {
      /* Same JTRM rule as LOADB: word loads from internal RAM perform a
       * full 32-bit read of the long containing the address. */
      RN = GPUReadLong(RM & 0xFFFFFFFC, GPU);
   }
   else
   {
      GPU_PIPE_LOAD(RM);
      RN = JaguarReadWord(RM, GPU);
   }
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
   GPU_PIPE_LOAD(RM);
   RN = GPUReadLong(RM & 0xFFFFFFFC, GPU);
#else
   GPU_PIPE_LOAD(RM);
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
      /* One 64-bit phrase = one bus transaction, not two. */
      GPU_PIPE_LOAD(RM);
      gpu_hidata = GPUReadLong(RM + 0, GPU);
      RN		   = GPUReadLong(RM + 4, GPU);
   }
#else
   /* One 64-bit phrase = one bus transaction, not two. */
   GPU_PIPE_LOAD(RM);
   gpu_hidata = GPUReadLong(RM + 0, GPU);
   RN		   = GPUReadLong(RM + 4, GPU);
#endif
}


INLINE static void gpu_opcode_load_r14_indexed(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2);

   GPU_PIPE_LOAD(address);
   if ((address >= 0xF03000) && (address <= 0xF03FFF))
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   {
      uint32_t address = gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2);
      GPU_PIPE_LOAD(address);
      RN = GPUReadLong(address, GPU);
   }
#endif
}


INLINE static void gpu_opcode_load_r15_indexed(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
   uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);

   GPU_PIPE_LOAD(address);
   if ((address >= 0xF03000) && (address <= 0xF03FFF))
      RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
   else
      RN = GPUReadLong(address, GPU);
#else
   {
      uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);
      GPU_PIPE_LOAD(address);
      RN = GPUReadLong(address, GPU);
   }
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

   /* Per JTRM ("Systolic Matrix Multiplies"), the packed vector operand
    * lives in the SECONDARY register bank (bank 1) — an absolute bank
    * reference, not "the bank not currently selected".  See the DSP's
    * dsp_opcode_mmult for the full story (Baldies clipped-music bug). */
   if (gpu_matrix_control & 0x10)				// Column stepping
   {
      for(i=0; i<count; i++)
      {
         int16_t a;
         int16_t b;
         if (i & 0x01)
            a = (int16_t)((gpu_reg_bank_1[IMM_1 + (i >> 1)] >> 16) & 0xFFFF);
         else
            a = (int16_t)(gpu_reg_bank_1[IMM_1 + (i >> 1)] & 0xFFFF);

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
            a = (int16_t)((gpu_reg_bank_1[IMM_1 + (i >> 1)] >> 16) & 0xFFFF);
         else
            a = (int16_t)(gpu_reg_bank_1[IMM_1 + (i >> 1)] & 0xFFFF);

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
   uint32_t res = (RN >> r1) | (RN << ((-r1) & 31));
   SET_ZN(res); gpu_flag_c = (RN >> 31) & 1;
   RN = res;
}


INLINE static void gpu_opcode_rorq(void)
{
   /* gpu_convert_zero[0] returns 32 (rotate-by-0 means rotate-by-full-word
    * which is a no-op).  Masking to 0x1F maps 32 -> 0, preserving that
    * semantic and avoiding `RN >> 32` UB in the rotate idiom below. */
   uint32_t r1 = gpu_convert_zero[IMM_1 & 0x1F] & 0x1F;
   uint32_t r2 = RN;
   uint32_t res = (r2 >> r1) | (r2 << ((-r1) & 31));
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


/* Save state serialization for GPU */

#include "state.h"

size_t GPUStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   uint8_t active_bank;

   STATE_SAVE_BUF(buf, gpu_ram_8, sizeof(gpu_ram_8));
   STATE_SAVE_VAR(buf, gpu_pc);
   STATE_SAVE_VAR(buf, gpu_acc);
   STATE_SAVE_VAR(buf, gpu_remain);
   STATE_SAVE_VAR(buf, gpu_hidata);
   STATE_SAVE_VAR(buf, gpu_flags);
   STATE_SAVE_VAR(buf, gpu_matrix_control);
   STATE_SAVE_VAR(buf, gpu_pointer_to_matrix);
   STATE_SAVE_VAR(buf, gpu_data_organization);
   STATE_SAVE_VAR(buf, gpu_control);
   STATE_SAVE_VAR(buf, gpu_div_control);
   STATE_SAVE_VAR(buf, gpu_flag_z);
   STATE_SAVE_VAR(buf, gpu_flag_n);
   STATE_SAVE_VAR(buf, gpu_flag_c);
   STATE_SAVE_BUF(buf, gpu_reg_bank_0, sizeof(gpu_reg_bank_0));
   STATE_SAVE_BUF(buf, gpu_reg_bank_1, sizeof(gpu_reg_bank_1));

   /* Save which register bank is active (0 or 1) */
   active_bank = (gpu_reg == gpu_reg_bank_0) ? 0 : 1;
   STATE_SAVE_VAR(buf, active_bank);

   STATE_SAVE_VAR(buf, gpu_instruction);
   STATE_SAVE_VAR(buf, gpu_opcode_first_parameter);
   STATE_SAVE_VAR(buf, gpu_opcode_second_parameter);
   STATE_SAVE_VAR(buf, gpu_in_exec);
   STATE_SAVE_VAR(buf, gpu_releaseTimeSlice_flag);

   return (size_t)(buf - start);
}


size_t GPUStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   uint8_t active_bank;

   STATE_LOAD_BUF(buf, gpu_ram_8, sizeof(gpu_ram_8));
   STATE_LOAD_VAR(buf, gpu_pc);
   STATE_LOAD_VAR(buf, gpu_acc);
   STATE_LOAD_VAR(buf, gpu_remain);
   STATE_LOAD_VAR(buf, gpu_hidata);
   STATE_LOAD_VAR(buf, gpu_flags);
   STATE_LOAD_VAR(buf, gpu_matrix_control);
   STATE_LOAD_VAR(buf, gpu_pointer_to_matrix);
   STATE_LOAD_VAR(buf, gpu_data_organization);
   STATE_LOAD_VAR(buf, gpu_control);
   STATE_LOAD_VAR(buf, gpu_div_control);
   STATE_LOAD_VAR(buf, gpu_flag_z);
   STATE_LOAD_VAR(buf, gpu_flag_n);
   STATE_LOAD_VAR(buf, gpu_flag_c);
   STATE_LOAD_BUF(buf, gpu_reg_bank_0, sizeof(gpu_reg_bank_0));
   STATE_LOAD_BUF(buf, gpu_reg_bank_1, sizeof(gpu_reg_bank_1));

   /* Restore register bank pointers */
   STATE_LOAD_VAR(buf, active_bank);
   if (active_bank == 0)
   {
      gpu_reg = gpu_reg_bank_0;
      gpu_alternate_reg = gpu_reg_bank_1;
   }
   else
   {
      gpu_reg = gpu_reg_bank_1;
      gpu_alternate_reg = gpu_reg_bank_0;
   }

   STATE_LOAD_VAR(buf, gpu_instruction);
   STATE_LOAD_VAR(buf, gpu_opcode_first_parameter);
   STATE_LOAD_VAR(buf, gpu_opcode_second_parameter);
   STATE_LOAD_VAR(buf, gpu_in_exec);
   STATE_LOAD_VAR(buf, gpu_releaseTimeSlice_flag);

   return (size_t)(buf - start);
}

/* dsp_exec_untimed.h -- DSPExecUntimed(), the default DSP inner loop.
 *
 * Included TWICE from dsp.c (issue #1054), once with DSP_UNTIMED_GDB 0
 * and once with 1, to make two separate out-of-line functions:
 *
 *   DSPExecUntimed       no GDB code at all -- the path every user runs;
 *   DSPExecUntimedArmed  per-instruction breakpoint test, idle skip off.
 *
 * DSPExec() picks one per slice from gdbArmedDSP, so a DSP breakpoint
 * set while a plain slice is on the stack (from a watchpoint halt, say)
 * takes effect at the next slice.  That is the same once-per-slice
 * sampling the single loop already had: it cached gdbArmedDSP in a local
 * at slice entry and re-read it only after its own GDBHalt().
 *
 * The includer defines DSP_UNTIMED_NAME and DSP_UNTIMED_GDB; this file
 * #undefs both.  dsp.c is the only includer; no include guard, by design.
 */
static DSP_NOINLINE void DSP_UNTIMED_NAME(int32_t cycles)
{
	int idleSkipActive;
#if DSP_UNTIMED_GDB
	int gdbArmedSlice;
#endif
	/* Issue #811 late load write-back, armed for exactly the next
	 * instruction (see dsp_ldx_slot). */
	uint32_t *ldxArmed = NULL;
	uint32_t ldxVal = 0, ldxPc = 0;
	/* Issue #811 stale indexed-store data after a divide (see
	 * dsp_div_store_fixups), armed for exactly the next instruction. */
	uint32_t *divArmed = NULL;
	uint32_t divOld = 0, divPc = 0;

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

#if DSP_UNTIMED_GDB
	/* Same shape as GPUExec's cached locals (issue #532): a hot global
	 * read per emulated instruction with an opaque opcode call in
	 * between.  Only GDBHalt() can re-arm inside a slice. */
	gdbArmedSlice = gdbArmedDSP;
#endif
	dsp_in_exec++;

	/* Idle-loop fast-forward gates (issue #569).  Every one of these adds
	 * per-instruction state the affine extrapolation does not model, so
	 * any of them turns the whole thing off:
	 *   - vjs.riscIdleSkip: the user-facing core option itself;
	 *   - blitMemoMode / blitMemoRecording: the JaguarRead and JaguarWrite
	 *     families note reads and writes for the memo (blit_memo.h:64-65,
	 *     src/core/jaguar.c:1139), and skipping loads would change what
	 *     the memo records;
	 *   - riscClockScalePct is deliberately NOT a gate (#378 overclock
	 *     presets): SCALE_RISC_CYCLES is applied where the budget is handed out (jaguar.c), not inside
	 *     DSPExec, so `cycles` here is already in the same currency the
	 *     measured per-iteration cost was charged in -- cost and opcost
	 *     are measured from live `cycles` deltas, never recomputed, and
	 *     nothing per-instruction in this function depends on the scale.
	 *     Scaling `n` or `cost` again would double-scale.  The GPU has
	 *     one scale-dependent per-instruction term (gpu_stall_scale_accum);
	 *     see the note in GPUExec;
	 *   - busArbiter.enabled (the dram_timing option): bus occupancy is
	 *     accounted per access.  Belt-and-braces here -- dsp.c never
	 *     touches busArbiter and JaguarReadLong charges it only for
	 *     `who == OP` -- but a DSP-side arbiter charge added later must
	 *     not silently invalidate this;
	 *   - vjs.gpuPipelineTiming: drives the DSP pipeline/gateway model
	 *     (issue #313), which runs in DSPExecTimed() and never reaches
	 *     this loop -- kept in the gate as belt-and-braces;
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
	              && !busArbiter.enabled
	              && !vjs.gpuPipelineTiming
	              && !(dsp_control & 0x18);
#ifdef VJ_TRACE
	if (vjtrace_armed || vjtrace_nwatch)
		idleSkipActive = 0;
#endif
#if DSP_UNTIMED_GDB && !defined(VJ_GDB_STUB_DISABLE_IDLE_GATE)
	/* GDB stub (issue #652): DSPIdleLoopProbe() extrapolates the PC
	 * forward over many idle-loop iterations without visiting each one,
	 * exactly the class of per-instruction side effect the #569 idle-skip
	 * gate list above already disables for (blit memo, busArbiter,
	 * vjtrace watch). A DSP breakpoint or pending step would be stepped
	 * clean over.  DSPExec() only enters this instance with gdbArmedDSP
	 * set, so the gate is unconditional here and absent from the plain
	 * instance. */
	idleSkipActive = 0;
#endif
	/* Probe + reject memo are re-derived from scratch every call, so
	 * nothing here reaches a savestate and nothing survives a slice. */
	idleProbeStage = 0;
	idleMemoCount  = 0;
	idleMemoNext   = 0;
	dsp_ldx_slot   = NULL;

	/* An armed late write-back runs one instruction past the budget so a
	 * load/overwrite pair never straddles a slice (see dsp_ldx_slot);
	 * likewise a divide/indexed-store pair (see dsp_div_store_fixups). */
	while ((cycles > 0 || ldxArmed || divArmed) && DSP_RUNNING)
	{
      uint16_t opcode;
      uint32_t index;
      uint32_t pcThis;
#ifdef VJ_TRACE
      VJT_PCHIST_DSP(dsp_pc);
#endif

		/* If IMASK was cleared, see if any other interrupts are pending --
		 * but not until the D_FLAGS store that cleared it has retired, so
		 * the instruction still in the pipeline behind it (typically the
		 * epilogue's `jump` back to the interrupted code) runs first. */
		if (IMASKCleared && dspFlagsRetireDelay == 0)
		{
			DSPHandleIRQsNP();
			IMASKCleared = false;
		}

		/* PC escape bail-out.  When the DSP PC has wandered into a
		 * region that doesn't contain executable code -- register
		 * space at $F00000-$F1FFFF outside DSP local SRAM, or the
		 * unmapped territory above $E40000 -- every "fetched opcode"
		 * is bus-default 0xFFFF garbage that decodes to a near-zero-
		 * cost opcode.  The inner loop then burns the entire
		 * timeslice without making progress, hanging the frontend.
		 *
		 * Wolf3D headless triage caught this in v2.3.0: the runtime
		 * watchdog logged `dsp_pc_escape pc=$00FFF004E8` (PC top-byte
		 * corrupted) and the harness wedged for 12+ minutes per
		 * frame.  An earlier dsp-diag snapshot showed PC=$0006EE in
		 * RAM at frame 48 -- that's the upstream bug (separate from
		 * this bail-out: $0006EE is *valid* RAM and decodes to real
		 * opcodes; it's where the DSP eventually drifts INTO bad
		 * territory that triggers the wedge).  Drain cycles here and
		 * let the runtime watchdog (src/core/crash_detect.c) log the
		 * actual escape PC.  DSP_RUNNING is left alone so games that
		 * legitimately stop the DSP via DSPGO=0 are unaffected.
		 *
		 * Valid execution regions match JaguarReadX address decoding
		 * (src/core/jaguar.c): anything <= $E3FFFF (main RAM mirrored
		 * 4x for the bottom 8MB, cart ROM, boot ROM) plus DSP local
		 * SRAM.  Earlier versions of this check used `<= 0x1FFFFF`
		 * and would have false-flagged DSP code running from a RAM
		 * mirror at $200000-$7FFFFF or from cart ROM at $800000+.
		 * Caught by Copilot review on PR #182. */
		if (!((dsp_pc <= 0x00E3FFFF) ||
		      (dsp_pc >= DSP_WORK_RAM_BASE && dsp_pc < DSP_WORK_RAM_BASE + 0x2000)))
		{
			cycles = 0;
			break;
		}

		pcThis = dsp_pc;

#if DSP_UNTIMED_GDB
		/* GDB stub (issue #652): checked after the IRQ dispatch and the
		 * PC-escape bail, so a breakpoint on an ISR's first instruction
		 * still fires.  GDBHalt() blocks in place (does not unwind this
		 * loop) until the client continues, steps, or disconnects; the
		 * client may disarm meanwhile, which the refresh below picks up. */
		if (gdbArmedSlice && GDBCheckPC(GDB_TGT_DSP, pcThis))
		{
			GDBHalt(GDB_TGT_DSP, GDB_STOP_BREAKPOINT, pcThis);
			gdbArmedSlice = gdbArmedDSP;
		}
#endif

		if (dsp_pc >= DSP_WORK_RAM_BASE && dsp_pc < DSP_WORK_RAM_BASE + 0x2000)
		{
			uint32_t off = dsp_pc - DSP_WORK_RAM_BASE;
			opcode = ((uint16_t)dsp_ram_8[off] << 8) | (uint16_t)dsp_ram_8[off + 1];
		}
		else
			opcode = DSPReadWord(dsp_pc, DSP);
		index = opcode >> 10;
		dsp_opcode_first_parameter = (opcode >> 5) & 0x1F;
		dsp_opcode_second_parameter = opcode & 0x1F;
		dsp_pc += 2;
		dsp_exec_opcode_count++;
		if (divArmed && pcThis == divPc
		      && &dsp_reg[dsp_opcode_second_parameter] == divArmed)
		{
			/* Issue #811: indexed store right behind a divide into its
			 * data register -- the quotient has not been written back
			 * yet, so the store sees the old contents. */
			uint32_t quot = *divArmed;

			*divArmed = divOld;
			dsp_executeOpcode(index);
			*divArmed = quot;
			dsp_div_store_fixups++;
			divArmed = NULL;
		}
		else if (index == 21)
		{
			uint32_t *dst = &dsp_reg[dsp_opcode_second_parameter];
			uint32_t old = *dst;

			dsp_executeOpcode(index);
			divArmed = DSPDivStorePeek(dst, &divPc);
			divOld = old;
		}
		else
		{
			dsp_executeOpcode(index);
			divArmed = NULL;
		}
		cycles -= dsp_opcode_cycles[index];

		/* Issue #811: the previous instruction was an external load and
		 * this one overwrote its target -- the load data lands last. */
		if (ldxArmed)
		{
			if (pcThis == ldxPc
			      && &dsp_reg[dsp_opcode_second_parameter] == ldxArmed)
			{
				*ldxArmed = ldxVal;
				dsp_load_wb_fixups++;
			}
			ldxArmed = NULL;
		}
		if (dsp_ldx_slot)
		{
			ldxArmed = DSPLoadOverwritePeek(&ldxVal, &ldxPc);
			dsp_ldx_slot = NULL;
		}

		/* Idle-loop fast-forward (issue #569).  A taken `jr` that landed
		 * on or behind its own address, at most 8 words back, is the only
		 * candidate; the unsigned difference rejects a not-taken jr
		 * (pc = pcThis + 2) and every forward branch in one compare. */
		if (idleSkipActive && index == 53
		    && (uint32_t)(pcThis - dsp_pc) <= 14)
			cycles = DSPIdleLoopProbe(cycles, dsp_pc, pcThis);

		/* Age out a D_FLAGS store once the instruction that was already
		 * behind it in the pipeline has run (see DSPWriteLong, D_FLAGS
		 * case).  Retiring re-opens interrupt recognition and commits the
		 * register-bank switch the store selected. */
		if (dspFlagsRetireDelay)
		{
			dspFlagsRetireDelay--;
			if (dspFlagsRetireDelay == 0)
				DSPUpdateRegisterBanks();
		}
	}

	dsp_in_exec--;

	VJP_LEAVE(VJP_DSP);
}

#undef DSP_UNTIMED_NAME
#undef DSP_UNTIMED_GDB

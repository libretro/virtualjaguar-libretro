/* dsp_exec_body.h -- the DSPExec() inner-loop body, textually included.
 *
 * Included TWICE from DSPExec(), once into a loop that tests for a GDB
 * breakpoint and once into a loop that does not.  The non-debug loop
 * therefore contains no GDB code at all.
 *
 * Why only the DSP got this treatment: measurement (issue #652) attributed
 * essentially the whole residual GDB hook cost to this loop -- ~2.3-2.7%,
 * p=0.0000 -- while the GPU's share went to zero from the slice-cached
 * local alone, and the 68K hook and the idle-skip gating both measured no
 * effect. GPUExec() was specialised the same way first and measured no
 * gain, so it was reverted rather than kept for symmetry.
 *
 * This is a textual include, NOT a function: every local of DSPExec()
 * must stay in scope and in registers. The loop's own declarations
 * (opcode/index/pcThis) live at the top of each including block, since
 * C89 forbids declarations after a statement and the armed loop runs its
 * breakpoint test first.
 */
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
		dsp_executeOpcode(index);
		cycles -= dsp_opcode_cycles[index];

		/* Idle-loop fast-forward (issue #569).  A taken `jr` that landed
		 * on or behind its own address, at most 8 words back, is the only
		 * candidate; the unsigned difference rejects a not-taken jr
		 * (pc = pcThis + 2) and every forward branch in one compare. */
		if (idleSkipActive && index == 53
		    && (uint32_t)(pcThis - dsp_pc) <= 14)
			cycles = DSPIdleLoopProbe(cycles, dsp_pc, pcThis);

		/* Age out a D_FLAGS store once the instruction that was already
		 * behind it in the pipeline has run (see DSPWriteLong, D_FLAGS
		 * case).  Retiring re-opens interrupt recognition and stops
		 * dsp_opcode_jump reaching for the pre-store bank. */
		if (dspFlagsRetireDelay)
			dspFlagsRetireDelay--;
